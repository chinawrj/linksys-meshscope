# MQTT Parent-steering experiment overlays

These overlays are intentionally small and model-independent. The analyzed
MX4200 `1.0.13.216903` and MX5300 `1.1.12.210066` firmware images contain
byte-identical Mosquitto configuration and ACL files.

## Plan A: narrow steering I/O permissions

`plan-a-strict-steering-io.patch` keeps the stock LAN listener on port 1883
and adds only:

```text
topic write network/+/BH/config
topic read network/+/DEVINFO
topic read network/+/BH/status
topic write network/BH/status_resend_all
```

An external client first subscribes to the two read Topics, then publishes to
`network/BH/status_resend_all`. The stock Slave handler republishes DEVINFO,
WLAN status, and backhaul status, allowing the client to resolve each Parent's
current `5GL`/`5GH` BSSID and channel before publishing `BH/config`. All other
stock `strict.acl` restrictions remain in place.

## Offline broker verification

The Plan A rules were loaded into the stock MX5300 ARM Mosquitto 1.6.2 binary
under QEMU, with separate simulated LAN and localhost listeners. The test
confirmed delivery for both read Topics and both write Topics. It also
confirmed that unrelated `network/+/AC/config` writes and
`network/+/AC/status` reads are not delivered. The reproducible ACL and broker
configuration are in `tests/fixtures/`.

## Plan B: stock open ACL

`plan-b-open-acl.patch` keeps the stock LAN listener and changes its ACL file
from `strict.acl` to the already bundled `open.acl`. Stock `open.acl` grants:

```text
topic readwrite #
```

This is useful only as a short isolated-lab control. It gives every LAN MQTT
client access to every broker Topic and must not be used on an untrusted LAN.

## Why Plan B is not an iptables loopback redirect

The unrestricted system listener is bound to `localhost:1883`. Reliably
forwarding packets from a LAN-only port to that socket requires a dedicated
port, `route_localnet`, DNAT, filter rules, and conntrack-compatible return
traffic. Switching the existing LAN listener to the bundled `open.acl` tests
the same no-ACL condition with fewer runtime dependencies.

Neither overlay contains credentials, changes the secure port-8883 listener,
or enables SSH/BLE.

The `ubi` directory contains geometry-preserving volume templates for the
analyzed firmware builds. Run `ubinize` with the corresponding model template
from a directory containing `rootfs.ubifs` or `rootfs.squashfs`.

## One-command build

On the analysis Mac, the default official-image locations can be built with:

```sh
tools/build_linksys_mqtt_images_lima.sh
```

Optional positional arguments are:

```text
MX5300_OFFICIAL.img MX4200_OFFICIAL.img OUTPUT_PARENT
```

The wrapper starts the `mesh-firmware` Lima VM, performs a clean extraction
from each official IMG, creates both plans, re-extracts every final IMG, and
copies only verified outputs back to
`firmware-analysis/work/mqtt-parent-images/<run-id>/`.

The build fails if final re-extraction finds an unexpected added/missing path
or a difference in file type, mode, UID/GID, mtime, symlink target, device
number, hardlink relationship, extended attributes, or unchanged-file
content. For the one intentionally changed file, only size and content may
differ; its metadata must still match the official image.

See `EXPERIMENT.md` for the Master-only deployment boundary and recommended
MX5300 test order.
