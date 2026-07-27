# MX4200 SSH bootstrap and exact-Parent control plan

Analysis date: 2026-07-27

This document turns the offline firmware findings into a bounded,
recoverable implementation plan. It is not a one-click installer. No command
in this analysis was sent to a router.

The two conclusions are:

1. MX4200 firmware has no hidden SSH enable switch. It has the surrounding
   account, service-registration, host-key, and persistent boot-hook
   scaffolding, but the released root filesystem omits the SSH daemon and its
   service script.
2. Exact-Parent steering is a live production mechanism. A command on the
   Master publishes a child-specific backhaul request containing a band,
   channel, and the target Parent radio's BSSID.

## Version coverage

The full root filesystems from these official images were compared:

| Version | Image SHA-256 | Result |
| --- | --- | --- |
| `1.0.13.210200` | `403ec9275b0d610e9c572fe927824c72bc075669367e52891dedc4ac60ebaf4d` | SSH scaffold and Parent control path present; no SSH daemon |
| `1.0.13.216903` | `b0a954835c879822fd1a2da23f09a6ec37da69914aacf07d8038b06fa02fad25` | Same result |

The following files are byte-identical between those versions:

- `/etc/registration.d/15_ssh_server`
- `/etc/registration.d/01_init`
- `/etc/init.d/service_init.sh`
- `/etc/system/wait`
- `/usr/sbin/pub_bh_config`
- `/usr/local/lib/lua/5.1/nodes/util/init.lua`
- `/etc/subscriber.d/slave.subs`
- `/etc/init.d/service_node-mode.sh`
- `/etc/init.d/service_wifi/service_wifi_ext.sh`
- `/etc/init.d/service_wifi/smart_connect_client_monitor.sh`

Linksys's `1.0.13.216903` release notes say that cloud services were removed
and the release supports local login only. This does not mean SSH was added;
the complete filesystem still has no SSH daemon.

## Why there is no stock SSH toggle

### Present in both releases

- `/etc/registration.d/15_ssh_server` registers a service named `sshd`, with
  `/etc/init.d/service_sshd.sh` as the intended handler for service,
  `lan-status`, and `wan-status` events.
- `/etc/registration.d/01_init` creates `root`, `admin`, and the unprivileged
  `sshd` account.
- `/etc/init.d/service_init.sh` copies the encrypted
  `http_admin_password` value to the `root` and `admin` shadow entries.
- `/etc/system/sysinit` creates `/dev/pts` and explicitly mentions SSH.
- `/etc/system/wait` mounts the persistent `syscfg` UBI volume at
  `/var/config`.
- The same wait script executes every executable in
  `/var/config/run_scripts` during boot. Its own comment calls this a
  developer entry point for test scripts.
- `/etc/inittab` starts a serial login prompt on `ttyMSM0` at 115200 baud.

### Absent in both releases

The complete case-sensitive extraction contains no:

- `dropbear`, `dropbearmulti`, or Dropbear protocol implementation;
- `sshd`, OpenSSH server, or OpenSSH client;
- `scp` or `sftp-server`;
- `/etc/init.d/service_sshd.sh`; or
- JNAP, CA, or ordinary web action that enables SSH.

The official `1.0.13.210200` GPL archive also contains no Dropbear/OpenSSH
source or SSH service script. Therefore a `syscfg set ssh_enable 1`-style
operation cannot work: there is no daemon for such a setting to start.

The factory Dropbear RSA and DSS private keys are also byte-identical across
the two public images. They are public material, not device identities, and
must never be used by a real bootstrap.

## Verified Dropbear payload

This analysis went beyond string inspection:

- source: Dropbear `2026.93`;
- source archive SHA-256:
  `310a6087952897c182efbe16088fa0c4d07c467e850a22699472137278fabf09`;
- compiler: Linksys's published
  `arm-openwrt-linux-uclibcgnueabi` GCC 5.2.0/uClibc 1.0.14 toolchain;
- output: stripped ARM EABI5 `dropbearmulti`, 247,784 bytes;
- SHA-256:
  `e670aec336b9c7944ed51e03469d360623ea37efacde2950832ded8486fc8e61`;
- interpreter: `/lib/ld-uClibc.so.0`;
- dynamic dependencies: `libutil.so.1`, `libcrypt.so.1`,
  `libgcc_s.so.1`, `libc.so.1`, and `ld-uClibc.so.1`.

Every dependency is present in the `1.0.13.216903` root filesystem. The same
binary also executes against the `1.0.13.210200` filesystem.

In isolated ARM user-mode QEMU, the payload:

1. displayed its server help and version;
2. generated a new Ed25519 host key;
3. bound to `127.0.0.1:22222`;
4. presented `SSH-2.0-dropbear_2026.93`; and
5. completed an SSH host-key scan with that generated key.

This proves the ABI and SSH protocol path. It does not prove an installation
transport or the physical router's firewall behavior.

The compiled payload is intentionally kept in ignored analysis storage and
is not committed to MeshScope. A reviewed, reproducible build should be used
before any device experiment.

### Build reproduction notes

The Linksys compiler itself is a 32-bit x86 Linux executable. It was run in an
isolated Linux VM with i386 runtime libraries and produced the target ARM
binary. With `TCROOT` pointing to the extracted toolchain directory, the
essential build configuration was:

```sh
TC="$TCROOT/bin/arm-openwrt-linux-uclibcgnueabi-"
STAGING_DIR=/path/to/extracted/qsdk/staging_dir \
CC="${TC}gcc" AR="${TC}ar" RANLIB="${TC}ranlib" \
CFLAGS="-Os -ffunction-sections -fdata-sections" \
LDFLAGS="-Wl,--gc-sections" \
./configure \
  --host=arm-openwrt-linux-uclibcgnueabi \
  --disable-zlib \
  --disable-lastlog --disable-utmp --disable-utmpx \
  --disable-wtmp --disable-wtmpx

STAGING_DIR=/path/to/extracted/qsdk/staging_dir \
make -j2 PROGRAMS="dropbear dropbearkey" MULTI=1

"${TC}strip" -s dropbearmulti -o dropbearmulti-mx4200
```

The first static-link attempt was rejected because the shipped uClibc archive
left ARM unwind symbols unresolved. The verified payload is deliberately a
small dynamic build against the exact libraries already shipped by MX4200.

## Where an SSH payload can persist

The OEM root filesystem is read-only SquashFS. The writable persistent
location is the `syscfg` NAND partition:

```text
syscfg UBI volume -> mounted as UBIFS at /var/config
```

The OEM boot sequence executes:

```text
/var/config/run_scripts/*
```

after service registrations are installed. This is the cleanest persistence
point because it does not modify either A/B firmware slot.

The latest `216903` SquashFS has only 686 bytes left in its embedded volume.
The verified 247,784-byte daemon cannot be added to that image without
rebuilding the UBI layout or removing other content. A modified firmware
image is therefore not the preferred bootstrap.

### Proposed persistent layout

```text
/var/config/meshscope-ssh/
  dropbearmulti
  dropbear -> dropbearmulti
  dropbearkey -> dropbearmulti
  dropbear_ed25519_host_key
  authorized_keys
  payload.sha256

/var/config/run_scripts/
  90-meshscope-ssh
```

The launcher must be idempotent and should:

1. reject an unexpected model or missing payload checksum;
2. wait until `br0` exists;
3. generate a per-Node Ed25519 host key only when absent;
4. require a non-empty `authorized_keys` before starting;
5. refuse to reuse `/etc/dropbear_*_host_key`;
6. avoid duplicate processes by validating its PID file;
7. bind to the LAN bridge rather than all interfaces;
8. disable password authentication and both forwarding directions; and
9. log start/failure state without copying credentials.

A suitable reviewed server policy is equivalent to:

```text
dropbear
  public-key authentication only
  root public-key login allowed
  bind to br0
  local forwarding disabled
  remote forwarding disabled
  host key in /var/config/meshscope-ssh
  authorized_keys in /var/config/meshscope-ssh
```

Root public-key login is needed for system-level diagnostics and control.
Password authentication should remain disabled even though the firmware
synchronizes the root hash with the local web password.

After the payload, symlinks, and `authorized_keys` have been verified, the
manual one-shot launch to test is:

```sh
SSH_BASE=/var/config/meshscope-ssh
"$SSH_BASE/dropbearkey" -t ed25519 \
  -f "$SSH_BASE/dropbear_ed25519_host_key"
"$SSH_BASE/dropbear" \
  -s -j -k -l br0 -p 22 \
  -r "$SSH_BASE/dropbear_ed25519_host_key" \
  -D "$SSH_BASE" \
  -P /var/run/meshscope-dropbear.pid
```

Here `-s` disables password authentication, `-j` and `-k` disable forwarding,
`-l br0` restricts the listener to the LAN bridge, `-r` selects the new host
key, and `-D` selects the directory containing `authorized_keys`. If binding
to `br0` fails on the physical Node, do not silently fall back to all
interfaces; inspect the live interfaces and firewall first.

## Bootstrap routes, ranked

### A. UART plus OEM `/var/config` — preferred first experiment

