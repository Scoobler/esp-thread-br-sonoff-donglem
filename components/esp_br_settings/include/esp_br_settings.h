/*
 * SPDX-FileCopyrightText: 2026 Sonoff Dongle-M OTBR contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load persisted border router settings from NVS and register the settings,
 *        status and backup/restore endpoints with the border router web server.
 *
 * Must be called after nvs_flash_init() and before the web server starts.
 */
esp_err_t esp_br_settings_init(void);

/**
 * @brief Record the stack defaults and apply persisted settings.
 *
 * Call after esp_openthread_border_router_init() and before Thread is started, so that settings
 * which can only change while Thread is stopped (for example the maximum number of children) and
 * settings that the border router initialisation would otherwise overwrite are applied.
 * Acquires the OpenThread lock (the lock is recursive, so callers may already hold it).
 */
void esp_br_settings_apply_boot(void);

#ifdef __cplusplus
}
#endif
