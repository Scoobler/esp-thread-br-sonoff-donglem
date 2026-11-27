# Border router settings, status and backup/restore

The `esp_br_settings` component (`CONFIG_ESP_BR_SETTINGS`, enabled in the Dongle-M
build) adds persistent settings, a radio/link status view and a JSON backup/restore
to the border router web server. It registers its routes through
`esp_br_web_register_extension()`, ahead of the static-file catch-all, and serves two
embedded pages: `/settings.html` and `/radio.html`.

Status: built and host-tested only. Not yet validated on Dongle-M hardware.

Like the rest of the Web UI, these endpoints have no authentication. Backups contain
the Thread network key.

All JSON responses use the Web UI envelope:

```json
{"error": 0, "result": { ... }, "message": "Settings: Success"}
```

`error` is an OpenThread error code (`0` on success).

## Settings

Settings are overrides stored in NVS (namespace `br_settings`). Without an override
the firmware/stack default is used. Overrides are applied at boot, after
`esp_openthread_border_router_init()` and before Thread starts, and immediately when
changed, except settings marked `requires_restart`.

| Key | Type | Range / options | Notes |
|---|---|---|---|
| `radio.tx_power` | int (dBm) | -20 … 20 | Stock MG24 RCP starts at 0 dBm. EU: max. 20 dBm EIRP |
| `radio.cca_threshold` | int (dBm) | -100 … -10 | CCA energy-detect threshold |
| `link.max_frame_retries_direct` | int | 0 … 15 | |
| `link.max_frame_retries_indirect` | int | 0 … 15 | Sleepy children |
| `trel.enabled` | bool | | Default from `CONFIG_ESP_BR_SETTINGS_TREL_DEFAULT` (on) |
| `thread.router_eligible` | bool | | |
| `thread.leader_weight` | int | 0 … 255 | |
| `thread.router_selection_jitter` | int (s) | 1 … 255 | |
| `thread.router_upgrade_threshold` | int | 1 … 32 | |
| `thread.router_downgrade_threshold` | int | 1 … 32 | |
| `thread.network_id_timeout` | int (s) | 10 … 255 | |
| `thread.max_children` | int | 1 … `CONFIG_OPENTHREAD_MLE_MAX_CHILDREN` | Requires restart |
| `border_routing.enabled` | bool | | |
| `border_routing.rio_preference` | enum | `low`, `medium`, `high` | RIO advertised to the LAN; reset clears the override |
| `border_routing.route_preference` | enum | `low`, `medium`, `high` | Route published in Network Data |
| `border_routing.dhcp6_pd` | bool | | DHCPv6 prefix delegation for the OMR prefix |
| `border_routing.nat64` | bool | | NAT64 prefix publishing |
| `bbr.enabled` | bool | | Backbone Router |
| `bbr.registration_jitter` | int (s) | 1 … 255 | |
| `log.ot_level` | enum | `none`, `crit`, `warn`, `note`, `info`, `debg` | |
| `log.esp_level` | enum | `none` … up to the build's maximum level | Applies to all tags |

### `GET /br/settings`

Returns `result.settings`, an array with one entry per setting:

```json
{"key": "radio.tx_power", "group": "Radio", "label": "Transmit power", "unit": "dBm",
 "type": "int", "min": -20, "max": 20, "requires_restart": false,
 "value": 17, "configured": 17, "default": 0, "apply_error": null}
```

`value` is read from the stack, `configured` is the saved override (`null` if none),
`default` is the value recorded at boot before overrides were applied.

### `POST /br/settings`

Partial update. `null` removes an override and restores the default. The request is
validated completely before anything is changed.

```sh
curl -X POST http://<br>/br/settings -H 'Content-Type: application/json' \
  -d '{"radio.tx_power": 17, "trel.enabled": true, "border_routing.rio_preference": null}'
```

`result.changes` reports `applied`, `restart_required`, `apply_failed` or an NVS error
per changed key. Invalid input returns HTTP 400 with per-key reasons in `result`.

### `POST /br/settings/reset`

`{"keys": ["radio.tx_power"]}` resets the listed keys; an empty body resets all.

## Status

### `GET /br/status`

Read-only snapshot with `node`, `radio` (channel, TX power, CCA threshold and failure
rate, receive sensitivity, RCP version), `mac_counters`, `mle_counters`, `neighbors`
(average/last RSSI, link margin, link quality, frame/message error rate, age),
`routers` (next hop, path cost, link quality in/out), `children`, `trel` (enabled,
UDP port, peers, counters), `border_routing` (state, OMR and on-link prefixes,
preferences, DHCPv6-PD and NAT64 state, prefix table) and `services` (Backbone Router,
SRP server).

### `POST /br/status/counters/reset`

Resets MAC, MLE and TREL counters.

## Backup and restore

### `GET /br/backup`

Downloads `otbr-backup-<ext-address>.json`:

```json
{"format": "esp-br-backup", "version": 1,
 "firmware": {"project": "esp_ot_br", "version": "...", "idf": "v5.5.6"},
 "node": {"ext_address": "..."},
 "dataset": {"active": "<Active Operational Dataset TLVs, hex>"},
 "settings": {"radio.tx_power": 17}}
```

`dataset.active` is the same value as `ot dataset active -x`.

### `POST /br/restore`

Body: a backup file, optionally with `"restore": {"dataset": true, "settings": true}`
(both default to `true`).

- **Settings** replace all saved overrides. Settings that are not in the file return
  to their defaults.
- **Dataset**: must contain at least the network key, channel, PAN ID, extended PAN ID
  and active timestamp. If it differs from the current Active Dataset, Thread is
  stopped, the dataset is written and Thread is restarted if it was running. No reboot
  is needed. An identical dataset is reported as `unchanged`.

`result.dataset` is `applied`, `unchanged`, `skipped` or an OpenThread error.

## TREL notes

- TREL peers are discovered through mDNS (`_trel._udp`) on the backbone. The other
  border routers must support and enable TREL.
- ESP-IDF binds TREL to the interface on which the first peer was discovered. After a
  Dongle-M Ethernet/Wi-Fi switch, restart the border router to rebind TREL.
- Check with `dns-sd -B _trel._udp local.` (macOS) or the **Radio** page.
