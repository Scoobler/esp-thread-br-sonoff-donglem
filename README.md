# ESP-IDF OpenThread Border Router on Sonoff Dongle-M

Custom OpenThread Border Router firmware for the **Sonoff Dongle-M / Dongle Max**, built on Espressif’s `esp-thread-br` and adapted for the Dongle-M hardware.

The ESP32 inside the Dongle-M runs the OpenThread Border Router, while the onboard **EFR32MG24** continues to operate as the Thread Radio Co-Processor (RCP).

This project provides a practical, self-contained OTBR solution with:

- Ethernet and Wi-Fi networking
- Ethernet-first connection policy with Wi-Fi fallback
- OpenThread Border Router functionality
- Thread dataset management
- Thread topology view
- Persistent Thread network state
- Home Assistant integration
- Multiple Border Router support
- RGB status indication, including a flashing-red RCP fault alarm
- Automatic compatibility with Sonoff OpenThread RCP at 115200 or 460800 baud
- Reproducible GitHub Actions release builds

> **Community project**
>
> This is not official Sonoff or Espressif firmware.
>
> It has been tested on real Sonoff Dongle-M hardware and is actively developed, but it should still be used at your own risk.

---

## Download

**Recommended (v2.3.0 onward):** [Scoobler Dongle-M Web Flasher](https://scoobler.github.io/esp-thread-br-sonoff-donglem/) — choose **Fresh installation** or **Upgrade**. The Web Flasher and two-image packages become available after the v2.3.0 release and its first successful GitHub Pages deployment.

**[View the latest published GitHub Release](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/releases/latest)**

The v2.3.0 release will include two ESP32 firmware files:

- `sonoff-dongle-m-otbr-v2.3.0.bin` — full merged firmware, written at `0x0` for Fresh installation (with full-chip erase).
- `sonoff-dongle-m-otbr-v2.3.0-upgrade.bin` — settings-preserving Upgrade image, written at `0x20000` **only for devices already running Scoobler firmware**.

Both files include matching SHA-256 checksums. Neither image updates the MG24 OpenThread RCP.

Until v2.3.0 is published, the currently released v2.2.0 firmware remains available through [GitHub Releases](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/releases).

---

## Overview

This firmware allows the ESP32 inside the Sonoff Dongle-M to operate as a full **OpenThread Border Router**.

The onboard EFR32MG24 remains in **Thread RCP mode** and communicates with the ESP32 over UART using Spinel.

The ESP32 handles:

- OpenThread Border Router services
- Ethernet and Wi-Fi uplink
- NAT64 / Thread infrastructure services
- Thread dataset management
- Web-based Thread configuration
- Thread topology information
- Home Assistant integration
- Network recovery and failover behaviour

The firmware is based on Espressif’s current `esp-thread-br` architecture rather than the older heavily modified proof-of-concept implementation.

---

## Prerequisites

**Before the first installation, use the official Sonoff firmware to configure the onboard EFR32MG24 in OpenThread RCP Mode.** The ESP32 firmware in this project cannot configure or flash the MG24. Zigbee coordinator firmware is not compatible.

### Step 1 — Initial Sonoff setup

1. Power up the Dongle-M with its official Sonoff ESP32 firmware.
2. Open the Sonoff Web UI using Ethernet if available, or follow Sonoff's initial Wi-Fi access-point setup: join the setup AP, set a password, reconnect, configure Wi-Fi if needed, and browse to the device IP address.
3. Log in to the Sonoff Web UI.

### Step 2 — Enable Thread RCP Mode

1. Log into the Sonoff Web UI.
2. Open:

   **EFR32MG24 → Operation Mode**

3. Select:

   **Thread RCP Mode**

This prepares the onboard EFR32MG24 with the OpenThread RCP firmware expected by this project. If the MG24 is currently running Zigbee firmware, switch it to **Thread RCP Mode using Sonoff's normal procedure** before flashing the custom ESP32 host image.

> [!IMPORTANT]
> **RCP compatibility (v2.2.0 onward):** One ESP32 image supports Sonoff OpenThread
> RCP **v2.4.4 at 115200 baud** and **v2.4.5 at 460800 baud**. On every boot,
> the ESP32 probes and validates the RCP's Spinel protocol automatically;
> **no manual UART baud selection is needed**. The probe is read-only and does
> not reset, configure or flash the MG24. See [issue #15](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/issues/15).
>
> The release image updates the **ESP32 host only**. It does not include,
> automatically install, or replace MG24 firmware. Configure a compatible
> **Thread RCP** using the standard Sonoff firmware **before** flashing this image.

> [!WARNING]
> If you are upgrading an existing Dongle-M installation, [back up your Active Operational Dataset first](#dataset-backup--upgrade-notes). This applies to every firmware update, including v1 to v2 and later v2 releases. First-time installations do not have an existing dataset to back up.

## Dataset Backup & Upgrade Notes

If the Dongle-M already belongs to a Thread network, keep a secure copy of its Active Operational Dataset before flashing or upgrading. **Fresh installation erases the ESP32 settings; the settings-preserving Upgrade option does not deliberately erase NVS.** Keep a backup even when using Upgrade.

<details>
<summary><strong>How to back up and restore your Thread network</strong></summary>

### Step 1 — Connect to the existing console

Connect the Sonoff Dongle-M to a computer using USB-C. Open a serial terminal such as PuTTY at 115200 baud, 8N1, with no flow control.

The console may continuously display ESP32 diagnostic messages. Press Enter to obtain the `esp32>` prompt.

### Step 2 — Back up the Thread dataset

Run:

```text
ot dataset active -x
```

Copy and securely save the complete hexadecimal Active Operational Dataset. It contains sensitive network credentials, including the Thread network key, so do not post it publicly.

### Step 3 — Flash the new firmware

Close the serial terminal to release the USB port, then follow the existing [Flashing the firmware](#flashing-the-firmware) instructions.

Flashing operations may erase stored credentials, particularly when partition layouts change. Avoid unnecessary full-chip erases.

### Step 4 — Reconnect Ethernet or configure Wi-Fi

After flashing, connect via Ethernet or configure Wi-Fi using the SoftAP if required, as described in [First boot and network behaviour](#first-boot-and-network-behaviour). For a settings-preserving Upgrade, existing connectivity should be retained. Wait until the device reaches normal operation before attempting OpenThread dataset commands.

### Step 5 — Restore the Thread dataset

Only after normal operation has resumed, reconnect the USB-C serial console and run:

```text
ot dataset active -x
```

Compare the active dataset with the saved backup. If it matches, leave it unchanged. Otherwise restore it using:

```text
ot dataset set active <YOUR_SAVED_HEX_DATASET>
```

Replace the placeholder with the saved hexadecimal dataset, then reboot the Dongle-M. Once the normal console is available again, verify the dataset:

```text
ot dataset active -x
```

Confirm that the restored dataset matches the backup.

### Step 6 — Verify the existing Thread network

Confirm that the Dongle-M has rejoined the original Thread network. Check Thread connectivity, the Web UI and Home Assistant. Preserving the original dataset should avoid having to recommission existing Matter-over-Thread devices.

</details>

### Updating the MG24 OpenThread RCP firmware

If you want to update the MG24 RCP, **back up your Thread dataset first** using the steps above.

1. Connect the Dongle-M to your PC via USB-C, open the [Sonoff Dongle Flasher](https://dongle.sonoff.tech/sonoff-dongle-flasher/), select the device and flash the **latest official Sonoff firmware**.
2. Join the Sonoff setup Wi-Fi AP, set a password, then reconnect to the AP using that password and open the Sonoff Web UI.
3. In the Web UI, choose **Firmware → MG24 → OpenThread** and flash the latest **compatible Thread RCP** firmware.
4. Use this project's [Web Flasher](#method-1--web-flasher-recommended) with **Fresh installation** to replace the official Sonoff ESP32 firmware.
5. Reconnect the Dongle-M and [restore your Thread dataset](#dataset-backup--upgrade-notes) if required.

The ESP32 automatically detects the RCP at **115200 or 460800 baud**; no manual baud setting is needed. The custom ESP32 image does **not** update the MG24 itself.

---

# Flashing the firmware

For most users the easiest way to install or upgrade is the **Scoobler Dongle-M Web Flasher** using USB-C and desktop Chrome or Edge.

### Method 1 — Web Flasher (recommended)

**[Open the Dongle-M Web Flasher](https://scoobler.github.io/esp-thread-br-sonoff-donglem/)**

> **Release note:** The new flasher is being prepared in Draft PR #22. Its GitHub Pages address will be active only after the feature is hardware-approved, merged and released. For the currently published v2.2.0 image, use the [existing Sonoff Dongle Flasher method](#method-2--sonoff-dongle-flasher-alternative) or [esptool](#method-3--manual-esptool).

1. [Complete the prerequisites](#prerequisites): the MG24 must already be in **OpenThread RCP Mode**.
2. Connect the Dongle-M via USB-C and open the flasher in a supported desktop browser.
3. Choose:
   - **Fresh installation** when converting from official Sonoff ESP32 firmware, changing from another firmware or intentionally starting over. This **erases all ESP32 settings**, including the Thread dataset and saved Wi-Fi configuration.
   - **Upgrade** **only when the Dongle-M already runs Scoobler OpenThread Border Router firmware** with the compatible partition layout. This writes the application and Web UI while leaving NVS and network settings in place.
4. Keep **Latest published release** selected, or choose a trusted **Custom .bin** if you are testing a development candidate; acknowledge the operation and select **Connect and flash**.
5. After the reset, access the Dongle-M over Ethernet or configure Wi-Fi as appropriate. With Fresh installation, restore the saved Thread dataset if required. With Upgrade, verify that the dataset and existing connectivity remain unchanged.

**Both modes update only the ESP32.** The MG24 OpenThread RCP firmware is not flashed. Back up your dataset before either operation. See [dataset backup and restoration](#dataset-backup--upgrade-notes).

### Method 2 — Sonoff Dongle Flasher (alternative)

For manual installation, recovery, or before the GitHub Pages flasher is published, you can use the [official Sonoff Dongle Flasher](https://dongle.sonoff.tech/sonoff-dongle-flasher/).

1. Download the [merged ESP32 firmware](#download) from a published release.
2. Connect USB-C, choose your serial device in the Sonoff Dongle Flasher and select **Customize**.
3. Upload the merged `.bin` and start flashing.

The merged image at `0x0` is **not a settings-preserving upgrade image**. It overwrites NVS. Use the dedicated Upgrade image at `0x20000` if preservation is required, once the new firmware packaging is released.

The standard Sonoff Web UI firmware-update page does **not** accept this merged ESP32 image.

### Method 3 — Manual esptool

These examples use the **future two-image releases**. Replace `vNEXT` with the published version and choose the correct serial port. The `--baud 460800` option is the **ESP32 USB flashing speed**, not the MG24 RCP UART speed.

**Fresh installation (erases all ESP32 flash):**

```powershell
py -m esptool --chip esp32 --port COM3 erase_flash
py -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x0 sonoff-dongle-m-otbr-vNEXT.bin
```

**Upgrade an existing Scoobler firmware installation (preserves settings):**

```powershell
py -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x20000 sonoff-dongle-m-otbr-vNEXT-upgrade.bin
```

Linux / macOS: use `python3 -m esptool` instead of `py -m esptool` and replace `COM3` with the actual port (such as `/dev/ttyUSB0`). **Never run `erase_flash` when preserving settings, and never flash the merged full image at `0x0` as an Upgrade.**

CI release packages will contain both `.bin` files and matching SHA-256 checksums. The Upgrade image is deliberately written to both OTA application slots and the Web UI; it does not provide application rollback. For implementation and test details, see [USB upgrade technical notes](docs/usb-web-flasher.md).

---

# First boot and network behaviour

## MG24 RCP detection

Before selecting Ethernet or Wi-Fi, the ESP32 probes the MG24 for a compatible
**OpenThread Spinel RCP** at **115200 baud**, then **460800 baud**. Only a valid
protocol and version response is accepted; there is no unverified default
baud-rate fallback.

If both rates fail, the firmware waits **2 seconds** and repeats the bounded
check once to accommodate a slow-starting MG24. If neither detection cycle
succeeds, **OpenThread does not start** and the LED flashes **red continuously
(500 ms on / 500 ms off)**. The device will not enter an automatic reboot loop.
This can indicate Zigbee firmware, an absent RCP or an RCP that is not ready or
responding. Restore a compatible Sonoff Thread RCP and restart the ESP32.

## Ethernet-first backbone selection

Once a compatible RCP has been detected, the firmware uses an Ethernet-first connection policy.

On boot, it attempts connectivity in this order:

1. **Ethernet**
   - Ethernet is preferred.
   - If a usable Ethernet connection is obtained, it is selected.
   - Without a cable, the firmware moves on after about 8 seconds.
   - With a cable, it waits 15 seconds for a DHCP lease. If the lease arrives later, for example because of switch port authentication or spanning tree, later boots wait up to 30 seconds.

2. **Saved Wi-Fi**
   - If Ethernet is unavailable, saved Wi-Fi credentials are tried.

3. **Provisioning access point**
   - If no Wi-Fi credentials are stored, a provisioning SoftAP is started.
   - If Ethernet becomes available while the SoftAP is open, the device leaves provisioning and reboots into Ethernet.

4. **Wi-Fi recovery**
   - Repeated failed Wi-Fi connection attempts eventually trigger a recovery SoftAP.
   - If no new credentials are entered, normal connection attempts resume.

This provides a predictable fallback path without requiring manual recovery after a failed network configuration.

---

## Ethernet / Wi-Fi switching

The current Espressif OpenThread Border Router integration does not safely support arbitrary live backbone rebinding.

To avoid unsafe interface switching, the firmware uses controlled reboots when the preferred infrastructure interface changes.

### Ethernet lost while Wi-Fi is available

The Dongle-M performs a controlled reboot so that normal startup can select Wi-Fi.

### Ethernet returns while running on Wi-Fi

Ethernet must remain stable for approximately 30 seconds before a controlled reboot returns the unit to Ethernet.

This avoids repeatedly switching interfaces during unstable network conditions. If Ethernet keeps returning but does not stay up, the device stays on Wi-Fi for 30 minutes after three attempts. The recovery attempt count is stored in NVS, but the 30-minute cooldown timer is RAM-only: a power cycle starts its cooldown timing again. Persisting cooldown time across resets is deferred.

---

# LED status

The onboard RGB LED is used to show the current operating state.

## Boot

A short self-test is shown after a compatible RCP is detected:

```text
Red → Green → Blue
```

If RCP detection fails, the normal self-test is skipped and the flashing-red fault indication takes priority.

## Infrastructure connection

| State | LED |
| --- | --- |
| Ethernet connected | Blue |
| Wi-Fi connected | Orange |
| Provisioning / recovery AP | Purple |

## Thread state

| State | LED |
| --- | --- |
| Thread attached | Short green pulse approximately every 2 seconds |
| Thread detached / disabled | Short red pulse approximately every 2 seconds |

The detached indication is suppressed briefly after Thread startup to avoid showing a false failure while the network attaches.

## RCP detection fault

| State | LED |
| --- | --- |
| No compatible MG24 OpenThread RCP detected after two bounded probe cycles | **Continuous red flashing: 500 ms ON / 500 ms OFF** |

The fault indication overrides all infrastructure and Thread status colours.
It means the ESP32 could not verify a compatible Spinel RCP at either 115200
or 460800 baud; **it is not the normal short red Thread-detached pulse**.
This can occur with Zigbee firmware, an absent/incompatible MG24, or a slow or
unresponsive RCP. OpenThread remains stopped without automatic reboot until
an operator corrects the RCP and restarts the ESP32. Serial startup logs give
the attempted rates and the detection result.

---

# Web UI and Thread management

Once the Dongle-M has obtained an IP address, open that address in a browser.

The Espressif/OpenThread Web UI provides access to Thread configuration and diagnostics.

Depending on the current upstream implementation, this includes:

- Thread network creation
- Joining an existing Thread network
- Active Operational Dataset management
- Dataset backup / restore
- Network properties
- Thread topology
- Border Router information
- Thread diagnostics

The selected Thread dataset and network state persist across normal reboots.

---

# Home Assistant setup

The Dongle-M can be used as a standalone OpenThread Border Router with Home Assistant.

## 1. Add the OpenThread Border Router integration

In Home Assistant, add the **OpenThread Border Router** integration.

Use:

```text
http://<dongle-ip-address>
```

For example:

```text
http://192.168.1.50
```

The `http://` prefix is required.

## 2. Add the Thread integration

Add or open the **Thread** integration.

The Dongle-M should appear as an available Border Router.

Set the network as the preferred Thread network if appropriate.

## 3. Mobile credential sync

If required, use the Home Assistant Companion App to send the Thread credentials to your phone.

This allows compatible Matter-over-Thread devices to be commissioned onto the same Thread network.

---

# Multiple Border Routers

Thread supports multiple Border Routers on the same Thread network.

Multiple Dongle-M units can be used together provided they share the **same Active Operational Dataset**.

Simply using the same:

- Network Name
- Channel
- PAN ID

is not sufficient.

The full Active Dataset must match.

You can inspect the dataset from the ESP console using:

```text
ot dataset active -x
```

When two Dongle-M Border Routers use the same dataset and are within Thread radio range of the mesh, they should join the same Thread partition.

This provides additional infrastructure paths and Border Router redundancy.

---

# Hardware

The validated Dongle-M hardware uses:

- ESP32-D0WDQ2-V3 host
- 16 MB ESP32 flash
- EFR32MG24 Thread RCP
- IP101GA Ethernet PHY

## UART to EFR32MG24

| Function | Value |
| --- | --- |
| UART | UART1 |
| RX | GPIO13 |
| TX | GPIO17 |
| Baud | Automatic: 115200 or 460800 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Flow control | None |

## Ethernet

| Function | Value |
| --- | --- |
| PHY | IP101GA |
| Interface | RMII |
| PHY address | 1 |
| MDC | GPIO23 |
| MDIO | GPIO18 |
| Reset | GPIO5 |
| RMII clock | GPIO0 |

## RGB LED

The onboard RGB LED is active-high PWM controlled.

| Colour | GPIO |
| --- | --- |
| Red | GPIO4 |
| Green | GPIO14 |
| Blue | GPIO2 |

## MG24 control

| Function | ESP32 GPIO |
| --- | --- |
| Reset (active low) | GPIO15 |
| Bootloader selection | GPIO12 |

These pins are board metadata only in the current release; normal operation does not drive them and automatic MG24 flashing remains disabled. GPIO12 is an ESP32 bootstrapping pin, so any future active bootloader control must keep it from being driven high during ESP32 reset.

---

# Building from source

The current validated build environment uses:

```text
ESP-IDF v5.5.6
```

Clone the repository:

```bash
git clone https://github.com/Scoobler/esp-thread-br-sonoff-donglem.git
cd esp-thread-br-sonoff-donglem
git submodule update --init --recursive
```

Then build the Dongle-M configuration:

```bash
cd examples/basic_thread_border_router

idf.py -B build-sonoff-dongle-m \
  -D SDKCONFIG=sdkconfig.sonoff_dongle_m \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.sonoff_dongle_m \
  build
```

---

## Generate a merged firmware image

From the repository root:

```bash
python3 tools/release/merge_dongle_m_image.py \
  examples/basic_thread_border_router/build-sonoff-dongle-m \
  --output artifacts/sonoff-dongle-m-otbr.bin
```

This creates:

```text
artifacts/sonoff-dongle-m-otbr.bin
artifacts/sonoff-dongle-m-otbr.bin.sha256
```

The merge script reads the generated ESP-IDF flash metadata and uses the correct:

- flash offsets
- DIO flash mode
- 40 MHz flash frequency
- 16 MB flash size

The generated ESP-IDF metadata remains the source of truth.

---

# Developer / manual flashing

Individual ESP32 images can also be flashed directly.

The currently validated layout is:

```text
0x1000   bootloader/bootloader.bin
0x8000   partition_table/partition-table.bin
0xf000   ota_data_initial.bin
0x20000  esp_ot_br.bin
0x620000 web_storage.bin
```

Example:

```powershell
py -m esptool --chip esp32 --port COM3 --baud 460800 `
  --before default_reset --after hard_reset `
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m `
  0x1000 build-sonoff-dongle-m\bootloader\bootloader.bin `
  0x8000 build-sonoff-dongle-m\partition_table\partition-table.bin `
  0xf000 build-sonoff-dongle-m\ota_data_initial.bin `
  0x20000 build-sonoff-dongle-m\esp_ot_br.bin `
  0x620000 build-sonoff-dongle-m\web_storage.bin
```

For source builds, always prefer the generated `flash_args` / `flasher_args.json` from that build rather than assuming these offsets remain unchanged forever.

---

# Automated GitHub builds

Release firmware is built automatically using GitHub Actions.

Normal branch builds produce CI artifacts such as:

```text
sonoff-dongle-m-otbr-ci.bin
sonoff-dongle-m-otbr-ci.bin.sha256
sonoff-dongle-m-otbr-ci-upgrade.bin
sonoff-dongle-m-otbr-ci-upgrade.bin.sha256
```

Tagged releases use the tag name in the output filename.

For example, version `vNEXT` will produce both full and settings-preserving upgrade images once the two-image CI workflow is released:

```text
sonoff-dongle-m-otbr-vNEXT.bin
sonoff-dongle-m-otbr-vNEXT.bin.sha256
sonoff-dongle-m-otbr-vNEXT-upgrade.bin
sonoff-dongle-m-otbr-vNEXT-upgrade.bin.sha256
```

The same files are attached automatically to the corresponding GitHub Release.

This means the downloadable release firmware is built directly by GitHub Actions from the tagged source.

---

# Recovery / returning to stock

The Dongle-M can be returned to the official Sonoff firmware using the Sonoff Dongle Flasher:

https://dongle.sonoff.tech/sonoff-dongle-flasher/

Follow the Sonoff recovery / flashing instructions for the device.

The merged image provided by this project is a raw ESP32 flash image and is **not** intended for use with the Sonoff Web UI firmware-update page.

---

# MG24 RCP

The firmware automatically supports **Sonoff OpenThread RCP v2.4.4 at 115200 baud** and
**v2.4.5 at 460800 baud** (see [issue #15](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/issues/15)).

The ESP32 host communicates using Spinel over UART and selects the verified
rate during every boot. The NCP's internal version string is not itself a
reliable indicator of the UART baud setting; the valid Spinel response at a
particular baud rate determines the selection.

A future improvement may be to provide a fully reproducible host + RCP release path so that both processors can be built and maintained from source as one project.

For now, the safe and validated approach is:

1. install or restore the official Sonoff Dongle-M ESP32 firmware;
2. follow the official Sonoff Web UI procedure to select **Thread RCP Mode** and install a compatible MG24 RCP (v2.4.4 / 115200 or v2.4.5 / 460800), **not Zigbee coordinator firmware**;
3. flash this project's merged **ESP32 host** firmware using the [Sonoff Dongle Flasher](#method-1--pre-built-firmware-using-sonoff-dongle-flasher) or `esptool`;
4. leave the working MG24 RCP firmware unchanged during later ESP32-only upgrades.

---

# Credits

This project builds on the work of:

- [Espressif](https://www.espressif.com/)
- [ESP-IDF](https://github.com/espressif/esp-idf)
- [Espressif esp-thread-br](https://github.com/espressif/esp-thread-br)
- [OpenThread](https://openthread.io/)
- [SONOFF](https://sonoff.tech/)

The OpenThread Border Router implementation is based on Espressif’s upstream work and adapted for the Sonoff Dongle-M hardware.

Additional project notes, migration details, build evidence and hardware validation are available in:

```text
docs/sonoff-dongle-m-migration.md
```

---

# Disclaimer

This is an independent community project.

It is not affiliated with, endorsed by, or officially supported by Sonoff, Espressif or the OpenThread project.

Although the firmware is tested on real hardware, flashing third-party firmware always carries some risk.

You are responsible for verifying that the firmware is appropriate for your hardware and environment.

---

# ☕ Support

If this project has been useful to you, saved you some time, or helped you learn something, you can support continued development here:

👉 **https://buymeacoffee.com/scoobler**

Completely optional, but always appreciated ❤️
