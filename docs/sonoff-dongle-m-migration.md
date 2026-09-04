# Sonoff Dongle-M Upstream Migration

## Recorded revisions

- Development branch: `codex/upstream-rebase`
- Stage 1 firmware source commit: `5cb0b14d301a5de03e16ceb67919b4a2e477ae33`
- Upstream baseline used: `ff0d1e3cfd661e146963174c3886a6d32b911b6b`
- Upstream fetched on 2026-09-04: `0bad9f1f69cebe2e2ab768bbc6f71769a3661e33`
- Legacy donor (`main` and `legacy/current-working`): `0a1c04447762d31abd7acd8ff28dcc810f041e19`
- Legacy upstream ancestor: `b8bffd291b8608533a20c7a2406e1b493d953bce`
- Toolchain: ESP-IDF v5.5.4

The branch remains based on `ff0d1e3`. The newer fetched upstream commits are Web UI/mDNS changes and were recorded but not merged or rebased into this milestone.

## Stage 1 hardware-profile decision

Current upstream already provides the `ESP_BR_BOARD_TYPE` Kconfig choice. The Dongle-M is implemented as `CONFIG_ESP_BR_BOARD_SONOFF_DONGLE_M` within that architecture, with board facts in Kconfig and compile-time guards in `esp_br_board.c`.

The profile selects a classic ESP32 host and records:

- 16 MB DIO flash at 40 MHz;
- stock MG24 Spinel UART on UART1, host RX GPIO13, host TX GPIO17, 115200 8N1 without flow control;
- MG24 reset GPIO12 and control/mute GPIO15, defined but not manipulated;
- IP101GA on the ESP32's fixed RMII pins, external clock GPIO0, MDC GPIO23, MDIO GPIO18, reset GPIO5, PHY address 1;
- RGB red GPIO4, green GPIO14, blue GPIO2, defined without implementing the status policy.

The donor describes Ethernet as using the default classic-ESP32 RMII wiring. ESP-IDF v5.5.4 fixes the RMII data signals to TX_EN GPIO21, TXD0 GPIO19, TXD1 GPIO22, RXD0 GPIO25, RXD1 GPIO26, and CRS_DV GPIO27. The donor's remaining Ethernet settings came from ESP-IDF defaults and are now explicit in the profile.

## Stock RCP compatibility

The verified stock RCP reports radio capabilities `0x00ff` and does not report `RX_ON_WHEN_IDLE`. ESP-IDF v5.5.4 exposes the supported `CONFIG_OPENTHREAD_RX_ON_WHEN_IDLE` switch. Setting it to `n` removes that capability from the host's required mask passed to `RadioSpinel::Init`; it does not change or fake the RCP capability response.

Automatic RCP update is disabled at Kconfig and enforced by a profile compile-time error. The Stage 1 partition table has no `rcp_fw` partition.

## Partition layout

The Dongle-M layout preserves upstream OTA operation while using the 16 MB device safely:

| Partition | Offset | Size |
| --- | ---: | ---: |
| NVS | 0x9000 | 24 KiB |
| OTA data | 0xf000 | 8 KiB |
| PHY init | 0x11000 | 4 KiB |
| OTA 0 | 0x20000 | 3 MiB |
| OTA 1 | 0x320000 | 3 MiB |
| Web storage | 0x620000 | 512 KiB |

Unused flash is deliberately left unallocated for later migration stages. No space is assigned to an RCP image in this milestone.

## Clean build evidence

From `examples/basic_thread_border_router`:

```sh
idf.py -B build-sonoff-dongle-m \
  -D SDKCONFIG=sdkconfig.sonoff_dongle_m \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.sonoff_dongle_m \
  fullclean

idf.py -B build-sonoff-dongle-m \
  -D SDKCONFIG=sdkconfig.sonoff_dongle_m \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.sonoff_dongle_m \
  build
```

Result: SUCCESS, 1424/1424 targets. The application is `0x132440` bytes with 60% of the smallest app partition free.

Warnings are limited to the two pre-existing CMake minimum-version deprecations in the project and managed esp-serial-flasher CMake files. There were no new compiler warnings.

The hardware-test bundle is generated locally at `artifacts/sonoff-dongle-m-stage1-5cb0b14/`.

## Legacy delta classification

- Hardware-required: classic ESP32 target, 16 MB flash, stock-MG24 UART, IP101GA RMII, RGB GPIOs, and MG24 control GPIO definitions. Reimplemented in this milestone.
- Connectivity-required: Ethernet-first, Wi-Fi fallback, bounded recovery SoftAP, and deterministic backbone selection. Deferred.
- Product behavior: RGB self-test/interface color/Thread pulse policy. Deferred; physical pins only are defined.
- Web UI/UX and dataset handling: deferred; no legacy frontend files were copied.
- Diagnostic: RCP capability/version display. Deferred to Stage 2.
- Obsolete/superseded: legacy hard-coded generic-file UART values and monolithic frontend architecture. Not ported.
- Later-stage RCP work: replacement firmware and automatic MG24 flashing. Explicitly excluded.

## Hardware status and next gate

**PENDING HARDWARE VALIDATION**

Required initial evidence is limited to ESP32 boot/flash detection, named-profile logging, stock MG24 communication at 115200, Spinel initialization or its exact error, IP101GA initialization, and confirmation that no MG24 update occurs.

Do not commission the test unit into the production Thread network yet. Do not begin connectivity-policy, LED-policy, diagnostics UI, replacement RCP, or automatic RCP-update work until this hardware gate is reported.
