/* SPDX-License-Identifier: CC0-1.0 */

#include "dongle_m_led.h"

#include "sdkconfig.h"

#if CONFIG_ESP_BR_BOARD_SONOFF_DONGLE_M

#include <stdint.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "openthread/instance.h"
#include "openthread/thread.h"

#define TAG "dongle_m_led"
typedef struct { uint8_t red, green, blue; } rgb_t;

/* The Dongle-M RGB LED is active-high (common-cathode). */
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_TIMER_BITS LEDC_TIMER_8_BIT
#define LEDC_FREQ_HZ    5000
#define LEDC_CH_RED     LEDC_CHANNEL_0
#define LEDC_CH_GREEN   LEDC_CHANNEL_1
#define LEDC_CH_BLUE    LEDC_CHANNEL_2

static volatile dongle_m_led_interface_t s_interface = DONGLE_M_LED_INTERFACE_ETHERNET;
static volatile bool s_thread_ready;

static rgb_t base_colour(void)
{
    switch (s_interface) {
    case DONGLE_M_LED_INTERFACE_ETHERNET: return (rgb_t){0, 0, 255};
    case DONGLE_M_LED_INTERFACE_WIFI: return (rgb_t){255, 40, 0};
    case DONGLE_M_LED_INTERFACE_SOFTAP: return (rgb_t){128, 0, 128};
    default: return (rgb_t){0, 0, 0};
    }
}

static void set_rgb(rgb_t colour)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CH_RED, colour.red));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CH_GREEN, colour.green));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CH_BLUE, colour.blue));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CH_RED));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CH_GREEN));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CH_BLUE));
}

static bool attached(void)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!s_thread_ready || !instance) {
        return false;
    }
    otDeviceRole role;
    esp_openthread_lock_acquire(portMAX_DELAY);
    role = otThreadGetDeviceRole(instance);
    esp_openthread_lock_release();
    return role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER;
}

static void led_task(void *arg)
{
    (void)arg;
    TickType_t last_pulse = xTaskGetTickCount();
    TickType_t ready_at = 0;
    bool previous_ready = false;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if (s_thread_ready && !previous_ready) {
            ready_at = now;
        }
        previous_ready = s_thread_ready;

        bool is_attached = attached();
        bool in_suppression = s_thread_ready && !is_attached &&
            now - ready_at < pdMS_TO_TICKS(CONFIG_ESP_BR_DONGLE_M_LED_DETACHED_SUPPRESS_MS);
        bool pulse = now - last_pulse < pdMS_TO_TICKS(CONFIG_ESP_BR_DONGLE_M_LED_PULSE_DURATION_MS);
        if (now - last_pulse >= pdMS_TO_TICKS(CONFIG_ESP_BR_DONGLE_M_LED_PULSE_INTERVAL_MS)) {
            last_pulse = now;
            pulse = true;
        }

        if (pulse && s_thread_ready && (!in_suppression || is_attached)) {
            set_rgb(is_attached ? (rgb_t){0, 255, 0} : (rgb_t){255, 0, 0});
        } else {
            set_rgb(base_colour());
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void dongle_m_led_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_TIMER_BITS,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = CONFIG_ESP_BR_BOARD_LED_RED_GPIO,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CH_RED,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    channel.gpio_num = CONFIG_ESP_BR_BOARD_LED_GREEN_GPIO;
    channel.channel = LEDC_CH_GREEN;
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    channel.gpio_num = CONFIG_ESP_BR_BOARD_LED_BLUE_GPIO;
    channel.channel = LEDC_CH_BLUE;
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    set_rgb((rgb_t){255, 0, 0}); vTaskDelay(pdMS_TO_TICKS(250));
    set_rgb((rgb_t){0, 255, 0}); vTaskDelay(pdMS_TO_TICKS(250));
    set_rgb((rgb_t){0, 0, 255}); vTaskDelay(pdMS_TO_TICKS(250));
    set_rgb((rgb_t){0, 0, 0});
    ESP_ERROR_CHECK(xTaskCreate(led_task, "dongle_led", 3072, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "Dongle-M RGB status policy started");
}

void dongle_m_led_set_interface(dongle_m_led_interface_t interface) { s_interface = interface; }
void dongle_m_led_set_thread_ready(bool ready) { s_thread_ready = ready; }

#else

void dongle_m_led_init(void) {}
void dongle_m_led_set_interface(dongle_m_led_interface_t interface) { (void)interface; }
void dongle_m_led_set_thread_ready(bool ready) { (void)ready; }

#endif
