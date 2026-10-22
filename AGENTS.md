# AGENTS.md

## Project

This repository maintains Sonoff Dongle-M / Dongle Max support on top of Espressif's `esp-thread-br`.

Upstream:

```text
https://github.com/espressif/esp-thread-br
```

The goal is to keep the Dongle-M-specific patchset small, isolated and practical to maintain as Espressif continues to develop the upstream project.

The public `main` branch is the canonical project history. Published history must not be rewritten simply to incorporate later upstream changes.

## Contributor workflow

Start every operation by fetching the latest `origin/main`. Work from a
branch created from that current `main`; never rebase or rewrite published
`main` history. Keep housekeeping work on its own branch, and keep Espressif
updates on a dedicated `upstream-sync/<date>` or
`upstream-sync/<short-commit>` branch.

Do not create an upstream-sync branch when Espressif has no new upstream
commits. An upstream-sync development branch may be pushed to GitHub and a
Draft PR may be opened before hardware validation. GitHub Actions may build
and publish CI artifacts for those PRs, but a Draft PR is not a release.

Maintain `CHANGELOG.md` as part of contributor work. Add concise, factual
entries for user-visible changes, identify contributors and PRs where useful,
and distinguish unreleased work from published firmware changes. Upstream
integrations should also prepare version-specific draft release notes without
claiming hardware validation.

Record the exact candidate source commit, ESP-IDF version, firmware SHA-256
and incorporated upstream baseline. Never merge an upstream-sync PR into
`main`, create a version tag, or publish a GitHub Release before the user
explicitly reports `HARDWARE PASS`. Promotion is allowed after that report if
the firmware-relevant source is unchanged. Any firmware-relevant change after
hardware testing requires another hardware test; documentation-only release
metadata may be finalized without rebuilding.

The release process must work whether an upstream-sync Draft PR already exists
or not. Preserve the safeguards below, including stopping an upstream update
that requires a major redesign rather than a reasonable incremental merge.

---

## Maintenance principles

- Preserve upstream Espressif behaviour wherever practical.
- Keep Dongle-M-specific hardware and product behaviour behind its board profile or dedicated components.
- Do not modify generic upstream behaviour unless genuinely necessary.
- Do not restore the legacy custom Web UI; the current upstream Web UI is authoritative.
- The stock Sonoff EFR32MG24 Thread RCP is the current supported radio firmware.
- Do not enable automatic MG24/RCP flashing without explicit instruction and hardware validation.
- Do not include an RCP image in the ESP32 merged release image.
- Keep upstream changes and Dongle-M-specific changes understandable from Git history.
- Do not rewrite published `main` history to catch up with upstream.

---

## Current upstream baseline

The v2 codebase currently contains Espressif upstream through:

```text
29a2345e3f0c61bba9307f3daea5ed0c94388ed3
```

At the time this baseline was recorded:

```text
upstream/main = 29a2345e3f0c61bba9307f3daea5ed0c94388ed3
```

This value is a maintenance reference, not a permanent pin.

After a successfully validated upstream update is merged into `main`, update this section to the newly incorporated Espressif commit.

---

## Toolchain

The most recently hardware-validated toolchain is:

```text
ESP-IDF v5.5.4
```

Treat this as the known-good baseline, not a permanent version requirement.

When incorporating newer Espressif upstream changes:

- use the ESP-IDF version required or recommended by that upstream revision;
- do not downgrade or hold back upstream solely to preserve the previous validated version;
- build and report the exact ESP-IDF version used;
- identify toolchain changes before hardware validation;
- treat any toolchain change as requiring normal hardware validation before release.

---

## Dongle-M build

From `examples/basic_thread_border_router`:

```sh
idf.py -B build-sonoff-dongle-m \
  -D SDKCONFIG=sdkconfig.sonoff_dongle_m \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.sonoff_dongle_m \
  build
```

A successful build is not hardware validation.

---

## Release image

From the repository root:

```sh
python3 tools/release/merge_dongle_m_image.py \
  examples/basic_thread_border_router/build-sonoff-dongle-m \
  --output artifacts/sonoff-dongle-m-otbr.bin
```

The generated merged ESP32 image is flashed at offset `0x0`.

The merge tool must continue to use ESP-IDF-generated flash metadata and must not include MG24/RCP firmware.

Generated binaries must not be committed to the repository.

---

# Upstream update workflow

The repository contains an automated periodic check for changes to Espressif's `upstream/main`.

When that check reports new upstream commits, do not modify `main` directly.

## 1. Inspect the upstream change

Fetch the latest upstream state:

```sh
git fetch upstream
```

Determine:

- the current incorporated upstream commit;
- the new `upstream/main` commit;
- the number of new upstream commits;
- the upstream commit range being considered;
- which files and components were changed upstream.

Review the actual upstream commits before attempting integration.

Pay particular attention to changes affecting:

- `examples/basic_thread_border_router`;
- Ethernet or Wi-Fi initialisation;
- OpenThread Border Router infrastructure;
- backbone interface handling;
- Web UI / web storage;
- Thread dataset persistence;
- Spinel / RCP communication;
- ESP-IDF version requirements;
- partition layout or flash metadata;
- configuration defaults;
- components touched by Dongle-M-specific patches.

Do not assume that a clean Git merge means the update is behaviourally safe.

---

## 2. Create an upstream-sync branch

Never test an upstream update directly on `main`.

Create a dedicated branch from the current `main`.

Use a descriptive branch name such as:

```text
upstream-sync/<date>
```

or:

```text
upstream-sync/<short-upstream-commit>
```

Example:

```sh
git switch main
git pull --ff-only origin main
git switch -c upstream-sync/2026-09
```

---

## 3. Merge upstream

Use a normal Git merge.

