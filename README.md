# MeshScope

**See why your Linksys mesh is slow—and keep its topology aligned with what
you expect.**

Linksys mesh owners repeatedly run into two frustrating problems:

1. **The network is a black box.** The normal Linksys interfaces do not put the
   live parent/child tree, `5GH` and `5GL` backhaul, channel, negotiated rate,
   signal, node health, and attached clients together in one useful view.
2. **A node chooses an unexpected parent.** A child can attach through a weak
   or distant mesh node instead of the route you expected. The Internet may be
   healthy while Wi-Fi throughput drops and latency becomes painfully high.

MeshScope is a local-first Linksys Velop / Intelligent Mesh dashboard built to
make both problems visible. Its **Topology Lock** records the parent structure
you want, continuously compares it with the live network, and uses guarded
local MQTT Parent steering to restore a mismatched child to the exact saved
parent when the compatible router ACL is available.

[![CI](https://github.com/chinawrj/linksys-meshscope/actions/workflows/ci.yml/badge.svg)](https://github.com/chinawrj/linksys-meshscope/actions/workflows/ci.yml)
![Linksys](https://img.shields.io/badge/Linksys-local%20JNAP-16769b)
![Runtime](https://img.shields.io/badge/runtime-Python%20%7C%20ESPHome-2d705b)
![License](https://img.shields.io/badge/license-MIT-2d705b)

<p align="center">
  <a href="docs/assets/meshscope-dashboard-overview.jpg">
    <img src="docs/assets/meshscope-dashboard-overview.jpg" alt="MeshScope live dashboard showing Linksys nodes, parent links, 5GH and 5GL backhaul, signal, rate, health, and topology recovery state" width="100%">
  </a>
</p>

_A real ESP32-C5 dashboard reached through WireGuard. Node-level topology is
shown with the network owner's approval. The Client/Device table and every
client identifier are deliberately excluded from this repository image._

> **v0.7.1 highlight:** The ESP32 dashboard now opens directly without a
> username, password, unlock screen, or session cookie. Keep it on a trusted
> LAN or behind an authorized WireGuard peer because reachable users can use
> every control shown on the page. Topology Lock restores the exact saved
> Parent over guarded local MQTT, tracks per-child steering health, and can recover an
> otherwise idle requested Parent after repeated accepted-but-unverified
> moves. Every wireless Node now shows both the Linksys measured hop throughput
> and its latest MQTT PHY sample. The topology automatically uses the browser
> width and confines scrolling to the map only when complete cards cannot fit.
> Generated Linksys IMG files are never distributed.

> MeshScope is an independent community project and is not affiliated with or
> endorsed by Linksys. Use it only on networks you own or are authorized to
> manage.

## How it works

1. **Connect locally.** MeshScope signs in to the primary Linksys node with the
   local admin password you provide. It does not require a Linksys cloud
   account.
2. **Collect and correlate.** Allowlisted local HTTPS JNAP reads collect the
   node inventory, live parent relationships, `5GH`/`5GL` backhaul metrics,
   steering state, and Client/STA records. MeshScope correlates them into one
   topology instead of making you compare several router screens.
3. **Present locally.** The Python app or ESP32 serves the dashboard directly
   to your browser. ESPHome additionally publishes encrypted Home Assistant
   entities. There is no MeshScope cloud service or analytics endpoint.
4. **Recover only when enabled.** Topology Lock compares saved and current
   parents. After three confirmed mismatches, and only when the desired parent
   is online and the five-minute cooldown is clear, it queues one exact local
   MQTT `BH/config` request and requires two fresh topology generations to
   verify success. For a non-primary Parent, the child uses the opposite radio
   from that Parent's own uplink (`5GH` → `5GL`, or `5GL` → `5GH`).
5. **Probe before writing.** An owner-built narrow-ACL firmware image exposes
   Linksys's existing local MQTT Parent command. **Auto**, the default mode,
   enables recovery only after a fresh broker round trip. Force on/off remain
   explicit persistent choices.

```mermaid
flowchart LR
    Linksys["Primary node and mesh"] -->|"Local HTTPS JNAP reads"| Engine["MeshScope on Python or ESP32"]
    Engine -->|"Local dashboard"| Browser["Browser"]
    Engine -->|"Encrypted Native API"| HA["Home Assistant"]
    Remote["Authorized remote client"] -.->|"WireGuard to ESP32 only"| Engine
    Engine -.->|"Optional selected-Node or requested-Parent recovery"| Linksys
    Engine -.->|"Optional owner-enabled MQTT exact-Parent request"| Linksys
```

In appliance mode, the ESP32 remains on the Linksys LAN and collects directly
from the primary node. WireGuard changes only how an authorized browser or
Home Assistant reaches the ESP32; it does not move Linksys collection into the
tunnel and does not require exposing the router LAN.

| I want to… | Start here |
|---|---|
| See the UI without a router | [Run desktop demo mode](#desktop-app-start-in-five-minutes) |
| Inspect a live mesh from a computer | [Run the desktop local app](#desktop-app-start-in-five-minutes) |
| Keep an always-on dashboard | [Install the ESPHome appliance](#esphome--esp32-always-on-appliance) |
| Diagnose or recover parent drift | [Understand Topology Lock](#topology-lock-visual-guide) |
| Build and test exact Parent steering | [Use the MQTT Parent-steering path](docs/mqtt-parent-steering.md) |
| Reach MeshScope remotely | [Configure optional WireGuard access](#optional-wireguard-remote-access) |
| Add sensors and controls to Home Assistant | [Add the ESPHome integration](#3-add-meshscope-to-home-assistant) |

## Choose how to run MeshScope

| | Desktop local app | ESPHome / ESP32 appliance |
|---|---|---|
| Best for | Trying the UI, diagnostics, occasional use | Always-on access, phones, Home Assistant |
| Runs on | Python on macOS, Linux, or Windows | ESP32-C5, ESP32-C6, or ESP32-C3 with the page served by the device |
| Requirements | Python 3.10+, no runtime packages | ESPHome 2026.7.2; see the target table below |
| Router credentials | Python process memory only | Ignored YAML, build output, and device flash |
| Web access | `127.0.0.1:8765` by default | Direct access on a trusted LAN, optionally reachable through WireGuard |
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
  parent compliance and MQTT-move countdowns directly on the map, and can
  recover a persistent mismatch under strict safety gates
- Drag-and-drop desired-parent editing on desktop browsers, with accessible
  child/parent selectors as an equivalent input method
- The last complete topology remains visible, clearly marked as cached, when
  the router is temporarily unreachable

Runtime Client/STA details are intentionally comprehensive, but public
documentation follows a stricter rule: **node-level topology may be shown;
client identity must not be shown**. Repository screenshots exclude the
Client/Device table, client names, MAC addresses, client IP addresses, UUIDs,
and serial numbers. Use demo mode when an issue report needs a client-list
example.

## Topology Lock: visual guide

### 1. See the path that traffic is really taking

Each node card identifies its current parent, backhaul band, channel, rate,
and RSSI. The map makes a distant parent or weak intermediate hop visible
without cross-referencing multiple Linksys screens.

### 2. Record or edit the parent structure you expect

Select **Edit & lock topology**. Drag a child node onto its desired parent, or
use the accessible child and parent selectors. Editing is a preview: it sends
nothing to the router.

<p align="center">
  <a href="docs/assets/meshscope-topology-lock.jpg">
    <img src="docs/assets/meshscope-topology-lock.jpg" alt="MeshScope Topology Lock editor showing current and desired Linksys node parents without any client device list" width="760">
  </a>
</p>

_The live Node list is visible; the sensitive Client/Device table below the
dashboard was excluded before capture._

### 3. Monitor drift and recover only when it is useful

```mermaid
flowchart LR
    Desired["Saved parent"] --> Compare["Compare with live topology"]
    Current["Current parent"] --> Compare
    Compare -->|"Match"| Healthy["Green: parent correct"]
    Compare -->|"Mismatch"| Confirm["Confirm across 3 snapshots"]
    Confirm --> Parent{"Desired parent online?"}
    Parent -->|"No"| Wait["Wait; exact move cannot help"]
    Parent -->|"Yes"| Cooldown{"5-minute cooldown clear?"}
    Cooldown -->|"No"| Wait
    Cooldown -->|"Yes"| Radio["Choose opposite Parent uplink radio"]
    Radio --> MQTT["Publish exact MQTT BH/config"]
    MQTT --> Verify["Verify 2 fresh topology generations"]
    Verify --> Compare
```

The node card changes color with compliance state and shows the next eligible
recovery countdown directly on the topology. Recovery requires three
confirmed snapshots, an online desired parent, and a global five-minute
cooldown. A topology mismatch is handled with MQTT first; it never restarts
the mismatched child. The separate Parent-health guard described below may
restart a non-primary requested Parent only after repeated exact MQTT moves
were published but could not be verified. Recovery never steers the primary
node, an offline or wired child, or a child whose saved Parent is offline.

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

All targets contain the same topology page, `5GH`/`5GL` fields, auto-refresh,
Home Assistant entities, selected-node restart, and Topology Lock. The UI is
not reduced. Client/STA data defaults to `auto`:
targets with enough memory retain the complete raw topology payload, while
smaller devices can use the explicit `nodes-only` capability described below.
Successful compilation does not prove that internal RAM is sufficient for
every real mesh, so this release does not claim runtime parity until each
target passes the hardware acceptance gate.

Network assumptions:

- The ESP32 and primary Linksys router are on the same reachable trusted LAN;
  the browser is normally on that LAN or connected through an authorized
  WireGuard path
- An ESP32-C3 or ESP32-C6 must be able to join a 2.4 GHz SSID that can reach
  the Linksys router; these targets do not support 5 GHz Wi-Fi
- The Linksys firmware provides local HTTPS JNAP

Optional WireGuard access does not change the first two assumptions for the
ESP32 itself: its Wi-Fi interface must still reach the Linksys router. Remote
browsers and Home Assistant may instead initiate connections to the ESP32's
WireGuard address.

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
direct dashboard/API access without a login gate, persistent Topology Lock
state, the three-snapshot mismatch gate, a single-node restart, the five-minute
global cooldown, card colors, and the visible `MM:SS` countdown. Household
node names, addresses, and credentials are intentionally omitted.

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

Generate the two independent credentials used by Home Assistant and OTA:

```bash
# Prompts without echo and returns the Linksys password as Base64 for the C++ config
python3 tools/encode_secret.py

# ESPHome Native API key; use this as meshscope_api_key
openssl rand -base64 32

# Generate a separate OTA password
openssl rand -hex 24
```

Edit the local YAML for your selected target:

- `meshscope_wifi_ssid` / `meshscope_wifi_password`: home Wi-Fi credentials
- `meshscope_router_host`: primary Linksys router LAN address
- `meshscope_router_password_b64`: output from `encode_secret.py`, not the
  plaintext password
- `meshscope_api_key`: 32-byte Base64 key used by Home Assistant
- `meshscope_ota_password`: password for later OTA updates
- `meshscope_timezone`: timezone used by ESPHome logs and time components, such
  as `America/New_York` or `Europe/London`
- `meshscope_client_details`: `auto` (recommended), `full`, or `nodes-only`
- `meshscope_mqtt_parent_steering`: `auto` (recommended), `force-on`, or
  `force-off`; a mode saved from the page overrides this initial value

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
| ESPHome API key | Encrypted Home Assistant connection | Home Assistant integration |
| OTA password | Future firmware uploads | ESPHome update command |
| WireGuard private key | Identifies the ESP32 tunnel peer | Ignored local YAML only |
| WireGuard pre-shared key | Optional additional tunnel secret | ESP32 and WireGuard server peer configuration |

The dashboard has no separate credential. These remaining credentials serve
different purposes and should not be reused.

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
`http://<esp32-ip>/`. The dashboard opens immediately; there is no dashboard
username, password, unlock screen, or session cookie.

### 3. Add MeshScope to Home Assistant

The encrypted ESPHome Native API remains protected by its own key even though
the HTTP dashboard is open to reachable clients.

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
Assistant is offline or has never been connected. Wi-Fi and the ESPHome Native
API use a five-minute recovery reboot timeout when their connection remains
unavailable.

#### Optional WireGuard remote access

MeshScope can serve the same dashboard on its local Wi-Fi address and on a
WireGuard address. This avoids exposing an HTTP port to the Internet: traffic
is encrypted by WireGuard before it reaches the dashboard.
The ESP32 still talks directly to Linksys over the home Wi-Fi network.

In the ignored target YAML, add the optional package and its substitutions:

```yaml
packages:
  meshscope: !include esphome_meshscope_c5.yaml
  wireguard: !include esphome_meshscope_wireguard.yaml

substitutions:
  # Keep the normal MeshScope substitutions from the local example, then add:
  meshscope_wireguard_address: "10.23.0.48"
  # /32 keeps WireGuard inbound-only for the ESP32. Do not use 0.0.0.0.
  meshscope_wireguard_netmask: "255.255.255.255"
  meshscope_wireguard_private_key: "YOUR_DEVICE_PRIVATE_KEY"
  meshscope_wireguard_peer_endpoint: "vpn.example.com"
  meshscope_wireguard_peer_port: "51820"
  meshscope_wireguard_peer_public_key: "YOUR_SERVER_PUBLIC_KEY"
  meshscope_wireguard_peer_preshared_key: "YOUR_PRESHARED_KEY"
  meshscope_wireguard_tunnel_network: "10.23.0.0/24"
  # Example remote HA/client LAN. It must not overlap the ESP32 Wi-Fi LAN.
  meshscope_wireguard_ha_network: "192.168.50.0/24"
  meshscope_wireguard_keepalive: "25s"
```

Generate a dedicated device key pair and the pre-shared key required by this
package template on a trusted machine with WireGuard installed:

```bash
umask 077
mkdir -p .meshscope-keys
wg genkey | tee .meshscope-keys/private.key | \
  wg pubkey > .meshscope-keys/public.key
wg genpsk > .meshscope-keys/preshared.key
```

Copy the private key and pre-shared key only into the ignored local YAML. Add
the generated public key to the server peer. Delete or securely retain the key
files after provisioning; never commit them or a credential-bearing firmware
binary.

Add a peer like this to the WireGuard server (syntax varies by server UI):

```ini
[Peer]
PublicKey = <contents of .meshscope-keys/public.key>
PresharedKey = <contents of .meshscope-keys/preshared.key>
AllowedIPs = 10.23.0.48/32
```

On a remote phone, laptop, or routed Home Assistant peer, include
`10.23.0.48/32` in the server peer's `AllowedIPs`. Do not add the Linksys LAN
unless that peer independently needs and is authorized for general LAN access.

ESPHome's WireGuard `netmask` creates the ESP32's implicit outgoing route;
`peer_allowed_ips` only allows or rejects tunnel traffic and does not create
additional routes. Keep the `/32` netmask above for inbound dashboard and Home
Assistant access. In particular, never set the netmask to `0.0.0.0`, which
would make WireGuard the ESP32's default route. The tunnel and remote-client
CIDRs must not overlap the ESP32 Wi-Fi LAN or the subnet containing
`meshscope_router_host`. If Home Assistant already has a tunnel address, the
second CIDR may be the same tunnel network instead of a separate LAN. See the
[ESPHome WireGuard routing documentation](https://esphome.io/components/wireguard/#static-routes-and-outgoing-connections).

MeshScope deliberately leaves DNS, Linksys JNAP, and ordinary local OTA
traffic on Wi-Fi. A 25-second keepalive allows remote-initiated dashboard and
ESPHome API connections to survive typical NAT timeouts without making tunnel
availability a boot requirement.

The WireGuard server must define this ESP32 as a peer, using the generated
public key and `AllowedIPs = 10.23.0.48/32`. Remote clients and the Home
Assistant host must route `10.23.0.48/32` through the server, and the server's
forwarding/firewall policy must permit that traffic. After flashing, both
`http://<local-wifi-ip>/` and `http://<wireguard-address>/` open directly.
Home Assistant can connect to the WireGuard address with
the existing encrypted ESPHome API key. WireGuard status, latest handshake,
address, and MeshScope WireGuard URL entities are exposed to Home Assistant
for diagnostics.

The dashboard does not create an authentication session on either browser
origin. Home Assistant discovery normally does not cross a WireGuard tunnel:
use **Settings → Devices & services → Add integration → ESPHome**, enter the
WireGuard address and port `6053`, then enter the existing
`meshscope_api_key`.

This release was validated on ESP32-C5 with a live tunnel: direct dashboard,
status, and topology access plus the encrypted Home Assistant Native API
worked through the WireGuard address. C3 and C6 WireGuard builds
are compile/link tested but remain experimental hardware targets.

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

To update through WireGuard, pass the tunnel address explicitly:

```bash
.esphome-venv/bin/esphome run esphome_meshscope_c5.local.yaml \
  --device 10.23.0.48
```

Changing the WireGuard address, keys, endpoint, or server route during a remote
update can cut off the next OTA connection. Keep a verified local Wi-Fi/USB
recovery path before making those changes.

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

Each wireless Node shows two deliberately separate link rates. **Hop
throughput** is Linksys JNAP `GetBackhaulInfo.speedMbps`, an active Thrulay
measurement from that child Node to its immediate Parent. It is a per-hop
measurement, not end-to-end throughput to the gateway or Internet.
**PHY rate** is the child Node's current wireless backhaul bitrate from MQTT
`network/<node UUID>/BH/status` (`phyRate_2`, with `phyRate` retained as the
human-readable raw value). The ESP32 asks for one PHY sample after boot and no
more than once every 30 minutes. This avoids repeatedly invoking Linksys'
backhaul status workflow merely to refresh an instantaneous value. The
topology link label, Node card, and Node details all show the result; samples
older than two minutes are explicitly marked stale instead of being presented
as current.

The topology panel uses the full browser width and reacts immediately when its
container changes size. It expands hierarchy spacing on wide screens, compacts
safe gaps on medium screens, and keeps horizontal scrolling inside the map on
narrow screens where full 220-pixel diagnostic cards physically cannot fit.
The rest of the dashboard never becomes wider than the browser viewport.

### Topology Lock

Topology Lock is available on the ESP32-hosted page. Select **Edit & lock
topology** to capture the current live structure. On a desktop browser, drag a
child card onto its desired parent; on touch devices or with a keyboard, use
the child and desired-parent selectors. The preview keeps the current `5GH` or
`5GL` link visible beside the proposed relationship. Nothing is sent to a
router while editing. Review the short backhaul-disconnection warning,
acknowledge it, then select **Enable MQTT recovery**.

After applying, every node card becomes a live status surface:

- Green: the current parent matches the saved parent
- Amber: a mismatch is being confirmed across three successful snapshots
- Red/orange: the mismatch is confirmed and an MQTT move is queued or counting
  down; the `MM:SS` countdown is shown directly on the node card
- Blue/gray: the desired parent is offline, so recovery is blocked
- Violet: the MQTT request was accepted and the node is being verified
- Gray: the child node is offline

The ESP32 stores the lock in non-volatile storage and evaluates it after each
successful ten-second topology collection. An automatic MQTT move is allowed
only when Auto capability probing has succeeded (or Force on was selected),
the wireless child and its saved parent are both online, and the mismatch has
appeared in three consecutive snapshots. Automatic actions share a global
five-minute cooldown, so at most one node can be moved in that period. If
several nodes remain mismatched, the scheduler rotates among them instead of
repeatedly favoring one node.
Recent attempts are displayed below the map, and Home Assistant receives
**Topology Lock Active**, **Topology Lock Issues**, and **Topology Lock
Summary** entities.

Topology Lock uses the same guarded exact MQTT Parent transport as the manual
panel. It chooses the radio opposite a non-primary Parent's own uplink and
requires two fresh topology observations before reporting success. Use **Stop
automatic recovery** to clear the saved structure and stop automatic actions.
Locks include only nodes online at
apply time; edit and reapply after intentionally adding, removing, or
relocating mesh nodes.

### Exact Parent Steering

If the primary Linksys node runs a compatible owner-built MQTT ACL image, the
**Exact Parent Steering** panel can move an online wireless child to a selected
online Parent's current `5GH` or `5GL` radio. The operation may briefly
disconnect the child and its clients.

- **Auto** probes only the narrow discovery topics and enables the button after
  a fresh DEVINFO round trip. This is the default.
- **Force on** permits an attempt when detection cannot be confirmed. It does
  not bypass offline, cycle, wired-backhaul, current-Parent, radio, or Topology
  Lock checks.
- **Force off** performs no new probe or Parent publish.

The page distinguishes **Queued**, **Broker accepted**, **Verifying**, and
**Parent verified**. MQTT PUBACK alone never appears as success: the requested
Parent must be observed in two different topology generations within a
180-second verification window. Reloading the page does not lose an
in-progress ESP32 operation.

Each steered Node also gets a persistent **Parent Steering Health** record.
The topology card and Node drawer show the requested Parent, exact band/BSSID/
channel source, MQTT publish and echo evidence, consecutive and total failures,
successes, the requested Parent's online mesh-child count, Parent restart count,
last timestamps, and the five-minute restart countdown. These fields are added
to the existing card; Clients, backhaul, RSSI, `5GH`/`5GL`, and Topology Lock
information remain visible.

Only an exact `BH/config` operation that reached broker acceptance and then
expired unverified counts as a failure. Preflight, offline-Node, radio lookup,
broker, and ACL errors do not. Success clears the consecutive count. At two
consecutive qualifying failures, the ESP32 may send one `core/Reboot` directly
to the requested Parent when that Parent is online, is not Main, and currently
has zero online mesh children. The conditions are checked again immediately
before the request, and Parent restarts are globally limited to one every five
minutes. The target child is never restarted by this health guard.

MeshScope resolves the requested Parent radio from fresh MQTT `DEVINFO` first.
Some Linksys primary nodes do not publish their own DEVINFO record; in that
case the ESP32 safely falls back to the current JNAP backhaul snapshot and uses
the Parent `apBSSID` and channel already observed by an online child on the
requested band. It still applies the same BSSID, channel, topology, and
Topology Lock checks before publishing.

Topology Lock and the manual panel share the exact steering transport but have
different timing. Dragging nodes changes the saved automatic-recovery target;
use the Exact Parent Steering panel to move a node immediately. If an enabled lock expects a
different Parent, MeshScope blocks the manual move so recovery cannot fight it.

Home Assistant receives read-only diagnostics for Parent steering
availability, saved mode, and the latest result. The potentially disruptive
move remains in the dashboard and is available to every client that can reach
it.

### Selected-node restart

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

Do not expose the ESP32 HTTP service to LAN or WireGuard users who should not
have permission to restart or steer a node.

## Troubleshooting

| Symptom | What to do |
|---|---|
| The target's `.local` address does not open | Find the IP in Linksys DHCP or serial logs, then create a DHCP reservation |
| The page says the router is offline but still shows topology | This is the last successful snapshot, not a false online state. Check the router address, Wi-Fi, and Linksys local password |
| The first page remains on “Loading” | Allow one complete JNAP collection cycle; check ESPHome logs if it continues |
| Home Assistant does not discover MeshScope | Add the ESPHome integration manually with the ESP32 IP and `meshscope_api_key` |
| WireGuard address does not open | Confirm the peer handshake, server peer route for the ESP32 `/32`, return routes from the remote client or Home Assistant network, and forwarding/firewall policy on the WireGuard server |
| Client/STA details are unavailable | Check `/api/status` or the page notice for `nodes-only`; set `meshscope_client_details: "full"` only if the board has enough memory |
| ESPHome reports missing PSRAM on C5 | Verify the exact C5 development board and its PSRAM specification, or use the matching no-PSRAM C3/C6 target |
| Front-end changes do not appear on ESP32 | Regenerate embedded assets, then rebuild or update over OTA |
| The device disappeared after changing Wi-Fi or OTA settings | Connect over USB and run `esphome run` again |
| The Linksys password contains special characters | Use `tools/encode_secret.py`; never insert a plaintext password directly into the firmware lambda |

## Security and privacy boundaries

The desktop and ESPHome versions use different credential models:

- The desktop password remains in Python process memory, and the web service
  binds to localhost by default.
- The ESPHome version stores Wi-Fi, Linksys, API, and OTA credentials in an
  ignored local YAML and compiles them into device flash. HTTP APIs never
  return those values to the browser.
- The ESP32 page and HTTP APIs intentionally have no authentication gate.
  Anyone who can reach port 80 can read topology/client data and invoke the
  dashboard's restart, steering, and recovery controls. HTTP itself does not
  encrypt traffic. Use it only on a trusted home LAN or through authorized
  WireGuard peers; never port-forward it or expose it directly to the Internet.
- The ESPHome Native API uses a separate encrypted key. It does not encrypt the
  MeshScope web page.
- Linksys local JNAP uses the router's self-signed HTTPS certificate. MeshScope
  skips certificate verification for local compatibility, so its trust model
  assumes the home LAN and gateway have not been maliciously intercepted.

Read operations are restricted to an allowlist of `Get*` and `Check*` actions.
The JNAP write allowlist contains only `core/Reboot`. A manual target must be
the explicitly selected online node with a known private address. An automatic
Parent-health target must independently pass the repeated-published-failure,
online, non-primary, zero-online-mesh-children, and five-minute cooldown gates.
Topology Lock first uses only the exact
`network/<CHILD_UUID>/BH/config` MQTT topic after saved-parent-online,
three-snapshot confirmation, radio, cycle, and global five-minute cooldown
checks. Reset, firmware updates, and all other JNAP mutations are rejected
before a router request is generated.

The BLE overlays remain offline, owner-controlled research artifacts. The
desktop dashboard can invoke MQTT Parent steering only when the owner has
installed a compatible ACL image and the capability probe succeeds. Generated
IMG files and vendor firmware inputs are never published.

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
currently exposes automatic Client Steering and Node Steering only. Firmware
analysis and a physical MX4200 test confirmed the local MQTT exact-Parent data
path. MeshScope v0.5.0 exposes it through guarded desktop API endpoints; the
normal firmware and JNAP-only dashboard remain read/restart only. See
[Exact Parent steering over Linksys MQTT](docs/mqtt-parent-steering.md).

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
- [MQTT Parent-steering build, API, and verified data path](docs/mqtt-parent-steering.md)

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
identifying node or client names. Before attaching an image, follow the
[privacy-safe screenshot checklist](docs/sharing-screenshots-safely.md).

## License

MeshScope is released under the [MIT License](LICENSE).
