# USB web flasher and settings-preserving upgrades (development candidate)

This feature is **not included in v2.2.0**. Hardware validation of browser flashing and settings-preserving upgrades was reported on **2026-11-25**, but the feature is not yet merged, tagged or published. The GitHub Pages deployment remains untested until a tagged release.

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

- **Fresh installation:** explicit full-chip erase then merged image at `0x0`. This removes Wi-Fi credentials, Thread state and ESP32 settings. It is required when replacing the original Sonoff ESP32 firmware.
- **Upgrade:** **only for an existing Scoobler OTBR installation** with the compatible partition layout. No full-chip erase; writes only the upgrade binary at `0x20000`. In the validated layout, this leaves NVS and OTA metadata untouched.

Both operations support published firmware or a manually selected custom `.bin` file. The default published image is downloaded from GitHub Pages and verified against its SHA-256 checksum before flashing. Custom binary files require extra care and are not release-verified.

The page has passed the reported fresh/upgrade hardware tests, including automatic reset. It is still a Draft PR and not a published production flasher. MG24 updates remain **out of scope**.

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

The USB upgrade has been hardware-tested to preserve Wi-Fi and Thread settings on the reported units. The existing OTA slot selection was retained after writing both application slots; this is not a rollback-enabled update scheme. The proposed v2.0.1-to-current browser-upgrade test remains outstanding.

## GitHub Pages publishing

The existing approved `.github/workflows/build_dongle_m.yml` workflow packages both images during CI. When a **version tag is pushed** and GitHub Release publishing succeeds, its Pages job deploys the static flasher together with the release binaries and a checked `firmware.json` manifest.

**One-time repository setup required:** In GitHub Settings → Pages, select **GitHub Actions** as the build/deployment source, and ensure repository/org Pages policies permit deployments. No extra GitHub Actions workflow file is added. The intended URL after the first successful tagged deployment is:

```text
https://scoobler.github.io/esp-thread-br-sonoff-donglem/
```

A PR build creates test firmware artifacts but **does not publish the website**. This avoids deploying unvalidated firmware. The first Pages deployment occurs after a validated release tag.

## Hardware validation record — 2026-11-25

**HARDWARE PASS reported by the hardware tester.**

- Windows manual esptool upgrade on a Wi-Fi unit: firmware booted, rejoined saved Wi-Fi and Thread; `ot dataset active -x` matched the saved dataset exactly.
- Raspberry Pi manual esptool upgrade on an Ethernet unit: firmware booted over Ethernet, retained its Thread dataset and stored Wi-Fi credentials; fallback to Wi-Fi was exercised.
- Browser Fresh installation: complete ESP32 erase and merged-image flash succeeded; initial automatic reset needed an explicit DTR/RTS EN pulse and was corrected. On retest the ESP32 booted normally, rejoined Wi-Fi after provisioning, and the Thread dataset was reapplied.
- Browser Upgrade: settings-preserving upgrade image was programmed at `0x20000`, the corrected automatic reset worked, the Dongle-M rejoined Wi-Fi, and the Thread dataset remained correct.
- Browser UI wording was refined to support Ethernet installations and to reserve Upgrade for existing Scoobler firmware only.

**Not yet verified:** upgrading directly from v2.0.1 to the new build; published GitHub Pages/latest-release path and deployment; rejection of a deliberately invalid published SHA-256. These are useful additional checks, not claimed successes.

## Remaining release checks

1. Optionally exercise v2.0.1 → new candidate with a saved dataset and Wi-Fi settings on the **115200-baud** MG24 RCP unit; v2.0.1 does not auto-detect 460800 baud, though its partition layout matches.
2. Verify the latest PR CI build/checksums after final documentation changes.
3. After merge/tag, confirm GitHub Pages deployment, hosted firmware downloads and SHA-256 verification in the real published page.

Do not claim the remaining checks passed without evidence. Do not merge or release another firmware source change without hardware validation.
