# Offline BLE exact-Parent proof overlay

This directory contains the smallest currently identified MX4200/MX5300
firmware-side proof for one owner-controlled exact-Parent experiment. It is an
offline root-filesystem overlay, not a scanner, uploader, flasher, exploit, or
finished production feature.

## Files and single hook

Copy the executable helper to:

```text
/usr/bin/meshscope_parent_steer
```

Then apply [`btjnap-hook.patch`](btjnap-hook.patch) to the extracted stock
`/usr/bin/btjnap`. The hook intercepts only:

```text
http://linksys.com/jnap/nodes/meshscope/SteerToParent
```

All other Actions continue through the byte-for-byte stock path.

The request body is deliberately limited to the tuple consumed by the existing
child backhaul state machine:

```json
{
  "band": "5GH",
  "channel": 149,
  "bssid": "00:11:22:33:44:55"
}
```

The helper:

1. passes the supplied Basic header through stock
   `/core/CheckAdminPassword`;
2. accepts only `5GL` or `5GH`, a bounded decimal channel, and one MAC-format
   BSSID;
3. writes `mqttsub::bh_channel`;
4. writes `mqttsub::bh_bssid`; and
5. triggers `backhaul::set_intf`.

It does not resolve a Parent UUID, prove that the BSSID belongs to that Parent,
persist a pin, disable normal optimization, or provide rollback. Those remain
client-side/live-test responsibilities. The purpose of this overlay is only to
isolate and test the missing BLE-JNAP-to-existing-steering bridge.

The helper has command-path environment overrides solely so its behavior can be
unit-tested with local mocks. The stock `btsetup -> btjnap` execution path does
not set them.

No firmware image or device-specific credential belongs in this directory.
