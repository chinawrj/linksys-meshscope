# MX5300 MQTT, exact-Parent control, and custom-IMG feasibility

Analysis date: 2026-07-28

This is an offline analysis of the official MX5300 v1 image
`1.1.12.210066`. No request was sent to a router and no image was flashed.
The proof IMG, private key, extracted firmware, and rebuilt UBIFS/UBI are
ignored by Git and must not be published.

## Conclusions

1. **Exact-Parent steering already exists in the MX5300 firmware.** Unlike
   the newer MX4200 `topomgmt -> steerer.change_node_parent` wrapper, MX5300
   uses the older direct path:

   ```text
   pub_bh_config
     -> network/<child UUID>/BH/config
     -> mqttsub::bhconfig
     -> backhaul_intf_choose
     -> disconnect current wireless backhaul
     -> reconnect toward requested band/channel/BSSID
   ```

   This is a preferred reconnect request, not a proven persistent topology
   pin. The physical mesh can still reject or later replace the requested
   Parent.
2. **A persistent MQTT ACL change can make that operation directly
   reachable, but editing the stock anonymous listener is unsafe.** The
   minimum rule is `topic write network/+/BH/config`. A dedicated TLS,
   authenticated listener with a narrow ACL is the acceptable direct-MQTT
   design. A small SSH helper remains easier to audit.
3. **An ACL change cannot enable SSH.** It grants access only to MQTT
   consumers that already exist. MX5300 has an SSH service-registration
   scaffold but does not ship Dropbear or the referenced service script.
4. **A key-only SSH OEM-style IMG is buildable and passes the stock updater's
   offline validation.** A proof image was rebuilt as UBIFS, UBI, and Linksys
   IMG, extracted again, and accepted by the stock
   `/usr/sbin/fwcc verify_signature`.
5. **Updater acceptance is not hardware boot proof.** NAND geometry,
   bootloader behavior, inactive-slot selection, device revision, firewall
   behavior, and recovery still require read-only live inspection and a
   controlled physical test with UART available.

## Firmware identity

The analyzed local file is:

```text
FW_MX5300_1.1.12.210066_prod.img
```

| Field | Value |
| --- | --- |
| Product | `bronx` |
| Product type | `production` |
| Version | `1.1.12.210066` |
| Original size | 60,686,336 bytes |
| Original SHA-256 | `f51adff1395bb9d1b7f8d62fd080ba5c9f5e5589b6d22f92335792ba3261d651` |
| FIT start | `0x000000` |
| FIT total size | `0x3B4EFC` |
| UBI start | `0x600000` |
| Footer size | 256 bytes |

Linksys still lists `1.1.12.210066`, dated 2022-04-14, as the latest MX5300 v1
firmware. The official manual-update article also documents opening a
specific Parent or child Node by its IP address and appending `CA`.

## Linksys IMG container

The first `0x600000` bytes contain the original FIT kernel and DTBs. The UBI
starts on a 6 MiB boundary. A one-PEB erased trailer ends with a 256-byte
Linksys footer whose first 64 bytes are:

```text
.LINKSYS.01000407MX5300         08733B550       K0000000F03B4EFC
```

Relevant fields are:

- magic: `.LINKSYS.`;
- SKU: `MX5300`;
- image checksum: `08733B55`;
- rootfs/FIT-size field: `03B4EFC`; and
- an eight-character uppercase POSIX `cksum` value computed over every byte
  before the footer.

The checksum was independently reproduced. Stock `fwcc` extracts the final
256 bytes, checks the magic, recalculates `cksum` over the body, and compares
the eight hexadecimal characters. Its code contains a model/SKU check, but
that check is commented out.

This firmware contains `/etc/fwcaps.force` with `force_update:2` and has no
`/etc/fwcaps.sig`. Therefore `fwcc verify_signature` selects the Linksys
footer path instead of GPG image verification. This is a property of this
exact build, not a promise about other versions or regions.

## UBIFS and UBI geometry

The official image has:

