/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Patched for: Ethernet-preferred, Wi-Fi fallback, SoftAP provisioning w/ fail counter.
 */

#include "border_router_launch.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_cli_extension.h"
#include "esp_ot_rcp_update.h"
#include "esp_rcp_update.h"
#include "esp_system.h"
#include "esp_vfs_eventfd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#if CONFIG_EXAMPLE_CONNECT_WIFI
#include "esp_wifi.h"
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
#include "esp_br_wifi_config.h"
#endif
#if CONFIG_OPENTHREAD_CLI_WIFI
#include "esp_ot_wifi_cmd.h"
#endif
#endif

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
#include "example_common_private.h" // for example_ethernet_connect() helper
#include "esp_eth.h"
#endif

#include "mdns.h"
#include "ot_examples_common.h"

#include "openthread/backbone_router_ftd.h"
#include "openthread/border_router.h"
#include "openthread/cli.h"
#include "openthread/dataset_ftd.h"
#include "openthread/instance.h"
#include "openthread/ip6.h"
#include "openthread/logging.h"
#include "openthread/thread_ftd.h"

#if !CONFIG_EXAMPLE_CONNECT_WIFI && !CONFIG_EXAMPLE_CONNECT_ETHERNET
#error No backbone netif! Enable at least Wi-Fi or Ethernet.
#endif

#define TAG "esp_ot_br"

/* ----------------------- Policy tuning knobs ---------------------------- */

#define ETH_WAIT_MS                 (10000)   // wait up to 10s for Ethernet IP
#define WIFI_WAIT_MS                (12000)   // wait up to 12s for Wi-Fi IP
#define SOFTAP_WINDOW_MS            (180000)  // 3 minutes SoftAP window
#define FAIL_COUNT_TRIGGER_SOFTAP   (5)       // after 5 failed boots, open SoftAP
#define WIFI_SSID_MAX_LEN           (32)
#define WIFI_PASS_MAX_LEN           (64)

/* ----------------------- NVS keys --------------------------------------- */

#define NVS_NS_BR                   "br"
#define NVS_KEY_FAIL_COUNT          "fail_count"
#define NVS_KEY_LAST_SSID           "last_ssid"

/* ----------------------- Events / state --------------------------------- */

#define BIT_ETH_GOT_IP              (1U << 0)
#define BIT_WIFI_GOT_IP             (1U << 1)

static EventGroupHandle_t s_net_ev;

static esp_netif_t *s_backbone = NULL;
static bool s_backbone_locked = false;

/* For Ethernet connect helper isolation */
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
static TaskHandle_t s_eth_task = NULL;
#endif

/* ------------------------------------------------------------------------ */
/* Helpers: netif discovery                                                  */
/* ------------------------------------------------------------------------ */

static esp_netif_t *br_find_wifi_netif(void)
{
#if CONFIG_EXAMPLE_CONNECT_WIFI
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("example_netif_sta");
    if (n) return n;

    n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (n) return n;
#endif
    return NULL;
}

static esp_netif_t *br_find_eth_netif(void)
{
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("example_netif_eth");
    if (n) return n;

    n = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (n) return n;
#endif
    return NULL;
}

static void br_dump_netifs(void)
{
    esp_netif_t *n = NULL;
    while ((n = esp_netif_next_unsafe(n)) != NULL) {
        const char *k = esp_netif_get_ifkey(n);
        ESP_LOGI(TAG, "netif: if_key=%s", k ? k : "(null)");
    }
}

static void br_lock_backbone(esp_netif_t *netif)
{
    if (!s_backbone_locked && netif) {
        s_backbone = netif;
        s_backbone_locked = true;
        ESP_LOGI(TAG, "Backbone locked to if_key=%s", esp_netif_get_ifkey(netif));
    }
}

/* ------------------------------------------------------------------------ */
/* Helpers: NVS fail counter                                                 */
/* ------------------------------------------------------------------------ */

static uint8_t br_nvs_get_fail_count(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NVS_NS_BR, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_FAIL_COUNT, &v);
        nvs_close(h);
    }
    return v;
}