Do not rebase published `main` history onto the new upstream revision.

Typical operation:

```sh
git merge --no-ff upstream/main
```

If conflicts occur:

- inspect each conflict individually;
- preserve upstream behaviour wherever practical;
- preserve required Dongle-M-specific board support;
- do not blindly choose either "ours" or "theirs";
- do not discard Ethernet/Wi-Fi policy, GPIO, LED, RCP or release-image behaviour without understanding the consequence;
- document any non-obvious conflict resolution.

If the upstream change would require a substantial redesign, stop and report that before forcing the merge through.

---

## 4. Review the resulting delta

After the merge, inspect what remains specific to this project.

The intent is to keep the Dongle-M patchset small and understandable.

Check particularly for:

- duplicated functionality that upstream now provides natively;
- obsolete Dongle-M workarounds;
- upstream APIs or configuration options that can replace custom code;
- behavioural changes that affect network recovery;
- changed build or partition metadata;
- changed ESP-IDF requirements.

Remove obsolete custom work only when the replacement upstream behaviour is understood.

---

## 5. Build the candidate

Build the Dongle-M firmware using the toolchain required by the merged source.

Record:

- upstream commit incorporated;
- project commit built;
- ESP-IDF version;
- build result;
- warnings or errors;
- merged firmware filename;
- SHA-256.

Generate the merged ESP32 image using the normal release-image tooling.

A successful compile and merged image generation are only software validation.

---

## 6. STOP for hardware validation

After a candidate firmware image has been generated:

**STOP.**

Do not:

- merge the update into `main`;
- tag a release;
- create a GitHub Release;
- publish the candidate as a stable firmware release;
- claim the update is hardware validated.

The user performs the physical Dongle-M test.

---

# Hardware-validation gate

Only after the user explicitly reports a:

```text
HARDWARE PASS
```

may the upstream update be promoted.

Hardware validation should include the areas relevant to the upstream changes and, at minimum, confirm that the Dongle-M:

- boots correctly;
- communicates with the MG24 RCP;
- attaches to or restores the expected Thread network;
- provides the Web UI;
- operates over Ethernet;
- performs expected Wi-Fi provisioning/fallback behaviour where applicable;
- remains usable from Home Assistant;
- produces the expected LED state indications.

If the user reports a failure:

- do not publish;
- do not tag;
- record the failure accurately;
- investigate using the exact tested source and image;
- generate a new candidate;
- return to the hardware-validation gate.

---

# Promotion after HARDWARE PASS

After explicit hardware validation:

1. ensure the upstream-sync branch is clean;
2. commit any final documentation/baseline updates;
3. push the upstream-sync branch if required;
4. merge the validated upstream-sync branch into `main`;
5. push `main`;
6. create the next appropriate semantic version tag;
7. push the tag.

For a compatible upstream maintenance refresh, normally increment the patch version.

Example:

```text
v2.0.0 -> v2.0.1
```

Use a minor or major version instead if the resulting behaviour or compatibility warrants it.

Do not create the version tag before hardware validation has passed.

---

# Release notes and upstream changelog

Every release that incorporates new Espressif upstream commits must have a useful GitHub Release description.

The GitHub Release body should explain what changed and why the release exists.

It should include:

## Summary

A short human-readable explanation of the release.

Example:

```text
This release updates the Dongle-M OTBR firmware to incorporate the latest
Espressif esp-thread-br changes since the upstream baseline used by v2.0.0.
```

## Upstream range

Record both ends of the incorporated upstream range.

Example:

```text
Previous upstream baseline:
<previous baseline>

New upstream baseline:
<new commit>

Upstream comparison:
<previous baseline>..<new commit>
```

## Upstream changes incorporated

Review the actual upstream Git log and summarise the meaningful changes being brought into this release.

Do not invent a changelog from commit titles alone when the underlying changes need inspection.

Focus particularly on changes relevant to:

- OpenThread;
- Border Router behaviour;
- ESP-IDF;
- networking;
- Web UI;
- RCP/Spinel;
- Thread infrastructure;
- stability or bug fixes;
- build system changes.

Minor maintenance commits may be grouped together.

Where useful, include the upstream commit IDs.

## Dongle-M impact

State whether the upstream changes required:

- no Dongle-M-specific code changes;
- simple conflict resolution;
- modification of Dongle-M-specific code;
- changes to build configuration;
- changes to the required ESP-IDF version.

## Validation

State that the firmware was built successfully and physically validated on Sonoff Dongle-M hardware before release.

---

# GitHub release assets

Tagged releases are built by GitHub Actions.

The release should contain at least:

```text
sonoff-dongle-m-otbr-<version>.bin
sonoff-dongle-m-otbr-<version>.bin.sha256
```

The GitHub Release **description/body** is the release changelog.

The changelog does not belong inside the SHA-256 file.

Where practical, the release workflow should use a generated Markdown release-notes file as the GitHub Release body so that the description corresponds to the exact source/tag being built.

The release description should be created from the validated upstream range and project changes before or as part of tagging the release.

---

# Publishing

Do not push, tag, create a release, or otherwise promote a newly generated firmware candidate unless:

1. the requested workflow explicitly permits it; and
2. the required hardware-validation gate has passed.

Generated binaries belong in `artifacts/` or GitHub Release assets and must not be committed to the repository.

A release tag must correspond to the exact source that passed hardware validation.

---

# Documentation

Keep:

- `README.md` focused on users, installation and building;
- `docs/sonoff-dongle-m-migration.md` as the concise technical architecture/migration record;
- this file focused on repository maintenance instructions.

Do not turn documentation into a chronological debugging log.

When changing behaviour, update documentation only after the implementation and its validation status are known.

When an upstream release is successfully incorporated, update the upstream baseline recorded in this file.
