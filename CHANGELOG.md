# Changelog

## v0.8.1 — 2026-09-06

- Replaced the continuously redrawn topology Canvas with SVG links. Fit graph
  and browser zoom no longer retain a DPR-scaled bitmap for the entire mesh,
  and overlapping redraws cannot leave behind Canvas animation loops.
- Preserved curved links, endpoints, `5GH`/`5GL`/Ethernet styling, desired/current
  parent previews, labels, node metrics, and all node actions. Only decorative
  moving dots were removed; live topology and recovery countdowns still refresh.
- Separated the one-shot resize callback from rendering. Fit now accommodates
  diagrams that need a scale below 10%, and an all-offline snapshot resets the
  previous graph dimensions correctly.
- Added an offline v0.8.0 failure-path reproducer, vector-rendering/Fit regression
  tests, and an enlarged synthetic topology for mobile and Safari zoom checks.
  Physical iPhone pinch/crash tracing is not claimed by these tests.

## v0.8.0 — 2026-09-05

- Reorganized the dashboard into **Topology**, **Clients**, and **Recovery**
  workspaces. Summary cards open the relevant view; network details and the
  link legend remain available on demand.
- Added a searchable, parent-grouped Node list with a Needs attention filter.
  Phones default to the list; desktop browsers default to the graph. Both
  views use the same Node card renderer and retain parent state, countdowns,
  5GH/5GL, channel, hop throughput, PHY, RSSI, and online mesh-child counts.
- Added Fit graph, 100%, and Expand view controls. Tree layout now measures
  each card's actual height so alerts do not detach link anchors or cause
  overlapping cards, and healthy nodes do not inherit large empty cards.
- Organized Node details into Overview, Clients, Actions, and Details tabs,
  including parent/child navigation and a client preview on the first tap.
  All original identity fields, capabilities, samples, and steering counters
  remain available.
- Made the Client table readable as vertical cards on iPhone-sized screens,
  retaining every field and adding a node filter. Improved touch targets,
  input sizes, dialog focus, keyboard navigation, and reduced-motion behavior.
- Preserved open configuration forms, edited values, detail tabs, and scroll
  positions across automatic refresh. Missing PHY values remain unknown
  instead of being converted to a zero rate.
- Added an offline ESP32-style preview with synthetic recovery, degraded,
  offline, and nodes-only scenarios. It exercises the actual gzip bundle
  under the ESP32 Content Security Policy without contacting a router.

- Publish a clearly labeled, read-only degraded topology when Linksys returns
  an invalid `GetBackhaulInfo` response during node reboot or mesh
  reconvergence. Device-list connections keep Node and Client liveness useful,
  while live Parent relationships remain explicitly unverified and all
  automatic steering is paused. MeshScope does not automatically invoke
  Linksys' active `RefreshSlaveBackhaulData` performance test.
- Show every offline Node in the topology's horizontally scrollable offline
  strip instead of hiding entries behind a non-interactive “more” count.
- Prefer ESPHome's persisted fast-connect path after the first successful Wi-Fi
  association. This reduces full beacon scans during mesh reconvergence and
  avoids the ESP32-C5 driver path observed crashing in `scan_parse_beacon`.

- Fixed Topology Lock round-robin starvation during long MQTT verification.
  The scheduler now advances and persists its per-depth cursor only after a
  request is actually queued; scans blocked by another active operation no
  longer cause the same node to be selected repeatedly.
- Fixed a false cycle rejection when steering a wireless child toward a wired
  Parent. MQTT preflight now uses the Topology Lock's explicit wired layout for
  cycle detection instead of Linksys's ambiguous LLDP-derived wired Parent.
- Added evidence-based radio fallback for wired Parents. Topology Lock reuses a
  previously verified band, alternates `5GL`/`5GH` after an exact request fails,
  and exposes the selection reason with each MQTT operation.
- Made the Topology Lock automatic MQTT action limit configurable from 10 to
  86,400 seconds under **Device configuration** and as a Home Assistant number
  entity. The ESP32 persists the value in NVS and defaults new installations to
  60 seconds.
- Changed multi-node recovery scheduling to follow the saved hierarchy from
  the gateway toward the leaves. Eligible nodes at the same depth retain fair
  round-robin ordering, and children whose desired Parent is offline remain
  skipped without consuming the global action slot.
