# Linksys hidden interfaces and Node Parent steering

Analysis date: 2026-07-27

This is an offline static and isolated user-mode analysis of the extracted filesystem from
`FW_WHW03_2.1.19.215389_prod.img`. The router was offline during the final
review. No JNAP mutation, restart, steering request, or shell command was sent
to any live Node.

## Result

The firmware contains a real internal operation for moving a wireless Node to
a specific upstream AP:

```text
topomgmt
  -> nodes.topology.steerer.change_node_parent
  -> pub_bh_config <child UUID> <band> <channel> <target BSSID>
  -> network/<child UUID>/BH/config
  -> the child Node's backhaul manager
```

This is stronger evidence than the public UI or JNAP surface provides. It is
an implemented backhaul control path, not merely a label for automatic Node
Steering.

It is **not** currently a MeshScope feature. No public JNAP action or web form
that accepts both a child Node and a target Parent was found. The discovered
operation requires local shell-level access to `topomgmt`/`pub_bh_config`, or a
future narrowly scoped authenticated helper that invokes it.

Follow-up analysis of MX4200 1.0.13.210200 independently found the same
`nodes.util.steer_node_to_parent` function, byte-identical `pub_bh_config`,
automatic optimizer, child topic, and backhaul consumer. That MX build omits
the WHW03 `topomgmt` and `nodes.topology.steerer` wrappers, but retains the
complete data plane. This resolves the earlier MX-family uncertainty for that
specific version; the currently observed MX42 1.0.13.216903 and WHW03
2.1.20.216892 builds should still be verified before live activation.

## Exact internal execution chain

### 1. Topology management exposes `change_node_parent`

`/usr/bin/topomgmt` is a Lua command dispatcher with these principal options:

```text
-m, --mod       module
-c, --cmd       command
-p, --params    parameters
-d, --debug
-v, --verbose
-h, --help
```

Its `steerer` module maps to
`/usr/local/lib/lua/5.1/nodes/topology/steerer.lua`. That module documents
`change_node_parent` and requires:

- `uuid`: the child Node to move
- `band`: `5GL` or `5GH` for a target-specific move
- `channel`: the target radio's current channel
- `bssid`: the target Parent's user AP BSSID on that band

The function constructs `pub_bh_config` with those four values.

### 2. `pub_bh_config` targets one Node

`/usr/sbin/pub_bh_config` publishes a `set` payload to:

```text
network/<child UUID>/BH/config
```

The payload carries `band`, `channel`, and `bssid`. The target UUID is
validated before publishing.

### 3. The selected child consumes the request

`/etc/subscriber.d/slave.subs` subscribes each child to its
`network/%uuid/BH/config` topic and raises `mqttsub::bhconfig`.

`/etc/init.d/service_node-mode.sh` then:

1. reads and validates the channel and BSSID;
2. stores them in `mqttsub::bh_channel` and `mqttsub::bh_bssid`; and
3. sets `backhaul::set_intf` for `5GL`, `5GH`, or `AUTO`.

### 4. The backhaul monitor performs the move

`service_wifi_ext.sh` compares the requested band/BSSID with the current
backhaul. When they differ, it brings down the current wireless backhaul,
marks the backhaul down, and starts reconnection.

`smart_connect_client_monitor.sh` first attempts the exact requested
BSSID/channel with the `MQTT_BACKHAUL_SELECTOR` path. On success it records
the new `backhaul::preferred_bssid` and `backhaul::preferred_chan`. On failure
it falls back to the ordinary automatic backhaul selector.

While connected, the monitor also detects an unexpected change away from the
preferred BSSID and reconnects. This confirms that the request is operational,
not dead source code.

## How to identify the requested Parent

The authoritative per-Node radio data lives in:

```text
/tmp/msg/DEVINFO/<uuid>
```

Relevant fields are:

```text
data.userAp5GL_bssid
data.userAp5GL_channel
data.userAp5GH_bssid
data.userAp5GH_channel
```

The firmware itself uses these fields to map an upstream `ap_bssid` back to a
Parent UUID. A safe UI must resolve the selected Parent to one of these live
radio tuples server-side; it must never accept a free-form BSSID or channel
from the browser.

There are three possible read paths:

1. `topomgmt -m devinfo ...` reads the exact DEVINFO records from a shell.
2. The authenticated `/ui/cgi/sysinfo.cgi` output includes
   `cat /tmp/msg/DEVINFO/*`; `diagnostics/GetSysinfoData` runs this same
   sysinfo generator. This is a possible read-only bridge, but it is large,
   slow, and contains sensitive diagnostic data.
