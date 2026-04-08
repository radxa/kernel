// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for PCA9632 4-channel 8-bit PWM LED controller
 *
 * Based on the pwm-pca9685.c driver
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

/*
 * The PCA9632 has 4 PWM channels with 8-bit resolution.
 * Unlike PCA9685, it has a fixed PWM frequency.
 */

#define PCA9632_MODE1		0x00
#define PCA9632_MODE2		0x01
#define PCA9632_PWM0		0x02
#define PCA9632_PWM1		0x03
#define PCA9632_PWM2		0x04
#define PCA9632_PWM3		0x05
#define PCA9632_GRPPWM		0x06
#define PCA9632_GRPFREQ		0x07
#define PCA9632_LEDOUT		0x08
#define PCA9632_SUBADR1		0x09
#define PCA9632_SUBADR2		0x0A
#define PCA9632_SUBADR3		0x0B
#define PCA9632_ALLCALLADR	0x0C

#define PCA9632_COUNTER_RANGE		256	/* 8-bit resolution */
#define PCA9632_DEFAULT_PERIOD_NS	640000	/* 1.5625 kHz fixed frequency */

#define PCA9632_NUMREGS		0x0D
#define PCA9632_MAXCHAN		0x04

#define MODE1_SLEEP		BIT(4)
#define MODE1_ALLCALL		BIT(0)
#define MODE1_SUB1		BIT(1)
#define MODE1_SUB2		BIT(2)
#define MODE1_SUB3		BIT(3)

#define MODE2_INVRT		BIT(4)
#define MODE2_OUTDRV		BIT(2)

#define LEDOUT_LED0_MASK	GENMASK(1, 0)
#define LEDOUT_LED1_MASK	GENMASK(3, 2)
#define LEDOUT_LED2_MASK	GENMASK(5, 4)
#define LEDOUT_LED3_MASK	GENMASK(7, 6)

#define LEDOUT_MODE_OFF		0x00
#define LEDOUT_MODE_ON		0x01
#define LEDOUT_MODE_PWM		0x02
#define LEDOUT_MODE_GRP		0x03

struct pca9632 {
	struct pwm_chip chip;
	struct regmap *regmap;
	struct mutex lock;
};

static inline struct pca9632 *to_pca(struct pwm_chip *chip)
{
	return container_of(chip, struct pca9632, chip);
}

static int pca9632_read_reg(struct pca9632 *pca, unsigned int reg, unsigned int *val)
{
	struct device *dev = pca->chip.dev;
	int err;

	err = regmap_read(pca->regmap, reg, val);
	if (err)
		dev_err(dev, "regmap_read of register 0x%x failed: %pe\n", reg, ERR_PTR(err));

	return err;
}

static int pca9632_write_reg(struct pca9632 *pca, unsigned int reg, unsigned int val)
{
	struct device *dev = pca->chip.dev;
	int err;

	err = regmap_write(pca->regmap, reg, val);
	if (err)
		dev_err(dev, "regmap_write to register 0x%x failed: %pe\n", reg, ERR_PTR(err));

	return err;
}

static int pca9632_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
								const struct pwm_state *state)
{
	struct pca9632 *pca = to_pca(chip);
	unsigned int duty;
	int ret = 0;

	if (state->polarity != PWM_POLARITY_NORMAL &&
		state->polarity != PWM_POLARITY_INVERSED)
		return -EINVAL;

	mutex_lock(&pca->lock);

	if (!state->enabled) {
		ret = pca9632_write_reg(pca, PCA9632_PWM0 + pwm->hwpwm, 0);
		goto out;
	}

	duty = DIV_ROUND_UP(state->duty_cycle * PCA9632_COUNTER_RANGE,
				state->period);

	if (duty > PCA9632_COUNTER_RANGE - 1)
		duty = PCA9632_COUNTER_RANGE - 1;

	if (state->polarity == PWM_POLARITY_INVERSED)
		duty = PCA9632_COUNTER_RANGE - 1 - duty;

	ret = pca9632_write_reg(pca, PCA9632_PWM0 + pwm->hwpwm, duty);

out:
	mutex_unlock(&pca->lock);
	return ret;
}

static int pca9632_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
									struct pwm_state *state)
{
	struct pca9632 *pca = to_pca(chip);
	unsigned int val;

	/* Fixed period for PCA9632 */
	state->period = PCA9632_DEFAULT_PERIOD_NS;

	/* Polarity is fixed */
	state->polarity = PWM_POLARITY_NORMAL;

	if (pca9632_read_reg(pca, PCA9632_PWM0 + pwm->hwpwm, &val))
		return -EIO;

	state->duty_cycle = DIV_ROUND_UP(val * state->period, PCA9632_COUNTER_RANGE);
	state->enabled = (val > 0);

	return 0;
}

static int pca9632_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	pm_runtime_get_sync(chip->dev);
	return 0;
}

static void pca9632_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	pm_runtime_put(chip->dev);
}

static const struct pwm_ops pca9632_pwm_ops = {
	.apply = pca9632_pwm_apply,
	.get_state = pca9632_pwm_get_state,
	.request = pca9632_pwm_request,
	.free = pca9632_pwm_free,
};

