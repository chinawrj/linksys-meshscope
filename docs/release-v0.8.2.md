# MeshScope v0.8.2 — Clear recovery, preserved history

Fixes a misleading combination on Node cards: **Parent correct** beside an old
**Parent restart blocked · 2/2** warning, even though the requested link has
already recovered.

## What changed

- **Connection recovery stays enabled.** Wi-Fi loss uses a 5-minute ESP32
  reboot timeout, the optional WireGuard peer uses 10 minutes, and the Native
  API uses a 15-minute fallback so it does not preempt tunnel recovery.
  These are normal restarts, never factory resets or Linksys resets.
- **Local maintenance resumes without remote access.** Saved Parent mappings,
  rate limits, MQTT mode, and history reload before the workers start. A
  missing NTP clock no longer freezes restored action cooldowns indefinitely.
  See the [recovery audit](https://github.com/chinawrj/linksys-meshscope/blob/v0.8.2/docs/esp32-reboot-recovery.md).
- **Recovery follows the live topology.** The ESP32 observes two consecutive
  new, valid snapshots of the wireless child on its online requested Parent,
  even when the original MQTT verification window has ended.
- **No premature success.** One observation shows **Confirming Parent recovery
  · 1/2**. Duplicate/old generations cannot confirm recovery; mismatches,
  missing/offline nodes, Ethernet ambiguity, and failed/degraded refreshes
  interrupt the sequence.
- **Obsolete work is cancelled.** Queued Parent restarts are cancelled when
  the requested attachment returns, including recovery while the worker waits
  for its network workspace. Genuine unresolved failures retain the existing
  guarded recovery path.
- **History is not deleted.** Lifetime failures, successful MQTT moves, target
  radio data, publish/echo evidence, and timestamps remain in Node details.
  A separate **Last observed recovery** timestamp records passive recovery
  without inflating the successful-MQTT-move count.
- **Graph and Node list agree.** Resolved warnings also leave the Needs
  attention filter. Newer/unrelated failures, weak links, and offline nodes
  still need attention. No topology metrics or client information are removed.

## Verification

Regression tests execute the production C++ health/restart functions with
host-only hardware stubs, including delayed recovery, stale/duplicate samples,
offline/degraded interruption, persisted-history behavior, obsolete requests,
and recovery while a worker waits. JavaScript tests cover cards, details,
operation identity, and attention filtering. The offline `recovered` preview
serves the real embedded gzip bundle under the ESP32 Content Security Policy.

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
node --test tests/test_*.js
python3 tests/serve_workspace_preview.py --port 8766 --scenario recovered
```

## Updating

This fix includes **ESP32 backend code**. Reloading the page or updating only
the Python host cannot repair the ESP32's persisted health state. Compile and
OTA with your existing private local YAML; do not use a CI configuration on a
real device. The stored history is reconciled after fresh topology returns.

```sh
python3 tools/generate_esp32_meshscope_assets.py --check
esphome run esphome_meshscope_c5.local.yaml --device <your-device-address>
```

Use the corresponding private configuration for C3/C6. Saved Parent mappings,
action limits, Wi-Fi/WireGuard credentials and routes, Linksys credentials,
and router firmware are not changed. Only the recovery timeouts described
above change. No credential-bearing binaries or private configurations are
included in the GitHub release.
