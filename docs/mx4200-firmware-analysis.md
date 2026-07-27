# MX4200 1.0.13.210200 firmware analysis

Analysis date: 2026-07-27

This is a static, offline and isolated user-mode analysis of
`FW_MX4200_1.0.13.210200_prod.img`. macOS denied terminal access to the
OneDrive file-provider directory, so the inspected input was the same-named
local copy in `~/Downloads`. Its SHA-256 is recorded below so the OneDrive
object can be compared later. Neither copy was modified.

Firmware binaries and extracted files are ignored by Git and must not be
published with MeshScope. No request was sent to a router, and none of the
discovered control operations was invoked.

## Result

The MX4200 image independently confirms the exact-Parent backhaul mechanism
previously found in WHW03 firmware:

```text
nodes.util.steer_node_to_parent
  -> pub_bh_config <child UUID> <5GL|5GH> <channel> <target BSSID>
  -> network/<child UUID>/BH/config
  -> mqttsub::bhconfig on the selected child
  -> backhaul reconnect to the requested Parent radio
```

The important cross-model difference is that this build has no
`/usr/bin/topomgmt` and no `nodes/topology/steerer.lua`. The reusable Lua
function, publisher, subscriber, backhaul consumer, and automatic optimizer
are all present. Therefore the capability exists, but an eventual SSH or
narrow authenticated helper must call `nodes.util.steer_node_to_parent` or
`pub_bh_config` directly rather than relying on the WHW03 `topomgmt` wrapper.

This image also contains explicit proof that `core/Reboot` sent directly to a
specific Node IP reboots that Node: `/usr/sbin/reset_slave_nodes` implements
its `-i <IP> -B` path by POSTing an empty `core/Reboot` request to that IP's
`/JNAP/` endpoint. This validates MeshScope's current selected-Node restart
design more strongly than the UI label alone.

No ordinary administrator JNAP action, hidden web page, stock test server, or
anonymous MQTT route was found that exposes the exact Parent tuple. The
firmware still contains the same incomplete SSH scaffold as WHW03, so a
controlled Dropbear bootstrap remains the most plausible future command
transport.

## Image identity

| Field | Value |
| --- | --- |
| Model | MX4200 |
| Firmware | 1.0.13.210200 |
| Product | `chiron` |
| File size | 37,879,808 bytes (`0x2420000`) |
| SHA-256 | `403ec9275b0d610e9c572fe927824c72bc075669367e52891dedc4ac60ebaf4d` |
| Architecture | 32-bit little-endian ARM, EABI5 |
| Userspace ABI | uClibc, interpreter `/lib/ld-uClibc.so.0` |
| Kernel description | `ARM OpenWrt Linux-4.4.60` |
| Build timestamp | 2022-04-13 23:51:38 UTC (SquashFS timestamp) |

The final 256-byte Linksys footer contains:

```text
.LINKSYS.01000407MX4200         34B8A0E70       K0000000F03A82BC
```

The POSIX `cksum` of every byte except the footer is decimal `884515047`,
hexadecimal `34B8A0E7`, matching the checksum field.

## Container layout

| Range | Contents |
| --- | --- |
| `0x0000000..0x03A82BC` | FIT containing kernel and device tree |
| `0x03A82BC..0x0600000` | zero padding |
| `0x0600000..0x2400000` | UBI image, 240 physical erase blocks |
| `0x2400000..0x241FF00` | padding |
| `0x241FF00..0x2420000` | 256-byte Linksys footer |

The FIT contains CRC32 and SHA-1 hash nodes for the kernel and FDT. It has no
FIT signature node. The root filesystem is outside the FIT.

UBI/SquashFS parameters:

| Parameter | Value |
| --- | --- |
| PEB size | 131,072 bytes |
| LEB size | 126,976 bytes |
| Minimum I/O | 2,048 bytes |
| Volume | dynamic `squashfs`, ID 0, autoresize |
| Volume blocks embedded in image | 238 data LEBs |
| Reserved PEBs | 603 |
| SquashFS | version 4, XZ, 262,144-byte blocks |
| Exact SquashFS size | 30,096,914 bytes |
| Capacity of embedded data LEBs | 30,220,288 bytes |
| Embedded margin after filesystem | 123,374 bytes |

This build's rootfs margin is much tighter than the WHW03 UBIFS image. Adding a
normal SSH binary without changing compression or the number of embedded PEBs
is unlikely to fit. The live flash partition size must be read before assuming
that an expanded UBI image is safe.

## Exact Parent steering evidence

### Entry point retained without `topomgmt`

`/usr/local/lib/lua/5.1/nodes/util/init.lua` contains:

