# Dongle-M Web Flasher and settings-preserving upgrades

The **[Dongle-M Web Flasher](https://scoobler.github.io/esp-thread-br-sonoff-donglem/)** provides USB-C installation and upgrades for this project's ESP32 firmware. It was introduced in **[v2.3.0](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/releases/tag/v2.3.0)** and uses [Espressif's esptool-js](https://github.com/espressif/esptool-js) for browser flashing.

**Hardware tested:** Fresh installation, automatic restart, settings-preserving Upgrade, and an upgrade from the older v2.0.1 firmware. The subsequently deployed GitHub Pages flasher has also successfully completed a firmware upgrade. This document describes how its two image formats work and what each operation preserves.

## Design

Each release provides two **ESP32-only** firmware images:

| Image | Flash offset | What it writes |
| --- | --- | --- |
| `sonoff-dongle-m-otbr-<tag>.bin` | `0x0` | Full merged image for a Fresh installation; requires full-chip erase and replaces ESP32 settings |
| `sonoff-dongle-m-otbr-<tag>-upgrade.bin` | `0x20000` | Both application slots and Web UI; preserves the configuration partitions below `0x20000` |

The upgrade image is a contiguous binary. Between the first application, second application and Web UI, the packaging script fills the gaps with `0xFF`. It therefore **erases both application slots and updates the Web UI** on writing and **does not preserve OTA rollback**, deliberately. The current project does not offer an established, safe runtime OTA update path.

The upgrade image is generated only when the partition table matches the known Dongle-M layout:

| Partition | Offset | Length |
| --- | --- | --- |
| NVS | `0x9000` | `0x6000` |
| OTA selection | `0xF000` | `0x2000` |
| PHY calibration | `0x11000` | `0x1000` |
| OTA0 | `0x20000` | `0x300000` |
| OTA1 | `0x320000` | `0x300000` |
| Web storage | `0x620000` | `0x80000` |

**Important:** Upgrade is supported only on existing compatible installations of **this project's firmware**, not directly from stock Sonoff ESP32 firmware. Preserving NVS does not guarantee compatibility with every future firmware version. Back up your Thread Active Operational Dataset before flashing. **Never write an `*-upgrade.bin` image at `0x0` or use it in the official Sonoff Dongle Flasher.**

## Browser flasher

The site source is in `site/flasher/`. It uses Web Serial and Espressif's [esptool-js](https://github.com/espressif/esptool-js) library and works with desktop Chrome or Edge over HTTPS.

The UI offers two operations:

- **Fresh installation:** explicit full-chip erase then merged image at `0x0`. This removes Wi-Fi credentials, Thread state and ESP32 settings. It is required when replacing the original Sonoff ESP32 firmware.
- **Upgrade:** **only for an existing compatible installation of this project's firmware**. No full-chip erase; writes only the upgrade binary at `0x20000`. In the validated layout, this leaves NVS and OTA metadata untouched.

Both operations support published firmware or a manually selected custom `.bin` file. The default published image is downloaded from GitHub Pages and verified against its SHA-256 checksum before flashing. Custom binary files require extra care and are not release-verified.

The published GitHub Pages flasher has completed a hardware Upgrade test, in addition to the pre-release Fresh/Upgrade validation. **Neither option flashes or changes the MG24 RCP.**

## Manual USB commands

**Fresh installation (destructive):**

```bash
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 erase_flash
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x0 sonoff-dongle-m-otbr-v2.3.0.bin
```

**Settings-preserving upgrade (non-destructive):**

```bash
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x20000 sonoff-dongle-m-otbr-v2.3.0-upgrade.bin
```

The examples use the **v2.3.0 release assets**. Replace `/dev/ttyUSB0` with your serial port (for example, `COM3` on Windows). Never run `erase_flash` for an Upgrade, and never use the full merged image at `0x0` if you intend to preserve NVS. The `--baud 460800` argument here is the ESP32 USB programming speed, **not** the MG24 RCP UART setting.

The USB upgrade has been hardware-tested to preserve Wi-Fi and Thread settings on the reported units. The existing OTA slot selection was retained after writing both application slots; this is not a rollback-enabled update scheme. **Direct upgrading from v2.0.1 to v2.3.0 has passed hardware validation.**

## GitHub Pages publishing

The `.github/workflows/build_dongle_m.yml` workflow builds both firmware images and publishes their SHA-256 checksums. GitHub Pages hosts the flasher, the release binaries and a `firmware.json` manifest. The site's preparation and deployment run as separate jobs. A manual `pages_release_tag` input can redeploy the website using the existing published release binaries without changing a version tag.

**Repository configuration:** GitHub Pages uses **GitHub Actions** as its deployment source, with `v*` tags allowed in the `github-pages` environment. Flasher URL:

```text
https://scoobler.github.io/esp-thread-br-sonoff-donglem/
```

Pull-request builds generate test firmware artifacts but **do not deploy the public website**. The initial v2.3.0 deployment was followed by a JavaScript syntax fix; future website changes are checked for JavaScript syntax before publication.

## Hardware validation record — 2026-11-25

**HARDWARE PASS reported by the hardware tester.**

- Windows manual esptool upgrade on a Wi-Fi unit: firmware booted, rejoined saved Wi-Fi and Thread; `ot dataset active -x` matched the saved dataset exactly.
- Raspberry Pi manual esptool upgrade on an Ethernet unit: firmware booted over Ethernet, retained its Thread dataset and stored Wi-Fi credentials; fallback to Wi-Fi was exercised.
- Browser Fresh installation: complete ESP32 erase and merged-image flash succeeded; initial automatic reset needed an explicit DTR/RTS EN pulse and was corrected. On retest the ESP32 booted normally, rejoined Wi-Fi after provisioning, and the Thread dataset was reapplied.
- Browser Upgrade: settings-preserving upgrade image was programmed at `0x20000`, the corrected automatic reset worked, the Dongle-M rejoined Wi-Fi, and the Thread dataset remained correct.
- Browser UI wording was refined to support Ethernet installations and to reserve Upgrade for existing Scoobler firmware only.
- **Legacy upgrade PASS (v2.0.1 → new candidate):** Restored official Sonoff ESP32 firmware, downgraded MG24 to **OpenThread RCP v2.4.4 (115200 baud)**, flashed our **v2.0.1** ESP32 firmware, provisioned Wi-Fi and applied the existing Thread dataset. Used the browser **Upgrade** path at `0x20000`; firmware updated, restarted, rejoined Wi-Fi, and retained the exact Thread dataset.

**Hosted-site validation:** the corrected published flasher loads and has successfully flashed an Upgrade image on physical hardware. The test of deliberately corrupted firmware or checksum data being rejected **has not been reported**.

## Additional verification

- **Passed:** the publicly hosted v2.3.0 Web Flasher selected a published Upgrade image, flashed the ESP32 and completed the update on physical hardware.
- **Not yet demonstrated:** rejection of a deliberately corrupted firmware download or manifest checksum before any erase/write. This should be tested in an isolated development environment, without risking a working device.

Firmware changes still require the project's usual hardware-validation process. Website and documentation updates do not require changing the published v2.3.0 firmware assets.
