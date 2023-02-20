#
# Makefile for Shortcut FE.
#

obj-m += shortcut-fe.o
shortcut-fe-objs := sfe_ipv4.o

ifeq ($(CONFIG_SFE_SUPPORT_IPV6),y)
    ccflags-$(CONFIG_SFE_SUPPORT_IPV6) += -DSFE_SUPPORT_IPV6
    obj-m += shortcut-fe-ipv6.o
    shortcut-fe-ipv6-objs := sfe_ipv6.o
endif

obj-m += shortcut-fe-cm.o
shortcut-fe-cm-objs := sfe_cm.o

ccflags-y += -Werror -Wall
