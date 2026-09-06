# Sonoff Dongle-M Upstream Migration

## Recorded revisions

- Development branch: `codex/donglem-network-led`
- Network/LED milestone source commit: `3ffd86d1db9e03dc69fa9fa93df447484403af51` (work started from this clean baseline)
- Stage 1 firmware source commit: `ddc0ccef3f5dbf7b794bd5000d4a9d335cab845` (hardware-validated rebased build, 2026-09-05)
- Previous upstream base: `ff0d1e3cfd661e146963174c3886a6d32b911b6b`
- Current upstream baseline used: `0bad9f1f69cebe2e2ab768bbc6f71769a3661e33`
- Legacy donor (`main` and `legacy/current-working`): `0a1c04447762d31abd7acd8ff28dcc810f041e19`
- Legacy upstream ancestor: `b8bffd291b8608533a20c7a2406e1b493d953bce`
- Toolchain: ESP-IDF v5.5.4

The Dongle-M patchset was rebased onto current `upstream/main` without source
conflicts. The four absorbed upstream commits are `325e0c6` (Web UI
configuration guards), `78c272f` (IPv6 Web UI and mDNS updates), `20121cf`
(M5Stack Web UI hostname updates), and `20d0d57` (mDNS hostname documentation).
These modern upstream Web UI changes were preserved; no legacy UI was ported.

## Stage 1 hardware-profile decision

Current upstream already provides the `ESP_BR_BOARD_TYPE` Kconfig choice. The Dongle-M is implemented as `CONFIG_ESP_BR_BOARD_SONOFF_DONGLE_M` within that architecture, with board facts in Kconfig and compile-time guards in `esp_br_board.c`.

The profile selects a classic ESP32 host and records:

- 16 MB DIO flash at 40 MHz;
- stock MG24 Spinel UART on UART1, host RX GPIO13, host TX GPIO17, 115200 8N1 without flow control;
- MG24 reset GPIO12 and control/mute GPIO15, defined but not manipulated;
- IP101GA on the ESP32's fixed RMII pins, external clock GPIO0, MDC GPIO23, MDIO GPIO18, reset GPIO5, PHY address 1;
- RGB red GPIO4, green GPIO14, blue GPIO2, with active-high output handled by the separate status-policy component.

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

Result: SUCCESS, 1424/1424 targets. The application is `0x132700` bytes with 60% of the smallest app partition free.

Warnings are limited to the two pre-existing CMake minimum-version deprecations in the project and managed esp-serial-flasher CMake files. There were no new compiler warnings.

The rebased hardware-test bundle is generated locally at
`artifacts/sonoff-dongle-m-rebased-488c888/`.

## Legacy delta classification

- Hardware-required: classic ESP32 target, 16 MB flash, stock-MG24 UART, IP101GA RMII, RGB GPIOs, and MG24 control GPIO definitions. Reimplemented in this milestone.
- Connectivity-required: Ethernet-first, saved-Wi-Fi fallback, bounded recovery SoftAP, NVS failure tracking, and deterministic per-boot backbone selection. Implemented in this milestone; hardware validation is pending.
- Product behavior: RGB self-test, Ethernet/Wi-Fi/SoftAP base colors, and attached/detached Thread pulse policy. Implemented in this milestone; hardware validation is pending.
- Web UI/UX and dataset handling: deferred; no legacy frontend files were copied.
- Diagnostic: RCP capability/version display. Deferred to Stage 2.
- Obsolete/superseded: legacy hard-coded generic-file UART values and monolithic frontend architecture. Not ported.
- Later-stage RCP work: replacement firmware and automatic MG24 flashing. Explicitly excluded.

## Network and LED milestone

This milestone is based on upstream `0bad9f1f69cebe2e2ab768bbc6f71769a3661e33`,
ESP-IDF v5.5.4, and legacy donor reference `0a1c04447762d31abd7acd8ff28dcc810f041e19`.
The implementation is isolated behind `CONFIG_ESP_BR_BOARD_SONOFF_DONGLE_M`;
non-Dongle builds retain the upstream launch path.