static void br_nvs_set_fail_count(uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BR, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY_FAIL_COUNT, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void br_nvs_reset_fail_count(void)
{
    br_nvs_set_fail_count(0);
}

static bool br_nvs_get_last_ssid(char *out, size_t out_len)
{
    if (!out || out_len == 0) return false;
    out[0] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NS_BR, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    size_t required = 0;
    esp_err_t err = nvs_get_str(h, NVS_KEY_LAST_SSID, NULL, &required);
    if (err != ESP_OK || required == 0 || required > out_len) {
        nvs_close(h);
        return false;
    }

    err = nvs_get_str(h, NVS_KEY_LAST_SSID, out, &required);
    nvs_close(h);
    return (err == ESP_OK && out[0] != '\0');
}

static void br_nvs_set_last_ssid(const char *ssid)
{
    if (!ssid) ssid = "";
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BR, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_KEY_LAST_SSID, ssid);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* If SSID changed since last successful run, clear fail counter */
static void br_reset_fail_if_ssid_changed(const char *current_ssid)
{
    char last[WIFI_SSID_MAX_LEN] = {0};
    bool have_last = br_nvs_get_last_ssid(last, sizeof(last));
    if (!have_last) {
        return;
    }
    if (current_ssid && strcmp(last, current_ssid) != 0) {
        ESP_LOGW(TAG, "SSID changed (%s -> %s), resetting fail counter",
                 last, current_ssid);
        br_nvs_reset_fail_count();
    }
}

/* ------------------------------------------------------------------------ */
/* Event handlers                                                           */
/* ------------------------------------------------------------------------ */

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base != IP_EVENT) {
        return;
    }

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
    if (id == IP_EVENT_ETH_GOT_IP) {
        ESP_LOGI(TAG, "IP_EVENT_ETH_GOT_IP");
        xEventGroupSetBits(s_net_ev, BIT_ETH_GOT_IP);
        if (!s_backbone_locked) {
            br_lock_backbone(br_find_eth_netif());
        }
        return;
    }
#endif

#if CONFIG_EXAMPLE_CONNECT_WIFI
    if (id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
        xEventGroupSetBits(s_net_ev, BIT_WIFI_GOT_IP);
        if (!s_backbone_locked) {
            br_lock_backbone(br_find_wifi_netif());
        }
        return;
    }
#endif
}

#if CONFIG_EXAMPLE_CONNECT_WIFI
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base != WIFI_EVENT) {
        return;
    }

    /* Not strictly required for the policy, but useful for logs/debug */
#if defined(WIFI_EVENT_STA_DISCONNECTED)
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED");
    }
#endif
}
#endif

/* ------------------------------------------------------------------------ */
/* Ethernet attempt (non-blocking to policy)                                 */
/* ------------------------------------------------------------------------ */

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
static void br_eth_connect_task(void *arg)
{
    (void)arg;
    /* This helper blocks forever when no cable/DHCP. Keep it isolated. */
    esp_err_t err = example_ethernet_connect();
    ESP_LOGW(TAG, "example_ethernet_connect() returned: %s", esp_err_to_name(err));
    s_eth_task = NULL;
    vTaskDelete(NULL);
}

static bool br_try_eth_with_wait(uint32_t wait_ms)
{
    ESP_LOGI(TAG, "Trying Ethernet...");

    if (s_eth_task == NULL) {
        xTaskCreate(br_eth_connect_task, "eth_conn", 4096, NULL, 4, &s_eth_task);
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_net_ev,
        BIT_ETH_GOT_IP,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(wait_ms)
    );

    if (bits & BIT_ETH_GOT_IP) {
        ESP_LOGI(TAG, "Ethernet got IP");
        return true;
    }

    ESP_LOGW(TAG, "Ethernet timed out after %u ms", (unsigned)wait_ms);
    return false;
}
#endif

/* ------------------------------------------------------------------------ */
/* Wi-Fi attempt + SoftAP provisioning                                       */
/* ------------------------------------------------------------------------ */

#if CONFIG_EXAMPLE_CONNECT_WIFI

