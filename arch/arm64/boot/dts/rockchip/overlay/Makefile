# SPDX-License-Identifier: GPL-2.0
dtbo-$(CONFIG_ARCH_ROCKCHIP) += \
	i2c1-hym8563.dtbo \
	rk3308-console-on-uart0.dtbo \
	rk3308-console-on-uart1.dtbo \
	rk3308-console-on-uart2.dtbo \
	rk3308-pwm1.dtbo \
	rk3308-pwm2.dtbo \
	rk3308-pwm3.dtbo \
	rk3308-i2c0.dtbo \
	rk3308-i2c1.dtbo \
	rk3308-i2c2.dtbo \
	rk3308-i2c3.dtbo \
	rk3308-i2c3-m0.dtbo \
	rk3308-i2c3-m1.dtbo \
	rk3308-uart0.dtbo \
	rk3308-uart1.dtbo \
	rk3308-uart2.dtbo \
	rk3308-uart3.dtbo \
	rk3308-usb20-host.dtbo \
	rk3308-spi-spidev.dtbo \
	rk3308-w1-gpio.dtbo \
	rockpis-v11-spi2-waveshare35b-v2.dtbo \
	rockpis-v11-spi2-waveshare35c.dtbo \
	rockpis-v12-spi2-waveshare35b-v2.dtbo \
	rockpis-v12-spi2-waveshare35c.dtbo \
	rockpis-dmic-8ch-pdm.dtbo \
	rockpis-i2s-out.dtbo \
	rockpis-spdif-out.dtbo

scr-$(CONFIG_ARCH_ROCKCHIP) += \
	rockchip-fixup.scr

dtbotxt-$(CONFIG_ARCH_ROCKCHIP) += \
	README.rockchip-overlays

targets += $(dtbo-y) $(scr-y) $(dtbotxt-y)

always			:= $(dtbo-y) $(scr-y) $(dtbotxt-y)
clean-files		:= *.dtbo *.scr

