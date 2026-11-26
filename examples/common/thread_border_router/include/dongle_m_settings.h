/* SPDX-License-Identifier: CC0-1.0 */

#pragma once

#include "esp_netif.h"

typedef enum {
    DONGLE_M_NETMODE_AUTO = 0,
    DONGLE_M_NETMODE_ETHERNET = 1,
    DONGLE_M_NETMODE_WIFI = 2,
} dongle_m_netmode_t;

/* Loads the NVS device settings, applies the mDNS hostname and registers the "dongle" console command. */
void dongle_m_settings_init(void);

/* Network mode for this boot; changes made on the console apply after a reboot. */
dongle_m_netmode_t dongle_m_settings_netmode(void);

/* Uses a configured hostname as DHCP hostname; leaves the ESP-IDF default otherwise. */
void dongle_m_settings_apply_dhcp_hostname(esp_netif_t *netif);