| Field | Value |
| --- | --- |
| Original UBI SHA-256 | `83c4471a2275f599c8fd0d4f231ba4dede03d3a63925c0c5da302c1b5532e9fc` |
| UBI PEBs | 414: two layout plus 412 data |
| PEB / LEB | 131,072 / 126,976 bytes |
| Minimum I/O | 2,048 bytes |
| VID-header offset | 2,048 bytes |
| UBI image sequence | `1038325328` |
| Volume | ID 0, dynamic `ubifs`, autoresize |
| Reserved LEBs | 603 |
| UBIFS format | v4 |
| Default compression | zlib |
| Fanout | 8 |
| Maximum LEB count | 615 |
| Log / LPT / orphan LEBs | 5 / 2 / 1 |
| Maximum journal size | 8 MiB |
| Original total-used value | 49,137,496 bytes |

A root-owned extraction contained 5,141 filesystem entries. Rebuilding the
unchanged tree with:

```sh
mkfs.ubifs \
  -r ubifs-root \
  -m 2048 \
  -e 126976 \
  -c 615 \
  -x zlib \
  -F \
  -o baseline.ubifs
```

reproduced the official structural values, including 412 LEBs, format v4,
zlib, fanout 8, space-fixup, and the exact 49,137,496-byte `total_used`
value. A byte-identical UBIFS is not expected because the rebuild creates new
filesystem UUID and journal metadata.

## MQTT architecture

MX5300 ships ARM Mosquitto `1.6.2`, started only on the Master. Its principal
files are byte-identical to the analyzed MX4200 build:

| File | SHA-256 |
| --- | --- |
| `/etc/mosquitto/mosquitto.conf` | `e626f78748f628e1f7be9cb3aec823fb946511540ed782a6239d891b9e978587` |
| `/etc/mosquitto/strict.acl` | `2a3cc39225e2254e33ef56744ecfbeaab6ead1e62d668753226933814a95cebd` |
| `/lib/libnodes_psk_auth_plugin.so` | `67225661fbb04b7fc3d6d9d927cb5978bd2792808550af36da3f385be0f261a5` |

| Listener | Binding | Authentication | ACL |
| --- | --- | --- | --- |
| `1883` | Master `br0` address | Anonymous | `/tmp/etc/mosquitto/strict.acl` |
| `1883` | localhost | Anonymous | No ACL restriction |
| `8883` | Configured mesh addresses | TLS-PSK plus username/password | Authentication plugin permits authenticated Topics |

`per_listener_settings true` makes these settings listener-specific.
`service_mosquitto.sh` copies `/etc/mosquitto` into
`/tmp/etc/mosquitto` on startup and substitutes live values. Consequently,
editing `/tmp` is temporary; a persistent ACL or listener needs a rootfs
change or another privileged startup hook.

The secure listener's values come from internal mesh state:

```text
omsg::psk_id
omsg::psk
smart_connect::auth_login
smart_connect::auth_pass
```

They are not the local web-admin password and must never be returned to a
browser. Disassembly of `libnodes_psk_auth_plugin.so` shows that its ACL
callback returns success after optional logging, so an authenticated 8883
client is not Topic-limited by that plugin.

## Exact-Parent control chain

### Producer

`/usr/sbin/pub_bh_config` creates an infrastructure payload and publishes:

```text
network/<child UUID>/BH/config
```

The stock MX5300 Lua runtime was executed under QEMU and generated this
payload shape:

```json
{
  "uuid": "CHILD-DEVICE-UUID",
  "type": "set",
  "TS": "CURRENT-UTC-TIMESTAMP",
  "data": {
    "band": "5GL",
    "bssid": "02:11:22:33:44:55",
    "channel": "36"
  }
}
```

`5GH`, `5GL`, and `AUTO` are accepted band values. The Topic UUID selects the
subscriber; the payload UUID should match it.

### Consumer

`/etc/subscriber.d/slave.subs` registers
`network/%uuid/BH/config`, maps it to `BH/%2/config`, and raises
`mqttsub::bhconfig`. `/etc/init.d/service_node-mode.sh` then:

- validates a one-to-three-digit channel;
- validates the BSSID as a MAC address;
- validates `5GL`, `5GH`, or `AUTO`;
- stores the requested backhaul channel and BSSID in runtime state; and
- triggers `backhaul::set_intf`.

`/etc/init.d/service_wifi/service_wifi_ext.sh` handles that event in
`backhaul_intf_choose`. It compares the requested band and BSSID with the
current upstream, brings the current wireless backhaul interface down, marks
the backhaul down, and lets the monitor reconnect with the requested
BSSID/channel as its preferred target.

