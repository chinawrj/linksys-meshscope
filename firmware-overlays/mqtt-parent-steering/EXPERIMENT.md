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
  Master image is the one that can expose `10.37.1.1:1883`.

## Plans

### Plan A — recommended first

The existing LAN `1883` listener keeps `strict.acl` and receives one rule:

```text
topic write network/+/BH/config
```

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
5. Publish only to `network/<child UUID>/BH/config`, initially using the
   child's current Parent tuple as a no-op.
6. Observe the actual resulting Parent; a QoS-1 `PUBACK` proves only broker
   acceptance.
7. Use Plan B only if Plan A is reachable but the required Topic is still
   denied or further Topic discovery is required.

Every final IMG was re-extracted and compared with its official source. See
`verification/*-filesystem.json`, `verification/*-container.json`,
`verification/*-fwcc.txt`, and `SHA256SUMS`.
