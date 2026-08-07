# Service Diagnostics

This directory is for reusable, explicitly selected board bring-up and fault
isolation procedures. A diagnostic may coordinate Device services and report
through `Communication/Serial`, but it must not access STM32 HAL or device
drivers directly.

Do not keep one-off GPIO toggles or register probes in `main.c` or a production
workflow. Remove them after the experiment, or promote them here with a small
state machine, a clear completion result, and an explicit configuration switch.

`UWB/uwb_sts_diagnostic_service.c` is the persistent STS/PDoA bench workflow.
It composes the reusable clock, UWB, capture-output, and console services; all
DW3220 register and accumulator access remains in the device driver/service.