3. `GetNodesWirelessNetworkConnections` exposes a serving AP BSSID for each
   connected STA. This can infer a Node's radio BSSID only when that radio has
   a client, so it fails for zero-client Nodes such as the previously observed
   BigTree state.

`nodes/setup/GetSelectedChannels` is insufficient by itself. Its source reads
the DEVINFO BSSID only to determine whether a radio exists, then deliberately
returns the radio ID, band, and channel without the BSSID.

## Safety and persistence findings

The manual `change_node_parent` function checks only that required parameters
exist. It does not verify:

- that the target BSSID belongs to the selected Parent;
- that both Nodes are online and wireless;
- that the target is not the child itself;
- that the target is not a descendant of the child;
- that the target is reachable on the requested band; or
- that a topology change is already in progress.

The automatic decision engine has a `filter_out_kids` step, but the manual
function does not call it. Its implementation checks the Node's direct
`kids` list, not an explicitly recursive descendant walk. MeshScope must
therefore enforce a full subtree cycle check itself.

The preference is stored in `sysevent` runtime state. It is not clearly
persisted as a permanent `syscfg` pin, and auto-channel processing explicitly
clears `backhaul::preferred_bssid` and `backhaul::preferred_chan`. The correct
product language is therefore **Steer now** or **Request Parent**, not
**permanently pin Parent**.

A production control needs all of these guards:

- child and Parent are known, online Nodes from the same fresh topology;
- the child is non-authority and currently uses wireless backhaul;
- Parent differs from child, current Parent, and every descendant of child;
- target BSSID/channel are resolved from the Parent's live DEVINFO data;
- target BSSID maps back to the selected Parent UUID;
- only one topology mutation runs at a time, with a cooldown;
- the original Parent, band, channel, and BSSID are snapshotted;
- success is observed from fresh backhaul data rather than command exit alone;
- failure has a bounded timeout and an explicit recovery/rollback policy; and
- the UI warns that the child and its entire subtree may temporarily disappear.

## Incomplete/dead alternate entry

`/etc/registration.d/60_topology_management` registers the events:

```text
topology_management::change_node_parent
topology_management::change_client_parent
```

However, `/etc/init.d/service_topology_management.sh` implements cases only
for temporary blacklist and 802.11v client steering. A change-parent event
falls into its `UNKNOWN` branch. This event route appears incomplete in this
build and must not be treated as a working API.

The direct `topomgmt` steerer module does contain and invoke the working
`pub_bh_config` path. An unrelated typo also leaves `topomgmt -m bh` broken
(`moudle` instead of `module`); it does not affect the `steerer` module.

## Alternate entry-point matrix

The internal steering primitive has several possible wrappers, but only one
transport that could plausibly be called directly from another LAN host. None
is currently available from the ordinary administrator web credential alone.

| Candidate | Exact Parent target? | Needs shell first? | Finding |
| --- | --- | --- | --- |
| `topomgmt -m steerer -c change_node_parent` | Yes | Yes | Intended WHW03 CLI dispatcher; absent from MX4200 1.0.13.210200. |
| `lua -e` with `nodes.util.steer_node_to_parent` | Yes | Yes | A second direct Lua wrapper around `pub_bh_config`; useful for a narrow helper, but not an independent entry. |
| `pub_bh_config` | Yes | Yes | Smallest existing executable primitive. It publishes the child-specific backhaul request. |
| Secure MQTT on port 8883 | Yes | No, if its internal credentials are already available | The broker and clients support TLS-PSK plus username/password. This is the strongest independent transport candidate, but the required internal Mesh credentials are not exposed by the ordinary admin JNAP login. |
| Anonymous MQTT on port 1883 | No | No | Deliberately blocked: `strict.acl` does not grant writes to `network/+/BH/config`, and the firmware readme names Node steering as a dangerous command excluded from this listener. |
| `topology_management::change_node_parent` sysevent | No in this build | Yes | Registered, but its service script has no handler and returns `UNKNOWN`. |
| Public JNAP, `/ca`, or `btjnap` | No | No | No action schema accepts child UUID plus Parent radio tuple. `/ca` only prevents a child UI from redirecting to Main. |
| A second Lighttpd/JNAP instance | Potentially | Yes | Lighttpd can load another config, but adding a new JNAP action also requires the corresponding compiled HDK schema/module. This is a post-shell adapter, not a way to gain initial access. |
| A LuaSocket test server | Potentially | Yes | The firmware contains bind/listen support, so a small authenticated helper is possible after shell access. |
| `sct_server` secure configuration server | Not with the stock template | Yes for a custom template | The stock `/etc/secure_config.conf` is an allowlist of selected `syscfg` keys and sysevents. It has no exact Parent operation, and the registered change-parent sysevent is dead. |
| `lsc_server` Smart Connect server | No | No useful route | Fixed setup/config/control protocol for onboarding and configuration, not a general command server. |
| `sectrans_server` | No | No useful route | Credentialed transfer of identified data such as the Cedar database, not command execution. |
| Automatic Tesseract optimizer | Indirectly | No manual route | It invokes the same `pub_bh_config` primitive, confirming that the primitive is active, but selects candidates by policy rather than accepting a user-selected Parent. |

