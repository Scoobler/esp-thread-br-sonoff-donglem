/* SPDX-License-Identifier: CC0-1.0 */

#include "dongle_m_network.h"
#include "dongle_m_led.h"

#include "sdkconfig.h"

#if CONFIG_ESP_BR_BOARD_SONOFF_DONGLE_M

#include <stdint.h>
#include <string.h>

#include "esp_br_wifi_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ot_wifi_cmd.h"
#include "esp_system.h"
#include "example_common_private.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "protocol_examples_common.h"

#define TAG "dongle_m_network"
#define NVS_NAMESPACE "br"
#define NVS_FAIL_COUNT "fail_count"
#define NVS_LAST_SSID "last_ssid"
#define SSID_MAX_LEN 32
#define PASSWORD_MAX_LEN 64
#define ETH_READY BIT0
#define WIFI_READY BIT1

static EventGroupHandle_t s_events;
static TaskHandle_t s_eth_task;

static esp_netif_t *netif_for_ifkey(const char *ifkey)
{
    return esp_netif_get_handle_from_ifkey(ifkey);
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != IP_EVENT || data == NULL) {
        return;
    }
    ip_event_got_ip_t *event = data;
    const char *ifkey = esp_netif_get_ifkey(event->esp_netif);
    if (id == IP_EVENT_ETH_GOT_IP && ifkey && strcmp(ifkey, "ETH_DEF") == 0) {
        xEventGroupSetBits(s_events, ETH_READY);
    } else if (id == IP_EVENT_STA_GOT_IP && ifkey && strcmp(ifkey, "WIFI_STA_DEF") == 0) {
        xEventGroupSetBits(s_events, WIFI_READY);
    }
}

static void ethernet_task(void *arg)
{
    (void)arg;
    esp_err_t err = example_ethernet_connect();
    ESP_LOGW(TAG, "Ethernet helper exited: %s", esp_err_to_name(err));
    s_eth_task = NULL;
    vTaskDelete(NULL);
}

static uint8_t fail_count_get(void)
{
    nvs_handle_t handle;
    uint8_t value = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, NVS_FAIL_COUNT, &value);
        nvs_close(handle);
    }
    return value;
}

static void fail_count_set(uint8_t value)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, NVS_FAIL_COUNT, value);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static void fail_count_reset(void)
{
    fail_count_set(0);
}

static void remember_ssid(const char *ssid)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, NVS_LAST_SSID, ssid);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static void reset_if_ssid_changed(const char *ssid)
{
    nvs_handle_t handle;
    char previous[SSID_MAX_LEN] = {0};
    size_t length = sizeof(previous);
    if (!ssid || nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    if (nvs_get_str(handle, NVS_LAST_SSID, previous, &length) == ESP_OK && strcmp(previous, ssid) != 0) {
        fail_count_reset();
        ESP_LOGI(TAG, "Wi-Fi SSID changed; reset failure counter");
    }
    nvs_close(handle);
}

static bool stored_wifi_credentials(char *ssid, char *password)
{
    ssid[0] = password[0] = '\0';
    return esp_ot_wifi_config_get_ssid(ssid) == ESP_OK && ssid[0] != '\0' &&
           esp_ot_wifi_config_get_password(password) == ESP_OK;
}

static bool wifi_start_nonblocking(const char *ssid, const char *password)
{
    wifi_config_t config = {0};
    strncpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid) - 1);
    strncpy((char *)config.sta.password, password, sizeof(config.sta.password) - 1);
    example_wifi_start();
    return example_wifi_sta_do_connect(config, false) == ESP_OK;
}

static bool provision_wifi(char *ssid, char *password)
{
    dongle_m_led_set_interface(DONGLE_M_LED_INTERFACE_SOFTAP);
    if (esp_br_wifi_config_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning SoftAP");
        return false;
    }
    esp_err_t err = esp_br_wifi_config_get_configured_wifi(ssid, SSID_MAX_LEN, password, PASSWORD_MAX_LEN,
                                                            CONFIG_ESP_BR_DONGLE_M_SOFTAP_WINDOW_MS);
    esp_br_wifi_config_stop();
    return err == ESP_OK && ssid[0] != '\0';
}

static bool wait_for(EventBits_t bit, uint32_t timeout_ms)
{
    return (xEventGroupWaitBits(s_events, bit, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms)) & bit) != 0;
}

esp_err_t dongle_m_network_select_backbone(esp_netif_t **backbone)
{
    char ssid[SSID_MAX_LEN] = {0};
    char password[PASSWORD_MAX_LEN] = {0};
    uint8_t failures;

    if (!backbone) {
        return ESP_ERR_INVALID_ARG;
    }
    *backbone = NULL;
    s_events = xEventGroupCreate();
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler, NULL), TAG,
                        "IP event registration failed");

    /* Dongle-M uses a single authoritative backbone for each boot. */
    if (xTaskCreate(ethernet_task, "dongle_eth", 4096, NULL, 4, &s_eth_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (wait_for(ETH_READY, CONFIG_ESP_BR_DONGLE_M_ETHERNET_WAIT_MS)) {
        *backbone = netif_for_ifkey("ETH_DEF");
        dongle_m_led_set_interface(DONGLE_M_LED_INTERFACE_ETHERNET);
        fail_count_reset();
        ESP_LOGI(TAG, "Selected Ethernet backbone");
        return *backbone ? ESP_OK : ESP_FAIL;
    }

    if (!stored_wifi_credentials(ssid, password)) {
        ESP_LOGI(TAG, "No saved Wi-Fi credentials; opening provisioning SoftAP");
        if (!provision_wifi(ssid, password)) {
            ESP_LOGW(TAG, "Provisioning window expired; rebooting");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
    } else {
        reset_if_ssid_changed(ssid);
    }

    if (wifi_start_nonblocking(ssid, password) && wait_for(WIFI_READY, CONFIG_ESP_BR_DONGLE_M_WIFI_WAIT_MS)) {
        esp_ot_wifi_config_set_ssid(ssid);
        esp_ot_wifi_config_set_password(password);
        remember_ssid(ssid);
        fail_count_reset();
        *backbone = netif_for_ifkey("WIFI_STA_DEF");
        dongle_m_led_set_interface(DONGLE_M_LED_INTERFACE_WIFI);
        ESP_LOGI(TAG, "Selected Wi-Fi backbone");
        return *backbone ? ESP_OK : ESP_FAIL;
    }

    failures = fail_count_get();
    if (failures < UINT8_MAX) {
        failures++;
    }
    fail_count_set(failures);
    ESP_LOGW(TAG, "Wi-Fi startup failed; failure count is %u", failures);
    if (failures >= CONFIG_ESP_BR_DONGLE_M_WIFI_FAIL_THRESHOLD) {
        memset(ssid, 0, sizeof(ssid));
        memset(password, 0, sizeof(password));
        if (provision_wifi(ssid, password)) {
            esp_ot_wifi_config_set_ssid(ssid);
            esp_ot_wifi_config_set_password(password);
        }
    }

    ESP_LOGW(TAG, "No selected backbone; rebooting for bounded retry");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_FAIL;
}

#else

esp_err_t dongle_m_network_select_backbone(esp_netif_t **backbone)
{
    (void)backbone;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
