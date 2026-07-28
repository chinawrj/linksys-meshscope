# Offline BLE-JNAP advanced-Action proof overlay

This directory contains a firmware-side proof for extending the Linksys
MX4200/MX5300 BLE transport beyond the setup Actions used by the official
phone application. It is an extracted-root-filesystem overlay, not a scanner,
uploader, flasher, exploit, or finished production feature.

## Architecture

Stock `btsetup` already converts one BLE request into:

```text
/usr/bin/btjnap -a <Action> -u <Authorization> -j <JSON>
```

Stock `btjnap` forwards registered Actions to `/www/JNAP/index.cgi`. The
overlay keeps that path byte-for-byte for every original Action and intercepts
only four new names:

```text
http://linksys.com/jnap/nodes/meshscope/GetCapabilities
http://linksys.com/jnap/nodes/meshscope/GetBackhaulStatus
http://linksys.com/jnap/nodes/meshscope/SteerToParent
http://linksys.com/jnap/nodes/meshscope/RollbackParent
```

Copy the executable dispatcher to:

```text
/usr/bin/meshscope_ble_dispatch
```

Then apply [`btjnap-hook.patch`](btjnap-hook.patch) to the extracted stock
`/usr/bin/btjnap`.

This is a bounded BLE-to-local-service extension, not an arbitrary command
forwarder. Stock JNAP already supplies generic pass-through for registered
Actions; the new dispatcher supplies only the advanced mesh operations that
stock JNAP does not register.

## Authentication

Every new Action passes the supplied Basic header through the stock privileged
`/core/CheckAdminPassword` Action and requires an exact `OK` result. A
configured Slave therefore uses the same synchronized local administrator
credential as its Main Node.

The overlay does not hard-code a router password, device identifier, BLE
address, or Parent tuple.

## Actions

### `GetCapabilities`

Returns the overlay API version, the fixed Action list, the authentication
model, and the fact that successful write responses mean “accepted before
reconnect,” not “new Parent observed.”

### `GetBackhaulStatus`

Returns without changing state:

- local device UUID and Smart Mode;
- backhaul state, media, interface, preferred band/channel/BSSID;
- currently requested band/channel/BSSID from the MQTT consumer tuples;
- last accepted overlay request; and
- the saved one-shot rollback tuple, when valid.

The current tuple is sourced from `backhaul::preferred_*`, not a fresh driver
query. The response labels that source rather than presenting it as a packet
capture.

### `SteerToParent`

Example body:

```json
{
  "requestId": "one-use-request-id",
  "expectedDeviceID": "child-device-uuid",
  "parentDeviceID": "selected-parent-uuid",
  "band": "5GH",
  "channel": 149,
  "bssid": "00:11:22:33:44:55"
}
```

The dispatcher:

1. checks the synchronized administrator credential;
2. compares `expectedDeviceID` with local `device::uuid`;
3. requires the local unit to be a configured Slave (`smart_mode::mode=1`);
4. accepts only `5GL` or `5GH`, a bounded decimal channel, and one MAC-format
   BSSID;
5. acquires one atomic mutation lock;
6. snapshots a valid current 5 GHz preferred tuple for one rollback;
7. writes `mqttsub::bh_channel`;
8. writes `mqttsub::bh_bssid`;
9. triggers `backhaul::set_intf`; and
10. records and returns an accepted request.

Those three sysevents are the same tuple and trigger consumed by the production
`network/<child UUID>/BH/config` MQTT path. The existing Wi-Fi service then
disconnects the current wireless backhaul and attempts the requested
band/BSSID.

`parentDeviceID` is an audit/selection value. The child cannot independently
map it to a BSSID while its infrastructure backhaul is down; MeshScope must
resolve the Parent radio tuple from fresh topology data before sending it.

### `RollbackParent`

Example body:

```json
{
  "requestId": "one-use-rollback-id",
  "expectedDeviceID": "child-device-uuid"
}
```

The dispatcher replays the saved previous band/channel/BSSID through the same
three production sysevents, then removes the rollback file. If the previous
tuple was missing or invalid, it returns `ErrorNoRollback` without changing
state.

## Offline verification

Tests replace JNAP, `jsonparse`, `syscfg`, and `sysevent` with local mocks and
verify:

- all four Actions are advertised;
- status retains both current and requested backhaul data;
- bad authorization, wrong child UUID, or malformed tuples emit no sysevents;
- a concurrent/stale mutation lock returns `ErrorBusy` without an event;
- a valid request emits exactly the production three-event sequence;
- rollback emits exactly the saved sequence and is one-shot; and
- the patched stock wrapper passes the complete Action, Authorization, and JSON
  arguments to the dispatcher.

The patch dry-runs against the extracted MX4200 and MX5300 stock wrappers.
Those wrappers are byte-identical (SHA-256
`d17c966c006fda53fd83f6274bcb0ba135bfd77ff2be49ffb05c161afb2967d0`).
The dispatcher also passes `sh -n` under each model's own ARM BusyBox executed
with user-mode QEMU.

## Remaining live boundary

The overlay has not been installed or executed on a router. Static and mocked
tests prove the missing BLE-JNAP-to-backhaul bridge and its exact local event
sequence; they do not prove:

- configured-Slave advertising or GATT permissions on the physical units;
- link-layer encryption before the Basic credential is sent;
- reconnection to the requested Parent;
- persistent pinning against later topology optimization; or
- rollback success after an actual failed move.

Those require one in-range, recoverable Node and should begin with
`GetCapabilities`, `GetBackhaulStatus`, and a no-op request to the current
Parent before trying an alternate Parent.
