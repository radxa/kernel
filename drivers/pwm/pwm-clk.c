// SPDX-License-Identifier: GPL-2.0
/*
 * Clock based PWM controller
 *
 * Copyright (c) 2021 Nikita Travkin <nikita@trvn.ru>
 *
 * This is an "adapter" driver that allows PWM consumers to use
 * system clocks with duty cycle control as PWM outputs.
 *
 * Limitations:
 * - Due to the fact that exact behavior depends on the underlying
 *   clock driver, various limitations are possible.
 * - Underlying clock may not be able to give 0% or 100% duty cycle
 *   (constant off or on), exact behavior will depend on the clock,
 *   unless a gpio pinctrl state is supplied.
 * - When the PWM is disabled, the clock will be disabled as well,
 *   line state will depend on the clock, unless a gpio pinctrl
 *   state is supplied.
 * - The clk API doesn't expose the necessary calls to implement
 *   .get_state().
 *
 * Optionally, a GPIO descriptor and pinctrl states ("default" and
 * "gpio") can be provided. When a constant output level is needed
 * (0% duty, 100% duty, or disabled), the driver switches the pin to
 * GPIO mode and drives the appropriate level. For normal PWM output
 * the pin is switched back to its clock function mux. If no GPIO is
 * provided, the driver falls back to the original clock-only behavior.
 */

#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pwm.h>

struct pwm_clk_chip {
	struct clk *clk;
	bool clk_enabled;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;  /* clock function mux */
	struct pinctrl_state *pins_gpio;     /* GPIO mode */
	struct gpio_desc *gpiod;
};

static inline struct pwm_clk_chip *to_pwm_clk_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int pwm_clk_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct pwm_clk_chip *pcchip = to_pwm_clk_chip(chip);
	int ret;
	u32 rate;
	u64 period = state->period;
	u64 duty_cycle = state->duty_cycle;
	bool constant_level = false;
	int gpio_value = 0;

	if (!state->enabled) {
		constant_level = true;
		gpio_value = 0;
	} else if (state->duty_cycle == 0) {
		constant_level = true;
		gpio_value = (state->polarity == PWM_POLARITY_INVERSED) ? 1 : 0;
	} else if (state->duty_cycle >= state->period) {
		constant_level = true;
		gpio_value = (state->polarity == PWM_POLARITY_INVERSED) ? 0 : 1;
	}

	if (constant_level) {
		if (pcchip->gpiod) {
			gpiod_direction_output(pcchip->gpiod, gpio_value);
			pinctrl_select_state(pcchip->pinctrl, pcchip->pins_gpio);
		}
		if (pcchip->clk_enabled) {
			clk_disable(pcchip->clk);
			pcchip->clk_enabled = false;
		}
		return 0;
	}

	if (pcchip->gpiod)
		pinctrl_select_state(pcchip->pinctrl, pcchip->pins_default);

	if (!pcchip->clk_enabled) {
		ret = clk_enable(pcchip->clk);
		if (ret)
			return ret;
		pcchip->clk_enabled = true;
	}

	/*
	 * We have to enable the clk before setting the rate and duty_cycle,
	 * that however results in a window where the clk is on with a
	 * (potentially) different setting. Also setting period and duty_cycle
	 * are two separate calls, so that probably isn't atomic either.
	 */

	rate = DIV64_U64_ROUND_UP(NSEC_PER_SEC, period);
	ret = clk_set_rate(pcchip->clk, rate);
	if (ret)
		return ret;

	if (state->polarity == PWM_POLARITY_INVERSED)
		duty_cycle = period - duty_cycle;

	return clk_set_duty_cycle(pcchip->clk, duty_cycle, period);
}

static const struct pwm_ops pwm_clk_ops = {
	.apply = pwm_clk_apply,
};

static int pwm_clk_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip;
	struct pwm_clk_chip *pcchip;
	int ret;

	chip = devm_pwmchip_alloc(&pdev->dev, 1, sizeof(*pcchip));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	pcchip = to_pwm_clk_chip(chip);

	pcchip->clk = devm_clk_get_prepared(&pdev->dev, NULL);
	if (IS_ERR(pcchip->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(pcchip->clk),
				     "Failed to get clock\n");

	pcchip->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pcchip->pinctrl)) {
		ret = PTR_ERR(pcchip->pinctrl);
		pcchip->pinctrl = NULL;
		if (ret == -EPROBE_DEFER)
			return ret;
	} else {
		pcchip->pins_default = pinctrl_lookup_state(pcchip->pinctrl,
							    PINCTRL_STATE_DEFAULT);
		pcchip->pins_gpio = pinctrl_lookup_state(pcchip->pinctrl,
							 "gpio");
		if (IS_ERR(pcchip->pins_default) || IS_ERR(pcchip->pins_gpio))
			pcchip->pinctrl = NULL;
	}

	/*
	 * Switch to GPIO pinctrl state before requesting the GPIO.
	 * The driver core has already applied the "default" state, which
	 * muxes the pin to the clock function and claims it.  We must
	 * release that claim first so that gpiolib can request the pin.
	 */
	if (pcchip->pinctrl)
		pinctrl_select_state(pcchip->pinctrl, pcchip->pins_gpio);

	pcchip->gpiod = devm_gpiod_get_optional(&pdev->dev, NULL, GPIOD_ASIS);
	if (IS_ERR(pcchip->gpiod))
		return dev_err_probe(&pdev->dev, PTR_ERR(pcchip->gpiod),
				     "Failed to get gpio\n");

	/*
	 * If pinctrl states were found but no GPIO was provided, the pin is
	 * stuck in GPIO mode from the switch above.  Restore the default
	 * (clock-function) mux and fall back to clock-only operation.
	 */
	if (pcchip->pinctrl && !pcchip->gpiod) {
		pinctrl_select_state(pcchip->pinctrl, pcchip->pins_default);
		pcchip->pinctrl = NULL;
	}

	chip->ops = &pwm_clk_ops;

	ret = pwmchip_add(chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Failed to add pwm chip\n");

	platform_set_drvdata(pdev, chip);
	return 0;
}

static void pwm_clk_remove(struct platform_device *pdev)
{
	struct pwm_chip *chip = platform_get_drvdata(pdev);
	struct pwm_clk_chip *pcchip = to_pwm_clk_chip(chip);

	pwmchip_remove(chip);

	if (pcchip->clk_enabled)
		clk_disable(pcchip->clk);
}

static const struct of_device_id pwm_clk_dt_ids[] = {
	{ .compatible = "clk-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pwm_clk_dt_ids);

static struct platform_driver pwm_clk_driver = {
	.driver = {
		.name = "pwm-clk",
		.of_match_table = pwm_clk_dt_ids,
	},
	.probe = pwm_clk_probe,
	.remove = pwm_clk_remove,
};
module_platform_driver(pwm_clk_driver);

MODULE_ALIAS("platform:pwm-clk");
MODULE_AUTHOR("Nikita Travkin <nikita@trvn.ru>");
MODULE_DESCRIPTION("Clock based PWM driver");
MODULE_LICENSE("GPL");
