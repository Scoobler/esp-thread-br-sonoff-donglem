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

### Changed

- Prepared an Espressif upstream refresh from `0bad9f1` through `29a2345`. The candidate updates Wi-Fi MAC handling, RCP-update configuration/component metadata, upstream documentation, and CI; physical hardware validation remains pending.
- The candidate uses the supported ESP-IDF v5.5.4 build environment. Espressif upstream uses an internal v6.2 image for some CI jobs, but v6.2 is not a project requirement.
- Candidate source commit: `6b103b6e61ced92d0bb8a4bc77512b94a6dfdd15`; merged ESP32 image SHA-256: `b6e9bc83e6ae6dfcfec31d28f617ae1360f98be38a6f5653d0389cadef59783f`. The v5.5.4 candidate build completed successfully and requires no v6.2 toolchain.

### Fixed

- **Dongle-M Ethernet:** receive all multicast traffic on the Ethernet
  backbone, preserving IPv6 multicast traffic needed by Border Router
  operation with ESP-IDF v5.5.4. Implemented by [Igor Sweet in PR #4](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/pull/4)
  ([commit `3cd969c`](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/commit/3cd969cc2c122284312af0c5faf78e53cf9bf024)).

## [2.0.0] - 2026-09-11

- First public release of the current Sonoff Dongle-M OTBR firmware and
  GitHub Actions release-image workflow.
