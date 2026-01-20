#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_IF_UNKNOWN = 0,
    LED_IF_ETH,
    LED_IF_WIFI,
    LED_IF_SOFTAP,
} led_if_state_t;

/**
 * Init PWM + start background task.
 * Safe to call early in app_main().
 */
esp_err_t led_status_init(void);

/** Set the “base colour” source (Eth/Wi-Fi/SoftAP). */
void led_status_set_interface(led_if_state_t st);

void led_status_set_ot_ready(bool ready);

/** Optional: force off (e.g., deep sleep) */
void led_status_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif
