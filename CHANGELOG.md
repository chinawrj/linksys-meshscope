# Changelog

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