```lua
function steer_node_to_parent(opts)
   local fields = { "uuid", "band", "channel", "bssid" }
   validate_fields(opts, fields)
   local cmd = "pub_bh_config %s %s %s %s > /dev/null"
   return os.execute(cmd:format(
      opts.uuid, opts.band, opts.channel, opts.bssid))
end
```

The function was loaded under the image's own Lua 5.1 interpreter using ARM
user-mode QEMU in an isolated network namespace. It resolved as a live
function; it was not called.

The following files are byte-identical to the WHW03 2.1.19 versions:

| File | SHA-256 |
| --- | --- |
| `/usr/sbin/pub_bh_config` | `afbfa610ecbb5347e480d9a73ba2d2dcaa0bbe8710f32e98ee3bf61e59beda68` |
| `nodes/util/commander.lua` | `4d8aa242c904135796d6779de30717217d71798480515b83976d81b78da74b5b` |
| `nodes/tess/optimizer.lua` | `426fda134a9dd90173cdd15113ac09c58637534759cca9130df58dd64ff23d14` |

The automatic Tesseract optimizer invokes the same publisher. This shows that
`pub_bh_config` is active production plumbing rather than an abandoned helper.

### Child-specific message and consumer

`pub_bh_config` validates the child UUID and publishes a `set` payload with
`band`, `channel`, and `bssid` to:

```text
network/<child UUID>/BH/config
```

`/etc/subscriber.d/slave.subs` maps the selected child's topic to
`mqttsub::bhconfig`. `/etc/init.d/service_node-mode.sh` validates the channel
and MAC address, writes `mqttsub::bh_channel` and `mqttsub::bh_bssid`, and
selects `5GL`, `5GH`, or `AUTO` through `backhaul::set_intf`.

`smart_connect_client_monitor.sh` first tries the exact requested BSSID and
channel through `MQTT_BACKHAUL_SELECTOR`. If that fails, it falls back to the
ordinary automatic selector. On success it records the preferred BSSID and
channel in runtime `sysevent` state.

As on WHW03, this is a best-effort **Steer now** operation, not evidence of a
permanent Parent pin.

### The public read API is deliberately incomplete

`nodes/setup/GetSelectedChannels` reads each DEVINFO record's BSSID to decide
whether a radio exists, but returns only:

```text
deviceID, radioID, band, channel
```

It omits the BSSID needed for exact Parent selection. The correct target tuple
still has to come from trusted live DEVINFO data, for example:

```text
data.userAp5GL_bssid
data.userAp5GL_channel
data.userAp5GH_bssid
data.userAp5GH_channel
```

The browser must never supply a free-form BSSID/channel. A future backend must
resolve the selected Parent to these fields and verify the tuple maps back to
the requested Parent UUID.

## MQTT and alternate server boundary

The MX4200 and WHW03 copies of `strict.acl`, `moderate.acl`, and the Mosquitto
readme are byte-identical.

- The anonymous listener's strict ACL does not permit writes to
  `network/+/BH/config`; the bundled readme explicitly treats Node steering as
  a dangerous command.
- The secure listener supports TLS-PSK plus messaging username/password.
  `omsgd -h` and `subscriber -h` confirm PSK identity/key options.
- Those internal Smart Connect credentials are not exposed by an ordinary
  local admin JNAP login.

The image includes `lsc_server`, `sct_server`, `sectrans_server`, `omsgd`,
`subscriber`, Lighttpd, and Mosquitto tools. Selected `-h` paths were executed
under user-mode QEMU with networking isolated. The stock `sct_server`
successfully reached `LISTEN 0.0.0.0:16061` inside that namespace, proving the
binary is runnable.

The stock secure-configuration template only exposes selected settings,
automatic Tesseract policy, and fixed events. It does not contain the exact
Parent tuple operation. Starting it manually would therefore be a way to
package already-authorized shell access, not a bridge from the administrator
web password to Parent steering.

Other apparent candidates also stop short:

| Candidate | Finding |
| --- | --- |
| `nodes/diagnostics/UploadSysinfoData` | Publishes a fixed diagnostic-upload request and triggers local `sysinfo::upload`; it does not accept a command. |
| `httpproxy/AddHttpProxyRule` | Creates bounded forwarding rules with ACLs and optional session ownership; it is not an HTTP request primitive or shell. |
| Anonymous MQTT 1883 | ACL blocks the exact steering topic. |
| Secure MQTT 8883 | Technically carries the operation, but requires separate internal Mesh credentials. |
| Public JNAP/UI | Server inventory contains 325 registered actions; none accepts child UUID plus Parent band/channel/BSSID. |
| `/ca` | Loads the current child Node's local support UI; it does not add a Parent steering action. |

## Selected-Node reboot and `/ca`

`JNAP/modules/core_server.lua` implements `core/Reboot` with no request input:

