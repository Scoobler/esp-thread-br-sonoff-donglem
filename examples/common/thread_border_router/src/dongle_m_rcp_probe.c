/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "dongle_m_rcp_probe.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#if CONFIG_ESP_BR_BOARD_SONOFF_DONGLE_M

/* This small, self-contained Spinel/HDLC reader deliberately avoids taking a
 * dependency on OpenThread's private Spinel headers. It sends only property
 * reads and never drives the MG24 reset or bootloader pins. */
#define DONGLE_M_HDLC_FLAG 0x7e
#define DONGLE_M_HDLC_ESCAPE 0x7d
#define DONGLE_M_HDLC_ESCAPE_XOR 0x20
#define DONGLE_M_HDLC_GOOD_FCS 0xf0b8
#define DONGLE_M_SPINEL_HEADER_FLAG 0x80
#define DONGLE_M_SPINEL_CMD_PROP_VALUE_GET 2
#define DONGLE_M_SPINEL_CMD_PROP_VALUE_IS 6
#define DONGLE_M_SPINEL_PROP_PROTOCOL_VERSION 1
#define DONGLE_M_SPINEL_PROP_NCP_VERSION 2
#define DONGLE_M_SPINEL_PROTOCOL_MAJOR 4
#define DONGLE_M_SPINEL_PROTOCOL_MINOR 3
#define DONGLE_M_UART_BUFFER_SIZE 256
#define DONGLE_M_MAX_SPINEL_FRAME_SIZE 128
#define DONGLE_M_RESPONSE_TIMEOUT_MS 500
#define DONGLE_M_RETRIES_PER_REQUEST 2

static const char *TAG = "dongle_m_rcp_probe";

typedef struct {
    uint8_t bytes[DONGLE_M_MAX_SPINEL_FRAME_SIZE];
    size_t length;
    uint16_t fcs;
    bool in_frame;
    bool escaped;
} dongle_m_hdlc_decoder_t;

static uint16_t hdlc_update_fcs(uint16_t fcs, uint8_t byte)
{
    fcs ^= byte;
    for (unsigned int bit = 0; bit < 8; bit++) {
        fcs = (fcs & 1) ? (fcs >> 1) ^ 0x8408 : fcs >> 1;
    }
    return fcs;
}

static void hdlc_decoder_reset(dongle_m_hdlc_decoder_t *decoder)
{
    decoder->length = 0;
    decoder->fcs = 0xffff;
    decoder->in_frame = true;
    decoder->escaped = false;
}