The Dongle-M startup policy is:

1. Start Ethernet and wait up to `CONFIG_ESP_BR_DONGLE_M_ETHERNET_WAIT_MS` (10 s by default).
2. If Ethernet has an IP, select `ETH_DEF` and lock that pointer as the OpenThread backbone.
3. Otherwise use saved Wi-Fi credentials, if present, and wait up to 12 s by default.
4. If no credentials exist, or the saved connection repeatedly fails, use the current upstream Web UI SoftAP for a bounded 3-minute provisioning window.
5. Reboot after an unsuccessful bounded attempt; a successful Wi-Fi connection resets the failure count.

The NVS namespace is `br`, with `fail_count` (`u8`) and `last_ssid` keys.
Changing the configured SSID resets the failure count, and the recovery threshold
is five failed boots by default. The selected backbone is authoritative for that
boot; a later interface IP event does not replace it. Automatic RCP update and
RCP partition changes remain excluded.

The RGB policy is in a separate Dongle-M component. It performs a red/green/blue
self-test, shows blue for Ethernet, orange for Wi-Fi, and purple for SoftAP, then
pulses green when Thread is attached or red when detached. The detached pulse is
suppressed for 15 seconds after OpenThread becomes ready. GPIO definitions remain
in the board profile and the policy timing is Kconfig-configurable.

## OpenThread startup lifecycle correction

Hardware testing of the Wi-Fi path exposed a reproducible assertion after
`esp_openthread_border_router_init()`. The Dongle-M path released the OpenThread
lock immediately after border-router initialization and then released it again
after `esp_openthread_auto_start()`. The second release occurred in the
`ot_br_init` task without owning the task-switching lock, matching the IDF
assertion. The corrected path keeps one `esp_openthread_lock_acquire()` scope
through border-router initialization, dataset access, and `auto_start()`, then
performs one matching release.

ESP-IDF v5.5.4 also documents that `esp_openthread_set_backbone_netif()` must
run before `esp_openthread_init()`. Since Dongle-M network selection may wait for
Ethernet, Wi-Fi, or provisioning, the corrected launch path selects the one
authoritative backbone before `esp_openthread_start()`, registers it, and then
starts OpenThread. No runtime failover, provisioning-policy, or RCP change was
made.

## Hardware status and next gate

**STAGE 1 HARDWARE VALIDATION: PASSED**

Physical validation was completed on 2026-09-04 using the pre-rebase Stage 1
image. The real device verified the classic ESP32 rev 3.1 host, 16 MB DIO/40
MHz flash, UART1 GPIO13/GPIO17 at 115200 8N1 without flow control, stock MG24
Spinel/OpenThread startup and attachment, IP101GA RMII Ethernet with DHCP and
IPv6, current upstream Web UI, dataset creation, border routing, NAT64, and
Leader state. No panic, watchdog, reboot loop, or Spinel framing/timeout error
was observed.

The rebased build at source HEAD `ddc0ccef3f5dbf7b794bd5000d4a9d335cab845` was subsequently validated on real Sonoff Dongle-M hardware. The validation confirmed classic ESP32 boot, 16 MB DIO/40 MHz flash, the Dongle-M board profile, stock MG24 Spinel/OpenThread operation over UART1 GPIO13/GPIO17 at 115200 8N1 without flow control, supported `RX_ON_WHEN_IDLE` compatibility, Ethernet/DHCP/IPv6/mDNS, current upstream Web UI, NAT64, and restoration of saved Thread state. No panic, reboot loop, or RCP framing errors were observed.

The corrected network/LED build is **PENDING HARDWARE VALIDATION**. The user must
verify the Ethernet-first selection, Wi-Fi fallback, SoftAP recovery threshold,
backbone lock, active-high RGB indications, and Thread pulse timing on a real Dongle-M.

