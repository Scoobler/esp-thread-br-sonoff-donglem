# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

## [2.1.0] - Unreleased

### Changed

- Prepared a Sonoff Dongle-M candidate incorporating 13 Espressif upstream
  commits from `29a2345e3f0c61bba9307f3daea5ed0c94388ed3` through
  `847d04f9211d37f082d660fa1f54944d496fca16`. The update aligns the Border
  Router REST API with ot-br-posix 0.6.0, refreshes the upstream Web UI,
  fixes REST-initialization deadlock and UI display issues, and updates the
  recommended ESP-IDF version to v5.5.6.
- The upstream basic-example 4 MB configuration and default Web GUI enablement
  are incorporated without changing the separate Dongle-M 16 MB board profile,
  Ethernet/Wi-Fi recovery behaviour, MG24 RCP configuration, LEDs, dataset
  handling, or release-image tooling.
- Excluded upstream GitHub Actions completely, including `pages.yml`; the two
  approved Sonoff workflows remain byte-for-byte identical to `origin/main`.

### Validation

- Candidate firmware source: `8be7a30418d5e0efd556f68547d4e427cfa68b91`;
  ESP-IDF v5.5.6; merged image SHA-256:
  `8607e238b04af9f7faac0c9fd42d99463fe738e4adc1329945efb003a65ab091`.
- The candidate builds successfully but is not hardware validated. It must not
  be released or promoted until an explicit `HARDWARE PASS` is recorded.

## [2.0.2] - 2026-11-24

### Added

- Contributor workflow guidance for origin synchronization, upstream-sync
  branches, Draft PRs, hardware validation, and release promotion.
- Pull-request firmware builds targeting `main`, including builds for Draft
  PRs, with read-only repository permissions.

### Fixed

- **Dongle-M network startup:** a present Ethernet link no longer falls back
  to Wi-Fi or SoftAP provisioning when the DHCP lease takes a few seconds
  longer, for example behind STP or 802.1X/MAB with dynamic VLANs. Networks
  that deliver the lease late are remembered and get a 30 s DHCP wait after
  the normal 15 s link-up DHCP wait,
  provisioning ends when Ethernet becomes ready, and repeated Ethernet
  recovery reboots back off to Wi-Fi. This incorporates [Igor Sweet's
  original PR #8](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/pull/8)
  for [#5](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/issues/5),
  with the DHCP wait increased from 10 s to 15 s after link-up.
- **Dongle-M Ethernet readiness:** clear stale readiness after IPv4 or link
  loss so fallback/provisioning cannot select an unavailable Ethernet
  backbone.
- **Dongle-M board profile:** correct MG24 metadata to reset GPIO15 and
  bootloader-selection GPIO12; normal firmware operation still does not drive
  either control pin or update MG24 firmware.
- **Dongle-M Ethernet multicast:** retain Igor Sweet's earlier [PR #4
  fix](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/pull/4) for
  IPv6 multicast reception on the Ethernet backbone.

### Validation

- Hardware validation passed on Ethernet and Wi-Fi Dongle-M units for tests
  1–5, 7 and 8, including the complete 30-minute Ethernet-recovery cooldown
  and its expiry. The remembered slow-DHCP/NVS behaviour remains untested.
- Validated source: `178975dbf68cbf16631f3f942d6ebdec5f94dd8a`; ESP-IDF
  v5.5.4. Tested firmware checksums: local `020bd736…`; CI `c0e96a58…`.
  These are records of the tested images, not a claim of binary
  reproducibility.

## [2.0.1] - Unreleased

### Added

- README guidance for backing up and restoring the Active Operational Dataset
  before and after firmware upgrades.

### Changed

- Prepared an Espressif upstream refresh consisting of 10 commits from
  `0bad9f1f69cebe2e2ab768bbc6f71769a3661e33` through
  `29a2345e3f0c61bba9307f3daea5ed0c94388ed3`. The refresh updates Wi-Fi MAC
  handling, RCP-update configuration and component metadata, upstream
  documentation, and CI.
- The candidate uses the supported ESP-IDF v5.5.4 build environment. Espressif upstream uses an internal v6.2 image for some CI jobs, but v6.2 is not a project requirement.
- Candidate firmware source: `6b103b6e61ced92d0bb8a4bc77512b94a6dfdd15`; the later candidate-build and README commits do not change firmware-relevant files. The merged ESP32 image SHA-256 is `b6e9bc83e6ae6dfcfec31d28f617ae1360f98be38a6f5653d0389cadef59783f`.
- CI run [#22](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/actions/runs/39788560926) built the PR merge ref `ca19c2b1501c6c424ea1805a95c74181eae2e900` from branch head `f7b657f965ccc9ff905bd8d249e122c298724672`.

### Fixed

- **Dongle-M Ethernet:** receive all multicast traffic on the Ethernet
  backbone, preserving IPv6 multicast traffic needed by Border Router
  operation with ESP-IDF v5.5.4. Implemented by [Igor Sweet in PR #4](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/pull/4)
  ([commit `3cd969c`](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/commit/3cd969cc2c122284312af0c5faf78e53cf9bf024)).

### Validation

- Physically validated on Sonoff Dongle-M hardware: boot, MG24 RCP
  communication, Thread operation, Web UI, Ethernet, Wi-Fi
  provisioning/fallback, Home Assistant operation, and LED indications.
- Firmware and Thread/Wi-Fi credentials are not guaranteed to survive every
  upgrade. Keep a dataset backup and follow the README restoration procedure.

## [2.0.0] - 2026-09-11

- First public release of the current Sonoff Dongle-M OTBR firmware and
  GitHub Actions release-image workflow.
