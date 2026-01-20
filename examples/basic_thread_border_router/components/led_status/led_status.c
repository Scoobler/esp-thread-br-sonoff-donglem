#include "led_status.h"

#include <stdint.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "openthread/thread_ftd.h"
#include "openthread/instance.h"

#define TAG "led_status"

/* GPIOs */
#define LED_GPIO_R 4
#define LED_GPIO_G 14
#define LED_GPIO_B 2

/* If your RGB LED is common-anode (active-low), set to 1 */
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif

/* LEDC config */
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_TIMER_BITS     LEDC_TIMER_8_BIT
#define LEDC_FREQ_HZ        5000

#define LEDC_CH_R           LEDC_CHANNEL_0
#define LEDC_CH_G           LEDC_CHANNEL_1
#define LEDC_CH_B           LEDC_CHANNEL_2

/* Animation */
#define TICK_MS             20          // task tick / update interval
#define PULSE_PERIOD_MS     2000        // every 2s
#define PULSE_ON_MS         200         // HARD blink duration
#define OT_GRACE_MS         15000        // no RED blink for 15s after OT ready

/* Base brightness scale (0..255). 255 = full brightness.
   If the blink still isn't obvious, reduce BASE_SCALE to e.g. 160. */
#define BASE_SCALE          255

typedef struct { uint8_t r, g, b; } rgb8_t;

static volatile led_if_state_t s_if_state = LED_IF_UNKNOWN;
static volatile bool s_enabled = true;
static volatile bool s_ot_ready = false;

static const char *if_to_str(led_if_state_t st)
{
    switch (st) {
        case LED_IF_ETH:    return "ETH";
        case LED_IF_WIFI:   return "WIFI";
        case LED_IF_SOFTAP: return "SOFTAP";
        default:            return "UNKNOWN";
    }
}

void led_status_set_ot_ready(bool ready)
{
    bool prev = s_ot_ready;
    s_ot_ready = ready;

    if (prev != ready) {
        ESP_LOGI(TAG, "OT ready: %s", ready ? "YES" : "NO");
    }
}

static inline uint8_t apply_active_level(uint8_t v)
{
#if LED_ACTIVE_LOW
    return (uint8_t)(255 - v);
#else
    return v;
#endif
}

static inline uint8_t scale_u8(uint8_t v, uint8_t scale)
{
    return (uint8_t)(((uint16_t)v * (uint16_t)scale) / 255U);
}

static esp_err_t ledc_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    r = apply_active_level(r);
    g = apply_active_level(g);
    b = apply_active_level(b);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CH_R, r));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CH_G, g));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CH_B, b));

    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CH_R));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CH_G));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CH_B));
    return ESP_OK;
}

static rgb8_t base_colour_for_if(led_if_state_t st)
{
    switch (st) {
        case LED_IF_ETH:    return (rgb8_t){ .r=0,   .g=0,   .b=255 }; // Blue
        case LED_IF_WIFI:   return (rgb8_t){ .r=255, .g=40,  .b=0   }; // Orange
        case LED_IF_SOFTAP: return (rgb8_t){ .r=128, .g=0,   .b=128 }; // Purple
        default:            return (rgb8_t){ .r=0,   .g=0,   .b=0   };
    }
}

static otDeviceRole thread_role(void)
{
    if (!s_ot_ready) {
        return OT_DEVICE_ROLE_DISABLED;
    }

    otInstance *ins = esp_openthread_get_instance();
    if (ins == NULL) {
        return OT_DEVICE_ROLE_DISABLED;
    }

    otDeviceRole role = OT_DEVICE_ROLE_DISABLED;
    esp_openthread_lock_acquire(portMAX_DELAY);
    role = otThreadGetDeviceRole(ins);
    esp_openthread_lock_release();
    return role;
}

static bool role_is_attached(otDeviceRole role)
{
    return (role == OT_DEVICE_ROLE_CHILD ||
            role == OT_DEVICE_ROLE_ROUTER ||
            role == OT_DEVICE_ROLE_LEADER);
}

static const char *role_to_str(otDeviceRole role)
{
    switch (role) {
        case OT_DEVICE_ROLE_DISABLED: return "DISABLED";
        case OT_DEVICE_ROLE_DETACHED: return "DETACHED";
        case OT_DEVICE_ROLE_CHILD:    return "CHILD";
        case OT_DEVICE_ROLE_ROUTER:   return "ROUTER";
        case OT_DEVICE_ROLE_LEADER:   return "LEADER";
        default:                      return "UNKNOWN";
    }
}

