# MeshScope v0.8.0 — A clearer mesh workspace

MeshScope makes Linksys mesh paths visible and helps restore the parent
relationships you want. This release brings a redesigned workspace for PC
and iPhone-sized screens while keeping the full network and debug data.

## A simpler daily workflow

- **Topology / Clients / Recovery:** move straight to monitoring, devices, or
  parent control. The summary cards also open the corresponding view.
- **A full-data Node list:** phones start here; desktops start on the graph.
  Search by name, model, IP, parent, or band, or filter Needs attention.
  Parent correctness, recovery countdowns, mesh-child counts, 5GH/5GL,
  channel, hop throughput, PHY, and RSSI remain on the cards.
- **Clear Node details:** Overview previews the attached clients and links to
  the parent and children. Clients, Actions, and Details provide direct access
  to the complete STA list, restart/hop/parent controls, and diagnostic evidence.
- **Readable client cards on phones:** every table field is retained, with a
  node filter and name/IP/MAC search. Inputs use phone-friendly sizes and the
  detail panel fills the screen.
- **Better graph navigation:** Fit graph, 100%, and Expand view. Layout measures
  real card heights, keeping link anchors centered even when recovery warnings
  expand a card. Healthy nodes no longer inherit large blank areas.
- **Uninterrupted inspection:** automatic refresh preserves open configuration
  edits, the active detail tab, and detail scroll position. Keyboard focus is
  contained in dialogs; closed drawers cannot receive focus.

## Network reliability improvements included

This release also ships the previously unreleased recovery work: configurable
automatic action spacing (60 seconds by default), root-to-leaf scheduling,
fair recovery rotation, wired-parent display assignments and radio fallback,
per-node Child → Parent Thrulay measurements, slower PHY polling, and explicit
degraded topology handling when Linksys returns invalid BackhaulInfo.

Missing PHY samples remain unknown. Completed healthy steering history stays
available in Details and Recovery without permanent warning boxes. Unverified
live Parent data blocks new manual moves until a valid snapshot returns.

## Upgrade

Desktop users can check out `v0.8.0` and restart the Python app. Existing ESP32
users should update the source, regenerate assets, then compile and OTA with
their existing private local YAML. Keep Wi-Fi, Linksys, ESPHome, OTA, and
WireGuard settings in that private configuration.

```sh
python3 tools/generate_esp32_meshscope_assets.py
esphome run esphome_meshscope_c5.local.yaml
```

Use the corresponding C3/C6 configuration for those targets. The CI YAML files
are compile-only examples, not deployable personal configurations. GitHub
publication does not automatically OTA any running device.

The dashboard still opens without a separate dashboard login. Existing API,
MQTT, Home Assistant, and optional WireGuard interfaces are preserved. Linksys
firmware images and private ESP32 binaries are not distributed.

## Verification

The release checks include Python and JavaScript regression tests, deterministic
gzip asset generation, and complete C3/C5/C6 compilation/linking. Browser review
covers PC and iPhone-sized viewports, filtered clients, node navigation, recovery
draft validation, configuration persistence, and all-offline/degraded states.
The offline UI preview serves the real ESP32 bundle under its strict CSP.

These browser checks use simulated phone viewports, not a physical iPhone.
Read the [workspace guide](../README.md#find-your-way-around) and full
[changelog](../CHANGELOG.md) for details.