The released firmware has an active `getty` on `ttyMSM0` at 115200 baud.
With a verified 3.3 V serial connection, the synchronized local administrator
credential should provide a shell. The UART pinout and voltage must be
confirmed from the board before connection; never connect a 5 V adapter.

From that shell, the safe sequence is:

1. record firmware version, active slot, `/proc/mtd`, `fw_printenv`, and
   `/var/config` free space;
2. make an off-device read-only backup of the `syscfg` partition;
3. copy the payload and one public key into a new directory;
4. verify ownership, modes, and SHA-256;
5. generate a fresh host key and start Dropbear once without a boot hook;
6. verify LAN-only key authentication and firewall reachability;
7. stop it and verify the web UI still works; then
8. install the boot hook and test exactly one reboot.

This route does not touch `kernel`, `rootfs`, `alt_kernel`, or `alt_rootfs`.

### B. Temporary custom OpenWrt initramfs — preferred non-OEM shell

OpenWrt's MX4200 device tree describes:

- `kernel` and `rootfs`;
- `alt_kernel` and `alt_rootfs`;
- `u_env`; and
- `syscfg` at offset `0x13f00000`, size `0x0b800000`.

Official OpenWrt deliberately marks `syscfg` read-only. A stock image can
inspect it, but should not be described as able to install the boot hook.

A purpose-built recovery initramfs can remove only the `read-only` property
for `syscfg`, then:

1. locate the partition by label from `/proc/mtd`, never by a hard-coded MTD
   number;
2. attach it to an unused UBI device;
3. mount volume 0 read-only and verify it is the expected `syscfg` UBIFS;
4. remount read-write only for the bounded file installation;
5. sync, unmount, detach, and reboot to the untouched OEM slot.

The recovery image should be booted temporarily, not flashed, for the first
experiment.

### C. Legacy `1.0.13.210200` command injection — possible, not preferred

Public advisory CVE-2026-27848 documents unauthenticated root command
execution through `sct_server` on the exact `1.0.13.210200` build. That could
write a bootstrap into `/var/config`, but it is a vulnerability path, not a
maintenance interface.

It must not be assumed to work on `216602` or `216903`; Linksys's `216602`
release notes explicitly describe security fixes. The offline comparison does
not prove whether CVE-2026-27848 was fixed:

- `sct_server`, its `/usr/sbin/smcdb_auth -L %s` command format, the service
  launcher, `libssl`, and `liboswak` are byte-identical;
- `smcdb_auth` changed and now rejects login/password characters outside
  `[A-Za-z0-9_.]`, which clearly hardens its SQL inputs; and
- `libcrypto` also changed.

Because the unchanged server passes attacker-controlled text through
`popen()`, validation inside `smcdb_auth` may occur only after a shell has
already interpreted that text. Other changed code or runtime policy could
still block the published route, but static diffing alone cannot establish
that. MeshScope therefore treats this route as version-specific, unverified,
and unsuitable for bootstrap. Downgrading a production router or exposing the
vulnerable service would create unnecessary risk. No exploit automation is
included.

### D. Repacked OEM image — highest avoidable risk

The Linksys container has no FIT signature node and its footer checksum can
be reproduced, but that only addresses format acceptance. It does not solve:

- the nearly full SquashFS volume;
- NAND bad-block and UBI geometry correctness;
- A/B boot recovery; or
- rollback if early userspace fails.

Repacking is not justified when the persistent `syscfg` hook already exists.

## SSH rollback design

Rollback must be possible without SSH:

1. The web UI remains the primary recovery surface.
2. UART or the temporary initramfs can remove only
   `/var/config/run_scripts/90-meshscope-ssh`.
3. The payload directory can remain inert until the system is confirmed
   healthy; removing the launcher is enough to disable persistence.
4. A second boot-hook script must not modify bootloader variables or firmware
   slots.
5. Factory reset behavior for `/var/config/run_scripts` must be confirmed
   before relying on it as a recovery method.

## Exact-Parent control

### Production call chain

The same chain is present and byte-identical in `210200` and `216903`:

```text
/usr/sbin/pub_bh_config
  <child UUID> <5GL|5GH> <target channel> <target Parent BSSID>
    -> network/<child UUID>/BH/config
    -> selected child's mqttsub::bhconfig event
    -> mqttsub::bh_channel and mqttsub::bh_bssid
    -> backhaul::set_intf
    -> current wireless backhaul is taken down if a change is needed
    -> smart_connect_client_monitor tries the requested BSSID/channel first
    -> ordinary automatic backhaul selection is the fallback
```

The direct primitive on the Master is:

```text
/usr/sbin/pub_bh_config CHILD_UUID BAND CHANNEL TARGET_BSSID
```

