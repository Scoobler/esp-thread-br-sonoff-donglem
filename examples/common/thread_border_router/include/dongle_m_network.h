/* SPDX-License-Identifier: CC0-1.0 */

#pragma once

#include "esp_err.h"
#include "esp_netif.h"

/* Selects and locks the Dongle-M infrastructure interface for this boot. */
esp_err_t dongle_m_network_select_backbone(esp_netif_t **backbone);
