/**
  ******************************************************************************
  * @file           : dw3000_qorvo_adapter.h
  * @brief          : Internal bridge between the project platform and Qorvo SDK
  ******************************************************************************
  */

#ifndef DW3000_QORVO_ADAPTER_H
#define DW3000_QORVO_ADAPTER_H

#include "dw3000.h"

#include <stdint.h>

typedef void (*dw3000_transport_status_callback_t)(int32_t status);

/** Bind one project platform to the singleton Qorvo compatibility driver. */
void dw3000_qorvo_adapter_bind(
    const dw3000_platform_t *platform,
    dw3000_transport_status_callback_t status_callback);

/** Probe the bound SPI device and select the Qorvo DW3000 implementation. */
int32_t dw3000_qorvo_adapter_probe(void);

#endif /* DW3000_QORVO_ADAPTER_H */
