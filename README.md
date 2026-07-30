# MeshScope

MeshScope is a local-first Linksys Velop / Intelligent Mesh topology and
Client/STA dashboard. It shows parent/child relationships, `5GH` and `5GL`
backhaul links, signal quality, negotiated rates, and the clients attached to
each node on one screen.

Topology collection is read-only. The only enabled write operation is an
immediate `core/Reboot` request sent directly to a selected online node.

[![CI](https://github.com/chinawrj/linksys-meshscope/actions/workflows/ci.yml/badge.svg)](https://github.com/chinawrj/linksys-meshscope/actions/workflows/ci.yml)
![Linksys](https://img.shields.io/badge/Linksys-local%20JNAP-16769b)
![Runtime](https://img.shields.io/badge/runtime-Python%20%7C%20ESPHome-2d705b)

> MeshScope is an independent community project and is not affiliated with or
> endorsed by Linksys. Use it only on networks you own or are authorized to
> manage.

## Choose how to run MeshScope

| | Desktop local app | ESPHome / ESP32-C5 appliance |
|---|---|---|
| Best for | Trying the UI, diagnostics, occasional use | Always-on access, phones, Home Assistant |
| Runs on | Python on macOS, Linux, or Windows | ESP32-C5 with the page served by the device |
| Requirements | Python 3.10+, no runtime packages | ESPHome 2026.7.2, 8 MB flash, at least 4 MB PSRAM |
| Router credentials | Python process memory only | Ignored YAML, build output, and device flash |
| Web access | `127.0.0.1:8765` by default | HTTP Basic Auth on the trusted home LAN |
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

## ESPHome / ESP32-C5 always-on appliance

### Verified target and network assumptions

- `esp32-c5-devkitc-1`
- 8 MB flash
- At least 4 MB PSRAM; configuration fails early when PSRAM is unavailable
- The ESP32, primary Linksys router, and browser are on the same trusted LAN
- The Linksys firmware provides local HTTPS JNAP

Development was validated on a mixed mesh containing MX42, MX5300, and WHW03
nodes. Other Linksys models may return different JNAP fields. Use desktop demo
mode to evaluate the UI, then use the desktop live connection to verify router
compatibility before flashing hardware.

### 1. Create a private local configuration

```bash
git clone https://github.com/chinawrj/linksys-meshscope.git
cd linksys-meshscope
cp esphome_meshscope_c5.local.example.yaml esphome_meshscope_c5.local.yaml
```

Generate three independent sets of credentials:

```bash
# Prompts without echo and returns the Linksys password as Base64 for the C++ config
python3 tools/encode_secret.py

# ESPHome Native API key; use this as meshscope_api_key
openssl rand -base64 32

# Generate separate Web and OTA passwords; do not reuse them
openssl rand -hex 24
openssl rand -hex 24
```

Edit `esphome_meshscope_c5.local.yaml`:

- `meshscope_wifi_ssid` / `meshscope_wifi_password`: home Wi-Fi credentials
- `meshscope_router_host`: primary Linksys router LAN address
- `meshscope_router_password_b64`: output from `encode_secret.py`, not the
  plaintext password
- `meshscope_web_username` / `meshscope_web_password`: credentials required by
  browsers opening the ESP32 page; use only letters, digits, `-`, or `_` in the
  username, and use a generated password
- `meshscope_api_key`: 32-byte Base64 key used by Home Assistant
- `meshscope_ota_password`: password for later OTA updates
- `meshscope_timezone`: timezone used by ESPHome logs and time components, such
  as `America/New_York` or `Europe/London`

The local YAML is ignored by Git. Never publish it, `.esphome/`, build logs, or
firmware binaries containing your credentials.

### 2. Install over USB for the first time

```bash
python3 -m venv .esphome-venv
.esphome-venv/bin/pip install -r requirements-esphome.txt
python3 tools/generate_esp32_meshscope_assets.py
.esphome-venv/bin/esphome config esphome_meshscope_c5.local.yaml
.esphome-venv/bin/esphome run esphome_meshscope_c5.local.yaml
```

Connect the ESP32 over USB and select its serial port when prompted. The first
build downloads ESP-IDF and may take several minutes; later builds use the
cache.

After installation, find the device IP in one of these places:

- ESPHome serial logs
- The Linksys DHCP client list
- Home Assistant's ESPHome discovery notification

Create a DHCP reservation for the ESP32. If mDNS works, open
`http://meshscope-c5.local/`. Otherwise use `http://<esp32-ip>/`. Your browser
will request the MeshScope Web username and password from the local YAML.

### 3. Add MeshScope to Home Assistant

The encrypted ESPHome Native API key is separate from the HTTP Basic
credentials used by the web page.

1. Wait up to five minutes for `MeshScope C5` to appear under
   **Settings → Devices & services**.
2. If it is not discovered, choose **Add integration → ESPHome**.
3. Enter `meshscope-c5.local` or the ESP32's reserved IP address.
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

Do not share the MeshScope Web credentials with LAN users who should not have
permission to restart a node.

## Troubleshooting

| Symptom | What to do |
|---|---|
| `meshscope-c5.local` does not open | Find the IP in Linksys DHCP or serial logs, then create a DHCP reservation |
| The page says the router is offline but still shows topology | This is the last successful snapshot, not a false online state. Check the router address, Wi-Fi, and Linksys local password |
| The first page remains on “Loading” | Allow one complete JNAP collection cycle; check ESPHome logs if it continues |
| The browser repeatedly asks for credentials | Verify `meshscope_web_username/password`, clear the incorrect Basic Auth entry for the address, and retry |
| Home Assistant does not discover MeshScope | Add the ESPHome integration manually with the ESP32 IP and `meshscope_api_key` |
| ESPHome reports missing PSRAM | This firmware cannot run without PSRAM; verify the exact development board and hardware specification |
| Front-end changes do not appear on ESP32 | Regenerate embedded assets, then rebuild or update over OTA |
| The device disappeared after changing Wi-Fi or OTA settings | Connect over USB and run `esphome run` again |
| The Linksys password contains special characters | Use `tools/encode_secret.py`; never insert a plaintext password directly into the firmware lambda |

## Security and privacy boundaries

The desktop and ESPHome versions use different credential models:

- The desktop password remains in Python process memory, and the web service
  binds to localhost by default.
- The ESPHome version stores Wi-Fi, Linksys, Web, API, and OTA credentials in an
  ignored local YAML and compiles them into device flash. HTTP APIs never
  return those values to the browser.
- The ESP32 page and all page APIs require independent HTTP Basic Auth. HTTP
  itself does not encrypt traffic. Use MeshScope only on a trusted home LAN;
  never port-forward it or expose it directly to the Internet.
- The ESPHome Native API uses a separate encrypted key. It does not encrypt the
  MeshScope web page.
- Linksys local JNAP uses the router's self-signed HTTPS certificate. MeshScope
  skips certificate verification for local compatibility, so its trust model
  assumes the home LAN and gateway have not been maliciously intercepted.

Read operations are restricted to an allowlist of `Get*` and `Check*` actions.
The write allowlist contains only `core/Reboot`, and its target must resolve to
an online node with a known private address in the live topology. Reset,
parent steering, firmware updates, and all other mutations are rejected before
a router request is generated.

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
  tests/test_linksys_normalize.js
```

After changing `mesh_web/`, regenerate the assets embedded in the ESP32 build:

```bash
python3 tools/generate_esp32_meshscope_assets.py
python3 tools/generate_esp32_meshscope_assets.py --check
```

CI checks Python and JavaScript syntax, runs every unit test, validates the
ESPHome configuration, and confirms that embedded assets match the web source.

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
