#
## Makefile for Fast classifier
#
obj-m += fast-classifier.o

ifeq ($(CONFIG_SFE_SUPPORT_IPV6),y)
    ccflags-$(CONFIG_SFE_SUPPORT_IPV6) += -DSFE_SUPPORT_IPV6
endif

ccflags-y += -I$(obj)/../shortcut-fe
ccflags-y += -Werror -Wall