Where:

- `CHILD_UUID` is the child Node's device UUID;
- `BAND` is `5GL` or `5GH`;
- `CHANNEL` is the target Parent radio's current numeric channel; and
- `TARGET_BSSID` is that Parent radio's BSSID.

The command publishes asynchronously and returns before reconnection
finishes. A zero exit status means only that the request was prepared, not
that the child adopted the requested Parent.

### Resolve the tuple from trusted live state

The Parent must be selected by UUID in the application. The backend then
reads the matching fresh record:

```text
/tmp/msg/DEVINFO/<parent UUID>
```

and resolves:

| Requested band | BSSID field | Channel field |
| --- | --- | --- |
| `5GL` | `data.userAp5GL_bssid` | `data.userAp5GL_channel` |
| `5GH` | `data.userAp5GH_bssid` | `data.userAp5GH_channel` |

The browser must never submit a free-form BSSID or channel. The backend must
also verify that the resolved BSSID still maps to the selected Parent UUID.

The Lua `steer_node_to_parent()` helper concatenates values into a shell
string without quoting. It is safe only for firmware-generated values. A
MeshScope helper must execute `pub_bh_config` with an argument vector after
strict validation and must never use `sh -c` or the Lua formatting wrapper
with browser input.

## Required control state machine

Before publishing:

1. refresh topology, DEVINFO, and backhaul state;
2. require the command transport to terminate on the Master Node;
3. reject the Master as the child;
4. reject self-parenting and the current Parent;
5. reject an offline, unconfigured, or wired-only target;
6. reject a wired-backhaul child;
7. compute the child's full descendant set and reject every descendant as a
   target to prevent a topology cycle;
8. require a current numeric channel and valid unicast BSSID from the target
   Parent's DEVINFO record;
9. snapshot the child's current Parent and its original radio tuple; and
10. allow only one mutation at a time with a per-child cooldown.

After publishing:

1. poll `nodes/diagnostics/GetBackhaulInfo` from the Master;
2. tolerate a bounded offline window while the child reconnects;
3. map the observed `parentIPAddress` back to the requested Parent Node;
4. declare success only after consecutive fresh observations agree;
5. if the timeout expires, publish the original Parent tuple once;
6. observe that rollback independently; and
7. if both attempts fail, stop mutating and surface recovery instructions.

The firmware's own exact-BSSID attempt falls back to automatic selection when
the target cannot be reached. This improves availability but means a failed
request may reconnect to a third Node. Observation is mandatory.

## Steering is not a persistent pin

The requested tuple is held in runtime `sysevent` state. The backhaul monitor
records a preferred BSSID/channel after connection, but several recovery and
setup paths clear that state, and automatic selection remains available.

The correct product name is therefore **Steer now**, not **Pin Parent**.
MeshScope should show:

- requested Parent and band;
- observed Parent during convergence;
- timeout/fallback state;
- the saved rollback Parent; and
- the fact that later self-healing may change the topology again.

## Remaining live checks

Offline analysis has closed the implementation and ABI questions. A controlled
live session still has to confirm:

- UART pinout and console authentication on this exact board revision;
- the live `syscfg` MTD label, UBI device, and free space;
- whether binding Dropbear to `br0` is sufficient on both Master and child
  Nodes;
- firewall reachability from LAN and non-reachability from WAN;
- the factory-reset behavior of `/var/config/run_scripts`;
- fresh DEVINFO tuple parsing on the current network; and
- one leaf-Node steering attempt with a captured rollback tuple.

Until those checks pass, MeshScope should keep the mutation endpoint disabled.

## Public references

- [Linksys MX4200 downloads and release notes](https://support.linksys.com/kb/article/112-en/)
- [Linksys GPL Code Center](https://support.linksys.com/kb/article/316-en/)
- [MX4200 1.0.13.210200 GPL archive](https://downloads.linksys.com/support/assets/gpl/MX4200_v1.0.13.210200.tar.gz)
- [OpenWrt MX4200 device tree](https://raw.githubusercontent.com/openwrt/openwrt/main/target/linux/qualcommax/dts/ipq8174-mx4200.dtsi)
- [OpenWrt Linksys A/B upgrade logic](https://raw.githubusercontent.com/openwrt/openwrt/main/target/linux/qualcommax/ipq807x/base-files/lib/upgrade/platform.sh)
- [Dropbear SSH](https://matt.ucc.asn.au/dropbear/dropbear.html)
- [SYSS-2025-010 / CVE-2026-27848](https://www.syss.de/fileadmin/dokumente/Publikationen/Advisories/SYSS-2025-010.txt)