The stock-RCP baseline is now the known-good starting point for replacement MG24 RCP investigation. Hardware tests for replacement firmware remain **PENDING HARDWARE VALIDATION**.

## Runtime backbone failover investigation (2026-09-06)

Investigation only: no runtime source, RCP, LED, provisioning, or firmware artifact changed. Audit inputs were codex/donglem-network-led at 7c79d932aafd22b77b88eafbe6b57dabb64a9103, upstream/main at 0bad9f1f69cebe2e2ab768bbc6f71769a3661e33, and ESP-IDF v5.5.4 at 735507283d5b2f9fb363a1901172dbd9e847945d.

### Sources and lifecycle

Inspected app_main; launch_openthread_border_router; dongle_m_network; ESP-IDF esp_openthread.cpp, esp_openthread_netif_glue.c, esp_openthread_udp.c, esp_openthread_border_router.h, esp_openthread_lock.h, and the shipped libopenthread_br.a; OpenThread border_routing.h, border_routing_api.cpp, infra_if.cpp; the ESP-IDF ot_examples_br and protocol_examples_common Wi-Fi/Ethernet helpers; and README_MDNS.md.

The lifecycle is: app_main initializes NVS, SPIFFS, esp-netif, the event loop, mDNS, and Web UI. The Dongle-M launcher selects and waits for the backbone, calls esp_openthread_set_backbone_netif before esp_openthread_start, and the ESP-IDF worker initializes OpenThread, attaches the Thread netif, and enters the main loop. The ot_br_init task acquires the normal OpenThread lock, calls esp_openthread_border_router_init, loads/creates the dataset, and calls esp_openthread_auto_start, which enables IPv6 and Thread but does not select the backbone.

### Findings

esp_openthread_set_backbone_netif has an explicit contract: it must be called before esp_openthread_init. The public ESP-IDF API has no post-init backbone setter or complete rebind operation. The ESP glue also reads esp_openthread_get_backbone_netif for UDP binding, host-interface classification, link-layer lookup, and infrastructure traffic. Changing only an OpenThread interface index would leave platform state inconsistent. Calling the setter after start, Border Router init, or Thread attachment is unsupported; Border Router deinit does not make the pre-init contract valid.

OpenThread core does expose a related facility. In this revision, otBorderRoutingInit(instance, if_index, is_running) is documented as re-initializable: changing the index stops Border Routing and mDNS-related operations on the old interface before restarting on the new one. otBorderRoutingGetInfraIfInfo reports the configured index and running state, while otPlatInfraIfStateChanged updates running state only for that index. This cannot safely be used by the current application alone because the Espressif netif pointer and OpenThread index cannot be changed atomically through public APIs.

Conclusion: live backbone replacement is NOT SUPPORTED by the current public Espressif ESP-IDF integration as an application-only operation. The core API supports infrastructure-manager reinitialization, but the ESP wrapper does not expose the complete safe platform rebind. No checked-out Espressif example implements Ethernet/Wi-Fi backbone replacement at runtime.

ESP-IDF provides separate health events: Ethernet ETH_EVENT_CONNECTED/DISCONNECTED and IP_EVENT_ETH_GOT_IP/LOST_IP; Wi-Fi WIFI_EVENT_STA_CONNECTED/DISCONNECTED and IP_EVENT_STA_GOT_IP/LOST_IP. Future policy should require link/association and usable IP, including required IPv6 state, with timers. The OpenThread infra state signal only describes the selected index and cannot select another ESP netif.

Wi-Fi standby is feasible at the classic ESP32 esp-netif/Wi-Fi level, but is unproven for this OTBR integration. The current app does not associate Wi-Fi when Ethernet wins, and no checked-in Espressif OTBR example validates standby. It requires a hardware gate for coexistence, route priority, IPv6, mDNS, resource use, and interface classification.

### Recommended future architecture

Retain boot-only deterministic selection until a reviewed Espressif-supported platform rebind API exists. That API should atomically:

1. confirm candidate link, IPv4, and required IPv6;
2. update the ESP backbone-netif pointer used by platform callbacks;
3. call otBorderRoutingInit(instance, new_if_index, true) in OpenThread task context;
4. let OpenThread stop old-interface BR/mDNS activity and restart on the new index; and
5. refresh or restart host-side NAT64, DNS64, mDNS, RA, ND, route, and service bindings outside OpenThread.

Callbacks should enqueue observations only. One state-machine task should hold esp_openthread_lock_acquire(portMAX_DELAY) for ordinary OT calls. Use the task-switching lock only when the exact operation yields into lwIP, and release/reacquire it exactly as the ESP-IDF port does. Never release it from another task; incorrect ownership asserts/crashes.

A full esp_openthread_stop/start cycle is not a suitable first solution: ESP-IDF refuses to stop while Thread is active and the v5.5.4 radio path reports RCP deinitialization as unsupported. A correct in-process rebind should preserve Thread radio, dataset, RCP, and attachment because the OpenThread index-switch lifecycle is scoped to infrastructure services. This is a design expectation, not Dongle-M hardware evidence.

Validate RA/RS, ND, on-link/OMR routes, discovered prefixes, NAT64 prefix and sockets, DNS64 reachability, OpenThread mDNS/DNS-SD, and the separate ESP-IDF mDNS responder. If atomic rebind cannot be obtained, retain boot-only selection and report infrastructure loss. Do not call the undocumented post-init setter or partially update the OT index.

### Proposed state machine and acceptance tests

Not implemented:

    ETH_ACTIVE -- ETH unusable 5-10 s --> WIFI_CANDIDATE
    WIFI_CANDIDATE -- Wi-Fi usable --> WIFI_ACTIVE
    WIFI_CANDIDATE -- bounded timeout --> INFRA_DEGRADED (retain Thread)
    WIFI_ACTIVE -- unusable --> ETH_CANDIDATE or INFRA_DEGRADED
    ETH_CANDIDATE -- healthy 20-30 s --> ETH_ACTIVE

Maintain one active_backbone; change the LED only after successful rebind and ignore late non-selected events except as health observations. Do not immediately preempt Wi-Fi when Ethernet returns.

Later hardware tests must cover both promotion directions, standby disabled and enabled, link flapping, DHCP renewal, IPv6-only loss, and both interfaces unavailable. Verify LED, IPv4/IPv6, RA/ND, NAT64, DNS64, mDNS, routes, no stale bindings or duplicate advertisements, no lock/watchdog/RCP failure, and continued Thread attachment. Repeat BILRESA sleepy-device presses during and after failover with no multi-second or approximately 30-second delay. Reboot after each path and verify dataset and Wi-Fi persistence.

Conclusion: DO NOT implement runtime failover yet. Obtain or design a reviewed Espressif platform-level rebind API and test it independently from LED and provisioning policy.

## Current migration path

1. **Stage 1 — Current upstream plus Dongle-M board support:** **HARDWARE VALIDATED**.
2. **Stage 2 — Replacement EFR32MG24 OpenThread RCP investigation and A/B test:** **CURRENT**.
3. **Stage 3 — BILRESA sleepy-end-device latency A/B test** using stock and replacement RCP firmware.
4. **Stage 4 — Restore the normal upstream `RX_ON_WHEN_IDLE` requirement only if the replacement RCP genuinely advertises and supports it.**
5. **Stage 5 — Restore Dongle-M Wi-Fi fallback and deterministic backbone selection using current upstream architecture:** **IMPLEMENTED; HARDWARE VALIDATION PENDING**.
6. **Stage 6 — Restore Dongle-M LED status behaviour separately from networking policy:** **IMPLEMENTED; HARDWARE VALIDATION PENDING**.
7. **Stage 7 — If proven successful, integrate the replacement RCP image and evaluate current upstream RCP update support.**

The current upstream Web UI remains authoritative; the legacy Web UI is not being ported wholesale.