static const struct regmap_config pca9632_regmap_i2c_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PCA9632_NUMREGS - 1,
	.cache_type = REGCACHE_NONE,
};

static int pca9632_pwm_probe(struct i2c_client *client)
{
	struct pca9632 *pca;
	unsigned int reg;
	int ret;

	pca = devm_kzalloc(&client->dev, sizeof(*pca), GFP_KERNEL);
	if (!pca) {
		return -ENOMEM;
	}

	pca->regmap = devm_regmap_init_i2c(client, &pca9632_regmap_i2c_config);
	if (IS_ERR(pca->regmap)) {
		ret = PTR_ERR(pca->regmap);
		dev_err(&client->dev, "Failed to initialize register map: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, pca);

	mutex_init(&pca->lock);

	/* Wake up the chip by clearing SLEEP bit */
	ret = pca9632_read_reg(pca, PCA9632_MODE1, &reg);
	if (ret)
		return ret;
	reg &= ~MODE1_SLEEP;
	ret = pca9632_write_reg(pca, PCA9632_MODE1, reg);
	if (ret)
		return ret;

	/* Configure MODE2 */
	ret = pca9632_read_reg(pca, PCA9632_MODE2, &reg);
	if (ret)
		return ret;

	if (device_property_read_bool(&client->dev, "nxp,totem-pole"))
		reg |= MODE2_OUTDRV;
	else
		reg &= ~MODE2_OUTDRV;

	if (device_property_read_bool(&client->dev, "nxp,inverted-out"))
		reg |= MODE2_INVRT;
	else
		reg &= ~MODE2_INVRT;

	ret = pca9632_write_reg(pca, PCA9632_MODE2, reg);
	if (ret)
		return ret;

	/* Enable all LEDs for PWM mode */
	ret = pca9632_write_reg(pca, PCA9632_LEDOUT,
				(LEDOUT_MODE_PWM << 0) |  /* LED0 */
				(LEDOUT_MODE_PWM << 2) |  /* LED1 */
				(LEDOUT_MODE_PWM << 4) |  /* LED2 */
				(LEDOUT_MODE_PWM << 6));  /* LED3 */
	if (ret)
		return ret;

	/* Disable all subaddresses and all-call to avoid collisions */
	ret = pca9632_read_reg(pca, PCA9632_MODE1, &reg);
	if (ret)
		return ret;
	reg &= ~(MODE1_ALLCALL | MODE1_SUB1 | MODE1_SUB2 | MODE1_SUB3);
	ret = pca9632_write_reg(pca, PCA9632_MODE1, reg);
	if (ret)
		return ret;

	pca->chip.ops = &pca9632_pwm_ops;
	pca->chip.npwm = PCA9632_MAXCHAN;
	pca->chip.dev = &client->dev;

	ret = pwmchip_add(&pca->chip);
	if (ret < 0)
		return ret;

	pm_runtime_enable(&client->dev);

	/* Put chip in sleep state initially if runtime PM enabled */
	if (pm_runtime_enabled(&client->dev))
		pm_runtime_set_suspended(&client->dev);

	return 0;
}

static void pca9632_pwm_remove(struct i2c_client *client)
{
	struct pca9632 *pca = i2c_get_clientdata(client);

	pwmchip_remove(&pca->chip);
	pm_runtime_disable(&client->dev);
}

static int __maybe_unused pca9632_pwm_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pca9632 *pca = i2c_get_clientdata(client);
	unsigned int reg;
	int ret;

	/* Put chip into sleep mode */
	ret = pca9632_read_reg(pca, PCA9632_MODE1, &reg);
	if (ret)
		return ret;
	reg |= MODE1_SLEEP;
	ret = pca9632_write_reg(pca, PCA9632_MODE1, reg);

	return ret;
}

static int __maybe_unused pca9632_pwm_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pca9632 *pca = i2c_get_clientdata(client);
	unsigned int reg;
	int ret;

	/* Wake up the chip */
	ret = pca9632_read_reg(pca, PCA9632_MODE1, &reg);
	if (ret)
		return ret;
	reg &= ~MODE1_SLEEP;
	ret = pca9632_write_reg(pca, PCA9632_MODE1, reg);

	return ret;
}

static const struct i2c_device_id pca9632_id[] = {
	{ "pca9632", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, pca9632_id);

#ifdef CONFIG_OF
static const struct of_device_id pca9632_dt_ids[] = {
	{ .compatible = "nxp,pca9632-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pca9632_dt_ids);
#endif

static const struct dev_pm_ops pca9632_pwm_pm = {
	SET_RUNTIME_PM_OPS(pca9632_pwm_runtime_suspend,
			pca9632_pwm_runtime_resume, NULL)
};

static struct i2c_driver pca9632_i2c_driver = {
	.driver = {
		.name = "pca9632-pwm",
		.of_match_table = of_match_ptr(pca9632_dt_ids),
		.pm = &pca9632_pwm_pm,
	},
	.probe = pca9632_pwm_probe,
	.remove = pca9632_pwm_remove,
	.id_table = pca9632_id,
};

module_i2c_driver(pca9632_i2c_driver);

MODULE_AUTHOR("William Norman <nascs@radxa.com>");
MODULE_DESCRIPTION("PWM driver for PCA9632");
MODULE_LICENSE("GPL");
