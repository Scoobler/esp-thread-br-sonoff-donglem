/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "esp_br_board.h"

#include "esp_log.h"
#include "sdkconfig.h"

#define TAG "esp_br_board"

#if CONFIG_ESP_BR_BOARD_SONOFF_DONGLE_M
#if !CONFIG_IDF_TARGET_ESP32
#error "The Sonoff Dongle-M board profile requires the classic ESP32 target"
#endif
#if !CONFIG_ESPTOOLPY_FLASHSIZE_16MB
#error "The Sonoff Dongle-M board profile requires a 16 MB flash configuration"
#endif
#if !CONFIG_OPENTHREAD_RADIO_SPINEL_UART
#error "The Sonoff Dongle-M stock MG24 RCP requires Spinel UART"
#endif
#if CONFIG_AUTO_UPDATE_RCP
#error "MG24 automatic RCP update is intentionally unsupported in the Stage 1 Dongle-M profile"
#endif
#if CONFIG_OPENTHREAD_RX_ON_WHEN_IDLE
#error "The stock Sonoff MG24 RCP does not report RX_ON_WHEN_IDLE"
#endif
#if !CONFIG_EXAMPLE_CONNECT_ETHERNET || !CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET || !CONFIG_EXAMPLE_ETH_PHY_IP101
#error "The Sonoff Dongle-M profile requires the ESP32 EMAC and IP101GA PHY"
#endif
#if !CONFIG_ETH_PHY_INTERFACE_RMII || !CONFIG_ETH_RMII_CLK_INPUT
#error "The Sonoff Dongle-M IP101GA must use RMII with its external clock on ESP32 GPIO0"
#endif

_Static_assert(CONFIG_ESP_BR_RCP_UART_PORT == 1, "Dongle-M MG24 must use UART1");
_Static_assert(CONFIG_ESP_BR_RCP_UART_BAUDRATE == 115200, "Dongle-M stock MG24 requires 115200 baud");
_Static_assert(CONFIG_PIN_TO_RCP_TX == 13, "Dongle-M host RX/RCP TX must use GPIO13");
_Static_assert(CONFIG_PIN_TO_RCP_RX == 17, "Dongle-M host TX/RCP RX must use GPIO17");
_Static_assert(CONFIG_PIN_TO_RCP_RESET == CONFIG_ESP_BR_BOARD_MG24_RESET_GPIO, "MG24 reset GPIO mismatch");
_Static_assert(CONFIG_PIN_TO_RCP_BOOT == CONFIG_ESP_BR_BOARD_MG24_CONTROL_GPIO, "MG24 control GPIO mismatch");
_Static_assert(CONFIG_EXAMPLE_ETH_MDC_GPIO == CONFIG_ESP_BR_BOARD_ETH_MDC_GPIO, "Ethernet MDC GPIO mismatch");
_Static_assert(CONFIG_EXAMPLE_ETH_MDIO_GPIO == CONFIG_ESP_BR_BOARD_ETH_MDIO_GPIO, "Ethernet MDIO GPIO mismatch");
_Static_assert(CONFIG_EXAMPLE_ETH_PHY_RST_GPIO == CONFIG_ESP_BR_BOARD_ETH_RESET_GPIO, "Ethernet reset GPIO mismatch");
_Static_assert(CONFIG_EXAMPLE_ETH_PHY_ADDR == CONFIG_ESP_BR_BOARD_ETH_PHY_ADDR, "Ethernet PHY address mismatch");
_Static_assert(CONFIG_ETH_RMII_CLK_IN_GPIO == CONFIG_ESP_BR_BOARD_ETH_RMII_CLK_GPIO, "RMII clock GPIO mismatch");
#endif

void esp_br_board_log_profile(void)
{
#if CONFIG_ESP_BR_BOARD_SONOFF_DONGLE_M
    ESP_LOGI(TAG, "Board profile: Sonoff Dongle-M (classic ESP32, 16 MB flash)");
    ESP_LOGI(TAG, "Stock MG24 RCP: UART%d, host RX GPIO%d, host TX GPIO%d, %d 8N1, no flow control",
             CONFIG_ESP_BR_RCP_UART_PORT, CONFIG_PIN_TO_RCP_TX, CONFIG_PIN_TO_RCP_RX,
             CONFIG_ESP_BR_RCP_UART_BAUDRATE);
    ESP_LOGI(TAG, "MG24 control GPIOs: reset GPIO%d, control GPIO%d (defined only; automatic update disabled)",
             CONFIG_ESP_BR_BOARD_MG24_RESET_GPIO, CONFIG_ESP_BR_BOARD_MG24_CONTROL_GPIO);
    ESP_LOGI(TAG, "Ethernet: IP101GA RMII, PHY addr %d, MDC GPIO%d, MDIO GPIO%d, reset GPIO%d, clock input GPIO%d",
             CONFIG_ESP_BR_BOARD_ETH_PHY_ADDR, CONFIG_ESP_BR_BOARD_ETH_MDC_GPIO,
             CONFIG_ESP_BR_BOARD_ETH_MDIO_GPIO, CONFIG_ESP_BR_BOARD_ETH_RESET_GPIO,
             CONFIG_ESP_BR_BOARD_ETH_RMII_CLK_GPIO);
    ESP_LOGI(TAG, "RGB hardware: red GPIO%d, green GPIO%d, blue GPIO%d (status policy not enabled)",
             CONFIG_ESP_BR_BOARD_LED_RED_GPIO, CONFIG_ESP_BR_BOARD_LED_GREEN_GPIO,
             CONFIG_ESP_BR_BOARD_LED_BLUE_GPIO);
    ESP_LOGW(TAG, "Stock MG24 compatibility: RX_ON_WHEN_IDLE is not required or advertised by the host");
#elif CONFIG_ESP_BR_BOARD_DEV_KIT
    ESP_LOGI(TAG, "Board profile: ESP Thread Border Router dev kit");
#elif CONFIG_ESP_BR_BOARD_M5STACK_CORES3
    ESP_LOGI(TAG, "Board profile: M5Stack CoreS3");
#else
    ESP_LOGI(TAG, "Board profile: standalone dev kits");
#endif
}
