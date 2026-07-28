# Linksys MX4200/MX5300 BLE and exact-Parent steering

## Scope and conclusion

This is an offline, owner-authorized analysis of the Linksys MX4200 and MX5300
firmware and the official Linksys Android application. The objective is narrow:
move one known child Node to one user-selected Parent. It is not a vulnerability
or access-control bypass assessment.

The central result is:

1. **BLE is a real local JNAP transport on both models.** Static firmware
   control flow keeps a configured Slave in the Peripheral role and runs
   `btsetup`; the Master becomes the BLE Central. A live configured-Slave scan
   is still required to confirm this on the analyzed hardware.
2. **The transport is generic enough to carry an arbitrary registered JNAP
   Action.** It is not limited by the phone application's list of setup calls.
3. **Stock firmware has no registered JNAP Action for an exact Parent.** It can
   toggle global Node Steering, inspect setup state, and reboot a connected
   Node, but no stock BLE request accepts the required
   `band + channel + Parent BSSID` tuple.
4. **The child-side implementation needed for exact-Parent steering is small.**
   The production MQTT consumer already validates that tuple, stores two
   temporary values, and triggers `backhaul::set_intf`. A narrowly scoped,
   authenticated BLE action can reuse the same child-local sequence.
5. **The best BLE design connects directly to the selected child Node.** It
   does not try to turn the Master into a Peripheral and does not need BLE to
   relay an MQTT packet.

So the answer is not “stock BLE already has a hidden change-Parent command.”
The answer is “stock BLE supplies the transport and the firmware supplies the
steering consumer; a small authenticated bridge is missing between them.”

## Analyzed artifacts

| Artifact | SHA-256 |
| --- | --- |
| Google Play APK pulled from the connected Pixel 8a | `04ebacce487417db0581302ec4a77ec7019ebeb3e3b08e2409450c4371a52e7f` |
| Independently obtained Linksys Android `3.6.1` APK | `15e5974ba5d2044dfa768614ab8f65751823745f9a6be0da1ca1ec2f824b5425` |
| `assets/www/app.js`, identical in both APKs | `95dde91064f90f151e56615d39eb8017eba1991c5d06c1af566809d9ebeeaf43` |
| MX5300 `btsetup` | `79df8dc81fe7340031d488019666eb04a6ebba017dff31fb74cc838dcfe455ff` |
| MX5300 `btsetup_central` | `5cbb1557e46fb3dae0f6281e551d2ceca6d7f852df541ae2e06dafa87b13baf3` |
| MX5300 `iotd_ctl` | `769ef0793ae1b83e2a3bfff84d6df59f4d84ddc3a0cc4c530163c4ffcc4b47dc` |
| MX4200 `btsetup` | `92899292b767cbf030e97820d6cfb2832a8e2bb33502779b6a8bc8d660c82f75` |
| MX4200 `btsetup_central` | `921b6a914c817d307187ddca02329911bb2185c68afd6a078c6535c5478db419` |
| Shared `/etc/btjnap.conf` | `6be2712a7ffd0962577d3e5e604889b4a742add3212a4ffd08c15c3486fb5cea` |

