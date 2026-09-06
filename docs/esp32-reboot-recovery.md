# ESP32 reboot recovery and independent local maintenance

MeshScope v0.8.2 keeps automatic recovery reboots enabled. They restart the
ESP32 application; they do not erase NVS, factory-reset the ESP32, reboot a
Linksys Node, or change the saved topology.

## Connection watchdogs

| Watchdog | Configuration | Trigger |
| --- | --- | --- |
| Wi-Fi | `reboot_timeout: 5min` | Station remains disconnected |
| WireGuard | `reboot_timeout: 10min` | Enabled peer remains offline |
| Native API | `reboot_timeout: 15min` | No API client is connected |

The pinned ESPHome 2026.7.2 implementations call `App.reboot()` for these
timeouts. WireGuard checks peer state every 10 seconds, so its timeout starts
when the component detects the peer offline, not at the instant an arbitrary
Internet packet is lost. Persistent keepalive remains 25 seconds. A local
API client cannot prevent the independent WireGuard watchdog from running.
The API fallback is longer to avoid a lost HA tunnel causing an earlier
five-minute API restart. These timers are independent; the first expired
condition wins. An API outage that began earlier can still cause an earlier
restart, and losing Wi-Fi normally reaches its five-minute limit first.

These timers are not disabled (`0s`), and there is no long-outage lockout. If
the fault persists after a normal reboot, its timer starts again. Wi-Fi's
stock watchdog is suppressed when a fallback AP is configured; the shipped
packages and the audited deployed C5 do not configure one. Users adding a
fallback AP must re-evaluate that behavior rather than assuming the same
five-minute station recovery guarantee.

References: [ESPHome Wi-Fi](https://esphome.io/components/wifi/),
[WireGuard](https://esphome.io/components/wireguard/), and
[Native API](https://esphome.io/components/api/). The installed pinned source,
not an assumed default from newer documentation, was used for this audit.

## What happens after reboot

1. Wi-Fi reconnects using the existing credentials and saved fast-connect
   information. WireGuard uses `require_connection_to_proceed: false` and
   cannot hold up local application initialization.
2. MeshScope reloads the saved Parent mappings, lock enabled/disabled state,
   action limit, MQTT mode, and persistent health history before starting its
   collector, HTTP server, and MQTT worker.
3. The collector reads Linksys locally. It has no HA/API or WireGuard gate.
   The MQTT worker depends on Wi-Fi and actionable Linksys data, not on a
   remote connection. Auto mode still verifies the local broker's ACL.
4. Recovery uses new observations and the saved hierarchy. Desired Parents
   must be online, and the existing root-to-leaf scheduling, mismatch
   confirmation, Force off, and rate limits still apply.

Reboot does not automatically enable a disabled lock or bypass Force off.
There is a short interruption while the ESP32 boots. During a persistent VPN
outage the local UI and maintenance run between recovery restarts; remote
visibility returns only when the tunnel is reachable again.

## No Internet clock dependency

Previously a saved action timestamp plus an unavailable NTP clock could
return the full cooldown on every evaluation, so a restored lock never became
eligible to act. The same defect affected requested-Parent restart cooldowns.

Restored cooldowns now expire after at most one full cooldown measured from
boot using the monotonic timer. A valid wall-clock age may expire them sooner.
An unavailable/backwards clock cannot freeze them, and the 64-bit boot timer
does not wrap after 49 days. New actions in the current boot retain their
normal monotonic rate limits. No synthetic successful MQTT result is created.

## Verification and limits

- Configuration tests enforce the nonzero 5/10/15-minute timeouts, nonblocking
  WireGuard setup, restoration-before-worker ordering, and the absence of
  remote-connection gates in the local workers.
- Executable C++ regressions exercise the production cooldown helper with no
  NTP, a backwards/restored clock, long uptime, no previous action, and 10s,
  60s, 5min, and 24h configured limits. Parent health/restart tests cover
  cancelled stale work, preserved history, and genuine unresolved failures.
- The private C5 configuration is validated and compiled, and its generated
  code is checked for the actual watchdog values before OTA. Live OTA checks
  confirm local data collection and preserved mappings/history after reboot.
- Wi-Fi or the sole remote WireGuard path is **not deliberately disconnected
  on the deployed device**. Full-duration physical outage testing is not
  claimed; it requires local/serial access or an independent recovery path.