static bool spinel_read_packed_uint(const uint8_t **data, size_t *length, uint32_t *value)
{
    uint32_t result = 0;
    unsigned int shift = 0;

    while (*length > 0 && shift < 35) {
        uint8_t byte = **data;
        (*data)++;
        (*length)--;
        if (shift == 28 && (byte & 0x70) != 0) {
            return false;
        }
        result |= (uint32_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            *value = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

static bool spinel_response_matches(const uint8_t *frame, size_t length, uint8_t tid, uint32_t property,
                                    char *ncp_version, size_t ncp_version_size)
{
    uint32_t command;
    uint32_t response_property;
    const uint8_t *data = frame;

    if (length < 3 || data[0] != (DONGLE_M_SPINEL_HEADER_FLAG | tid)) {
        return false;
    }
    data++;
    length--;
    if (!spinel_read_packed_uint(&data, &length, &command) ||
        !spinel_read_packed_uint(&data, &length, &response_property) ||
        command != DONGLE_M_SPINEL_CMD_PROP_VALUE_IS || response_property != property) {
        return false;
    }

    if (property == DONGLE_M_SPINEL_PROP_PROTOCOL_VERSION) {
        uint32_t major;
        uint32_t minor;
        return spinel_read_packed_uint(&data, &length, &major) && spinel_read_packed_uint(&data, &length, &minor) &&
               major == DONGLE_M_SPINEL_PROTOCOL_MAJOR && minor == DONGLE_M_SPINEL_PROTOCOL_MINOR;
    }

    if (property == DONGLE_M_SPINEL_PROP_NCP_VERSION) {
        size_t version_length = strnlen((const char *)data, length);
        if (version_length == 0 || version_length == length || version_length >= ncp_version_size) {
            return false;
        }
        for (size_t index = 0; index < version_length; index++) {
            if (data[index] < 0x20 || data[index] > 0x7e) {
                return false;
            }
        }
        memcpy(ncp_version, data, version_length);
        ncp_version[version_length] = '\0';
        return true;
    }

    return false;
}

static size_t hdlc_encode(const uint8_t *payload, size_t payload_length, uint8_t *encoded, size_t encoded_size)
{
    uint16_t fcs = 0xffff;
    size_t encoded_length = 0;

    if (encoded_size < 2) {
        return 0;
    }
    encoded[encoded_length++] = DONGLE_M_HDLC_FLAG;
    for (size_t index = 0; index < payload_length + 2; index++) {
        uint8_t byte;
        if (index < payload_length) {
            byte = payload[index];
            fcs = hdlc_update_fcs(fcs, byte);
        } else {
            uint16_t final_fcs = fcs ^ 0xffff;
            byte = (index == payload_length) ? (uint8_t)final_fcs : (uint8_t)(final_fcs >> 8);
        }
        if (byte == DONGLE_M_HDLC_FLAG || byte == DONGLE_M_HDLC_ESCAPE || byte == 0x11 || byte == 0x13 || byte == 0xf8) {
            if (encoded_length + 2 >= encoded_size) {
                return 0;
            }
            encoded[encoded_length++] = DONGLE_M_HDLC_ESCAPE;
            encoded[encoded_length++] = byte ^ DONGLE_M_HDLC_ESCAPE_XOR;
        } else {
            if (encoded_length + 1 >= encoded_size) {
                return 0;
            }
            encoded[encoded_length++] = byte;
        }
    }
    encoded[encoded_length++] = DONGLE_M_HDLC_FLAG;
    return encoded_length;
}

static bool hdlc_decode_byte(dongle_m_hdlc_decoder_t *decoder, uint8_t byte, const uint8_t **frame, size_t *length)
{
    if (byte == DONGLE_M_HDLC_FLAG) {
        bool complete = decoder->in_frame && decoder->length >= 2 && decoder->fcs == DONGLE_M_HDLC_GOOD_FCS;
        if (complete) {
            *frame = decoder->bytes;
            *length = decoder->length - 2;
        }
        hdlc_decoder_reset(decoder);
        return complete;
    }
    if (!decoder->in_frame) {
        return false;
    }
    if (byte == DONGLE_M_HDLC_ESCAPE) {
        decoder->escaped = true;
        return false;
    }
    if (decoder->escaped) {
        byte ^= DONGLE_M_HDLC_ESCAPE_XOR;
        decoder->escaped = false;
    }
    if (decoder->length == sizeof(decoder->bytes)) {
        decoder->in_frame = false;
        return false;
    }
    decoder->bytes[decoder->length++] = byte;
    decoder->fcs = hdlc_update_fcs(decoder->fcs, byte);
    return false;
}

static bool query_property(uint32_t property, uint8_t tid, char *ncp_version, size_t ncp_version_size)
{
    uint8_t request[] = {DONGLE_M_SPINEL_HEADER_FLAG | tid, DONGLE_M_SPINEL_CMD_PROP_VALUE_GET, (uint8_t)property};
    uint8_t encoded[sizeof(request) * 2 + 4];
    size_t encoded_length = hdlc_encode(request, sizeof(request), encoded, sizeof(encoded));
    dongle_m_hdlc_decoder_t decoder = {0};
    int64_t deadline = esp_timer_get_time() + (int64_t)DONGLE_M_RESPONSE_TIMEOUT_MS * 1000;

    if (encoded_length == 0 || uart_write_bytes(CONFIG_ESP_BR_RCP_UART_PORT, (const char *)encoded, encoded_length) != encoded_length ||
        uart_wait_tx_done(CONFIG_ESP_BR_RCP_UART_PORT, pdMS_TO_TICKS(50)) != ESP_OK) {
        return false;
    }
    while (esp_timer_get_time() < deadline) {
        uint8_t received[32];
        int received_length = uart_read_bytes(CONFIG_ESP_BR_RCP_UART_PORT, received, sizeof(received), pdMS_TO_TICKS(20));
        if (received_length < 0) {
            return false;
        }
        for (int index = 0; index < received_length; index++) {
            const uint8_t *frame;
            size_t frame_length;
            if (hdlc_decode_byte(&decoder, received[index], &frame, &frame_length) &&
                spinel_response_matches(frame, frame_length, tid, property, ncp_version, ncp_version_size)) {
                return true;
            }
        }
    }
    return false;
}

static bool probe_at_baud(uint32_t baud_rate, char *ncp_version, size_t ncp_version_size)
{
    const uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    bool installed = false;
    bool found = false;
    uint8_t tid = 1;

    if (uart_param_config(CONFIG_ESP_BR_RCP_UART_PORT, &uart_config) != ESP_OK ||
        uart_set_pin(CONFIG_ESP_BR_RCP_UART_PORT, CONFIG_PIN_TO_RCP_RX, CONFIG_PIN_TO_RCP_TX, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install(CONFIG_ESP_BR_RCP_UART_PORT, DONGLE_M_UART_BUFFER_SIZE, 0, 0, NULL, 0) != ESP_OK) {
        goto cleanup;
    }
    installed = true;
    (void)uart_flush_input(CONFIG_ESP_BR_RCP_UART_PORT);

    for (unsigned int protocol_attempt = 0; protocol_attempt < DONGLE_M_RETRIES_PER_REQUEST; protocol_attempt++) {
        (void)uart_flush_input(CONFIG_ESP_BR_RCP_UART_PORT);
        if (!query_property(DONGLE_M_SPINEL_PROP_PROTOCOL_VERSION, tid++, ncp_version, ncp_version_size)) {
            continue;
        }
        for (unsigned int version_attempt = 0; version_attempt < DONGLE_M_RETRIES_PER_REQUEST; version_attempt++) {
            (void)uart_flush_input(CONFIG_ESP_BR_RCP_UART_PORT);
            if (query_property(DONGLE_M_SPINEL_PROP_NCP_VERSION, tid++, ncp_version, ncp_version_size)) {
                found = true;
                goto cleanup;
            }
        }
    }

cleanup:
    if (installed) {
        esp_err_t cleanup_error = uart_driver_delete(CONFIG_ESP_BR_RCP_UART_PORT);
        if (cleanup_error != ESP_OK) {
            ESP_LOGE(TAG, "UART%d cleanup failed: %s", CONFIG_ESP_BR_RCP_UART_PORT, esp_err_to_name(cleanup_error));
        }
    }
    return found;
}

esp_err_t dongle_m_rcp_probe(uint32_t *baud_rate)
{
    static const uint32_t candidate_bauds[] = {115200, 460800};
    char ncp_version[96];

    if (baud_rate == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0; index < sizeof(candidate_bauds) / sizeof(candidate_bauds[0]); index++) {
        memset(ncp_version, 0, sizeof(ncp_version));
        ESP_LOGI(TAG, "Probing MG24 Spinel at %" PRIu32 " baud", candidate_bauds[index]);
        if (probe_at_baud(candidate_bauds[index], ncp_version, sizeof(ncp_version))) {
            *baud_rate = candidate_bauds[index];
            ESP_LOGI(TAG, "MG24 Spinel verified at %" PRIu32 " baud (NCP: %s)", *baud_rate, ncp_version);
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "No compatible MG24 Spinel RCP responded at 115200 or 460800 baud");
    return ESP_ERR_NOT_FOUND;
}

#else

esp_err_t dongle_m_rcp_probe(uint32_t *baud_rate)
{
    (void)baud_rate;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
