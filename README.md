# MeshScope

MeshScope is a local Linksys Velop dashboard. Its observation path is read-only
and presents the live Mesh topology, backhaul quality, Node relationships, and
Client/STA associations in one browser page. The only mutating capability is an
explicitly confirmed Mesh restart matching Linksys' CA support page.

![MeshScope](https://img.shields.io/badge/Linksys-Local%20JNAP-16769b)
![Safety](https://img.shields.io/badge/restart-typed%20confirmation-b9772b)

## Highlights

- Parent-subtree topology layout with animated backhaul paths
- Distinct `5GH` / `5GL` labels with channel, rate, and RSSI context
- Selectable 10, 30, or 60 second auto-refresh, pause, and manual refresh
- Immediate refresh after a background tab becomes visible and stale
- Per-Node Client/STA lists with connection band, signal, rate, and address
- Click-through Client details with navigation back to the serving Node
- Live automatic Client Steering and Node Steering status
- Read-only per-Node capability probe using credentials synchronized from Main
- Hidden Linksys child-Node support entry: `https://<node-ip>/ca`
- Gated `Restart Mesh WiFi system` action through the selected Node

The UI and backend are dependency-free at runtime: Python's standard library
serves the app and communicates with the router; the frontend is plain
HTML/CSS/JavaScript.

## Start

Requires Python 3.10 or newer.

```bash
python3 linksys_mesh_app.py
```

Then open:

```text
http://127.0.0.1:8765
```

On macOS, `Start MeshScope.command` starts the local service and opens the page.

For an automatic startup connection, pass the password through an environment
variable:

```bash
LINKSYS_PASSWORD='your-local-router-password' python3 linksys_mesh_app.py
```

The password is kept only in the local Python process memory and cleared when
the service stops. MeshScope binds to `127.0.0.1` by default and only permits
private, link-local, loopback, or `.local` router targets.

## Node details and CA support mode

Opening an online Node detail triggers safe direct calls to that Node:

- `core/CheckAdminPassword`
- `core/GetDeviceInfo`
- `nodes/smartmode/GetDeviceMode`
- `nodes/topologyoptimization/GetTopologyOptimizationSettings2`

This confirms the Node identity, synchronized credentials, Master/Slave mode,
and advertised capabilities without changing router state.

Linksys firmware exposes a child-Node redirect bypass at:

```text
https://<node-ip>/ca
```

The firmware preserves `#casupport`, accepts the locally synchronized router
credential, and enters `/ui/dynamic/home.html#casupport`. A browser may show a
self-signed certificate warning because the page is served directly by the
router.

## Steering and restart findings

The inspected firmware exposes automatic global Client Steering and Node
Steering settings. No supported action with both a child ID and target Parent ID
was found, so MeshScope does not claim or expose manual Parent pinning.

The CA Troubleshooting applet exposes Restart on a Node that advertises
`nodes/setup/Setup3`, but sends `core/Reboot` without a target `deviceID`.
MeshScope therefore labels the action as potentially whole-Mesh and only sends
it after the user types `RESTART <Node name>`. The request is sent directly to
the selected Node, matching its CA page; every Node and Client may temporarily
go offline.

Detailed evidence:

- [BigTree Node control feasibility](docs/node-control-findings.md)
- [Node Steering findings](docs/node-steering-findings.md)

## Safety model

Router calls are guarded by two backend allowlists. Observation operations must
start with `Get` or `Check`. The mutation allowlist contains only
`core/Reboot`, and the backend additionally requires an online known Node plus
the exact `RESTART <Node name>` confirmation. Reset, steering changes, firmware
updates, and every other mutation are rejected before a router request is
created.

No password, authentication token, or browser session data is written to disk
or returned by an API response.

## Tests

```bash
python3 -m unittest discover -s tests -p 'test_*.py' -v
node --test \
  tests/test_refresh_state.js \
  tests/test_topology_layout.js \
  tests/test_detail_data.js
```

The Python suite verifies topology normalization, Node probes, the read-only
allowlist, and the restart confirmation guard without contacting a router. The
JavaScript suite covers refresh state, parent-subtree layout, Client/STA
association, and Node capability reporting.