Both APKs identify as package `com.cisco.connect.cloud`, version `3.6.1`, and
are signed by `O=Cisco Consumer Products LLC`; the certificate SHA-256 is
`11555e56c7d2a21bb97d3a268c97476e2e1711c92ab43d1eb48afc7851b1e761`.
The complete APK hashes differ, but the BLE-bearing application bundle is
byte-identical. The Pixel copy is the authoritative installed artifact rather
than a third-party package copy.
Google Play describes the Linksys application as using Bluetooth during Velop
setup. The package name and purpose can be checked on the
[official Google Play listing](https://play.google.com/store/apps/details?hl=en_US&id=com.cisco.connect.cloud)
and Linksys's [official installation page](https://support.linksys.com/kb/article/254-en/).

No APK, extracted vendor binary, router credential, or firmware image is
committed to this repository.

## Connected Pixel 8a: what offline inspection proves

The USB-connected phone is a Pixel 8a running Android 17. Read-only package and
system inspection established:

| Property | Evidence |
| --- | --- |
| Installer | `com.android.vending` (Google Play) |
| Version | `3.6.1`, version code `2208632` |
| SDK range | minimum 28, target 35 |
| Bluetooth state | enabled |
| Runtime grants | `BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT`, fine location, and local-network access granted |
| App data | not debuggable; private data unavailable through `run-as` |
| BLE plugin | Cordova `cordova-plugin-bluetoothle` `6.7.4` |
| Historical capture | HCI snoop setting unset and no saved Linksys session available |

The application is logged in and can show the remote mesh dashboard. That
proves the account state is useful for later in-range testing, but it cannot
create a BLE session while the routers are physically absent. AppOps records
historical scan/connect access, but it does not preserve peer addresses, GATT
traffic, or Actions and therefore is not a packet capture.

The system bonded-device list contains no Linksys Node. That is consistent with
the application source: its Linksys setup path calls `connectGatt` directly,
then discovers, subscribes, and writes. The bundled generic Cordova plugin does
implement an optional `bond` operation, but exhaustive search of the Linksys
JavaScript bundle found no invocation of it.

### Built-in BLE recorder: present but not active

The application bundle contains a useful `MockData.writeResponses` recorder.
When `window.shouldWriteResponses` is true, it appends Cordova callback objects
to JSON files in `cordova.file.externalDataDirectory`. Coverage includes:

- `startScan`, `initialize`, `connect`, `discover`, and `isConnected`;
- every `subscribe`, `write`, `unsubscribe`, and `read` callback; and
- the final parsed response named after the JNAP Action.

This is a development recorder, not evidence of a historical session. On
production Android, `MockData.verifyOS()` does not set the flag true, so it is
undefined/false. The APK has no packaged `mocks/` response set (only an
unrelated widget test page), and the Pixel's application external-data
directory contains no recording files.

Cordova's native engine would expose a WebView DevTools socket only if the APK
were debuggable or `InspectableWebview=true`. This release is not debuggable
and its decoded `config.xml` contains no such preference. The only
`@chrome_devtools_remote` socket observed on the phone belonged to Android
Chrome: its targets were the local MeshScope BLE Lab and an unrelated web
page, not Linksys. The temporary host-side forward used to identify that
socket was removed immediately. There is no Linksys WebView target through
which the recorder can be enabled on this installed build.

A separately signed diagnostic build or emulator build could set
`InspectableWebview=true` and toggle the recorder, but it cannot update the
Google Play APK in place because the signing key differs. It would also not
inherit the official APK's private logged-in data. This is therefore a
preparation route for a future in-range test, not a way to manufacture a BLE
capture while the routers are absent.

## Hardware and service lifecycle

### MX5300

`/etc/system/wait` resets Bluetooth with GPIO 21, loads the address from
`bt_mac_addr`, checks `/dev/spidev32765.0`, and then raises
`btsetup-start`. `service_btsetup.sh` starts the Qualcomm `qca-iot`, `iotd`,
and `iotd_ctl` path for the external “Quartz” controller.

### MX4200

MX4200 uses an `efr32_ctl` controller process rather than the MX5300
`qca-iot` wrapper. Its startup also resets GPIO 21 and programs the synchronized
`bt_mac_addr`. The transport and high-level service behavior are nevertheless
the same.

### Role behavior shared by both models

| `smart_mode::mode` | Mesh role | BLE role | `btsetup` Peripheral |
| --- | --- | --- | --- |
| `0` | Unconfigured | Peripheral | Running |
| `1` | Slave Node | Peripheral | Running |
| `2` | Master | Central | Stopped |

This is the architectural reason to address the **child directly over BLE**.
The firmware is designed to leave a configured child advertising and
connectable as a Peripheral. The Main Node is normally not. This is strong
static evidence, not a substitute for one future in-range scan.

`backhaul::status` also causes the BLE advertisement to be refreshed. The
advertisement includes:

- local name `Linksys`;
- Belkin manufacturer ID `0x005c`;
- smart mode;
- backhaul up/down status; and
- a mode-limitation flag.

The configured raw advertising interval is `0x0800` BLE units, or approximately
1.28 seconds.

### Why the official application does not show configured Nodes

The four manufacturer bytes accepted by the application have two observed
prefixes:

| Bytes 0–1 | Vendor label |
| --- | --- |
| `5c 00` | Belkin |
| `ee 0b` | Linksys |

Byte 2 is WAN/backhaul status. The application calls byte 3
`modeLimitation`, but its own constants and comments establish these meanings:

| Byte 3 | Meaning | Official setup scan |
| --- | --- | --- |
| `00` | unconfigured, no limitation | accepted |
| `04` | unconfigured, Slave-only | accepted |
| `08` | unconfigured, Master-only | accepted |
| `01` | configured | rejected |

Both the Android and iOS analyzers accept only `00`, `04`, or `08`. iOS also
validates either expected manufacturer prefix. The current Android branch has
an `A != 0x5c || A != 0xee` logic error, so a four-byte record with an accepted
mode can bypass its intended first-byte check; that bug does not make
configured mode `01` pass. The older scanner embedded in the APK is stricter
and compares the complete unconfigured records. The configured record is
intentionally discarded in application JavaScript after the platform BLE
scanner reports it. This is an application setup-policy filter; it is not
evidence that the configured Slave stops advertising.

The offline decoder makes the distinction explicit:

```sh
python3 tools/linksys_ble_jnap.py --manufacturer-data-hex ee:0b:01:01
```

It reports `configured: true` and
`official_app_3_6_1_accepts: false` without scanning or accessing a Bluetooth
adapter.

The same tool can decode the raw Base64 Android scan record that the Cordova
plugin stores:

```sh
python3 tools/linksys_ble_jnap.py \
  --android-advertisement-base64 '<captured advertisement>'
```

## GATT and BLE-JNAP protocol

Both firmware families and Linksys Android `3.6.1` use:

| Purpose | UUID |
| --- | --- |
| JNAP service | `00002080-8eab-46c2-b788-0e9440016fd1` |
| Control Point | `00002081-8eab-46c2-b788-0e9440016fd1` |
| JNAP Data | `00002082-8eab-46c2-b788-0e9440016fd1` |

The phone subscribes to the Control Point and performs this state machine:

1. write `00 01` (`INIT`) to Control Point;
2. after the matching notification, write `00 02` (`START`);
3. write `00 03` followed by the two-byte big-endian request length;
4. write the full textual JNAP request to JNAP Data;
5. after `04 00 00 00` (`DATA ACK`), write `00 05` (`STOP`);
6. unsubscribe and read the JNAP response from JNAP Data.

The textual request contains `Host`, `X-JNAP-Action`,
`X-JNAP-Authorization`, `Content-Type`, and a JSON object separated by line
feeds. The application-side sender accepts an Action string as a function
argument; the protocol itself is not hard-coded to a finite Action enum.

The same sender nevertheless hard-codes this header for every phone-originated
BLE request:

```text
X-JNAP-Authorization: Basic <base64(admin:admin)>
```

Some call sites pass `auth: "admin"`. That does not replace the header:
`jnap.getAuth("admin")` returns a JavaScript header-mutator function, the sender
places that function in `payload.auth`, and `JSON.stringify` omits function
properties. The transmitted request therefore remains the default setup
credential. This is another independent indication that the official phone
path is setup-only.

Configured-node authorization is supported by the firmware protocol. On the
Master, `bluetooth.btGetSlaveSetupStatus()` explicitly selects the synchronized
raw administrator password for an online Slave, base64-encodes
`username:password`, and passes it to `btsetup_central`. It uses the default
password only before a new Slave has first come online and synchronized. A
custom diagnostic client can therefore construct the same header with the
known synchronized local credential; the official application simply does not
offer that configured-node path.

[`tools/linksys_ble_jnap.py`](../tools/linksys_ble_jnap.py) reproduces this
framing offline and never scans, connects, or writes to a Bluetooth adapter.
For example:

```sh
python3 tools/linksys_ble_jnap.py \
  --action /nodes/setup/GetVersionInfo \
  --payload-json '{"probe":true}' \
  --redact-request
```

It also decodes the official application's recorder files without printing a
captured Basic header:

```sh
python3 tools/linksys_ble_jnap.py --cordova-recording startScan.json
python3 tools/linksys_ble_jnap.py --cordova-recording subscribe.json
python3 tools/linksys_ble_jnap.py --cordova-recording read.json
```

The decoder recognizes Android advertising-data structures, the Linksys
manufacturer tuple, all observed BLE-JNAP control frames, the big-endian
request-length frame, textual JNAP requests, and JSON responses. It redacts the
authorization header and credential-like JSON fields and omits the raw hex for
textual payloads, retaining a SHA-256 only for correlation. This makes a future
real capture immediately comparable with the static protocol model without
turning decoded output into a credential dump.

The redaction option omits the request frame because a real configured-node
frame contains reusable authorization material.

### Link-layer security is still an open live test

The Android sender does not explicitly call a bond or pair API before the GATT
write. MX5300's controller binary imports pairability, authentication, and
security-reestablishment APIs, but imports alone do not prove the characteristic
permissions used at runtime. The phone had HCI snooping disabled, so no
historical exchange is available to settle this offline.

Therefore the offline evidence does **not** establish whether every request is
encrypted at the BLE link layer. No implementation should transmit the
synchronized router Basic credential until a live controller trace confirms
encryption and the expected peer-authentication behavior. A physical-button
authorization window or a one-time challenge is preferable for a custom action
if that property cannot be established.

## What `btsetup` actually does

The Peripheral binary parses:

- `X-JNAP-Action`;
- `X-JNAP-Authorization`; and
- the JSON body.

It then invokes:

```text
/usr/bin/btjnap -a <Action> -u <Authorization> -j <JSON>
```

`btjnap` creates the same CGI environment as a local `/JNAP` request and pipes
the body into `/www/JNAP/index.cgi`. BLE is consequently a JNAP transport, not
an arbitrary shell transport. Registered Action schemas and normal JNAP
authorization still matter.

### Counterintuitive filter default

Both models ship:

```text
btjnap::api_filtering_disabled=1
```

Static cross-reference analysis of `btsetup` confirmed the behavior implied by
that name:

- value `0`: consult `/etc/btjnap.conf` and require a listed CGI/HTTP mapping;
- value `1`: send the received Action directly through `btjnap`.

The shared configuration file lists only 18 setup, Smart Connect, firmware
update, and service-discovery Actions. Because filtering is disabled by
default, that list is **not** the active transport allowlist. This still does
not make an unknown Action work: `/www/JNAP/index.cgi` returns Unknown Action
unless the firmware has registered it.

## Stock BLE capability inventory

The real ARM `btsetup_central --help` was executed under user-mode QEMU against
both extracted root filesystems. Both builds expose:

- discover unconfigured devices;
- diagnostic scan for backhaul-down Slaves;
- connect/disconnect a specific Linksys BLE address;
- `StartSmartConnectClient`;
- `GetDevicePIN`;
- `GetSlaveSetupStatus`, with UUID and JNAP authorization;
- `GetDeviceMode`;
- `GetSmartConnectStatus`;
- reboot the connected device;
- `GetDeviceID`;
- `GetVersionInfo`; and
- `SmartConnectConfigure`.

The official Android application's BLE sender calls only setup-oriented
Actions:

- `core/IsServiceSupported`;
- setup identity, MAC, serial, port, WAN detection, WAN status, and provisioning
  Actions;
- `nodes/smartconnect/GetSmartConnectPIN`;
- `nodes/smartconnect/StartSmartConnectClient`; and
- `nodes/bluetooth/BTSelfDisconnect`.

It also filters configured advertisements before any connection attempt and
always emits the default `admin:admin` authorization header. Merely invoking a
hidden screen in this APK would not turn it into a configured-Node management
client; both the scan policy and credential construction would need to change.

The application's internal `selectParent` state does not steer an existing
mesh Node. During initial setup it chooses which unconfigured physical unit,
usually the one with WAN, will become the initial Parent/Main.

Searches of the application bundle and all registered firmware Actions found no
`change_node_parent`, `pub_bh_config`, `BH/config`, preferred-BSSID setter, or
equivalent exact-Parent BLE call.

## SmartConnect and Bluetooth Auto Onboarding are not Parent steering

Four superficially promising routes were traced through both MX4200 and MX5300:

| Route | Accepted input | Mode gate | Actual purpose |
| --- | --- | --- | --- |
| App `selectParent` state | WAN/cable status of scanned units | setup flow | choose which unconfigured physical unit becomes Main |
| `StartSmartConnectClient` | `setupAP`, `wiredEnabled` | Slave implementation requires `smart_mode == 0` | start initial onboarding |
| `SmartConnectConfigure` | SSID, passphrase, SRP login/password | requires `smart_mode == 0`, then changes it to Slave | provision an unconfigured Node |
| `xconnect/RequestOnboardSmartConnectClient` | one `bssid` | Node must be Master | onboard a non-Node SmartConnect client discovered by the Master |

The last route's `bssid` is easy to misread as a target Parent. It is passed to
`set_smartconnect_connect_client()` on the Master and identifies the client
being onboarded. It never reaches a configured Slave's
`backhaul::preferred_bssid`, `mqttsub::bh_bssid`, or `backhaul::set_intf`.

The corresponding JNAP libraries independently confirm the schemas:
`libjnap_smartconnect.so` contains `setupAP`, `configApSsid`,
`configApPassphrase`, `srpLogin`, and `srpPassword`; only
`libjnap_xconnect.so` contains `bssid`.

The firmware contains a hidden but intentional auto-onboarding mode:

1. a local physical-button/Smart Setup event sets
   `auto_onboarding::bt_enabled=1`;
2. a Master scans for nearby unconfigured Linksys devices;
3. it filters by signal strength and device mode;
4. it creates SRP credentials; and
5. it sends the configured SSID, passphrase, and SRP values with
   `SmartConnectConfigure`.

This can bring a new Node into the mesh. It does not send a target Parent
BSSID or reparent an already configured child. The actual shared
`bt_auto_onboard_start.sh` command is equivalent to:

```text
btsetup_central -f -A <SSID> -P <passphrase> -L <SRP login> -R <SRP password>
```

There is no band, channel, or Parent BSSID option. MX4200 and MX5300 carry the
same `xconnect.lua`, SmartConnect server module, and auto-onboarding script
byte-for-byte; the mode checks and absence of an exact-Parent input are not
model-specific accidents.

## The production exact-Parent path

The already verified IP/MQTT path is:

```text
pub_bh_config <child UUID> <5GL|5GH> <channel> <Parent BSSID>
  -> network/<child UUID>/BH/config
  -> Slave event mqttsub::bhconfig
  -> mqttsub::bh_channel = <channel>
  -> mqttsub::bh_bssid = <Parent BSSID>
  -> backhaul::set_intf = <5GL|5GH>
  -> service_wifi_ext.sh::backhaul_intf_choose()
  -> disconnect current wireless backhaul
  -> reconnect using the requested radio/BSSID
```

The Lua function `nodes.util.steer_node_to_parent()` and the automatic
Tesseract optimizer both invoke `pub_bh_config`, which independently confirms
that this is a production control path.

On the child, `mqttsub::bhconfig` validates:

- channel as one to three decimal digits;
- BSSID as six hexadecimal octets; and
- band as `5GL`, `5GH`, or `AUTO`.

`backhaul_intf_choose()` compares the requested band and BSSID with the current
backhaul. If a change is needed it takes the current interface down, marks the
backhaul down, and clears the current preferred BSSID so the existing Smart
Connect client reconnects using the requested values.

This is a runtime steering request, not a proven permanent Parent pin. Normal
optimization can later move the Node again.

## Why stock BLE cannot select the Parent

| Required part | Stock firmware |
| --- | --- |
| BLE Peripheral on a configured child | Present |
| Generic request/response framing | Present |
| Current-password JNAP authorization path | Present |
| Backhaul-down discovery | Present |
| Child-local exact-BSSID steering consumer | Present |
| JNAP Action accepting exact Parent tuple | **Absent** |

Turning on global Node Steering over BLE is not equivalent: it enables policy,
but does not identify the Parent chosen by the user. Rebooting or invoking
Smart Connect setup also does not provide the tuple.

## Recommended BLE implementation

### Architecture

```text
MeshScope topology
  -> select child UUID and Parent UUID
  -> resolve Parent 5GL/5GH BSSID + current channel from fresh DEVINFO
  -> connect to the selected child BLE address
  -> confirm child identity with GetDeviceID/GetMACAddress
  -> send one authenticated custom Action to that child
  -> child validates tuple and request state
  -> child sets mqttsub::bh_channel and mqttsub::bh_bssid
  -> child triggers backhaul::set_intf
  -> MeshScope observes disconnect/reconnect/new Parent
```

The suggested Action is conceptually:

```text
/nodes/meshscope/SteerToParent
```

with a body such as:

```json
{
  "requestId": "random-one-use-id",
  "expectedDeviceID": "child-device-uuid",
  "parentDeviceID": "selected-parent-uuid",
  "band": "5GH",
  "channel": 149,
  "bssid": "00:11:22:33:44:55"
}
```

The child needs only `band`, `channel`, and `bssid` for the existing reconnect
logic. The IDs prevent a stale UI selection or wrong BLE target from silently
moving another Node and provide an audit key.

### Two implementation variants

#### Proper JNAP module

Add a small Action schema/library plus Lua handler and register it normally.
This gives the cleanest authorization and input-schema behavior, but requires
building the matching `libjnap_*` component for the vendor ABI.

#### Minimal `btjnap` extension

Add a fixed Action namespace to the existing `btjnap` wrapper and a dedicated
dispatcher. Before handling an extension Action, pass the supplied
authorization through the existing privileged `core/CheckAdminPassword` JNAP
Action and require an exact `OK` result. The dispatcher must parse JSON with
`jsonparse`, validate every field, and expose a bounded Action list. It must
never interpolate unvalidated values into a shell command.

The offline reference overlay now implements four Actions:

| Action | Effect |
| --- | --- |
| `GetCapabilities` | Report extension version, semantics, and Action list |
| `GetBackhaulStatus` | Read current/requested backhaul tuples, last request, and rollback state |
| `SteerToParent` | Validate child identity and emit the stock three-sysevent exact-Parent sequence |
| `RollbackParent` | Replay one saved previous 5 GHz tuple, then consume it |

All original Actions fall through to the byte-for-byte stock JNAP path. This
is the useful meaning of “BLE forwarding” here: preserve generic registered
JNAP pass-through while adding a small whitelist of advanced mesh operations,
not an anonymous arbitrary-shell endpoint.

This variant changes less firmware and avoids compiling a new JNAP schema
library, but it duplicates some authorization and schema responsibilities. It
should be treated as a proof-of-concept route, not the final preferred design.

Changing `/etc/btjnap.conf` alone is insufficient: its CGI entries still pass
through the normal registered JNAP dispatcher.

### Fastest one-time feasibility experiment

For a disposable proof rather than a long-lived feature, the routes rank as
follows:

1. **Child shell/UART control experiment:** set
   `mqttsub::bh_channel`, `mqttsub::bh_bssid`, then
   `backhaul::set_intf`. This proves the child-local radio transition with no
   BLE variable.
2. **Master shell control experiment:** run stock `pub_bh_config` with child
   UUID plus the Parent tuple. This proves the complete production
   Master-to-Slave path.
3. **BLE proof:** add the fixed MeshScope Action interception to the existing
   `/usr/bin/btjnap` shell wrapper. Query capabilities/status first; then
   validate `expectedDeviceID`, `parentDeviceID`, `band`, `channel`, and
   `bssid`, verify the supplied Basic header with stock
   `/core/CheckAdminPassword`, set the same three sysevents, and return an
   accepted response before the backhaul transition. Preserve the previous
   valid tuple for one rollback.

Route 3 is the smallest exact-Parent BLE change because `btjnap` is already the
same shell script on both models. It avoids compiling a vendor-ABI JNAP library
and does not need to alter `btsetup`, the GATT service, MQTT, or the official
application. A custom Action can be intercepted before the normal JNAP
dispatcher would reject it.

The reviewed, offline-only reference helper and its single stock-wrapper hook
are in
[`firmware-overlays/ble-parent-steering`](../firmware-overlays/ble-parent-steering/README.md).
Unit tests replace JNAP, `jsonparse`, `syscfg`, and `sysevent` with local
mocks. They verify that bad authorization, a wrong child UUID, or malformed
tuples produce no event; a valid tuple writes exactly the three expected
events in order; a mutation lock rejects concurrent requests; status preserves
both current and requested data; and rollback replays the previous tuple once.
The patch dry-runs against extracted MX4200 and MX5300 wrappers, and each
model's ARM BusyBox accepts the dispatcher syntax under user-mode QEMU.

For an extremely short owner-controlled test, authorization could technically
be omitted in the custom interception. That would not answer any additional
BLE feasibility question and would make the image needlessly reusable by any
nearby peer, so the stock `CheckAdminPassword` round-trip is the preferable
minimal proof even when long-term hardening is out of scope.

### Required safety properties

Before the custom action is enabled, its handler and MeshScope must:

1. require the synchronized local admin credential or a stronger one-time
   authorization mechanism;
2. confirm the BLE peer's Device ID equals the selected child;
3. allow only `5GL` or `5GH`;
4. check the channel against the radio and regulatory domain;
5. validate and lowercase the BSSID;
6. confirm fresh topology data maps that BSSID back to the selected Parent;
7. reject self, current Parent, descendants, wired children, and offline target
   Parents;
8. serialize requests with a lock and rate limit;
9. snapshot the previous Parent tuple;
10. return “accepted” before scheduling the reconnect;
11. observe the resulting Parent rather than trusting command exit status; and
12. offer one rollback attempt using the saved tuple.

For a backhaul-down rescue, the last known tuple may be stale. The BLE extension
should therefore also expose a read-only status Action containing current
backhaul band, upstream BSSID, state, and last request result. This preserves
the same amount of diagnostic data when IP reachability is lost.

### Transport choice

| Route | Stock firmware | Works with child IP down | Exact Parent | Assessment |
| --- | --- | --- | --- | --- |
| Master `pub_bh_config` over existing infrastructure MQTT | Yes, from privileged local code | No | Yes | Production primitive; shortest after SSH/bootstrap |
| Direct BLE with stock Actions | Yes | Partly | No | Useful discovery/reboot/setup only |
| Direct child BLE plus one custom authenticated Action | Custom image | Yes | Yes | Best BLE design |
| Temporarily make Master a BLE Peripheral | Custom image | Not relevant | Indirect | More moving parts; not recommended |
| Broaden anonymous MQTT ACL | Custom image | No | Yes | Excessive authority; avoid |

## Concrete next experiment when routers are online

The routers were not physically present during the Pixel inspection, so no
advertisement, GATT properties, encryption state, or real BLE-JNAP response
could be captured. The next in-range session should use one recoverable
non-gateway Node:

1. capture its advertisement in normal and backhaul-down states;
2. confirm Service/Control Point/JNAP Data properties;
3. verify whether the BLE connection is encrypted and authenticated before any
   configured-node credential is sent;
4. send only `GetDeviceID` and `GetVersionInfo`;
5. compare the response bytes with
   `tools/linksys_ble_jnap.py`;
6. test a custom read-only status Action;
7. test steering to the Node's current Parent as a no-op;
8. finally test one alternate Parent with a saved rollback tuple.

Until steps 1–5 are complete, this document establishes firmware and
application-level feasibility, not a live-tested BLE steering feature. The
absence of a capture is an environmental boundary, not evidence for or against
configured-Slave connectivity.

## Final assessment

BLE is useful specifically because the firmware is designed to keep every
configured Slave in the Peripheral role even when its IP backhaul is down. If
the next in-range scan confirms the static path on the analyzed hardware, it is
a stronger recovery/control transport than exposing the infrastructure MQTT
broker to the LAN.

The missing piece is not a hidden radio command. It is one authenticated,
strictly bounded call from the BLE-JNAP dispatcher to the existing
child-local `backhaul::set_intf` consumer. The recommended end state is:

```text
select child + Parent in MeshScope
  -> resolve and validate live Parent radio tuple
  -> direct BLE request to child
  -> reuse stock child reconnect logic
  -> observe and, if necessary, roll back
```

Related analyses:

- [MX4200 SSH bootstrap and exact-Parent control](mx4200-ssh-parent-control.md)
- [MX4200 MQTT and custom-IMG feasibility](mx4200-mqtt-and-custom-img.md)
- [MX5300 MQTT and custom-IMG feasibility](mx5300-mqtt-and-custom-img.md)
- [Hidden firmware interfaces and Parent steering](hidden-firmware-interfaces.md)
