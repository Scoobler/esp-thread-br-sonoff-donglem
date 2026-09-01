# AGENTS.md — Sonoff Dongle-M Upstream Migration

## Mission

Create a maintainable Sonoff Dongle-M port from the current `espressif/esp-thread-br` upstream codebase.

This checkout is already on a clean upstream-derived development branch. Treat the existing customised branches in this repository as **legacy donor/reference implementations only**. Do not merge them wholesale into the new upstream-based branch.

The immediate goal is to establish a clean, supportable Dongle-M hardware and connectivity baseline on current upstream. Web UI enhancements, deeper RCP diagnostics, replacement MG24 RCP firmware, and automatic RCP flashing are later stages.

Prefer current upstream architecture and APIs wherever possible.

## Current Repository Layout

Expected repository roles:

- `upstream/main` — official Espressif `esp-thread-br` source.
- `codex/upstream-rebase` — clean upstream-derived development branch; this is the active migration branch.
- `main` — legacy customised Sonoff Dongle-M implementation; reference/donor only.
- `legacy/current-working` — frozen known-working legacy snapshot; reference/donor only.

At the start of this migration, the clean development branch was created directly from upstream commit `cd0b64f`.

Always fetch before comparing and record the actual SHAs in use. Do not assume the SHA above remains current forever.

### Critical Git rule

Do **not**:

- merge `main` into `codex/upstream-rebase`;
- merge `legacy/current-working` into `codex/upstream-rebase`;
- rebase the clean upstream branch onto the legacy branches;
- force-push published branches;
- rewrite published history;
- delete or overwrite the legacy safety branches/tags.

Use the legacy branches only for comparison and selective reimplementation.

Port behaviour, not history.

## Migration Philosophy

The legacy project is a donor and reference implementation, not the new base.

For each feature:

1. Inspect how current upstream implements the relevant subsystem.
2. Inspect the legacy implementation only to understand required Dongle-M behaviour.
3. Reimplement the minimum necessary behaviour using current upstream architecture.
4. Avoid copying entire legacy files unless there is a clear technical reason.
5. Preserve upstream fixes and newer APIs.
6. Keep changes small, reviewable, and independently testable.

Never solve migration difficulty by restoring old upstream infrastructure over newer upstream code.

# Architecture Rule: Isolate Dongle-M Hardware

Do not scatter Dongle-M GPIO numbers, UART settings, Ethernet PHY settings, flash settings, or RCP control logic throughout generic upstream files.

First inspect the current upstream board/Kconfig/component architecture.

Implement the Sonoff Dongle-M as a **named board profile, Kconfig board option, dedicated board component, or equivalent structure consistent with current upstream conventions**.

A suitable option may look conceptually like:

`CONFIG_ESP_BR_BOARD_SONOFF_DONGLE_M=y`

The exact name and implementation should follow current upstream style.

## Hardware facts belong in the board profile

Centralise physical board facts such as:

- MCU target / supported ESP target;
- flash size;
- RCP UART port;
- UART pins;
- UART speed/framing/flow control;
- Ethernet PHY type and RMII wiring;
- MG24 reset/bootloader control pins;
- RGB LED GPIOs;
- any board-specific electrical constraints.

Generic application code should consume the board profile rather than repeat literal Dongle-M hardware values.

## Product policy does not belong in the board profile

Keep higher-level behaviour in separate components/modules, including:

- Ethernet-first uplink policy;
- Wi-Fi fallback;
- recovery SoftAP behaviour;
- persistent failure counters;
- LED colour/timing/state policy;
- Web UI behaviour;
- dataset-management features.

The board profile should describe **what the hardware is**.

Separate application components should describe **how the product behaves**.

# Non-Negotiable Dongle-M Hardware Support

## Host / RCP architecture

- ESP32 host.
- 16 MB flash.
- EFR32MG24 acting as the Thread Radio Co-Processor.
- Host-to-RCP transport is UART.

## RCP UART baseline

Current known working Sonoff RCP interface:

- UART1
- 115200 baud
- 8 data bits
- no parity
- 1 stop bit
- no hardware flow control
- host RX: GPIO13
- host TX: GPIO17

