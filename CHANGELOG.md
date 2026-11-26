# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- **Dongle-M device settings:** a `dongle` console command sets the network
  mode (`auto`, `ethernet`, `wifi`) and the hostname. Ethernet-only never
  starts Wi-Fi or the provisioning SoftAP; Wi-Fi-only skips Ethernet. A set
  hostname is used for mDNS and as DHCP hostname. Settings are stored in NVS
  and survive the Upgrade image; Kconfig provides the defaults. Proposed in
  [#27](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/issues/27).

### Documentation

- Rework the README for the published v2.3.0 release: make the installation
  options easier to follow, clarify that the official Sonoff flasher accepts
  only the full image, and remove obsolete vNEXT release text.
- Distinguish the current Espressif Web UI from the original custom Web UI;
  Active Dataset import/export remains available via the USB console, not
  the current Web UI.
- Clarify MG24 GPIO reset wiring versus the disabled automatic RCP recovery
  handler, and fold the optional MG24 firmware procedure into a collapsible
  section matching the dataset backup procedure.
- Rename the project-specific browser interface to **Dongle-M Web Flasher**
  and explicitly credit Espressif's esptool-js implementation.

### Fixed

- GitHub Pages deployment now uploads its website artifact in a separate job
  before invoking `deploy-pages`, avoiding an artifact-discovery race observed
  during the v2.3.0 tag deployment. A manual workflow-dispatch option can
  redeploy the exact existing published release files without rebuilding or
  moving an already-published version tag.
- Fix the Pages deploy job's implicit success condition, which skipped
  deployment when the firmware-build and release jobs were intentionally
  skipped during manual redeployment.
- Correct the hosted flasher's JavaScript syntax error that prevented release
  manifest loading. Validate JavaScript syntax in CI and before Pages upload.
- During manual Pages redeployment, deploy the current website source from
  `main` alongside unmodified firmware and checksums from the specified release.
- Update README and technical guidance to describe the published v2.3.0 release
  rather than the previous prerelease state.


## [2.3.0] - 2026-11-25

### Added

- GitHub Pages USB flasher candidate with **Fresh installation** (full ESP32
  erase and merged image at `0x0`) and **Upgrade** (application slots and
  Web UI at `0x20000`, preserving NVS and the Thread dataset on the
  validated partition layout). Upgrade is restricted to existing Scoobler
  OpenThread Border Router installations.
- CI-generated full and upgrade images with SHA-256 checksums, an upgrade
  partition-layout safety check and a tagged-release GitHub Pages deployment.

### Documentation

- Make the project's browser flasher the recommended user installation path
  after release. Keep the official Sonoff flasher for MG24 prerequisites and
  ESP32 recovery, and document manual USB flashing for both modes.

### Validation (2026-11-25)

- **HARDWARE PASS** reported for browser full erase/flash with automatic
  reset and first-time Wi-Fi/Thread setup; settings-preserving browser upgrade
  with reboot, Wi-Fi reconnection and saved Thread dataset verified.
- Manual USB upgrade passed on Wi-Fi and Ethernet units; dataset persisted,
  and saved Wi-Fi credentials supported Ethernet-to-Wi-Fi fallback.
- Fixed browser chip-name detection for classic ESP32 revision 3 and used
  an explicit DTR/RTS reset pulse after flashing.
- **Legacy migration PASS:** stock Sonoff ESP32 firmware used to install MG24
  RCP v2.4.4 at 115200 baud, then our v2.0.1 installed and configured with
  Wi-Fi and the existing Thread dataset. Browser Upgrade to the current
  candidate rebooted, rejoined Wi-Fi and preserved the exact Thread dataset.
- Live GitHub Pages deployment and hosted checksum validation remain to be
  checked; no published release yet.

## [2.2.0] - 2026-11-25

### Added

- Sonoff Dongle-M startup now read-only probes the MG24 Spinel RCP at 115200
  baud (Sonoff RCP v2.4.4) and then 460800 baud (v2.4.5), allowing one ESP32
  image to use either version. The probe validates HDLC FCS, Spinel response
  transaction/property/protocol compatibility and the non-empty NCP version
  before OpenThread starts. It allows one delayed retry for slow RCP startup,
  but never resets, configures or flashes the MG24.

### Changed

- A missing or incompatible MG24 now produces a clear diagnostic and leaves
  OpenThread stopped, rather than entering a reboot loop. After both bounded
  detection cycles fail, the Dongle-M RGB LED flashes pure red at 500 ms on /
  500 ms off until power-off or reset.

### Validation

- **HARDWARE PASS** reported on 2026-11-25, using the ESP-IDF v5.5.6
  firmware from source `36ccfc50feda1ffb12ec41cd707825c8e3b3e1bb`,
  [CI run #48](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/actions/runs/45499255468),
  merged image SHA-256 `c03eb7b879704f0bd1f82fa01028576cfaf8753a75f8f7744295bbf4ee5b33f3`.
- Hardware checked: both 115200/460800 Spinel detection, recovery from
  re-flashing MG24, incompatible Zigbee firmware rejection, bounded retry
  and flashing-red fault LED, overnight Thread operation, and Ethernet
  failover/failback. Documentation changes after the tested commit do not
  modify firmware source.
- One 460800-baud RCP radio TX timeout at about 11 h 47 min was recovered
  by OpenThread on the first recovery attempt, with no observed impact;
  cause undetermined. Monitor recurrence separately.

## [2.1.0] - 2026-11-24

### Changed

- Incorporated 13 Espressif `esp-thread-br` upstream commits, from
  `29a2345e3f0c61bba9307f3daea5ed0c94388ed3` through
  `847d04f9211d37f082d660fa1f54944d496fca16`: ot-br-posix 0.6.0-aligned
  Thread REST API v2, refreshed Web UI and OpenAPI schema, REST-init deadlock
  correction, and Web UI display fixes.
- Updated the Dongle-M build toolchain to ESP-IDF v5.5.6. The 16 MB Sonoff
  board profile remains unchanged despite the upstream 4 MB example profile.
- Added a Sonoff-only Web UI startup gate that waits for
  `OPENTHREAD_EVENT_START` before registering the upstream Web/REST server
  and replaying the selected-backbone IP event. This fixes the startup
  crash found during validation, without changing upstream application or
  component source.
- Preserved Ethernet-first selection, Wi-Fi provisioning/fallback, IPv6
  multicast handling, fixed 115200-baud MG24 RCP, LED status and release
  tooling. Only the two approved GitHub Actions workflows are present.

### Validation

- **HARDWARE PASS** recorded on 2026-11-24 for source commit
  `c644ecde32650ad4e7bfd429bee3f3c5d256b72e`.
- [CI run #39](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/actions/runs/45419406778)
  passed on ESP-IDF v5.5.6. Tested CI merged firmware SHA-256:
  `4f130a7253506dce571cb1f58d3d5794002f121dc0dc062c0586028945d7cfee`.
- Physical tests passed on fresh-flashed Wi-Fi and Ethernet Dongle-M hardware:
  stable boot, Wi-Fi setup, Ethernet failover/recovery, Web UI/REST, MG24
  RCP, Thread/dataset, Home Assistant and LEDs. Observed on-mesh, local
  OMR and on-link prefixes remained stable across reported reboots.
- Tagged release binaries are rebuilt by CI; their SHA-256 may differ from
  the hardware-tested CI image. Issues #9, #10 and #15 remain open and
  outside the scope of this release.

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