MX5300 contains neither the MX4200 `topomgmt` binary nor the
`steerer.change_node_parent` Lua method. The direct MQTT/sysevent chain is its
equivalent implementation.

### Required MeshScope safety checks

The firmware consumer validates syntax, not graph safety. A future MeshScope
operation still must:

- fetch fresh trusted DEVINFO immediately before the mutation;
- identify both Nodes by UUID and normalize all radio BSSIDs;
- reject self, current Parent, wired targets, descendants, and stale targets;
- verify the requested `5GL`/`5GH` BSSID and channel belong to the Parent;
- serialize one topology mutation at a time;
- capture the previous Parent/radio/channel tuple;
- observe disconnect, reconnect, and the resulting Parent; and
- perform at most one rollback before stopping.

The operation should be presented as a steering attempt with observed result,
not as a permanent Parent assignment.

## MQTT exposure choices

### Stock anonymous ACL: technically works, unsafe

The minimum persistent ACL addition is:

```text
topic write network/+/BH/config
```

The actual MX5300 ARM broker was run under QEMU with that one added rule:

- QoS-1 publish to `network/child-uuid/BH/config` was accepted;
- publish to `network/child-uuid/AC/config` was denied by the broker with
  reason `0x87`; and
- the broker remained anonymous.

This gives every LAN client authority over every child Node and is unsuitable
outside an isolated lab.

### Stock secure listener: encrypted, over-privileged

The existing 8883 listener can transport the request when the legitimate
internal mesh credentials are available. Its authentication plugin does not
enforce a narrow Topic ACL, so exporting those credentials would give
MeshScope substantially broader authority. They must not be embedded in
JavaScript or stored in browser local storage.

### Dedicated listener: recommended direct-MQTT design

A defensible direct-MQTT listener needs:

- a separate port and MeshScope-specific credential;
- TLS and client authentication;
- `allow_anonymous false`;
- a source-IP firewall allowlist; and
- a dedicated ACL with only the required Topics.

For example:

```text
user meshscope
topic write network/+/BH/config
topic read network/+/BH/status
topic read network/+/DEVINFO
```

The stock ARM broker was also tested with a dedicated password file and this
narrow ACL:

- unauthenticated connection received CONNACK `Not authorized`;
- the test account could publish `BH/config`; and
- the same account was denied on `AC/config`.

The credential was throwaway and remains only in the analysis VM. A physical
deployment must additionally prove TLS and firewall isolation.

## SSH scaffold and proof image

The stock rootfs contains ARM ELF registration helper:

```text
/etc/registration.d/15_ssh_server
```

It registers:

```text
lan-status|/etc/init.d/service_sshd.sh
wan-status|/etc/init.d/service_sshd.sh
service:sshd
```

But `/etc/init.d/service_sshd.sh` and a Dropbear executable are absent. Old
static RSA and DSS host-key files exist, but a secure implementation must not
reuse baked, shared host keys.

The proof rootfs adds only:

```text
/usr/sbin/dropbearmulti
/usr/sbin/dropbear -> dropbearmulti
/usr/sbin/dropbearkey -> dropbearmulti
/etc/init.d/service_sshd.sh
/etc/meshscope-ssh/authorized_keys
```

The 247,784-byte ARM/uClibc Dropbear multicall binary has:

```text
SHA-256 e670aec336b9c7944ed51e03469d360623ea37efacde2950832ded8486fc8e61
```

It executed against the MX5300 uClibc under QEMU and identified itself as
Dropbear `2026.93`. The service script has:

```text
SHA-256 51722126df5c9ccbb2e7a3f294c3f864c9dd4d828fa1b6da2d307f4d3d64947a
```

The handler:

- waits for `br0`;
- copies the operator public key into persistent `/var/config`;
- creates a unique Ed25519 host key on first start;
- binds Dropbear only to the LAN bridge;
- disables password login and local/remote forwarding;
- uses a PID file; and
- handles service start, stop, restart, and LAN-status events.

The proof operator key fingerprint is:

```text
SHA256:vur3k4hKIU+7JwOeUShiZeUOdtyM6gqBv50uGGn/C7I
```

