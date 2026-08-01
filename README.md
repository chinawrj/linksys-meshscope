# MeshScope

MeshScope is a local-first Linksys Velop / Intelligent Mesh topology and
Client/STA dashboard. It shows parent/child relationships, `5GH` and `5GL`
backhaul links, signal quality, negotiated rates, and the clients attached to
each node on one screen.

Topology collection is read-only. Router changes are deliberately limited to
`core/Reboot`: either an immediate request for a selected online node, or the
same request issued by the opt-in ESP32 Topology Lock recovery guard.

[![CI](https://github.com/chinawrj/linksys-meshscope/actions/workflows/ci.yml/badge.svg)](https://github.com/chinawrj/linksys-meshscope/actions/workflows/ci.yml)
![Linksys](https://img.shields.io/badge/Linksys-local%20JNAP-16769b)
![Runtime](https://img.shields.io/badge/runtime-Python%20%7C%20ESPHome-2d705b)

> **v0.3.0 preview highlight:** lock the topology you trust. MeshScope now
> detects parent drift, shows compliance and restart countdowns directly on
> every node card, supports drag-and-drop desired-parent editing, and runs the
> guarded restart-based recovery monitor on an ESP32. Linksys still chooses the
> parent after a reboot. The ESP32 dashboard now unlocks with a
> single password—there is no browser username prompt.

> MeshScope is an independent community project and is not affiliated with or
> endorsed by Linksys. Use it only on networks you own or are authorized to
> manage.

## Choose how to run MeshScope

| | Desktop local app | ESPHome / ESP32 appliance |
|---|---|---|
| Best for | Trying the UI, diagnostics, occasional use | Always-on access, phones, Home Assistant |
| Runs on | Python on macOS, Linux, or Windows | ESP32-C5, ESP32-C6, or ESP32-C3 with the page served by the device |
| Requirements | Python 3.10+, no runtime packages | ESPHome 2026.7.2; see the target table below |
| Router credentials | Python process memory only | Ignored YAML, build output, and device flash |
| Web access | `127.0.0.1:8765` by default | MeshScope password-only session on the trusted home LAN |
| Home Assistant | Not exposed | Encrypted ESPHome Native API |

If you are evaluating compatibility, start with desktop demo mode. It includes
a complete multi-level topology, `5GH`/`5GL` links, node and client details,
and restart-state UI without authenticating with or controlling a router.

## What the dashboard shows

- Parent-aware topology layout with animated, directional backhaul paths
- Separate `5GH` and `5GL` labels, channel, negotiated rate, and RSSI
- 10, 30, or 60 second auto-refresh, pause, manual refresh, and refresh after
  returning from a background tab
- Node details with the Client/STA list attached to that specific node
- Client band, signal, rate, IP, MAC, model, and online state
- Automatic Client Steering and Node Steering status
- Read-only inspection of child-node identity, synchronized credentials, and
  firmware capabilities
- Direct access to the Linksys child-node support page at
  `https://<node-ip>/ca`
- Immediate restart of one selected online node, with offline and recovery
  tracking
- ESP32 Topology Lock that saves every online node's desired parent, shows
  parent compliance and restart countdowns directly on the map, and can
  recover a persistent mismatch under strict safety gates
- Drag-and-drop desired-parent editing on desktop browsers, with accessible
  child/parent selectors as an equivalent input method
- The last complete topology remains visible, clearly marked as cached, when
  the router is temporarily unreachable

## Desktop app: start in five minutes

```bash
git clone https://github.com/chinawrj/linksys-meshscope.git
cd linksys-meshscope
python3 linksys_mesh_app.py
```

Open [http://127.0.0.1:8765](http://127.0.0.1:8765). On macOS, you can also
double-click `Start MeshScope.command`.

On first launch, enter:

1. The LAN address of the primary Linksys router, such as `192.168.1.1`. Do not
   enter the address of the computer or ESP32 running MeshScope.
2. The Linksys local admin password. Linksys normally synchronizes this
   password to child nodes.

To explore MeshScope without a router:

```bash
python3 linksys_mesh_app.py --demo
```

For unattended startup, pass the router address and password at launch:

```bash
LINKSYS_PASSWORD='your-local-router-password' python3 linksys_mesh_app.py \
  --router 192.168.1.1
```

Entering the password in the web page is preferable on a shared computer,
because environment variables can be recorded in shell history or process
metadata. The desktop service listens only on `127.0.0.1` by default. The
password exists only in the Python process and is cleared when it stops.

## ESPHome / ESP32 always-on appliance

### Choose an ESP32 target

| Target | Board configuration | Memory | Status |
|---|---|---|---|
| ESP32-C5 | `esp32-c5-devkitc-1` | 8 MB flash and at least 4 MB PSRAM | Recommended and supported |
| ESP32-C6 | Exact `esp32-c6-devkitc-1` target | 4 MB flash, no PSRAM required; 2.4 GHz Wi-Fi | Hardware tested with full client details; experimental until the 100-cycle gate is complete |
| ESP32-C3 | Exact `esp32-c3-devkitm-1` target | 4 MB flash, no PSRAM required; 2.4 GHz Wi-Fi | Experimental; compile and link verified, hardware runtime not yet certified |

All targets contain the same topology page, Client/STA and `5GH`/`5GL`
fields, auto-refresh, Home Assistant entities, selected-node restart, and
Topology Lock. The page is never reduced. Client details default to `auto`:
targets with enough memory retain the complete raw topology payload, while
smaller devices can use the explicit `nodes-only` capability described below.
Successful compilation does not prove that internal RAM is sufficient for
every real mesh, so this release does not claim runtime parity until each
target passes the hardware acceptance gate.

Network assumptions:

- The ESP32, primary Linksys router, and browser are on the same trusted LAN
- An ESP32-C3 or ESP32-C6 must be able to join a 2.4 GHz SSID that can reach
  the Linksys router; these targets do not support 5 GHz Wi-Fi
- The Linksys firmware provides local HTTPS JNAP

Development was validated on a mixed mesh containing MX42, MX5300, and WHW03
nodes. Other Linksys models may return different JNAP fields. Use desktop demo
mode to evaluate the UI, then use the desktop live connection to verify router
compatibility before flashing hardware.

Before a no-PSRAM target is promoted from experimental status, the exact target
must pass a cold boot and at least 100 refresh cycles on a realistic mesh,
concurrent web and Home Assistant access, node-detail collection,
cached-offline behavior, OTA, and selected-node restart/recovery. Free heap
must remain stable throughout. The full-mode acceptance gate requires complete
Client/STA data; `nodes-only` results do not qualify a target for promotion.

ESP32-C6 hardware validation used an ESP32-C6FH4 revision 0.1 with 4 MB flash
and no PSRAM. The board compiled, flashed over native USB, joined a 2.4 GHz
network, and retained a complete large Client/STA response without truncation.
The 100-cycle, Home Assistant concurrency, and restart/recovery acceptance
items remain pending, so C6 stays experimental.

ESP32-C5 validation covered USB and OTA installation, full Client/STA mode,
password-only login and sign-out, persistent Topology Lock state, the
three-snapshot mismatch gate, a single-node restart, the five-minute global
cooldown, card colors, and the visible `MM:SS` countdown. Household node names,
addresses, and credentials are intentionally omitted.

### 1. Create a private local configuration

```bash
git clone https://github.com/chinawrj/linksys-meshscope.git
cd linksys-meshscope
cp esphome_meshscope_c5.local.example.yaml esphome_meshscope_c5.local.yaml
```

For an ESP32-C3, use the C3 example instead:

```bash
cp esphome_meshscope_c3.local.example.yaml esphome_meshscope_c3.local.yaml
```

For an ESP32-C6, use:

```bash
cp esphome_meshscope_c6.local.example.yaml esphome_meshscope_c6.local.yaml
```

Generate three independent credentials:

```bash
# Prompts without echo and returns the Linksys password as Base64 for the C++ config
python3 tools/encode_secret.py

# ESPHome Native API key; use this as meshscope_api_key
openssl rand -base64 32

# Generate separate dashboard and OTA passwords; do not reuse them
openssl rand -hex 24
openssl rand -hex 24
```

Edit the local YAML for your selected target:

- `meshscope_wifi_ssid` / `meshscope_wifi_password`: home Wi-Fi credentials
- `meshscope_router_host`: primary Linksys router LAN address
- `meshscope_router_password_b64`: output from `encode_secret.py`, not the
  plaintext password
- `meshscope_dashboard_password`: the only value requested by the ESP32 web
  page; no username is configured or entered
- `meshscope_api_key`: 32-byte Base64 key used by Home Assistant
- `meshscope_ota_password`: password for later OTA updates
- `meshscope_timezone`: timezone used by ESPHome logs and time components, such
  as `America/New_York` or `Europe/London`
- `meshscope_client_details`: `auto` (recommended), `full`, or `nodes-only`

`auto` selects `full` when PSRAM or the measured internal heap can safely hold
the complete Linksys device list. The full mode preserves every Client/STA and
offline-node record. On smaller hardware, `nodes-only` asks Linksys to return
only currently participating mesh-node UUIDs; Client/STA details and historical
offline nodes are unavailable. The page clearly identifies this low-memory
mode. Use `full` to require complete data and surface a collection error rather
than allowing an automatic downgrade.

The local YAML is ignored by Git. Never publish it, `.esphome/`, build logs, or
firmware binaries containing your credentials.

| Credential | What it unlocks | Where you enter it |
|---|---|---|
| Wi-Fi password | Joins the ESP32 to your home network | Local YAML only |
| Linksys local admin password | Reads topology and sends an allowed node restart | Encoded into local YAML with `tools/encode_secret.py` |
| Dashboard password | Opens the MeshScope web page and APIs | Browser password-only form |
| ESPHome API key | Encrypted Home Assistant connection | Home Assistant integration |
| OTA password | Future firmware uploads | ESPHome update command |

These credentials serve different purposes. Do not reuse the Linksys, Wi-Fi,
Home Assistant, or OTA secret as the dashboard password.

### 2. Install over USB for the first time

```bash
python3 -m venv .esphome-venv
.esphome-venv/bin/pip install -r requirements-esphome.txt
python3 tools/generate_esp32_meshscope_assets.py
.esphome-venv/bin/esphome config esphome_meshscope_c5.local.yaml
.esphome-venv/bin/esphome run esphome_meshscope_c5.local.yaml
```

Replace `c5` with `c3` or `c6` in both commands when using that target.

Connect the ESP32 over USB and select its serial port when prompted. The first
build downloads ESP-IDF and may take several minutes; later builds use the
cache.

After installation, find the device IP in one of these places:

- ESPHome serial logs
- The Linksys DHCP client list
- Home Assistant's ESPHome discovery notification

Create a DHCP reservation for the ESP32. If mDNS works, open
`http://meshscope-c5.local/`, `http://meshscope-c6.local/`, or
`http://meshscope-c3.local/`, depending on the target. Otherwise use
`http://<esp32-ip>/`. Enter only `meshscope_dashboard_password` on the
MeshScope unlock screen. The internal session account is automatic. Sessions
are held in ESP32 memory, expire after 24 hours of inactivity, and end when the
ESP32 restarts; use **Connection settings → Sign out** on a shared browser.

### 3. Add MeshScope to Home Assistant

The encrypted ESPHome Native API key is separate from the dashboard password.

1. Wait up to five minutes for the selected MeshScope target to appear under
   **Settings → Devices & services**.
2. If it is not discovered, choose **Add integration → ESPHome**.
3. Enter the target's `.local` address or the ESP32's reserved IP address.
4. Enter `meshscope_api_key` from the local YAML when prompted.

Home Assistant receives these entities:

- Router Connected
- Online / Total Mesh Nodes
- Online Clients
- Weak Mesh Nodes
- Backhaul Total
- Topology Summary / Last Topology Update
- MeshScope URL
- ESP32 Free Heap
- Refresh Mesh Topology button
- Topology Lock Active
- Topology Lock Issues
- Topology Lock Summary

The ESP32 continues collecting data and serving the web page when Home
Assistant is offline or has never been connected. The configuration explicitly
disables the ESPHome Native API reboot behavior when no API client is present.

### 4. Update over the air

Pull the latest source, verify the local configuration, and target the reserved
device address:

```bash
git pull --ff-only
python3 tools/generate_esp32_meshscope_assets.py --check
.esphome-venv/bin/esphome run esphome_meshscope_c5.local.yaml \
  --device <esp32-ip>
```

Use the matching C3 or C6 local YAML when updating that target.

If an incorrect Wi-Fi setting, OTA password, or device address makes the board
unreachable, reconnect it over USB and run the same `esphome run` command.
MeshScope does not enable a fallback access point, avoiding an unexpected
configuration portal on the home network.

## Daily use and node restart

The browser's auto-refresh interval controls how often the display updates.
The ESP32 itself collects a complete topology every 10 seconds. Background
tabs do not keep issuing refresh requests; an overdue refresh runs immediately
when the page becomes visible again.

Select any topology card to open the node details and view the clients or STAs
attached to that node. `5GH` and `5GL` remain visible on the backhaul paths,
together with every available channel, rate, and RSSI value.

### Topology Lock

Topology Lock is available on the ESP32-hosted page. Select **Edit & lock
topology** to capture the current live structure. On a desktop browser, drag a
child card onto its desired parent; on touch devices or with a keyboard, use
the child and desired-parent selectors. The preview keeps the current `5GH` or
`5GL` link visible beside the proposed relationship. Nothing is sent to a
router while editing. Review the restart warning, acknowledge it, then select
**Enable restart-based recovery**.

After applying, every node card becomes a live status surface:

- Green: the current parent matches the saved parent
- Amber: a mismatch is being confirmed across three successful snapshots
- Red/orange: the mismatch is confirmed and a restart is queued or counting
  down; the `MM:SS` countdown is shown directly on the node card
- Blue/gray: the desired parent is offline, so recovery is blocked
- Violet: a restart was sent and the node is being observed during recovery
- Gray: the child node is offline

The ESP32 stores the lock in non-volatile storage and evaluates it after each
successful ten-second topology collection. An automatic restart is allowed
only when the child and its saved parent are both online, the child has a
private LAN address, and the mismatch has appeared in three consecutive
snapshots. Automatic actions share a global five-minute cooldown, so at most
one node can be restarted in that period. If several nodes remain mismatched,
the scheduler rotates among them instead of repeatedly favoring one node.
Recent attempts are displayed below the map, and Home Assistant receives
**Topology Lock Active**, **Topology Lock Issues**, and **Topology Lock
Summary** entities.

Topology Lock is recovery by guarded node restart, not direct parent steering.
Linksys firmware chooses the attachment again when the node returns, so the
desired parent is not guaranteed. Use **Stop automatic recovery** to clear the saved structure
and stop automatic actions. Locks include only nodes online at apply time; edit
and reapply after intentionally adding, removing, or relocating mesh nodes.

**Restart now is immediate and has no second confirmation.**

- The request is sent only to the currently selected, online node that is still
  present in the live topology
- The node and all clients attached to it will briefly go offline
- Restarting Main may temporarily interrupt management and Internet access for
  the entire mesh
- The UI tracks requested, offline, and recovered states and rejects duplicate
  restart requests for 90 seconds
- Demo nodes, offline nodes, unknown addresses, and IDs absent from the current
  topology cannot be restarted

Do not share the MeshScope dashboard password with LAN users who should not have
permission to restart a node.

## Troubleshooting

| Symptom | What to do |
|---|---|
| The target's `.local` address does not open | Find the IP in Linksys DHCP or serial logs, then create a DHCP reservation |
| The page says the router is offline but still shows topology | This is the last successful snapshot, not a false online state. Check the router address, Wi-Fi, and Linksys local password |
| The first page remains on “Loading” | Allow one complete JNAP collection cycle; check ESPHome logs if it continues |
| The dashboard password is rejected | Verify `meshscope_dashboard_password` in the target's ignored local YAML. Rebuild and flash if it changed |
| Home Assistant does not discover MeshScope | Add the ESPHome integration manually with the ESP32 IP and `meshscope_api_key` |
| Client/STA details are unavailable | Check `/api/status` or the page notice for `nodes-only`; set `meshscope_client_details: "full"` only if the board has enough memory |
| ESPHome reports missing PSRAM on C5 | Verify the exact C5 development board and its PSRAM specification, or use the matching no-PSRAM C3/C6 target |
| Front-end changes do not appear on ESP32 | Regenerate embedded assets, then rebuild or update over OTA |
| The device disappeared after changing Wi-Fi or OTA settings | Connect over USB and run `esphome run` again |
| The Linksys password contains special characters | Use `tools/encode_secret.py`; never insert a plaintext password directly into the firmware lambda |

## Security and privacy boundaries

The desktop and ESPHome versions use different credential models:

- The desktop password remains in Python process memory, and the web service
  binds to localhost by default.
- The ESPHome version stores Wi-Fi, Linksys, dashboard, API, and OTA credentials in an
  ignored local YAML and compiles them into device flash. HTTP APIs never
  return those values to the browser.
- The ESP32 page uses a password-only form and random, RAM-only session tokens.
  Protected APIs return `401` without a valid session. Cookies are `HttpOnly`
  and `SameSite=Strict`; no username is exposed or required. HTTP itself does
  not encrypt traffic. Use MeshScope only on a trusted home LAN; never
  port-forward it or expose it directly to the Internet.
- The ESPHome Native API uses a separate encrypted key. It does not encrypt the
  MeshScope web page.
- Linksys local JNAP uses the router's self-signed HTTPS certificate. MeshScope
  skips certificate verification for local compatibility, so its trust model
  assumes the home LAN and gateway have not been maliciously intercepted.

Read operations are restricted to an allowlist of `Get*` and `Check*` actions.
The write allowlist contains only `core/Reboot`, and its target must resolve to
an online node with a known private address in the live topology. Topology Lock
adds the saved-parent-online, three-snapshot confirmation, and global
five-minute cooldown gates before using that same action. Reset, direct parent
steering, firmware updates, and all other mutations are rejected before a
router request is generated.

The MQTT, BLE, and firmware overlays in this repository are offline,
owner-controlled research artifacts. The running dashboard cannot invoke them,
and generated IMG files and vendor firmware inputs are not published.

## Node support page, steering, and advanced research

Online node details use only these read-only calls:

- `core/CheckAdminPassword`
- `core/GetDeviceInfo`
- `nodes/smartmode/GetDeviceMode`
- `nodes/topologyoptimization/GetTopologyOptimizationSettings2`

The redirect-bypass entry point for a Linksys child node is:

```text
https://<node-ip>/ca
```

The browser will warn about the router's self-signed certificate. Public JNAP
currently exposes automatic Client Steering and Node Steering only. Offline
firmware analysis confirmed an internal exact-parent data path, but no
supported transport suitable for a normal web UI has been identified.
MeshScope therefore does not expose manual parent control.

Research notes:

- [Node control feasibility](docs/node-control-findings.md)
- [Node Steering findings](docs/node-steering-findings.md)
- [Hidden firmware interfaces and parent steering](docs/hidden-firmware-interfaces.md)
- [WHW03 firmware and SSH scaffold analysis](docs/whw03-firmware-analysis.md)
- [MX4200 firmware, steering, reboot, and SSH analysis](docs/mx4200-firmware-analysis.md)
- [MX4200 SSH bootstrap and exact-parent control plan](docs/mx4200-ssh-parent-control.md)
- [MX4200 MQTT control and custom-IMG feasibility](docs/mx4200-mqtt-and-custom-img.md)
- [MX5300 MQTT, exact-parent control, and custom-IMG feasibility](docs/mx5300-mqtt-and-custom-img.md)
- [MX4200/MX5300 BLE and exact-parent steering](docs/linksys-ble-parent-steering.md)
- [Offline BLE-JNAP advanced-action proof overlay](firmware-overlays/ble-parent-steering/README.md)
- [MQTT experiment-image builder](firmware-overlays/mqtt-parent-steering/README.md)

## Testing and front-end development

```bash
python3 -m unittest discover -s tests -p 'test_*.py' -v
node --test \
  tests/test_connection_mode.js \
  tests/test_refresh_state.js \
  tests/test_topology_layout.js \
  tests/test_detail_data.js \
  tests/test_node_restart_state.js \
  tests/test_topology_lock_state.js \
  tests/test_linksys_normalize.js
```

After changing `mesh_web/`, regenerate the assets embedded in the ESP32 build:

```bash
python3 tools/generate_esp32_meshscope_assets.py
python3 tools/generate_esp32_meshscope_assets.py --check
```

CI checks Python and JavaScript syntax, runs every unit test, validates the
ESPHome configurations, fully compiles and links C3, C5, and C6 firmware, and
confirms that embedded assets match the web source.

## Contributing

Issues and pull requests are welcome. Please:

- Write user-facing text, documentation, code comments, issues, and pull
  requests in English
- Keep the dashboard local-first and avoid cloud dependencies
- Preserve all topology and Client/STA detail when changing the layout
- Add or update tests for behavior changes
- Never commit router credentials, real household topology data, vendor
  firmware, or generated credential-bearing images

When reporting router compatibility, include the Linksys model and firmware
version, but remove serial numbers, MAC addresses, public IPs, and personally
identifying node or client names.

## License status

This repository is publicly visible but does not yet contain a `LICENSE`.
Public visibility does not grant permission to copy, modify, or redistribute
the project until the owner selects a license.
