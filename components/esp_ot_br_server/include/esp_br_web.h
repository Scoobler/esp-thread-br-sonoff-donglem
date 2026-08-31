/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(ESP_BR_WEB_EVENT);

typedef enum {
    ESP_BR_WEB_EVENT_SERVER_STARTED, /*!< Border router web server is ready to accept connections */
} esp_br_web_event_t;

/**
 * @brief Check whether the border router web server has started
 *
 * @return true when the web server is ready to accept connections
 */
bool esp_br_web_is_server_started(void);

/**
 * @brief Start border router web server, which provides REST APIs and GUI
 *
 * @param[in] base_path is the virtual file path of web server
 */
void esp_br_web_start(char *base_path);

/** Size of an ePSKc TAP string buffer: 8 digits + checksum digit + '\0' */
#define ESP_BR_EPSKC_TAP_LEN 10

/**
 * @brief Record (or clear) the currently active ePSKc TAP, so that any UI which
 * did not itself activate the ephemeral key (e.g. the M5Stack on-screen UI when
 * the REST API started the session, or vice versa) can still discover and
 * display it. Backed by an atomic, independently thread-safe of the OpenThread
 * lock, so it is safe to call regardless of whether that lock is held.
 *
 * @param[in] tap The active TAP string, or NULL/empty to clear it (session stopped).
 */
void esp_br_web_epskc_set_active_tap(const char *tap);

/**
 * @brief Get the currently known active ePSKc TAP, if any. Backed by an atomic,
 * independently thread-safe of the OpenThread lock, so it is safe to call
 * regardless of whether that lock is held.
 *
 * @param[out] tap_out Buffer to receive the TAP string, at least ESP_BR_EPSKC_TAP_LEN bytes.
 * @param[in] tap_out_len Size of tap_out.
 * @return true if a TAP is currently known and was copied into tap_out, false otherwise.
 */
bool esp_br_web_epskc_get_active_tap(char *tap_out, size_t tap_out_len);

#ifdef __cplusplus
}
#endif