The practical implementation choices are therefore:

1. use legitimate shell access and call `topomgmt` or `pub_bh_config`;
2. after obtaining shell access once, install a very small LAN-only,
   authenticated steering helper; or
3. call secure MQTT directly only if the internal Mesh TLS-PSK and messaging
   credentials can be legitimately obtained without publishing or persisting
   them.

Starting an existing “test server” does not create a pre-authenticated bridge
from the admin web UI to the steering primitive. It only changes how an
already-authorized shell could package that primitive.

## Hidden and support-oriented web entries

The Lighttpd configuration requires Basic authentication for CGI pages on the
main LAN, denies them on guest/SmartConnect ports, and uses
`/var/config/.sysinfo_pswd` as the credential file.

Notable entries found in the firmware are:

| Entry | Purpose / finding |
| --- | --- |
| `/ca` and `/CA` | On a slave, bypasses the root redirect to Main and loads the local Node UI. The UI preserves `#casupport`. |
| `/fwupdate.html` | Legacy authenticated local firmware upload page backed by `/ui/cgi/jcgi.cgi`. |
| `/sysinfoall.cgi` | Collects and downloads diagnostic archives for the mesh with `get_all_sysinfo -a`. |
| `/ui/cgi/sysinfo.cgi` | Large diagnostic report including `/tmp/msg/DEVINFO`, backhaul state, logs, interfaces, and system data. |
| `/ui/cgi/debug_syslog.cgi?section=...` | Firmware information and log sections. |
| `/ui/cgi/bootloader_info.cgi` | Boot environment, MTD layout, boot slot, and recovery information. |
| `/ui/cgi/node-bh-perf-data.cgi` | Historical Node backhaul performance data. |
| `/ui/cgi/cedar_info.cgi` | HomeKit/LRHK state and related diagnostic data. |
| `/ui/cgi/qos_info.cgi` | QoS configuration and runtime state. |
| `/ui/cgi/speedtest_info.cgi` | Speed-test diagnostics. |
| `/ui/cgi/tr69info.cgi` | TR-069 diagnostic material. |
| `/ui/cgi/usbinfo.cgi` | USB/storage diagnostics. |
| `/cgi-bin/lrhk.cgi` | Internal HomeKit/LRHK control and restart page. |
| `/cgi-bin/origin.cgi` | Internal motion-sensing controls and Wi-Fi restart action. |
| `/ui/local/dynamic/advanced-wireless.html` | Advanced wireless UI selected through an `advanced-wireless` cookie. |
| `/ui/local/dynamic/agent-login.html` | Remote-assistance login flow. |

The multipart `/ui/cgi/jcgi.cgi` accepts only three named operations:
firmware update, configuration backup, and configuration restore. Restore
extracts only `tmp/syscfg.tmp` and `var/config/ipa`; it is not an arbitrary
file upload or a Parent-steering bridge.

The local UI references 350 unique JNAP service/action URLs in this image.
Useful hidden diagnostics include Node neighbor scans, backhaul refresh,
scheduled reboot settings, sysinfo requests, and restoration of the previous
firmware. The only topology-optimization writes are the two global Boolean
settings for automatic Client Steering and Node Steering. No per-child target
Parent JNAP action was found.

No unauthenticated administrative page was identified. Diagnostic output can
contain identifiers and other sensitive material, so MeshScope must neither
cache it to disk nor publish captured output to GitHub.

## Executable help and related utilities

The extracted filesystem contains 886 32-bit little-endian ARM ELF files.
They cannot be executed natively on the analysis Mac. In addition to static
ELF strings and source inspection, selected binaries were run with ARM
user-mode QEMU inside an ARM64 Lima VM. The extracted filesystem was mounted
read-only, execution used a disposable copy, and every process ran in a Linux
network namespace with no network interfaces other than loopback. Only help,
version, and Lua module-load operations were attempted.

