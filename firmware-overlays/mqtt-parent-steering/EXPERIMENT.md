# Linksys MQTT Parent-steering experiment images

These images are offline proof builds for owner-controlled lab testing. They
have not been booted on physical hardware.

## Critical deployment requirement

The stock Mosquitto service starts only when the Linksys device is in Master
mode (`smart_mode::mode=2`). Both plans therefore modify the **Master**, not a
Slave Node.

- To test MX5300 first, use an MX5300 as the Master of an isolated test Mesh.
- Flashing either MX5300 image onto an MX5300 Slave in an MX4200-led Mesh will
  not expose a broker because the Slave does not start Mosquitto.
- In the currently analyzed MX4200-led topology, the corresponding MX4200
  Master image is the one that can expose `<main-router-ip>:1883`.

## Plans

### Plan A — recommended first

The existing LAN `1883` listener keeps `strict.acl` and receives four rules:

```text
topic write network/+/BH/config
topic read network/+/DEVINFO
topic read network/+/BH/status
topic write network/BH/status_resend_all
```

The read and resend permissions let the controller obtain a fresh UUID,
Parent, BSSID, band, and channel map before steering. They do not open unrelated
configuration Topics.

This ACL was also exercised offline using the stock MX5300 ARM Mosquitto 1.6.2
binary under QEMU. All four intended flows were delivered; unrelated
`network/+/AC/config` writes and `network/+/AC/status` reads were blocked.
This validates broker authorization behavior, not physical-router boot or the
downstream steering result.

### Plan B — isolated diagnostic control

The existing LAN `1883` listener switches from `strict.acl` to the bundled
`open.acl`, whose rule is:

```text
topic readwrite #
```

Plan B gives every LAN MQTT client access to all Topics. Use it only on an
isolated LAN for determining whether an unexpected failure is caused by Topic
permissions. Return to official firmware or Plan A after the test.

## Recommended MX5300 order

1. Confirm the test router is MX5300 v1 and currently runs `1.1.12.210066`.
2. Use a recoverable, isolated MX5300 Master; retain the official IMG and
   physical recovery access.
3. Install Plan A first.
4. Confirm TCP `1883` is reachable on the Master LAN address.
5. Subscribe to `network/+/DEVINFO` and `network/+/BH/status`.
6. Publish to `network/BH/status_resend_all`, collect the refreshed records,
   and resolve the selected Parent's `5GL` or `5GH` BSSID/channel.
7. Publish only to `network/<child UUID>/BH/config`, initially using the
   child's current Parent tuple as a no-op.
8. Observe the actual resulting Parent; a QoS-1 `PUBACK` proves only broker
   acceptance.
9. Use Plan B only if Plan A is reachable but the required Topic is still
   denied or further Topic discovery is required.

Every final IMG was re-extracted and compared with its official source. See
`verification/*-filesystem.json`, `verification/*-container.json`,
`verification/*-fwcc.txt`, and `SHA256SUMS`.