Do not increase the baud rate or enable flow control during the initial migration unless explicitly requested.

Later RCP work may test different transport settings after the baseline is proven.

## MG24 control

Existing reverse-engineered/documented control path:

- GPIO15 participates in holding/muting/controlling the MG24 path.
- GPIO12 is used for MG24 reset / bootloader entry.

Treat the exact electrical behaviour and timing as something to verify before implementing automatic RCP firmware updates.

Do not overwrite an MG24 bootloader or introduce destructive RCP update behaviour without a documented recovery path.

## Ethernet

- Ethernet PHY: IP101GA.
- Use the Dongle-M ESP32 RMII wiring already established in the legacy implementation.
- Ethernet support is mandatory.

Do not replace the board-specific PHY configuration with generic defaults unless they are electrically equivalent and verified.

## RGB LED

Known channels:

- Red: GPIO4
- Blue: GPIO2
- Green: GPIO14

Keep hardware definition separate from LED state policy.

# Required LED Behaviour

Preserve the intended user-visible status behaviour unless current upstream makes an equivalent implementation cleaner.

## Boot indication

Briefly show:

1. Red
2. Green
3. Blue

This acts as a basic RGB/self-test.

## Base interface colour

Indicate the selected OTBR backbone/uplink:

- Ethernet: blue
- Wi-Fi: orange
- provisioning/recovery SoftAP: purple

The displayed interface must match the interface actually selected as the OpenThread backbone.

Do not allow late network events to make the LED indicate one interface while OpenThread is using another.

## Thread status pulse

Once OpenThread is ready:

- pulse for approximately 200 ms every 2 seconds;
- green when attached as child/router/leader;
- red when detached/disabled.

Suppress the initial red Thread pulse for approximately 15 seconds after OpenThread becomes ready to avoid misleading startup indication.

Timing constants should be named/configurable rather than unexplained magic values.

# Required Connectivity Policy

The new implementation must preserve the legacy recovery intent while using current upstream networking architecture.

## Priority

1. Ethernet first.
2. Saved Wi-Fi fallback.
3. Provisioning/recovery SoftAP when required.

## Ethernet

- Try Ethernet first.
- If Ethernet obtains working network connectivity, select it as the deterministic OpenThread backbone.
- Do not silently switch backbone later because Wi-Fi or another interface subsequently becomes available.

The legacy implementation waits approximately 10 seconds for Ethernet. Preserve the intent; use current upstream mechanisms where possible.

## Wi-Fi

If Ethernet is unavailable:

- If saved Wi-Fi credentials exist, attempt Wi-Fi.
- The legacy implementation allows approximately 12 seconds for Wi-Fi connection.
- Retry must be bounded.
- Do not create blocking or infinite retry loops.

When Wi-Fi successfully connects:

- reset persistent Wi-Fi boot failure count;
- use Wi-Fi as the selected backbone for that boot unless the architecture deliberately restarts/reselects.

## No saved Wi-Fi credentials

If Ethernet is unavailable and no saved Wi-Fi credentials exist:

- start provisioning SoftAP immediately.

## Recovery after repeated Wi-Fi failure

If saved credentials repeatedly fail:

- maintain a boot failure count in NVS;
- after five failed boots, expose provisioning/recovery SoftAP;
- keep the recovery window bounded;
- the legacy implementation uses approximately 3 minutes;
- if no replacement credentials are supplied, reboot/retry normal connection logic;
- reset the failure count after successful connection;
- reset the failure count when the configured SSID changes.

Do not remain permanently stuck in SoftAP because the configured Wi-Fi was temporarily unavailable.

## Deterministic backbone selection

OpenThread must use one authoritative backbone interface.

A late Ethernet/Wi-Fi event must not:

- silently change the selected backbone;
- make UI/LED state disagree with the selected backbone;
- corrupt Thread operation through conflicting interface state.

# Thread / RCP Issue Context

The migration is partly motivated by a real Thread SED latency problem.

Observed behaviour:

- IKEA BILRESA Matter-over-Thread buttons operate normally on an Apple Thread network.
- The same class of BILRESA device becomes very slow on the Sonoff OTBR network.
- Physical button press to Matter/Home Assistant event can take tens of seconds.
- Once Home Assistant receives the event, downstream actions execute normally.
- Two slow BILRESA devices were attached to different Thread parents.
- RLOC churn was low, so repeated re-parenting is not currently the leading explanation.