Its private key remains in the analysis VM and is not in Git. A real build
must inject the intended operator's public key; no universal public image
should contain a shared authorized key, host key, default password, or
private key.

## Rebuild procedure

### Build UBIFS

The modified tree was rebuilt with the geometry-preserving command:

```sh
mkfs.ubifs \
  -r modified-root \
  -m 2048 \
  -e 126976 \
  -c 615 \
  -x zlib \
  -F \
  -o mx5300-meshscope-ssh.ubifs
```

The result is:

| Field | Value |
| --- | --- |
| UBIFS size | 52,441,088 bytes |
| UBIFS SHA-256 | `bf2c72ab39cdfe23f86dd9bc28c019b6f3aae9c9a1ba676f414f17e838171de3` |
| LEB count | 413 |
| Total-used value | 49,284,216 bytes |

All original format, compression, fanout, journal, and maximum-LEB settings
are preserved.

### Build UBI

The volume file is:

```ini
[ubifs]
mode=ubi
image=mx5300-meshscope-ssh.ubifs
vol_id=0
vol_type=dynamic
vol_name=ubifs
vol_size=76566528
vol_flags=autoresize
```

The matching command is:

```sh
ubinize \
  -o mx5300-meshscope-ssh.ubi \
  -p 131072 \
  -m 2048 \
  -s 2048 \
  -O 2048 \
  -Q 1038325328 \
  ubinize.ini
```

The resulting 54,394,880-byte UBI has 415 PEBs: two layout plus 413 data. Its
SHA-256 is:

```text
25954f50285efb4ae9b751e99a1dbe0d7204426271e66ad94767337d1fb6c83b
```

The original image sequence, volume ID/type/name, alignment, autoresize flag,
and 603-LEB reservation were preserved.

### Reassemble the Linksys IMG

MeshScope's offline-only assembler supports both MX4200 and MX5300 and
requires an explicit expected SKU:

```sh
python3 tools/linksys_mx_repack.py \
  --original FW_MX5300_1.1.12.210066_prod.img \
  --ubi mx5300-meshscope-ssh.ubi \
  --output FW_MX5300_1.1.12.210066_meshscope-ssh-poc.img \
  --expected-sku MX5300 \
  --expected-original-sha256 \
    f51adff1395bb9d1b7f8d62fd080ba5c9f5e5589b6d22f92335792ba3261d651
```

The tool:

1. verifies the optional original SHA-256;
2. requires a supported, exact footer SKU;
3. validates the existing Linksys magic and POSIX checksum;
4. validates every replacement UBI EC header;
5. preserves the original `0x600000`-byte FIT prefix;
6. appends an erased trailer PEB;
7. preserves all non-checksum footer metadata;
8. recalculates the footer checksum; and
9. writes the output atomically without any router or MTD operation.

The proof output is:

| Field | Value |
| --- | --- |
| Size | 60,817,408 bytes |
| SHA-256 | `0e468615888a3a53580413ac92a088b61d9f54afcbc54a02650b24d3d2873c94` |
| UBI PEBs | 415 |
| Footer checksum | `633A56BA` |

This exact proof output is intentionally not committed.

## Validation performed

The offline proof was checked through independent layers:

1. Feeding the unmodified official UBI through the generic assembler
   reproduced the official IMG byte-for-byte, including its SHA-256.
2. The proof IMG's first `0x600000` bytes match the official FIT prefix
   byte-for-byte.
3. UBI Reader parsed all 415 proof PEBs and extracted the `ubifs` volume.
4. Re-extraction recovered exact Dropbear and service-script hashes,
   multicall symlink targets, `root:root` ownership, intended modes, and the
   expected Ed25519 public-key fingerprint.
5. After excluding only the six injected filesystem paths, all 5,140 original
   filesystem entries matched the official extraction in type, mode,
   UID/GID, symlink target, and regular-file SHA-256: zero differences.
6. The OEM BusyBox shell accepted the service script with `sh -n`.
7. The actual stock MX5300 `/usr/sbin/fwcc verify_signature` returned `0`
   for both the official IMG and the proof IMG.
8. After changing one byte in the proof body without updating the footer,
   the same `fwcc` returned `2`.

