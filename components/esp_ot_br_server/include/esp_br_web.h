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

/**
 * @brief Callback that registers additional URI handlers on the border router web server.
 *
 * @param[in] server The httpd_handle_t of the running web server.
 * @param[in] ctx    The context passed to esp_br_web_register_extension().
 */
typedef esp_err_t (*esp_br_web_extension_register_t)(void *server, void *ctx);

/**
 * @brief Register an extension that adds URI handlers to the border router web server.
 *
 * Must be called before the web server starts. Extension handlers are registered before the
 * static-file catch-all handler, so GET routes registered by the extension are reachable.
 * Paths below "/api/" and "/node" are reserved for the Thread REST API.
 *
 * @param[in] register_fn  Function called once the server has started.
 * @param[in] ctx          Opaque context passed to @p register_fn.
 * @param[in] max_handlers Maximum number of URI handlers @p register_fn registers.
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if @p register_fn is NULL
 *      - ESP_ERR_INVALID_STATE if the server has already started
 *      - ESP_ERR_NO_MEM if no extension slot is free
 */
esp_err_t esp_br_web_register_extension(esp_br_web_extension_register_t register_fn, void *ctx,
                                        size_t max_handlers);

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
