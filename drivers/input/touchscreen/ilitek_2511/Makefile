ccflags-y += -Wall

ifndef __KERNEL__
ccflags-y += -D__KERNEL__
endif

#Un-mark below item to enable FW upgrade on boot
#ccflags-y += -DILITEK_BOOT_UPDATE

ILITEK_INTERFACE = i2c
#ILITEK_INTERFACE = spi


ifeq ($(ILITEK_INTERFACE), spi)
$(info start to build ilitek SPI driver)
ccflags-y += -DILITEK_SPI_INTERFACE
ilitek := ilitek_spi
else
$(info start to build ilitek I2C driver)
ilitek := ilitek_i2c
endif

obj-y += $(ilitek).o

$(ilitek)-y += \
	ilitek_def.o \
	ilitek_main.o \
	ilitek_platform_init.o \
	ilitek_update.o \
	ilitek_tool.o \
	ilitek_protocol.o \
	ilitek_crypto.o \
	ilitek_report.o
