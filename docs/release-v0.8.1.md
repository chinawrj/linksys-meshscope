# MeshScope v0.8.1 — Zoom-safe topology links

This patch addresses confirmed rendering defects found while investigating an
iPhone **Fit graph + zoom** crash report.

## What changed

- **SVG connections replace the animated Canvas.** No full-diagram Canvas
  allocation, unchecked Canvas context, or orphan drawing loop remains.
- **All network information stays.** Curves, endpoints, 5GH/5GL, Ethernet,
  throughput, PHY, RSSI, recovery countdowns, current/desired parent previews,
  node details and Client/STA lists remain available. Only decorative moving
  dots are removed.
- **Fit works for larger graphs**, including scales below 10%. Resize work is
  coalesced and an all-offline snapshot clears the previous graph dimensions.

## Evidence and verification

The v0.8.0 offline reproducer demonstrates an animation-loop leak after
overlapping redraws and a TypeError when Canvas allocation fails. The new
renderer is covered by unit tests, repeated Fit/100% and refresh checks on a
23-node synthetic mesh, phone-sized portrait/landscape layouts, and macOS
Safari zoom checks. Embedded ESP32 assets use the same code and strict CSP.

These are **not** physical iPhone crash/pinch traces. See the
[investigation](https://github.com/chinawrj/linksys-meshscope/blob/v0.8.1/docs/iphone-graph-zoom.md)
for the reproduction, technical reasoning, and validation limits.

## Updating

Desktop: update to `v0.8.1`, restart the Python app, and reload the browser page.

ESP32: regenerate assets and compile/OTA using your **existing private local
YAML**. Do not flash a CI configuration; it contains placeholder credentials.

```sh
python3 tools/generate_esp32_meshscope_assets.py --check
esphome run esphome_meshscope_c5.local.yaml --device <your-device-address>
```

Use the corresponding configuration for C3/C6. Wi-Fi, WireGuard, credentials,
MQTT behavior, stored topology locks, and router firmware are not changed by
this frontend patch. Publishing this release does not automatically OTA a device.