static bool br_get_nvs_wifi_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
    /* These come from esp_br_wifi_config (saved in NVS) */
    if (ssid && ssid_len) ssid[0] = '\0';
    if (pass && pass_len) pass[0] = '\0';

    if (esp_ot_wifi_config_get_ssid(ssid) == ESP_OK) {
        esp_ot_wifi_config_get_password(pass);
        return (ssid[0] != '\0');
    }
    return false;
#else
    /* If you disabled SoftAP setup, fall back to Kconfig values */
    if (ssid && ssid_len) {
        strncpy(ssid, CONFIG_EXAMPLE_WIFI_SSID, ssid_len - 1);
        ssid[ssid_len - 1] = '\0';
    }
    if (pass && pass_len) {
        strncpy(pass, CONFIG_EXAMPLE_WIFI_PASSWORD, pass_len - 1);
        pass[pass_len - 1] = '\0';
    }
    return (ssid && ssid[0] != '\0');
#endif
}

static bool br_wifi_connect_and_wait(const char *ssid, const char *pass, uint32_t wait_ms)
{
    if (!ssid || ssid[0] == '\0') {
        return false;
    }

    ESP_LOGI(TAG, "Trying Wi-Fi SSID: %s", ssid);

#if CONFIG_OPENTHREAD_CLI_WIFI
    /* esp_ot_wifi_connect exists when Wi-Fi CLI helper is enabled */
    if (esp_ot_wifi_connect(ssid, pass ? pass : "") != ESP_OK) {
        ESP_LOGW(TAG, "esp_ot_wifi_connect failed");
        return false;
    }
#else
    /* If you disable CONFIG_OPENTHREAD_CLI_WIFI, you must provide your own Wi-Fi connect routine.
       For now, keep CONFIG_OPENTHREAD_CLI_WIFI=y. */
    ESP_LOGE(TAG, "CONFIG_OPENTHREAD_CLI_WIFI is disabled; no Wi-Fi connect function available");
    return false;
#endif

    EventBits_t bits = xEventGroupWaitBits(
        s_net_ev,
        BIT_WIFI_GOT_IP,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(wait_ms)
    );

    if (bits & BIT_WIFI_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi got IP");
        return true;
    }

    ESP_LOGW(TAG, "Wi-Fi timed out after %u ms", (unsigned)wait_ms);
    return false;
}

#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
static bool br_softap_provision_window(uint32_t window_ms, char *out_ssid, size_t out_ssid_len,
                                       char *out_pass, size_t out_pass_len)
{
    if (out_ssid && out_ssid_len) out_ssid[0] = '\0';
    if (out_pass && out_pass_len) out_pass[0] = '\0';

    ESP_LOGW(TAG, "Starting SoftAP provisioning window (%u ms)", (unsigned)window_ms);
    esp_br_wifi_config_start();

    /* This blocks until configured OR timeout */
    esp_br_wifi_config_get_configured_wifi(
        out_ssid, out_ssid_len,
        out_pass, out_pass_len,
        window_ms
    );

    esp_br_wifi_config_stop();

    if (out_ssid && out_ssid[0] != '\0') {
        ESP_LOGI(TAG, "Provisioned Wi-Fi SSID: %s", out_ssid);
        return true;
    }

    ESP_LOGW(TAG, "SoftAP provisioning window expired with no new creds");
    return false;
}
#endif /* CONFIG_OPENTHREAD_BR_SOFTAP_SETUP */

#endif /* CONFIG_EXAMPLE_CONNECT_WIFI */

/* ------------------------------------------------------------------------ */
/* OTBR init task (policy + backbone selection)                              */
/* ------------------------------------------------------------------------ */

#if CONFIG_OPENTHREAD_BR_AUTO_START
static void ot_br_init(void *ctx)
{
    (void)ctx;

    /* Ensure event group exists */
    if (s_net_ev == NULL) {
        s_net_ev = xEventGroupCreate();
    } else {
        xEventGroupClearBits(s_net_ev, BIT_ETH_GOT_IP | BIT_WIFI_GOT_IP);
    }

    /* Register event handlers (safe to register once; duplicates return ESP_ERR_INVALID_STATE in some IDF setups) */
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL);

