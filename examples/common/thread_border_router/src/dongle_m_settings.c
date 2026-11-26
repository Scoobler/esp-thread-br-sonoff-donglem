/* SPDX-License-Identifier: CC0-1.0 */

#include "dongle_m_settings.h"

#include "sdkconfig.h"

#if CONFIG_ESP_BR_BOARD_SONOFF_DONGLE_M

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mdns.h"
#include "nvs.h"

#define TAG "dongle_m_settings"
#define NVS_NAMESPACE "br"
#define NVS_NETMODE "net_mode"
#define NVS_HOSTNAME "hostname"
#define DEFAULT_HOSTNAME "esp-ot-br"
#define HOSTNAME_MAX_LEN 32

static const char *const k_netmode_names[] = {"auto", "ethernet", "wifi"};

static dongle_m_netmode_t s_netmode;
static char s_hostname[HOSTNAME_MAX_LEN + 1];
static bool s_custom_hostname;

static dongle_m_netmode_t default_netmode(void)
{
#if CONFIG_ESP_BR_DONGLE_M_NETMODE_DEFAULT_ETHERNET
    return DONGLE_M_NETMODE_ETHERNET;
#elif CONFIG_ESP_BR_DONGLE_M_NETMODE_DEFAULT_WIFI
    return DONGLE_M_NETMODE_WIFI;
#else
    return DONGLE_M_NETMODE_AUTO;
#endif
}

static dongle_m_netmode_t stored_netmode(void)
{
    nvs_handle_t handle;
    uint8_t value = UINT8_MAX;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, NVS_NETMODE, &value);
        nvs_close(handle);
    }
    return value <= DONGLE_M_NETMODE_WIFI ? (dongle_m_netmode_t)value : default_netmode();
}

static bool stored_hostname(char *hostname, size_t length)
{
    nvs_handle_t handle;
    bool found = false;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        found = nvs_get_str(handle, NVS_HOSTNAME, hostname, &length) == ESP_OK && hostname[0] != '\0';
        nvs_close(handle);
    }
    return found;
}

static esp_err_t store_netmode(dongle_m_netmode_t mode)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, NVS_NETMODE, (uint8_t)mode);
        err = err == ESP_OK ? nvs_commit(handle) : err;
        nvs_close(handle);
    }
    return err;
}

static esp_err_t store_hostname(const char *hostname)
{
    /* NULL removes the stored hostname, so the Kconfig default applies again. */
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = hostname ? nvs_set_str(handle, NVS_HOSTNAME, hostname) : nvs_erase_key(handle, NVS_HOSTNAME);
        err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
        err = err == ESP_OK ? nvs_commit(handle) : err;
        nvs_close(handle);
    }
    return err;
}

static bool hostname_valid(const char *hostname)
{
    /* A single DNS label that also fits ESP-IDF's DHCP hostname limit. */
    size_t length = strlen(hostname);
    if (length == 0 || length > HOSTNAME_MAX_LEN || hostname[0] == '-' || hostname[length - 1] == '-') {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        char c = hostname[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

static void apply_mdns_hostname(const char *hostname)
{
    char current[MDNS_NAME_BUF_LEN] = {0};
    if (mdns_hostname_get(current) == ESP_OK && strcmp(current, hostname) == 0) {
        return;
    }
    esp_err_t err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set mDNS hostname %s: %s", hostname, esp_err_to_name(err));
    }
}

static void print_settings(void)
{
    char hostname[HOSTNAME_MAX_LEN + 1] = {0};
    dongle_m_netmode_t stored = stored_netmode();
    bool custom = stored_hostname(hostname, sizeof(hostname));

    printf("netmode:  %s%s\n", k_netmode_names[stored],
           stored != s_netmode ? " (applies after reboot)" : "");
    printf("hostname: %s%s\n", custom ? hostname : CONFIG_ESP_BR_DONGLE_M_HOSTNAME, custom ? "" : " (default)");
}

static void print_usage(void)
{
    printf("Usage:\n"
           "  dongle show\n"
           "  dongle netmode auto|ethernet|wifi\n"
           "  dongle hostname <name>|--default\n"
           "  dongle reboot\n");
}

static int cmd_netmode(const char *value)
{
    for (dongle_m_netmode_t mode = DONGLE_M_NETMODE_AUTO; mode <= DONGLE_M_NETMODE_WIFI; mode++) {
        if (strcmp(value, k_netmode_names[mode]) == 0) {
            if (store_netmode(mode) != ESP_OK) {
                printf("Failed to store the network mode\n");
                return 1;
            }
            printf("Network mode set to %s; run 'dongle reboot' to apply\n", k_netmode_names[mode]);
            return 0;
        }
    }
    print_usage();
    return 1;
}

static int cmd_hostname(const char *value)
{
    bool reset = strcmp(value, "--default") == 0;
    if (!reset && !hostname_valid(value)) {
        printf("Invalid hostname: use 1-%d lowercase letters, digits or '-', not starting or ending with '-'\n",
               HOSTNAME_MAX_LEN);
        return 1;
    }
    if (store_hostname(reset ? NULL : value) != ESP_OK) {
        printf("Failed to store the hostname\n");
        return 1;
    }
    apply_mdns_hostname(reset ? CONFIG_ESP_BR_DONGLE_M_HOSTNAME : value);
    printf("Hostname set to %s; the mDNS name changes now, the DHCP hostname after 'dongle reboot'\n",
           reset ? CONFIG_ESP_BR_DONGLE_M_HOSTNAME : value);
    return 0;
}

static int cmd_dongle(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "show") == 0)) {
        print_settings();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "netmode") == 0) {
        return cmd_netmode(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "hostname") == 0) {
        return cmd_hostname(argv[2]);
    }
    if (argc == 2 && strcmp(argv[1], "reboot") == 0) {
        esp_restart();
    }
    print_usage();
    return 1;
}

void dongle_m_settings_init(void)
{
    s_netmode = stored_netmode();
    s_custom_hostname = stored_hostname(s_hostname, sizeof(s_hostname));
    if (!s_custom_hostname) {
        snprintf(s_hostname, sizeof(s_hostname), "%s", CONFIG_ESP_BR_DONGLE_M_HOSTNAME);
        s_custom_hostname = strcmp(s_hostname, DEFAULT_HOSTNAME) != 0;
    }
    /* esp_ot_br.c sets the upstream default before the board code runs. */
    apply_mdns_hostname(s_hostname);
    ESP_LOGI(TAG, "Network mode: %s, hostname: %s", k_netmode_names[s_netmode], s_hostname);

    /* Registered before backbone selection so the mode can be changed while Ethernet-only waits for a cable. */
    const esp_console_cmd_t command = {
        .command = "dongle",
        .help = "Show or change Dongle-M settings: "
                "dongle [show | netmode auto|ethernet|wifi | hostname <name>|--default | reboot]",
        .func = &cmd_dongle,
    };
    esp_err_t err = esp_console_cmd_register(&command);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register the dongle console command: %s", esp_err_to_name(err));
    }
}

dongle_m_netmode_t dongle_m_settings_netmode(void)
{
    return s_netmode;
}

void dongle_m_settings_apply_dhcp_hostname(esp_netif_t *netif)
{
    if (!netif || !s_custom_hostname) {
        return;
    }
    esp_err_t err = esp_netif_set_hostname(netif, s_hostname);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set DHCP hostname %s: %s", s_hostname, esp_err_to_name(err));
    }
}

#endif