- Fixed wired-backhaul topology semantics. MeshScope now renders Ethernet
  separately from `5GH`/`5GL`, labels Linksys's LLDP-derived wired Parent as
  unverified, and lets a saved Topology Lock mapping provide an explicit
  display assignment (for example, LivingRoom → Main). Wired assignments never
  trigger wireless MQTT steering or Thrulay refreshes, while the raw
  Linksys-reported Parent IP remains visible for diagnosis.
- Added a per-Node **Measure Child → Parent now** action to refresh Linksys's
  existing Thrulay hop-throughput sample over MQTT without changing the
  topology. The UI identifies the upstream direction, current Parent, source,
  sample timestamp, waiting state, and bounded follow-up refreshes.
- Added ESP32 and desktop `/api/refresh-hop-throughput` implementations with
  online child/Parent validation, current-Parent targeting, MQTT Force-off
  enforcement, QoS-1 broker acknowledgement, and a one-minute per-Node
  cooldown.

## v0.7.1 — 2026-08-10

- Removed the ESP32 dashboard password, unlock modal, RAM session store,
  login/logout endpoints, session cookies, and per-request authorization gate.
  The page and every HTTP API now open directly to clients that can reach the
  ESP32 on its Wi-Fi or optional WireGuard address.
- Removed `meshscope_dashboard_password` from the shared ESPHome package, CI
  configurations, and local configuration examples. Existing private YAML
  files may retain the now-unused substitution during an upgrade.
- Preserved the independent Linksys local-admin credential, encrypted ESPHome
  Native API key, OTA password, Wi-Fi credentials, and optional WireGuard keys.
- Updated installation and security guidance to make the new network trust
  boundary explicit: do not port-forward port 80, and do not grant LAN or
  WireGuard reachability to users who must not restart or steer mesh nodes.

## v0.7.0 — 2026-08-10

- Made the topology panel use the full browser width and reflow live through a
  `ResizeObserver`. Wide views expand the hierarchy, medium views compact safe
  gaps, and narrow views confine horizontal scrolling to the map while keeping
  full-size Node cards and all diagnostics.
- Added live backhaul PHY rate collection from each child Node's MQTT
  `BH/status` (`phyRate_2`/`phyRate`) after boot and at most every 30 minutes.
  Topology links, Node cards, and Node details now show PHY rate alongside the
  existing JNAP `speedMbps` measured hop throughput, including source, raw
  value, age, and stale state.
- Changed ESP32 Topology Lock recovery from guarded `core/Reboot` retries to
  exact MQTT `BH/config` Parent steering. Auto capability detection remains
  the default and Force off blocks automatic recovery.
- Added Parent-radio selection for automatic recovery: a child uses `5GL`
  when a non-primary Parent's own uplink is `5GH`, and `5GH` when that uplink
  is `5GL`. A primary Parent preserves the child's observed band.
- Kept the three-snapshot mismatch gate, online Parent requirement,
  five-minute global action limit, and two-generation result verification.
- Restored ESPHome Wi-Fi, Native API, and WireGuard recovery reboots with an
  explicit five-minute timeout instead of disabling them with `0s`.
- Added persistent per-child Parent Steering Health diagnostics to topology
  cards and Node details: exact publish/echo evidence, consecutive/total
  failures, successes, target Parent child count, reason, restart count,
  timestamps, and a live cooldown.
- Added guarded requested-Parent recovery after two consecutive exact
  `BH/config` publishes time out unverified. The worker restarts only an online
  non-primary requested Parent with zero online mesh children, rechecks every
  gate immediately before `core/Reboot`, and limits attempts to one per five
  minutes. Preflight, broker, ACL, and radio-resolution errors never count.

## v0.6.0 — 2026-08-09

### Highlights

- Added exact MQTT Parent steering to the ESPHome/ESP32 appliance and the
  shared dashboard without removing topology, `5GH`/`5GL`, Client/STA,
  restart, or Topology Lock information.
- Added a persistent three-state control: **Auto** safely probes the local
  Linksys broker, **Force on** permits a controlled attempt when detection is
  inconclusive, and **Force off** prevents probes and publishes. Auto is the
  default.
- Added a background MQTT worker, bounded raw MQTT 3.1.1 parser, guarded
  child/Parent/radio validation, and Home Assistant diagnostic entities.

