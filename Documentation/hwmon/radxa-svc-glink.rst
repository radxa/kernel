.. SPDX-License-Identifier: GPL-2.0-only

Kernel driver radxa-svc-glink
=============================

Description
-----------

The Radxa SVC GLINK driver communicates with the ``RADXA_SVC_ADSP_APPS``
firmware service found on supported Radxa boards with Qualcomm SoCs. The
firmware provides fan control and dynamically discoverable temperature,
voltage, current, and power sensors.

Fan control
-----------

The fan controller provides the following attributes:

=============== ======= ======================================================
``pwm1``        RW      Current fan PWM value. Writes are accepted in manual
                        mode and use values from 0 to 255.
``pwm1_enable`` RW      Fan control mode, as described below.
=============== ======= ======================================================

The supported ``pwm1_enable`` values are:

  - 0: fan at full speed
  - 1: manual control using ``pwm1``
  - 2: automatic control using the quiet curve
  - 3: automatic control using the performance curve

When manual mode is selected, the driver starts with the current fan speed. If
the current speed cannot be determined, it starts at full speed.

Sensors
-------

Each sensor discovered through the firmware service is registered as a
separate hwmon device. Depending on the sensor type, it exposes the standard
``temp1_input``, ``in0_input``, ``curr1_input``, and ``power1_input``
attributes and their corresponding labels.
