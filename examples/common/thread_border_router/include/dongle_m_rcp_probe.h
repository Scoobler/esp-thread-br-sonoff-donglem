/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Probe the Sonoff Dongle-M's read-only Spinel interface and return its baud
 * rate. The function always releases the UART driver before returning.
 */
esp_err_t dongle_m_rcp_probe(uint32_t *baud_rate);

#ifdef __cplusplus
} /* extern "C" */
#endif
