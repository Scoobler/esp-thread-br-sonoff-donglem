# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Contributor workflow guidance for origin synchronization, upstream-sync
  branches, Draft PRs, hardware validation, and release promotion.
- Pull-request firmware builds targeting `main`, including builds for Draft
  PRs, with read-only repository permissions.

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