These tests prove container and updater acceptance offline. They do not
execute the kernel, mount the modified rootfs on MX5300 hardware, verify the
bootloader's exact NAND handling, or prove network reachability of Dropbear.

## Upgrade and A/B recovery path

The authenticated local update page is:

```text
https://<node-ip>/fwupdate.html
```

The child Node's Lighttpd rule redirects only the exact `/` path to the
Master. `/CA` and `/fwupdate.html` remain local, which supports a targeted
child-Node update after authenticating directly to that Node. This matches
Linksys's documented child-IP plus `CA` support entry.

Static call tracing shows:

```text
/www/fwupdate.html
  -> /jcgi/ updatefirmware
  -> update <uploaded IMG>
  -> fwcc verify_signature
  -> erase inactive aggregate slot
  -> nandwrite -p whole IMG
  -> switch_boot_image
  -> reboot
```

The stock updater chooses `alt_kernel` when booted from part 1 and `kernel`
when booted from part 2. `switch_boot_image` toggles `boot_part` between 1
and 2 and sets `boot_part_ready=3`.

OpenWrt's current MX5300 DTS independently documents:

- each aggregate `kernel`/`alt_kernel` slot as 150 MiB;
- each rootfs beginning 6 MiB into that slot; and
- each rootfs allocation as 144 MiB.

OpenWrt's Linksys MX pre-upgrade logic also toggles `boot_part`, sets
`boot_part_ready=3`, and enables `auto_recovery=yes`. These sources support
the A/B interpretation, but live values must still be read from the exact
router before flashing.

## Required physical checklist

Before any custom-image experiment:

1. verify the label and JNAP identity say MX5300 hardware v1;
2. connect over Ethernet, disconnect WAN, and isolate the test Node;
3. establish working 3.3 V UART access and capture the boot log;
4. record `/proc/mtd`, `/proc/cmdline`, active UBI attachment, and
   `fw_printenv`;
5. record `boot_part`, `boot_part_ready`, `auto_recovery`, and the exact
   inactive slot;
6. back up `u_env`, `syscfg`, and both aggregate firmware slots;
7. verify official and generated IMG hashes again;
8. test one recoverable non-gateway MX5300 Node first;
9. watch erase, write, slot switch, UBIFS mount, and service startup over
   UART;
10. prove SSH is key-only, bound to LAN, and unreachable from WAN; and
11. keep the official IMG and the verified bootloader rollback procedure
   available locally.

If the modified slot boots far enough to serve the UI but SSH fails, restore
the previous firmware from the local UI. If it cannot serve the UI, use the
verified bootloader auto-recovery or UART procedure to select the untouched
slot. Do not rely on A/B recovery until it has been observed on the exact
unit.

## Current decision

Both requested MX5300 routes are feasible:

- **Exact-Parent control:** the complete firmware consumer exists and was
  traced. Direct MQTT is viable after a privileged bootstrap, but the stock
  anonymous ACL is too broad. Prefer a dedicated authenticated/TLS listener
  or a narrowly scoped helper reached through key-only SSH.
- **SSH bootstrap:** the custom IMG route is proven through UBIFS, UBI,
  Linksys-container, re-extraction, and stock-updater validation. It is the
  clearest route that does not depend on finding an exploit, but it remains a
  controlled hardware experiment until UART, inactive-slot geometry, and
  rollback are confirmed.

## Public references

- [Official Linksys MX5300 downloads](https://support.linksys.com/kb/article/115-en/?section_id=57)
- [Official Linksys manual firmware update](https://support.linksys.com/kb/article/102-en/?section_id=160)
- [OpenWrt MX5300 partition layout](https://raw.githubusercontent.com/openwrt/openwrt/0e178e15429c0ca4574b32ccc1f029d69a6ca185/target/linux/qualcommax/dts/ipq8072-mx5300.dts)
- [OpenWrt Linksys MX A/B upgrade logic](https://raw.githubusercontent.com/openwrt/openwrt/0e178e15429c0ca4574b32ccc1f029d69a6ca185/target/linux/qualcommax/ipq807x/base-files/lib/upgrade/platform.sh)
- [OpenWrt MX5300 device page](https://openwrt.org/toh/linksys/mx5300)
- [Mosquitto documentation](https://mosquitto.org/documentation/)