static void led_task(void *arg)
{
    (void)arg;

    uint32_t t_ms = 0;
    led_if_state_t last_if = LED_IF_UNKNOWN;
    bool last_ot_ready = false;
    otDeviceRole last_role = OT_DEVICE_ROLE_DISABLED;

    /* Grace timer start (relative to t_ms) */
    uint32_t ot_ready_at_ms = 0;

    for (;;) {
        if (!s_enabled) {
            ledc_set_rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* Log interface changes */
        if (s_if_state != last_if) {
            ESP_LOGI(TAG, "Interface state -> %s", if_to_str(s_if_state));
            last_if = s_if_state;
        }

        /* Observe OT ready edge and start grace */
        if (s_ot_ready != last_ot_ready) {
            ESP_LOGI(TAG, "OT ready observed in task -> %s", s_ot_ready ? "YES" : "NO");
            if (!last_ot_ready && s_ot_ready) {
                ot_ready_at_ms = t_ms; // start grace window now
                ESP_LOGI(TAG, "Starting OT grace window (%u ms)", (unsigned)OT_GRACE_MS);
            }
            last_ot_ready = s_ot_ready;
        }

        const rgb8_t base_raw = base_colour_for_if(s_if_state);
        const rgb8_t base = (rgb8_t){
            .r = scale_u8(base_raw.r, (uint8_t)BASE_SCALE),
            .g = scale_u8(base_raw.g, (uint8_t)BASE_SCALE),
            .b = scale_u8(base_raw.b, (uint8_t)BASE_SCALE),
        };

        /* No pulse until OT is ready */
        if (!s_ot_ready) {
            ledc_set_rgb(base.r, base.g, base.b);
            vTaskDelay(pdMS_TO_TICKS(TICK_MS));
            t_ms += TICK_MS;
            continue;
        }

        /* Thread role */
        otDeviceRole role = thread_role();
        if (role != last_role) {
            ESP_LOGI(TAG, "Thread role -> %s", role_to_str(role));
            last_role = role;
        }

        const bool attached = role_is_attached(role);

        /* Grace logic:
           - During OT_GRACE_MS after OT ready: allow GREEN blink if attached,
             but suppress RED blink if not attached.
           - After grace: normal GREEN/RED blink. */
        const bool in_grace = ((t_ms - ot_ready_at_ms) < OT_GRACE_MS);

        /* Determine whether we should blink this cycle */
        bool do_blink = true;
        rgb8_t pulse_col = attached
            ? (rgb8_t){ .r=0,   .g=255, .b=0 }   // Green
            : (rgb8_t){ .r=255, .g=0,   .b=0 };  // Red

        if (in_grace && !attached) {
            do_blink = false; // suppress red during grace
        }

        /* Blink: 200ms ON every 2s, else base */
        const uint32_t t_in_period = (t_ms % PULSE_PERIOD_MS);

        if (do_blink && (t_in_period < PULSE_ON_MS)) {
            ledc_set_rgb(pulse_col.r, pulse_col.g, pulse_col.b);
        } else {
            ledc_set_rgb(base.r, base.g, base.b);
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        t_ms += TICK_MS;
    }
}

esp_err_t led_status_init(void)
{
    /* Timer */
    ledc_timer_config_t timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_TIMER_BITS,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "timer");

    /* Channels */
    ledc_channel_config_t ch_r = {
        .gpio_num   = LED_GPIO_R,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CH_R,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config_t ch_g = ch_r; ch_g.gpio_num = LED_GPIO_G; ch_g.channel = LEDC_CH_G;
    ledc_channel_config_t ch_b = ch_r; ch_b.gpio_num = LED_GPIO_B; ch_b.channel = LEDC_CH_B;

    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_r), TAG, "ch_r");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_g), TAG, "ch_g");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_b), TAG, "ch_b");

    /* Optional RGB sanity test (keep or remove) */
    ledc_set_rgb(255, 0, 0); vTaskDelay(pdMS_TO_TICKS(300));
    ledc_set_rgb(0, 255, 0); vTaskDelay(pdMS_TO_TICKS(300));
    ledc_set_rgb(0, 0, 255); vTaskDelay(pdMS_TO_TICKS(300));

    s_if_state = LED_IF_UNKNOWN;
    s_enabled = true;

    xTaskCreate(led_task, "led_status", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "LED status task started (active_low=%d, base_scale=%d, grace_ms=%u)",
             LED_ACTIVE_LOW, BASE_SCALE, (unsigned)OT_GRACE_MS);
    return ESP_OK;
}

void led_status_set_interface(led_if_state_t st)
{
    if (s_if_state != st) {
        ESP_LOGI(TAG, "led_status_set_interface(%s)", if_to_str(st));
    }
    s_if_state = st;
}

void led_status_set_enabled(bool enabled)
{
    if (s_enabled != enabled) {
        ESP_LOGI(TAG, "LED enabled: %s", enabled ? "YES" : "NO");
    }
    s_enabled = enabled;
}
