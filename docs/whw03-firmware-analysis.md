# WHW03 2.1.19.215389 firmware analysis

Analysis date: 2026-07-27

This is a static, offline analysis of `FW_WHW03_2.1.19.215389_prod.img`.
The source image was copied into the workspace for inspection; the original
OneDrive file was not modified. Firmware binaries and extracted files are
ignored by Git and must not be published with MeshScope.

## Result

This firmware contains most of an intentionally incomplete SSH implementation:

- Unix accounts for `root`, `admin`, and `sshd`
- root and admin password-hash synchronization with the local Linksys admin
  password
- Dropbear RSA and DSS host-key files
- `/dev/pts` setup specifically documented as being for SSH
- port 22 entries in `/etc/services`
- a sysevent registration binary for an `sshd` service
- a boot-time developer hook that executes `/var/config/run_scripts`

The two missing pieces are the SSH daemon itself and
`/etc/init.d/service_sshd.sh`. A compatible Dropbear build and a small,
idempotent launcher should therefore be sufficient; no replacement account or
authentication system is required.

## Image identity

| Field | Value |
| --- | --- |
| Model | WHW03 |
| Firmware | 2.1.19.215389 |
| Product | `nodes_v2`, production |
| File size | 61,079,552 bytes (`0x3A40000`) |
| SHA-256 | `f50973d001e792e724abbb7b1438ca6552de5cd8df5cac47182959156e5402e5` |
| SHA-1 | `fdc269953861e36676bf8cf823e03fc2fa1c3071` |
| Architecture | 32-bit little-endian ARM, EABI5 |
| Userspace ABI | uClibc, interpreter `/lib/ld-uClibc.so.0` |
| Kernel description | `ARM Linksys Linux-3.14.77` |

## Container layout

| Range | Contents |
| --- | --- |
| `0x0000000..0x028CB40` | FIT image containing kernel and device tree |
| `0x028CB40..0x0600000` | zero padding |
| `0x0600000..0x3A20000` | UBI image, 417 physical erase blocks |
| `0x3A20000..0x3A3FF00` | padding |
| `0x3A3FF00..0x3A40000` | 256-byte Linksys footer |

The FIT has CRC32 and SHA-1 hash nodes for its kernel and device-tree payloads,
but no FIT RSA/ECDSA signature node. The root filesystem is outside the FIT, so
a rootfs-only change does not require rebuilding the kernel/FDT hashes.

UBI/UBIFS parameters:

| Parameter | Value |
| --- | --- |
| PEB size | 131,072 bytes |
| LEB size | 126,976 bytes |
| Minimum I/O | 2,048 bytes |
| Volume | dynamic `ubifs`, ID 0, autoresize |
| Volume blocks in image | 415 |
| Reserved PEBs | 636 |
| UBIFS format | version 4, space-fixup |
| Default compression | 2 (zlib) |
| Maximum LEB count | 656 |
| Reported free space | 409,600 bytes |

The free-space margin is tight. A compact Dropbear build is preferable to
OpenSSH, and the repacked image must be checked against the exact PEB budget.

## SSH evidence

### Authentication is already wired

`/etc/init.d/service_init.sh` obtains `http_admin_password` from `syscfg` and
passes the existing encrypted value to `chpasswd -e` for both `root` and
`admin`. The runtime account files are symlinked into `/tmp/etc/.root/`.
Consequently, the normal Linksys local password is already the intended Unix
password source. No credential was copied into this report.

The embedded account templates show:

- `root` with `/bin/sh`
- `admin` with `/bin/sh`
- `sshd` with `/sbin/nologin`

### Startup plumbing is already present

`/etc/registration.d/15_ssh_server` registers:

```text
lan-status|/etc/init.d/service_sshd.sh
wan-status|/etc/init.d/service_sshd.sh
sshd
```

`/etc/system/sysinit` creates and mounts `/dev/pts`, with an adjacent comment
stating that this is needed when SSH is used.

### What is absent

The extracted filesystem has no executable named `dropbear`, `dropbearmulti`,
`sshd`, `ssh`, `scp`, or `sftp-server`, and
`/etc/init.d/service_sshd.sh` does not exist.

### Do not reuse the embedded host keys