A full `qemu-system` firmware boot is not the right validation vehicle here:
the image targets a Qualcomm IPQ4019 platform and Linux 3.14 hardware tree,
not QEMU's generic virtual board. User-mode QEMU is sufficient for CLI
parsing and dynamic linking without pretending to emulate the radios,
switches, flash layout, `sysevent`, or `devicedb`.

Runtime results:

- `lsc_server -h` exposes setup interface/port, configuration interface/port,
  a control socket, system-service plugin, and settings file.
- `sct_server -h` accepts a template file and port; `sct_client -h` accepts
  server IP, credentials, device ID, and a fixed message type.
- With the stock template, `sct_server` also successfully reached a real
  `LISTEN 0.0.0.0:16060` state in the isolated namespace. This confirms that
  it is runnable, but does not expand the stock template's allowlist or add a
  Parent-steering operation.
- `sectrans_server -h` accepts only login, secret, and daemon mode; its client
  additionally selects server, port, and a data ID.
- `omsgd -h` and `subscriber -h` confirm PSK identity/key plus messaging
  username/password options. The bundled Mosquitto client confirms TLS-PSK
  publishing support.
- `lighttpd -h` confirms that another configuration and module directory can
  be selected.
- Lua 5.1 successfully loaded both
  `nodes.util.steer_node_to_parent` and
  `nodes.topology.steerer.change_node_parent` as live functions without
  invoking either one.
- Loading the entire `topomgmt` script outside the router stops at its eager
  `devicedb` connection. This is an expected hardware/service dependency, not
  evidence against the two successfully loaded steering modules.
- `tess_steer -h` and `tess_steer_local_decision_eng -h` produce no runtime
  help in this build, so their option inventory still comes from static
  strings and Lua callers.

The most relevant utilities are:

| Utility | What its help/source reveals |
| --- | --- |
| `topomgmt` | Generic topology module dispatcher; the `steerer` module exposes `change_node_parent` and client-steering helpers. |
| `tess_steer` | Automatic client/Node steering coordinator with a dry-run `-n`, operation filters, survey timing, and status reporting. |
| `tess_steer_local_decision_eng` | Automatic decision engine; `--node` switches it to Node selection and it applies RCPI thresholds and child filtering. |
| `pub_bh_config` | Publishes an exact backhaul band/channel/BSSID request to one child UUID. |
| `pub_reconsider_bh` | Tells one child to reconsider its backhaul automatically; it does not select a Parent. |
| `pub_nodes_steering_start` | Starts 802.11v-style steering from a client MAC to an AP BSSID/channel; despite its name it is not the exact Node Parent API. |
| `pub_slave_parent_ip` | Reports a child's Parent IP and supports dry-run; it does not change the Parent. |
| `find_parent_ip`, `lldp_to_parent_ip` | Read or derive current Parent information. |
| `bssid_chan` | Resolves the channel for a discovered AP BSSID. |
| `bh_report`, `nb_report`, `status_report` | Produce backhaul, neighbor, and topology/status reports, including JSON modes used by JNAP. |
| `wifitool`, `wlanconfig` | Low-level Qualcomm radio controls. These are not an appropriate MeshScope API. |

The static binary scan also found recovery/flash utilities, Mosquitto clients,
boot-environment tools, and many generic networking utilities. None provides
a safer web-callable Parent selector than the topology manager path above.

## Implementation decision

The feature is technically feasible, but it should remain disabled in the
current web-only MeshScope build.

The next safe milestone is an offline implementation of:

1. target eligibility and descendant-cycle validation;
2. Parent radio tuple parsing from a captured/sanitized DEVINFO fixture;
3. a command preview and state machine with no transport attached; and
4. unit tests for BigTree-style zero-client targets, failures, timeouts, and
   subtree disruption.

Actual activation should wait until a legitimate authenticated command
transport exists (for example, the planned LAN-only SSH service or a minimal
firmware helper), and until the live network's exact MX42/WHW03 production
builds are confirmed to contain the same path. The first live trial should use
a non-critical leaf Node and observe the topology rather than assuming that a
successful publish means a successful re-parent.

The MX4200 cross-model evidence, image layout, selected-Node reboot proof, and
SSH differences are documented in
[MX4200 1.0.13.210200 firmware analysis](mx4200-firmware-analysis.md).
