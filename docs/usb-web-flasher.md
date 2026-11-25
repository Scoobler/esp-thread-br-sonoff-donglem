# USB web flasher and settings-preserving upgrades (development candidate)

This feature is **not included in v2.2.0**. It requires a CI build and hardware validation on the Sonoff Dongle-M before release.

## Design

The CI release workflow now packages two **ESP32-only** images:

| Image | Flash offset | What it writes |
| --- | --- | --- |
| `sonoff-dongle-m-otbr-<tag>.bin` | `0x0` | Existing merged full image (bootloader, NVS gap, OTA metadata, application, Web UI) |
| `sonoff-dongle-m-otbr-<tag>-upgrade.bin` | `0x20000` | Both OTA application slots and Web UI; preserves the sectors below `0x20000` |

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

**Warning:** preserving NVS storage bytes does not guarantee compatibility with future firmware versions. Back up the Thread Active Dataset and any important configuration before upgrading. Upgrade images must never be written at `0x0`.

## Browser flasher

The site source is in `site/flasher/`. It uses the Web Serial API and Espressif esptool-js. It is designed for desktop Chrome/Edge via HTTPS.

The UI offers two operations:

- **Fresh installation:** explicit full-chip erase then merged image at `0x0`. This removes Wi-Fi credentials, Thread state and ESP32 settings.
- **Upgrade:** no full-chip erase; writes only the upgrade binary at `0x20000`. In the validated layout, this leaves NVS and OTA metadata untouched.

Both operations support published firmware or a manually selected custom `.bin` file. The default published image is downloaded from GitHub Pages and verified against its SHA-256 checksum before flashing. Custom binary files require extra care and are not release-verified.

The page is not a production flasher until the hardware tests pass. MG24 updates remain **out of scope**.

## Manual USB commands

**Fresh installation (destructive):**

```bash
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 erase_flash
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x0 sonoff-dongle-m-otbr-vNEXT.bin
```

**Settings-preserving upgrade (non-destructive):**

```bash
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x20000 sonoff-dongle-m-otbr-vNEXT-upgrade.bin
```

Replace `/dev/ttyUSB0` and the `vNEXT` placeholders with your device and the exact filenames from a future release. Windows uses e.g. `COM3`. Never use `erase_flash` for an upgrade. Never flash the full merged image at `0x0` if you expect to preserve NVS.

These are **development instructions**, not claims that persistence has passed hardware testing. Validate that the existing OTA slot selection works after writing both application slots.

## GitHub Pages publishing

The existing approved `.github/workflows/build_dongle_m.yml` workflow packages both images during CI. When a **version tag is pushed** and GitHub Release publishing succeeds, its Pages job deploys the static flasher together with the release binaries and a checked `firmware.json` manifest.

**One-time repository setup required:** In GitHub Settings → Pages, select **GitHub Actions** as the build/deployment source, and ensure repository/org Pages policies permit deployments. No extra GitHub Actions workflow file is added. The intended URL after the first successful tagged deployment is:

```text
https://scoobler.github.io/esp-thread-br-sonoff-donglem/
```

A PR build creates test firmware artifacts but **does not publish the website**. This avoids deploying unvalidated firmware. The first Pages deployment occurs after a validated release tag.

## Hardware gate

Before merging and tagging a new release, test:

1. Full erase and fresh flashing with a backed-up dataset; confirm provisioning and Thread network setup.
2. Reconfigure Wi-Fi and restore the active Thread dataset.
3. Flash an upgrade candidate over an installed older build using the image at `0x20000`, with **no erase**.
4. Confirm Wi-Fi credentials, active dataset, Thread state, Home Assistant, Web UI and Ethernet/Wi-Fi failover survive.
5. Repeat via Chrome/Edge Web Serial (fresh and upgrade), including checksum rejection for a corrupted file.
6. Confirm firmware artifacts, checksums and GitHub Pages deploy after tagging.

**Do not merge or release without explicit HARDWARE PASS.**
