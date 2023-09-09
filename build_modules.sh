#!/bin/sh
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules -j8
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules_install INSTALL_MOD_PATH=./kernel_modules