### Correctness and safety

- Keeps MQTT socket waits out of the HTTP and JNAP collector tasks so the
  validated ESP32-C5 topology and Client/STA experience remains responsive.
- Treats QoS-1 PUBACK only as broker acceptance. A request becomes verified
  after the requested Parent appears in two distinct topology generations.
- Uses fresh Parent DEVINFO when available and a current JNAP backhaul
  observation when a primary node does not publish its own DEVINFO. Both paths
  validate the exact band, unicast BSSID, and channel before publishing.
- Keeps verification open for 180 seconds so a temporarily offline child is
  not failed before backhaul reassociation completes.
- Rejects the primary node as a child, offline/self/descendant/current Parent
  targets, wired children, invalid bands/radios, concurrent requests, and a
  request that conflicts with an active Topology Lock mapping.
- Force on bypasses only capability detection; it never bypasses topology or
  radio validation. Force off never starts a new probe or publish.

### Compatibility

- Preserves local Wi-Fi JNAP/MQTT routing while the dashboard and encrypted
  Home Assistant Native API remain reachable over optional WireGuard.
- Validates runtime behavior on the connected ESP32-C5. C3/C6 retain their
  existing compile-compatibility target, without a new runtime or heap claim.
- Verified a physical ESP32-C5 round trip through WireGuard: broker discovery,
  QoS-1 acceptance, child disconnection/reassociation, and two consecutive
  JNAP generations confirming the requested Parent.

## v0.5.0 — 2026-08-09

### Highlights

- Added a dependency-free MQTT 3.1.1 Parent-steering client, a guarded desktop
  API, and an end-to-end CLI that verifies the resulting Parent through JNAP.
- Added a one-command macOS/Lima builder for exact supported MX4200 and MX5300
  official images. It provisions a pinned build environment, builds selected
  ACL plans, re-extracts every output, and publishes verification artifacts.
- Documented the real firmware path from `BH/config` through Linksys topology
  management and the uppercase UUID wire-format requirement.

### Boot-compatible firmware rebuild

- Reproduced Linksys/QSDK's 12-byte XZ compressor-options structure for
  MX4200 instead of upstream SquashFS's incompatible 8-byte structure.
- Matched all 503 official streams as plain LZMA2, disabled unintended BCJ
  filters, preserved official SquashFS/UBI/final-IMG sizes and POSIX metadata,
  and kept the official FIT/kernel prefix unchanged.
- Booted Plan C2 on physical MX4200 v1 hardware, verified the required MQTT
  discovery path, and confirmed exact Parent steering in both directions.

### Safety and distribution

- Rejects offline/unknown nodes, steering the primary node, self-parenting,
  descendant targets, wired children, and no-op current-Parent requests.
- Treats MQTT PUBACK as broker acceptance only; end-to-end success requires
  two matching JNAP topology observations.
- Does not publish Linksys firmware or generated IMG files. Users must supply
  an exact supported official image and retain recovery access.

## v0.4.1 — 2026-08-03

### Highlights

- Reframed the README around the two problems MeshScope solves: understanding
  a Linksys mesh in detail and diagnosing unexpected parent selection that can
  produce poor throughput or latency.
- Added a live, illustrated dashboard tour and a visual Topology Lock workflow.
- Added a four-step **How it works** section, network architecture diagram, and
  task-oriented route to desktop, ESPHome, Home Assistant, WireGuard, and
  recovery instructions.
- Corrected the README license section to reference the existing MIT License.
- Kept the English-only CI gate for text while excluding binary documentation
  images from character matching.

### Privacy

- Captured the documentation images through the ESP32-C5 WireGuard address;
  the workstation did not require direct access to the Linksys LAN.
- Limited live images to node-level topology, aggregate counts, health, and
  recovery state. Real Client/Device rows and node-detail client lists are not
  present.
- Added a public screenshot checklist that excludes client names, MAC and IP
  addresses, UUIDs, serial numbers, credentials, and unrelated browser data.

### Compatibility

- This release changes documentation and repository images only. Firmware,
  stored topology mappings, credentials, and installation procedures are
  unchanged from v0.4.0.

## v0.4.0 — 2026-08-02

### Highlights

- Added an opt-in ESPHome WireGuard package for private remote dashboard access
  and encrypted Home Assistant Native API connections without HTTP port
  forwarding.