The factory image includes `/etc/dropbear_rsa_host_key` and
`/etc/dropbear_dss_host_key`. Because these private keys are distributed inside
the public firmware image, they must be considered compromised. A future SSH
payload should generate a unique key on each Node during first boot, store it
under a mode-0700 directory in persistent `/var/config`, and never enable DSS.

## Boot-time developer hook

`/etc/system/wait` contains this documented behavior:

```text
if [ -d "/var/config/run_scripts" ] ; then
    execute_dir "/var/config/run_scripts" &
fi
```

The surrounding comment describes it as an entry point for developers to add
test scripts executed after boot. This is the preferred integration point:

- it avoids invasive edits to the normal registration system;
- it can launch a daemon after the persistent config volume is mounted;
- it allows per-device keys and configuration to survive independently of the
  read-only firmware rootfs;
- the launcher can be made easy to disable by a flag file.

We still need a controlled way to place the initial launcher and daemon in the
persistent area. A one-time modified image can bootstrap them, or a later
analysis may find a legitimate authenticated config/diagnostic upload path.

## Firmware acceptance checks

The updater invokes:

```text
source /usr/sbin/fwcc verify_signature <image>
```

`fwcc` supports GPG validation when `/etc/fwcaps.sig` exists, but that file is
absent from this production WHW03 rootfs. This image therefore takes the
`verify_linksys_header` branch. It checks:

1. the `.LINKSYS.` footer magic;
2. the checksum stored at footer bytes 33–40;
3. the POSIX `cksum` CRC of every byte except the final 256-byte footer.

The firmware's SKU comparison code is commented out. For this image, the
calculated decimal checksum is `2352980514`, which is hexadecimal `8C3FA222`
and matches the footer.

This makes a modified local image feasible, but not automatically safe. The
repacker must preserve the total size and offsets, reproduce the UBI geometry,
recalculate every UBI/UBIFS checksum, and finally regenerate the Linksys footer
CRC. A different firmware version or region must be checked independently;
the absence of `fwcaps.sig` here must not be generalized.

## A/B update and rollback observations

The update scripts inspect `fwup_boot_part`, select `kernel` or `alt_kernel`,
erase that MTD target, and write the combined image with `nandwrite -p`.
`service_autofwup.sh` also contains primary/alternate boot-partition handling.
This is consistent with an A/B design and gives us a potential rollback path.

Before any test flash, live read-only inspection must confirm:

- which slot is currently active;
- the actual `/proc/mtd` sizes and ordering;
- bootloader retry/fallback behavior;
- whether a failed userspace boot automatically returns to the old slot;
- that serial/UART recovery is available if automatic fallback fails.

Do not assume that merely having two slots guarantees recovery.

## Recommended SSH payload design

1. Build Dropbear for ARM EABI5 and this uClibc ABI. Prefer a small dynamically
   linked build only if every required library resolves; otherwise use a
   carefully size-checked static build.
2. Use `/var/config/run_scripts` for an idempotent launcher.
3. Generate unique per-Node host keys under `/var/config/ssh/` on first start.
4. Start with public-key-only root login. Password login can be tested later
   because account synchronization already exists, but should not be the
   permanent default.
5. Listen only on the LAN/mesh management side. Add explicit firewall rules
   that reject WAN access even though the stock registration mentions
   `wan-status`.
6. Include a local disable flag, PID handling, duplicate-process protection,
   logging, and clean stop/restart actions.
7. Never change kernel, device tree, bootloader, radio partitions, calibration
   data, syscfg, or both boot slots during the first experiment.

## Low-risk validation sequence

1. Repack the image without changing its contents and prove it extracts to the
   same file tree and passes all footer/container checks.
2. Test a rootfs change that only writes a harmless boot marker; do not include
   a network listener yet.
3. Flash only the inactive slot of a non-critical WHW03 Node after recording
   the live partition map and recovery route.
4. Verify boot, Mesh rejoin, CA page, JNAP, and automatic fallback.
5. Add Dropbear with public-key-only, LAN-only settings.
6. Confirm port 22 is unreachable from WAN and guest networks, then test
   Node-local restart and persistence.

No modified or flashable image has been produced during this analysis.
