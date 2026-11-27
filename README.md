# ESP-IDF OpenThread Border Router on Sonoff Dongle-M

Custom OpenThread Border Router firmware for the **Sonoff Dongle-M / Dongle Max**, built on Espressif’s `esp-thread-br` and adapted for the Dongle-M hardware.

The ESP32 inside the Dongle-M runs the OpenThread Border Router, while the onboard **EFR32MG24** continues to operate as the Thread Radio Co-Processor (RCP).

This project provides a practical, self-contained OTBR solution with:

- Ethernet and Wi-Fi networking
- Ethernet-first connection policy with Wi-Fi fallback
- OpenThread Border Router functionality
- Thread network setup and a serial console for dataset backup/restore
- Live Thread topology view
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

**Recommended:** [Dongle-M Web Flasher](https://scoobler.github.io/esp-thread-br-sonoff-donglem/) — use **Fresh installation** when replacing the original Sonoff firmware or starting over, or **Upgrade** to keep your settings when updating an existing installation of this project's firmware.

**[View the latest published GitHub Release](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/releases/latest)**

The published **v2.3.0** release includes two ESP32 firmware files:

- `sonoff-dongle-m-otbr-v2.3.0.bin` — full merged firmware, written at `0x0` for Fresh installation (with full-chip erase).
- `sonoff-dongle-m-otbr-v2.3.0-upgrade.bin` — settings-preserving Upgrade image, written at `0x20000` **only for compatible existing installations of this project's firmware**.

Both files include matching SHA-256 checksums. Neither image updates the MG24 OpenThread RCP.

Previous versions, including v2.2.0, remain available under [GitHub Releases](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/releases).

---

## Overview

This firmware allows the ESP32 inside the Sonoff Dongle-M to operate as a full **OpenThread Border Router**.

The onboard EFR32MG24 remains in **Thread RCP mode** and communicates with the ESP32 over UART using Spinel.

The ESP32 handles:

- OpenThread Border Router services
- Ethernet and Wi-Fi uplink
- NAT64 / Thread infrastructure services
- Thread dataset storage and serial-console backup/restore
- Web-based Thread network creation and diagnostics
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

In the Sonoff Web UI, open **EFR32MG24 → Operation Mode** and select **Thread RCP Mode**. Follow Sonoff's firmware-installation instructions if needed. The MG24 must run a compatible **OpenThread RCP**, not Zigbee coordinator firmware, before you install the ESP32 firmware from this repository.

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

<details>
<summary><strong>Updating the MG24 OpenThread RCP firmware</strong></summary>

You only need this procedure if you want to change the MG24's radio firmware. **Back up your existing Thread dataset first** using the instructions above.

1. Connect the Dongle-M via USB-C and use the [official Sonoff Dongle Flasher](https://dongle.sonoff.tech/sonoff-dongle-flasher/) to restore Sonoff's ESP32 firmware.
2. Open the Sonoff Web UI over Ethernet, if available, or follow Sonoff's Wi-Fi access-point setup.
3. Use Sonoff's MG24 firmware options to install a **compatible OpenThread RCP** (not Zigbee firmware). The exact menu wording may vary with Sonoff's firmware version.
4. Reinstall this project's ESP32 firmware using the [Dongle-M Web Flasher](#method-1--web-flasher-recommended) in **Fresh installation** mode.
5. Reconnect over Ethernet or configure Wi-Fi, then [check and restore your Thread dataset](#dataset-backup--upgrade-notes) if required.

Our ESP32 firmware detects a compatible RCP at **115200 or 460800 baud** automatically. It does **not** update the MG24 firmware.

</details>

---

# Flashing the firmware

The easiest way to install or update the ESP32 firmware is the **Dongle-M Web Flasher**. All three methods below use USB-C. **None of them updates the MG24 RCP.**

### Method 1 — Web Flasher (recommended)

**[Open the Dongle-M Web Flasher](https://scoobler.github.io/esp-thread-br-sonoff-donglem/)**

Use desktop **Chrome or Edge** with a USB-C connection to the Dongle-M. The page uses [Espressif's esptool-js](https://github.com/espressif/esptool-js) to flash the ESP32 directly from the browser.

1. For a first installation, [set up the MG24 as a Thread RCP](#prerequisites) using the official Sonoff firmware.
2. Open the Web Flasher and select the Dongle-M's USB serial port.
3. Choose the correct operation:
   - **Fresh installation:** for devices still running Sonoff's ESP32 firmware, or when you want to start over. **This erases the ESP32's saved settings**, including Wi-Fi credentials and the Thread dataset.
   - **Upgrade:** only for a Dongle-M already running a compatible version of **this project's firmware**. Keeps the saved Wi-Fi and Thread settings; **do not use it to convert directly from Sonoff firmware**.
4. Leave **Latest published release** selected, acknowledge the selected operation and click **Connect and flash**. The **Custom .bin** option is intended for development/testing.
5. Wait for the ESP32 to restart. With Fresh installation, connect via Ethernet or configure Wi-Fi and restore the Thread dataset if needed. With Upgrade, check that the existing connection and dataset were retained.

Back up an existing [Thread dataset](#dataset-backup--upgrade-notes) before flashing, even when using Upgrade.

**If Web Serial isn't available** on your computer or browser, use [command-line esptool](#method-3--manual-esptool). The official Sonoff Dongle Flasher also runs in a browser, so it is not a workaround for missing browser serial support.

### Method 2 — Sonoff Dongle Flasher (alternative full installation)

The [official Sonoff Dongle Flasher](https://dongle.sonoff.tech/sonoff-dongle-flasher/) can flash this project's **full merged ESP32 image** as an alternative installation or recovery method.

1. From [GitHub Releases](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/releases/latest), download the **full** firmware file, for example `sonoff-dongle-m-otbr-v2.3.0.bin`.
2. Connect the Dongle-M via USB-C, open the Sonoff Dongle Flasher, select the serial port and choose **Customize**.
3. Select the full `.bin` file and flash it. Treat this as a fresh installation: **saved Wi-Fi and Thread settings may be overwritten**.

**Do not use any `*-upgrade.bin` file in the official Sonoff flasher.** That image is designed to be written at `0x20000` by our Web Flasher or by esptool. The Sonoff flasher does not support this settings-preserving upgrade method.

The firmware-update page inside the stock Sonoff Web UI also cannot install this project's merged ESP32 image.

### Method 3 — Manual esptool

For command-line flashing, install [Espressif's esptool](https://github.com/espressif/esptool) and download the matching firmware image from [GitHub Releases](https://github.com/Scoobler/esp-thread-br-sonoff-donglem/releases/latest). Replace `COM3` with your own USB serial port.

**Fresh installation — erases ESP32 settings:**

```powershell
py -m esptool --chip esp32 --port COM3 erase_flash
py -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x0 sonoff-dongle-m-otbr-v2.3.0.bin
```

**Upgrade — preserves settings on compatible existing installations:**

```powershell
py -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x20000 sonoff-dongle-m-otbr-v2.3.0-upgrade.bin
```

On Linux/macOS, use `python3 -m esptool` instead of `py -m esptool`, and use the appropriate serial port (for example `/dev/ttyUSB0`). The command-line `--baud 460800` is the **ESP32 USB flashing speed**, not the MG24 RCP's normal UART rate.

**Never run `erase_flash` or write the full merged image at `0x0` when you intend to retain settings.** The Upgrade image updates both ESP32 application slots and the Web UI; it does not provide OTA rollback. See [upgrade technical notes](docs/usb-web-flasher.md).

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

Once the Dongle-M is connected to Ethernet or Wi-Fi, open its IP address in a browser.

**v2.3.0 uses Espressif's upstream Web UI**, rather than the custom interface from the project's early proof-of-concept versions. Its pages provide:

- **Dashboard:** Thread role, channel, network identifiers, IPv6 addresses and system information.
- **Management → Network:** scan for nearby Thread networks or form a new network.
- **Management → Commissioner:** start a Thread commissioner for compatible joiners.
- **Management → Addresses:** inspect IPv6 addresses and manage on-mesh prefixes.
- **Management → Tools:** send diagnostic IPv6 pings.
- **Topology:** view the Thread mesh, routers and child devices.
- **About:** view the ESP-IDF, OpenThread and RCP versions.

**The current Web UI does not provide Active Operational Dataset TLV export/import or dataset backup/restore.** Those functions were part of the earlier custom UI. To back up or restore an existing network, use the [USB serial console instructions](#dataset-backup--upgrade-notes).

If your Dongle-M is already on the correct Thread network, avoid **Form Network** unless you deliberately want to create a different network.

## Settings, Radio status and backup/restore (unreleased)

> Not yet hardware-validated and not part of any published release.

Builds from the current source add three things to the Web UI and a REST API below `/br/`:

- **Management → Settings:** persistent overrides, stored in NVS, for radio transmit power and CCA threshold, MAC frame retries, TREL, Thread router/leader tuning, border routing (RIO and route preference, DHCPv6-PD, NAT64), the Backbone Router and log levels. Changes are applied immediately unless marked as needing a restart. Settings survive reboots and settings-preserving upgrades; a fresh installation resets them.
- **Radio:** link quality per neighbor (RSSI, link quality, frame and message error rates), router and child tables, MAC and MLE counters, CCA failure rate, TREL peers and border-routing prefixes.
- **Backup & Restore** (on the Settings page): download the Active Operational Dataset together with all saved settings as JSON, and restore the dataset and/or settings from such a file without using the USB console.

**TREL** (Thread Radio Encapsulation Link) is built in and enabled by default. Border routers on the same LAN then also exchange Thread frames over Ethernet/Wi-Fi, so the mesh stays connected even when they cannot hear each other over the radio. Other border routers need TREL as well for this to help.

**Transmit power:** the stock Sonoff MG24 RCP starts at 0 dBm. Raising it improves links to the border router considerably. Stay within your local limit; in the EU that is 20 dBm EIRP including antenna gain, so with the stock antenna (about 3 dBi) use at most 17 dBm.

The backup file contains the Thread network key. The settings and backup endpoints have no authentication, like the rest of the Web UI, so only expose the Web UI to trusted networks.

See [docs/br-settings.md](docs/br-settings.md) for the REST API reference.

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

These GPIO assignments are defined in the board configuration, but **v2.3.0 does not use GPIO15 to automatically reset a failed MG24**. Espressif has a GPIO reset handler in the codebase, but it is registered only when automatic RCP updating is enabled; the Dongle-M release deliberately disables that feature.

OpenThread can also request an **RCP software reset over the Spinel/UART connection**. That is different from physically pulsing the MG24 reset pin and may not recover an unresponsive RCP. Automatic MG24 firmware flashing is not supported.

**GPIO12 is also an ESP32 bootstrapping pin**. Any future MG24 bootloader-control feature must avoid driving it high during ESP32 reset.

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

For example, the published `v2.3.0` release contains:

```text
sonoff-dongle-m-otbr-v2.3.0.bin
sonoff-dongle-m-otbr-v2.3.0.bin.sha256
sonoff-dongle-m-otbr-v2.3.0-upgrade.bin
sonoff-dongle-m-otbr-v2.3.0-upgrade.bin.sha256
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
3. flash this project's merged **ESP32 host** firmware using the [Dongle-M Web Flasher](#method-1--web-flasher-recommended) (Fresh installation), the [Sonoff Dongle Flasher](#method-2--sonoff-dongle-flasher-alternative) or `esptool`;
4. leave the working MG24 RCP firmware unchanged during later ESP32-only upgrades.

---

# Credits

This project builds on the work of:

- [Espressif](https://www.espressif.com/)
- [ESP-IDF](https://github.com/espressif/esp-idf)
- [Espressif esp-thread-br](https://github.com/espressif/esp-thread-br) — original Border Router firmware and Web UI
- [Espressif esptool-js](https://github.com/espressif/esptool-js) — browser-based ESP32 flashing library ([demo](https://espressif.github.io/esptool-js/))
- [OpenThread](https://openthread.io/)
- [SONOFF](https://sonoff.tech/)

The OpenThread Border Router and current device Web UI come from Espressif's upstream project. This repository adapts that firmware for Sonoff Dongle-M hardware. The **Dongle-M Web Flasher** is a small, device-specific interface built using Espressif's esptool-js; the underlying browser flashing implementation is Espressif's work.

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