- Added Home Assistant diagnostics for peer connection, enabled state, latest
  handshake, tunnel address, and the MeshScope WireGuard URL.
- Fixed topology nodes, the Internet marker, and `5GH`/`5GL` edge labels
  stacking at the top-left when the strict Content Security Policy rejected
  generated inline style attributes.
- Kept the complete topology legend visible on mobile layouts.

### Safety and routing

- WireGuard is disabled unless the optional package is included in an ignored
  local target YAML.
- The documented `/32` netmask keeps the tunnel inbound-only for MeshScope;
  Linksys JNAP, DNS, and normal local OTA continue over Wi-Fi.
- Tunnel and remote-client CIDRs must not overlap the ESP32 Wi-Fi/router LAN.
- WireGuard availability never blocks boot and never triggers a reboot.

### Compatibility and validation

- ESP32-C5: compiled with WireGuard and validated on a live tunnel for the web
  login/session lifecycle, protected topology/status APIs, and Home Assistant.
- ESP32-C3 and ESP32-C6: WireGuard configurations compile and link in CI; both
  remain experimental hardware targets.
- Existing v0.3.0 installations are unchanged until the package is explicitly
  enabled. No credential migration is required.

### Known limitations

- The WireGuard server, peer routes, forwarding, and firewall remain external
  infrastructure that MeshScope cannot configure.
- HTTP is protected by the WireGuard tunnel while remote, but the local LAN URL
  remains plain HTTP and must stay on a trusted network.
- mDNS discovery normally does not cross WireGuard; add the ESPHome integration
  manually with the tunnel IP.

## v0.3.0 — 2026-08-01

### Highlights

- Added ESP32 Topology Lock with persistent desired-parent mappings,
  drag-and-drop editing, accessible selectors, and live state on every node
  card.
- Added guarded restart-based recovery after three confirmed mismatches, with
  online-parent gating, fair scheduling, and a global five-minute cooldown.
- Added a first-level recovery chip, parent compliance colors, visible restart
  countdowns, action history, and Home Assistant lock entities.
- Replaced browser HTTP Basic authentication with a password-only MeshScope
  unlock screen, random RAM-only sessions, explicit sign-out, and protected
  APIs.
- Added ESP32-C3 and ESP32-C6 no-PSRAM targets while retaining ESP32-C5 PSRAM
  support and the full dashboard experience.
- Added adaptive full versus nodes-only Client/STA collection for constrained
  devices without removing topology, backhaul, or control information from the
  page.
- Improved mobile sizing and fixed a responsive style that could relabel
  unrelated buttons as Settings.

### Safety and scope

- Topology recovery restarts a mismatched child node; it does not directly pin
  or steer that node to a parent. Linksys chooses the parent after reboot.
- Automatic recovery is opt-in and now requires a clear acknowledgement of
  the temporary node/client outage and retry behavior.
- Router writes remain limited to `core/Reboot` for a currently known online
  private-LAN node. Reset, firmware update, and direct-parent commands are not
  exposed by the dashboard.

### Compatibility

- ESP32-C5: compiled, linked, flashed, OTA-updated, and validated on hardware.
- ESP32-C6: compiled and linked; no-PSRAM hardware path previously exercised,
  but long-run acceptance remains incomplete.
- ESP32-C3: compiled and linked; hardware runtime remains experimental.

### Migration

- Replace `meshscope_web_username` and `meshscope_web_password` in the ignored
  local YAML with one `meshscope_dashboard_password` value of at least eight
  characters.
- Rebuild and flash the ESP32. Existing Topology Lock mappings remain in NVS.
- After an ESP32 reboot, open the page and enter only the dashboard password.

## v0.2.0 — 2026-07-30

- Internationalized the public project and documentation in English.
- Added the ESPHome ESP32-C5 always-on appliance, encrypted Home Assistant
  entities, embedded dashboard assets, full `5GH`/`5GL` topology detail, and
  guarded selected-node restart.
- Added ESP32-C3 and ESP32-C6 target groundwork and adaptive Client/STA
  collection for lower-memory hardware.

## v0.1.0

- Initial public desktop topology and Client/STA dashboard with local Linksys
  JNAP collection, parent-aware layout, node details, and demo mode.
