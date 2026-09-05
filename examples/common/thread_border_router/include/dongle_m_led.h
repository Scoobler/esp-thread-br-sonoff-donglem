/* SPDX-License-Identifier: CC0-1.0 */

#pragma once

#include <stdbool.h>

typedef enum {
    DONGLE_M_LED_INTERFACE_ETHERNET,
    DONGLE_M_LED_INTERFACE_WIFI,
    DONGLE_M_LED_INTERFACE_SOFTAP,
} dongle_m_led_interface_t;

void dongle_m_led_init(void);
void dongle_m_led_set_interface(dongle_m_led_interface_t interface);
void dongle_m_led_set_thread_ready(bool ready);