#if CONFIG_EXAMPLE_CONNECT_WIFI
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL);
#endif

    /* mDNS */
    ESP_ERROR_CHECK(mdns_init());

    /* For debugging: show what netifs exist at this moment */
    br_dump_netifs();

    /* Read stored fail count */
    uint8_t fail_count = br_nvs_get_fail_count();
    ESP_LOGI(TAG, "Boot fail_count=%u", (unsigned)fail_count);

    /* 1) Prefer Ethernet (if enabled) */
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
    if (br_try_eth_with_wait(ETH_WAIT_MS)) {
        /* Success via Ethernet */
        br_nvs_reset_fail_count();
        /* lock last_ssid not updated here */
        goto backbone_ready;
    }
#endif

    /* 2) Wi-Fi path (if enabled) */
#if CONFIG_EXAMPLE_CONNECT_WIFI
    {
        char ssid[WIFI_SSID_MAX_LEN] = {0};
        char pass[WIFI_PASS_MAX_LEN] = {0};
        bool have_creds = br_get_nvs_wifi_creds(ssid, sizeof(ssid), pass, sizeof(pass));

        /* If SSID changed (compared to last known good), reset fail count */
        if (have_creds) {
            br_reset_fail_if_ssid_changed(ssid);
            fail_count = br_nvs_get_fail_count();
        }

        /* If no creds, immediately go to SoftAP setup (easy onboarding) */
        if (!have_creds) {
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
            bool provisioned = br_softap_provision_window(SOFTAP_WINDOW_MS, ssid, sizeof(ssid), pass, sizeof(pass));
            if (!provisioned) {
                /* No creds entered in 3 minutes -> reboot once (self-heal path) */
                ESP_LOGW(TAG, "No creds entered; rebooting to self-heal...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            }
            /* Try Wi-Fi once with new creds */
            if (br_wifi_connect_and_wait(ssid, pass, WIFI_WAIT_MS)) {
                br_nvs_reset_fail_count();
                br_nvs_set_last_ssid(ssid);
                goto backbone_ready;
            }
#else
            ESP_LOGE(TAG, "No Wi-Fi creds and SoftAP setup disabled; cannot proceed");
#endif
        } else {
            /* Have creds */
            bool wifi_ok = br_wifi_connect_and_wait(ssid, pass, WIFI_WAIT_MS);
            if (wifi_ok) {
                br_nvs_reset_fail_count();
                br_nvs_set_last_ssid(ssid);
                goto backbone_ready;
            }

            /* Wi-Fi failed -> bump fail_count */
            fail_count = br_nvs_get_fail_count();
            if (fail_count < 255) {
                fail_count++;
            }
            br_nvs_set_fail_count(fail_count);
            ESP_LOGW(TAG, "Wi-Fi failed; fail_count now %u", (unsigned)fail_count);

            /* If we hit the trigger, open SoftAP for 3 minutes to allow reconfig,
               but do NOT get stuck there forever: reboot once if nothing changes. */
            if (fail_count >= FAIL_COUNT_TRIGGER_SOFTAP) {
#if CONFIG_OPENTHREAD_BR_SOFTAP_SETUP
                char new_ssid[WIFI_SSID_MAX_LEN] = {0};
                char new_pass[WIFI_PASS_MAX_LEN] = {0};

                bool provisioned = br_softap_provision_window(SOFTAP_WINDOW_MS,
                                                              new_ssid, sizeof(new_ssid),
                                                              new_pass, sizeof(new_pass));

                if (!provisioned) {
                    ESP_LOGW(TAG, "SoftAP window expired; rebooting to self-heal...");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                }

                /* Provisioned: try Wi-Fi once with updated creds */
                if (br_wifi_connect_and_wait(new_ssid, new_pass, WIFI_WAIT_MS)) {
                    br_nvs_reset_fail_count();
                    br_nvs_set_last_ssid(new_ssid);
                    goto backbone_ready;
                }

                /* Even new creds failed -> reboot (keeps trying in future boots, SoftAP will return after 5 failures) */
                ESP_LOGW(TAG, "Wi-Fi still failing after reprovision; rebooting...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
#else
                ESP_LOGE(TAG, "Fail_count reached %u but SoftAP setup disabled; rebooting", (unsigned)fail_count);
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
#endif
            }

            /* Not yet at trigger: just reboot to try again next boot */
            ESP_LOGW(TAG, "Rebooting after Wi-Fi failure (fail_count=%u)", (unsigned)fail_count);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
    }
#endif /* CONFIG_EXAMPLE_CONNECT_WIFI */

    /* If we got here: no Ethernet success and no Wi-Fi path (or both disabled) */
    fail_count = br_nvs_get_fail_count();
    if (fail_count < 255) fail_count++;
    br_nvs_set_fail_count(fail_count);

    ESP_LOGE(TAG, "No backbone available; rebooting (fail_count=%u)", (unsigned)fail_count);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

backbone_ready:
    /* Choose backbone if not already locked */
    if (!s_backbone_locked) {
        /* Prefer eth netif if it exists and has IP bit set; else wifi */
        EventBits_t bits = xEventGroupGetBits(s_net_ev);
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
        if (bits & BIT_ETH_GOT_IP) {
            br_lock_backbone(br_find_eth_netif());
        }
#endif
#if CONFIG_EXAMPLE_CONNECT_WIFI
        if (!s_backbone_locked && (bits & BIT_WIFI_GOT_IP)) {
            br_lock_backbone(br_find_wifi_netif());
        }
#endif
    }

    if (s_backbone == NULL) {
        ESP_LOGE(TAG, "Backbone netif still NULL. Dumping netifs and rebooting.");
        br_dump_netifs();
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    /* Now init OpenThread BR with the selected backbone */
    esp_openthread_lock_acquire(portMAX_DELAY);

    esp_openthread_set_backbone_netif(s_backbone);
    ESP_LOGI(TAG, "Backbone netif set: if_key=%s", esp_netif_get_ifkey(s_backbone));

    ESP_ERROR_CHECK(esp_openthread_border_router_init());

#if CONFIG_EXAMPLE_CONNECT_WIFI
#if CONFIG_OPENTHREAD_CLI_WIFI
    esp_ot_wifi_border_router_init_flag_set(true);
#endif
#endif

    /* Thread dataset / autostart logic (keep your existing behavior) */
    otOperationalDatasetTlvs dataset;
    otError err = otDatasetGetActiveTlvs(esp_openthread_get_instance(), &dataset);
    if (err != OT_ERROR_NONE) {
        otOperationalDataset new_dataset;
        err = otDatasetCreateNewNetwork(esp_openthread_get_instance(), &new_dataset);
        assert(err == OT_ERROR_NONE);

        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_BASE) == ESP_OK) {
            char network_name[OT_NETWORK_NAME_MAX_SIZE + 1];
            snprintf(network_name, sizeof(network_name), "ESP-BR-%02X%02X", mac[4], mac[5]);
            memcpy(new_dataset.mNetworkName.m8, network_name, strlen(network_name) + 1);
            new_dataset.mComponents.mIsNetworkNamePresent = true;
        }

        otDatasetConvertToTlvs(&new_dataset, &dataset);
        ESP_LOGI(TAG, "Created new random Thread dataset");
    }

    ESP_ERROR_CHECK(esp_openthread_auto_start(&dataset));

    esp_openthread_lock_release();

    vTaskDelete(NULL);
}
#endif /* CONFIG_OPENTHREAD_BR_AUTO_START */

/* ------------------------------------------------------------------------ */
/* Entry point                                                              */
/* ------------------------------------------------------------------------ */

void launch_openthread_border_router(const esp_openthread_config_t *config,
                                     const esp_rcp_update_config_t *update_config)
{
#if CONFIG_OPENTHREAD_CLI
    ot_console_start();
#endif

#if CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE
    ot_external_coexist_init();
#endif

#if CONFIG_AUTO_UPDATE_RCP
    ESP_ERROR_CHECK(esp_rcp_update_init(update_config));
    esp_ot_register_rcp_handler();
#else
    (void)update_config;
#endif

    ESP_ERROR_CHECK(esp_openthread_start(config));

#if CONFIG_AUTO_UPDATE_RCP
    esp_ot_update_rcp_if_different();
#endif

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif

#if CONFIG_OPENTHREAD_BR_AUTO_START
    xTaskCreate(ot_br_init, "ot_br_init", 8192, NULL, 4, NULL);
#endif
}