The fault boundary is therefore believed to be below Home Assistant automation logic, potentially involving:

- SED uplink handling;
- parent/router behaviour;
- RCP behaviour;
- host↔RCP interaction;
- OpenThread/RCP capability handling.

Historical detail:

- The legacy build had to compile around/remove an `rx-on-when-idle` RCP capability requirement because the Sonoff-provided MG24 Thread RCP did not advertise/support it as expected.

Do not assume this capability mismatch is conclusively the root cause.

Instrument first. Change RCP firmware later.

# Migration Stages

Keep the migration staged.

## Stage 1 — Clean upstream Dongle-M baseline

Goal:

- clean upstream builds;
- Dongle-M board profile exists;
- MG24 communicates over stock Sonoff RCP;
- Ethernet works;
- Wi-Fi fallback works;
- recovery SoftAP works;
- LED behaviour works;
- Home Assistant/OTBR basic functionality works.

Do not replace the RCP in this stage.

## Stage 2 — RCP diagnostics / Web UI

After Stage 1 is proven:

- expose RCP version;
- expose Spinel/RCP API information;
- expose radio capabilities;
- expose relevant host/OpenThread version information;
- establish stock Sonoff RCP baseline.

## Stage 3 — New/custom MG24 RCP

After diagnostics are available:

- evaluate current Sonoff RCP;
- evaluate `darkxst/silabs-firmware-builder` or a reproducible self-built MG24 OpenThread RCP;
- initially preserve 115200, 8N1, no flow control where practical;
- verify hardware target compatibility;
- test `RX_ON_WHEN_IDLE` and other radio capabilities;
- perform controlled BILRESA A/B testing.

## Stage 4 — Bundled RCP / automatic update

Only after a replacement RCP is proven:

- use current Espressif RCP update architecture where practical;
- bundle the known-good MG24 RCP with the ESP build;
- version-check before flashing;
- avoid unnecessary rewrites;
- provide bounded retry and recovery;
- preserve manual recovery path.

Do not prematurely implement later stages during earlier tasks.

# Upstream Baseline Rules

Before porting Sonoff changes:

1. Fetch `upstream`.
2. Confirm exact upstream commit.
3. Determine the ESP-IDF version required by that exact upstream revision.
4. Build unmodified upstream using the supported toolchain.
5. Record build command, IDF version, and result.

Do not assume the old legacy ESP-IDF version is still correct.

Do not update to an arbitrary moving branch without recording the commit SHA.

# Legacy Delta Inventory

Before copying code, inspect the legacy changes relative to their original upstream ancestor.

Classify each meaningful legacy change as:

- hardware-required;
- connectivity-required;
- product behaviour;
- Web UI / UX;
- diagnostic;
- obsolete;
- superseded by current upstream;
- later-stage RCP work.

Prefer extracting behaviour from the legacy implementation rather than copying files wholesale.

Useful legacy areas to inspect include:

- `examples/basic_thread_border_router/main/esp_ot_config.h`
- `examples/basic_thread_border_router/sdkconfig.defaults`
- `examples/basic_thread_border_router/components/led_status/`
- `examples/common/thread_border_router/src/border_router_launch.c`
- `components/esp_ot_br_server/src/esp_br_wifi_config.c`

Paths may differ in current upstream. Follow current architecture rather than recreating obsolete paths.

# Web UI Rules

Current upstream may have substantially changed the Web UI, REST API, and page structure.

Do not restore the legacy monolithic UI over current upstream merely because it contains useful features.

Specifically:

- do not overwrite current upstream frontend architecture with old `restful.js`;
- do not copy entire legacy frontend directories without first mapping the desired behaviour onto current upstream;
- preserve current upstream fixes and APIs.

For each desired legacy feature:

1. identify the user-visible behaviour;
2. identify the current upstream API/page structure;
3. implement only the missing behaviour using the current structure.

Potential later features include:

- active Thread dataset display;
- deliberate export of a portable TLV backup;
- dataset/TLV import with validation and confirmation;
- improved network properties;
- topology visualisation;
- commissioning information;
- mobile presentation;
- RCP diagnostics.

# Secret Handling

Treat the following as sensitive:

- Thread Active Operational Dataset;
- network key;
- PSKc;
- Wi-Fi credentials;
- commissioning credentials;
- any other secret-bearing provisioning information.

Rules:

- never print secrets in routine logs;
- never include secrets in diagnostic exports by default;
- never expose secrets merely because an API happens to provide them;
- dataset export must be a deliberate user action;
- dataset import must validate input and require clear user intent;
- malformed or oversized input must be rejected safely.

Do not commit credentials or generated secrets to Git.

# RCP Compatibility Rules

The EFR32MG24 is a standard OpenThread Spinel RCP, not a Home Assistant-specific transport.

When later changing RCP firmware, verify and record:

- board/radio target;
- firmware version;
- OpenThread version;
- Spinel/RCP API compatibility;
- UART baud;
- UART framing;
- flow-control expectation;
- reset/bootloader behaviour;
- RF/antenna/front-end configuration;
- checksums/provenance.

Pay particular attention to capabilities such as:

- `RX_ON_WHEN_IDLE`
- `SLEEP_TO_TX`
- `RX_TIMING`
- `TX_TIMING`

Never fake capability values in the UI.

If a capability cannot be queried reliably, show `unknown` / `not exposed`.

# Testing Rules

Do not claim a hardware validation gate passed without physical evidence.

If Codex cannot access hardware, clearly mark the test as:

`PENDING HARDWARE VALIDATION`

Separate:

- build/static tests Codex can perform;
- hardware tests the user must perform.

## Minimum clean-build gate

Before handing back a code stage:

- clean configure/reconfigure;
- clean build;
- no new compiler errors;
- no unexplained new warnings;
- flash artifacts identified;
- build commands documented.

## Stage 1 hardware gates

Require evidence for:

- MG24 responds over UART1;
- OpenThread initialises;
- Ethernet boot selects Ethernet;
- Ethernet LED state is correct;
- Ethernet unavailable + valid Wi-Fi selects Wi-Fi;
- Wi-Fi LED state is correct;
- no credentials starts provisioning SoftAP;
- repeated Wi-Fi failure reaches recovery SoftAP;
- successful Wi-Fi clears failure count;
- late interface events do not switch/misreport backbone;
- Thread attached/detached states produce intended pulses;
- reboot preserves valid Thread/network configuration.

## Later BILRESA acceptance test

The eventual sleepy-device success criterion is:

1. leave an IKEA BILRESA idle long enough to sleep;
2. press the button;
3. Matter/Home Assistant event arrives effectively immediately;
4. target latency should be comfortably below 250 ms where practical;
5. repeat across multiple idle periods;
6. no multi-second or approximately 30-second delayed events;
7. no abnormal detach/RLOC churn;
8. no repeated RCP resets/UART faults.

Do not claim the SED issue fixed solely because one press is fast.

# Commit / Handoff Rules

Make small commits with one purpose.

Prefer commit structure such as:

- board/profile definition;
- RCP transport;
- Ethernet PHY integration;
- LED state component;
- connectivity policy;
- documentation/tests.

Avoid one giant migration commit when practical.

Before handing a task back, report:

- active branch;
- upstream SHA used;
- relevant legacy SHA used;
- commits created;
- files changed;
- build command;
- build result;
- warnings;
- hardware tests required;
- hardware tests completed;
- unresolved risks;
- recommended next action.

Do not merge feature work yourself unless explicitly instructed.

# Documentation

Maintain a migration document in the repository.

Record:

- upstream commit;
- ESP-IDF version;
- donor commit/reference;
- architectural decisions;
- features ported;
- features deliberately not ported;
- build evidence;
- hardware observations;
- known issues;
- future migration stages.

Update it after each meaningful milestone.

README content should describe only behaviour that is actually merged/tested, not speculative future features.

# Stop Conditions

Stop and report evidence rather than improvising if:

- current upstream architecture conflicts materially with an assumption in this file;
- Dongle-M ESP target/flash/PHY wiring cannot be established confidently;
- candidate MG24 firmware targets different hardware/RF configuration;
- flashing could overwrite an unrecoverable bootloader;
- the only route forward would expose secrets;
- a change would require deleting major current upstream functionality;
- the migration would require merging legacy history wholesale;
- an unbounded reboot/retry/update loop appears necessary;
- hardware validation is required before a potentially destructive step.

Prefer a documented blocker over a speculative hardware change.

Build Artifacts and Remote Hardware Testing

Codex normally builds this project inside a hosted code-server / Docker environment on a Raspberry Pi.

The physical Sonoff Dongle-M used for hardware validation is connected to a separate laptop.

Therefore:

* Codex should perform source changes, clean builds, static validation, and artifact generation in the hosted build environment.
* Codex must not assume direct USB/serial access to the Dongle-M.
* Hardware flashing and physical validation will normally be performed manually by the user from the laptop.
* Do not mark hardware tests as passed unless the user reports the result.

Hardware-test artifact bundle

Whenever a build is ready for physical testing, create a self-contained test bundle in a predictable directory such as:

artifacts/<build-name>/

The bundle should include all files required to reproduce the exact flash operation.

Where applicable include:

* application .bin;
* bootloader .bin;
* partition table .bin;
* initial OTA data image;
* Web UI/storage image;
* bundled RCP image;
* any other partition image required by the current build;
* ELF and MAP files where useful for debugging;
* flash arguments;
* partition layout;
* build metadata;
* SHA-256 checksums.

Prefer preserving the original generated filenames where practical.

Required metadata

Each hardware-test bundle should contain a human-readable file such as:

BUILD_INFO.md

or:

build-info.txt

containing at minimum:

* Git branch;
* Git commit SHA;
* upstream commit SHA;
* ESP-IDF version;
* ESP target;
* selected board/profile;
* RCP firmware/version where applicable;
* UART configuration;
* flash size/mode/frequency;
* build date;
* exact clean-build command;
* build result;
* known warnings;
* hardware validation status.

Clearly mark hardware validation as:

PENDING HARDWARE VALIDATION

until the user reports otherwise.

Flash instructions

Include the exact flash layout generated by ESP-IDF.

Do not assume standard ESP32 offsets.

Generate a file such as:

flash-command.txt

containing the exact esptool/ESP-IDF flash arguments needed for that build.

The command may use a placeholder serial port such as:

<PORT>

because the physical device is connected to another machine.

For example, generate the equivalent of:

python -m esptool --chip <chip> --port <PORT> ...

using the actual chip, offsets, flash mode, flash frequency, flash size, and image filenames generated by the build.

Do not hard-code example offsets if they differ from the current partition table.

Where ESP-IDF generates flash_args, flash_project_args, or equivalent files, include them in the bundle.

Merged flash image

Where ESP-IDF/esptool supports it safely, also generate a single merged factory/test image in addition to the individual partition images.

Example name:

sonoff-dongle-m-full-flash.bin

The merged image is a convenience for hardware testing and must not replace the individual artifacts or documented partition map.

Document:

* required flash offset;
* erase requirements;
* chip target;
* whether flashing the merged image affects only the ESP host or also causes an MG24 RCP update at first boot.

Do not generate or distribute a merged image until the selected board configuration and flash layout are known to be correct for the Dongle-M.

Artifact transfer

The build environment and hardware-test laptop are separate systems.

Codex should therefore make the artifact directory easy to:

* download through code-server;
* copy through SCP/SFTP;
* expose through an existing mounted/shared directory if one is available.

Do not invent network paths or credentials.

If no shared path exists, simply provide the exact artifact directory and filenames so the user can transfer them manually.

Hardware validation handoff

For every hardware-test build, provide a concise test checklist.

At minimum state:

1. which artifact to flash;
2. exact flash command;
3. expected serial/boot behaviour;
4. expected LED behaviour;
5. expected Ethernet/Wi-Fi/AP behaviour;
6. expected OTBR/RCP behaviour;
7. specific observations/logs the user should return;
8. rollback/recovery procedure if the firmware does not boot.

Do not continue into destructive or higher-risk firmware changes until the required hardware gate from the previous stage has been reported by the user.