```lua
local function Reboot(ctx)
    local device = require('device')
    local sc = ctx:sysctx()
    local error = device.reboot(sc)
    return error or 'OK'
end
```

The decisive additional evidence is `/usr/sbin/reset_slave_nodes`. Its help
offers:

```text
-B          Reboot slaves
-i {IP}     Reset just one slave at {IP}
```

For that single-IP reboot path it sends `core/Reboot` with an empty body
directly to `https://<selected IP>/JNAP/`. This is the same targeting model
used by MeshScope: the HTTP destination selects the Node; the JNAP payload
does not need a `deviceID`.

The MX4200 Lighttpd configuration also explicitly says that a Slave redirects
root requests to the Master but keeps `/CA` local:

```text
^/(CA|ca)$ -> /ui/local/dynamic/index.html
```

The bootstrap preserves `#casupport`. Thus `https://<node-ip>/ca` is the
correct local support entry on this build as well.

## SSH scaffold

The image contains the same intended-but-incomplete SSH plumbing as WHW03:

- Dropbear RSA and DSS private host-key files;
- `/etc/registration.d/15_ssh_server`, which names
  `/etc/init.d/service_sshd.sh`;
- `/dev/pts` initialization documented for SSH;
- `/var/config/run_scripts` as a developer boot hook; and
- `service_init.sh`, which synchronizes the encrypted local HTTP administrator
  password hash to both `root` and `admin`.

It contains no `dropbear`, `dropbearmulti`, `sshd`, `ssh`, `scp`, or
`sftp-server` executable, and no `service_sshd.sh`.

The embedded factory host keys are public because they ship in the firmware
image and must never be reused. A future bootstrap should generate a unique
key per Node, store it in persistent `/var/config`, use public-key-only access
initially, and bind only to the LAN/mesh management side.

Because MX4200 and WHW03 both use ARM EABI5/uClibc, one carefully built
Dropbear payload may be ABI-compatible with both families. Library versions
and symbols still have to be checked before sharing a dynamic build.

## Firmware acceptance and A/B behavior

`/etc/fwcaps.sig` is absent and `/etc/fwcaps.force` is present. The updater
calls `fwcc verify_signature`; on this image the Linksys-footer validation path
checks footer magic and the POSIX checksum described above. A modified image
may therefore be accepted after a correct repack and footer update, but this
does not make flashing safe.

The scripts inspect `fwup_boot_part`, select `kernel` or `alt_kernel`, erase
the target, and use `nandwrite -p` for NAND. They also contain an eMMC path and
`switch_boot_image`, which changes U-Boot `boot_part` between 1 and 2. This is
consistent with A/B firmware slots.

Before any modified-image experiment, live read-only inspection must still
record:

- `/proc/mtd` or the eMMC partition map and exact slot sizes;
- the active slot and bootloader variables;
- retry/fallback behavior;
- the actual UBI volume size on the target hardware; and
- a serial/UART recovery route.

No modified or flashable image was produced during this analysis.

## Cross-model matrix

| Capability | MX4200 1.0.13.210200 | WHW03 2.1.19.215389 |
| --- | --- | --- |
| Kernel | Linux 4.4.60 | Linux 3.14.77 |
| Rootfs in UBI | SquashFS/XZ | UBIFS/zlib |
| ARM/uClibc ABI family | Yes | Yes |
| `topomgmt` CLI | Absent | Present |
| `nodes.topology.steerer` | Absent | Present |
| `nodes.util.steer_node_to_parent` | Present | Present |
| `pub_bh_config` | Present; byte-identical | Present |
| Child `BH/config` consumer | Present | Present |
| Automatic optimizer uses publisher | Present; byte-identical | Present |
| Secure/anonymous MQTT ACL design | Present; byte-identical | Present |
| Direct-IP selected-Node reboot helper | Present | Present |
| `/ca` local Node route | Present | Present |
| Incomplete SSH scaffold | Present | Present |
| Stock test-server Parent bridge | Not found | Not found |

## Implementation consequence for MeshScope

The earlier warning that the exact MX family needed confirmation is now
resolved for MX4200 1.0.13.210200: the full data plane for an exact Parent
request exists. What remains missing is a safe authenticated transport from
MeshScope and the protective state machine around it.

The feature should remain disabled until all of these are implemented:

1. obtain a legitimate LAN-only shell/helper transport;
2. resolve Parent radio tuples server-side from fresh DEVINFO;
3. reject self, current Parent, offline/wired targets, and the child's full
   descendant subtree;
4. serialize mutations and enforce a cooldown;
5. observe the new Parent from fresh backhaul data rather than command exit;
6. define bounded timeout and recovery behavior; and
7. first test only a non-critical leaf Node with the original tuple recorded.

The selected-Node restart feature does not need this Parent-steering transport;
the firmware explicitly validates its existing direct-JNAP implementation.
