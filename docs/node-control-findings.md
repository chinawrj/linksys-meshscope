# Linksys BigTree node-control feasibility

This note records a read-only feasibility investigation against the `BigTree`
node. No reboot, reset, steering, topology change, or undocumented mutation was
executed.

## Observed node

- Name: `BigTree`
- Model: `WHW03`, hardware version 2
- Firmware: `2.1.20.216892`
- Address: `10.37.1.208`
- Current parent: `Main`
- Current backhaul: `5GH`
- Current child: `RoadSouth` over `5GL`

The current topology proves that BigTree can act as an automatic upstream
Parent. A later offline WHW03 firmware analysis also found an internal
backhaul command that can request an exact Parent BSSID/channel. It has not
been executed against BigTree.

## Manual Parent selection

The authority firmware advertises `TopologyOptimization` and
`TopologyOptimization2`. Its UI uses these services only for the global Client
Steering and Node Steering Boolean settings, and no public child-plus-Parent
JNAP action was found.

The extracted WHW03 2.1.19 filesystem contains
`topomgmt`/`pub_bh_config`, which accepts a child UUID and a target radio's
band, channel, and BSSID. MX4200 1.0.13.210200 independently contains the same
underlying function and byte-identical publisher, although it omits the
`topomgmt` wrapper. BigTree is therefore a plausible manual **Steer now**
target once its live radio tuple and exact installed build are verified. This
is not evidence of a persistent Parent pin.

Linksys documents a power-on sequence as a way to influence the automatically
selected upstream chain. This is operational guidance, not a persistent manual
pin:

<https://support.linksys.com/kb/article/636-en/>

The complete offline command-chain and guardrail analysis is documented in
[Linksys hidden interfaces and Node Parent steering](hidden-firmware-interfaces.md)
and [MX4200 firmware analysis](mx4200-firmware-analysis.md).

## Individual restart

BigTree's own address responds to the read-only `core/GetDeviceInfo` call and
advertises the `Core` and `nodes/setup/Setup3` services.

The child-node redirect bypass is also confirmed from BigTree's own firmware:

1. open `https://10.37.1.208/ca`;
2. the bootstrap page preserves the `#casupport` hash for a configured Node;
3. the local login accepts the credential synchronized from Main; and
4. successful login redirects to `/ui/dynamic/home.html#casupport`.

A direct read-only probe authenticated successfully and reported device mode
`Slave`. Mesh-level diagnostics that require the authority still reject this
context with master-mode or unsupported-mode errors. MeshScope now exposes the
`/ca` link and performs only `Check`/`Get` calls when a Node detail is opened.

The bundled firmware Troubleshooting UI:

1. labels its action **Restart mesh WiFi system**;
2. warns that all nodes will restart;
3. invokes `core/Reboot` with an empty request body; and
4. provides no `deviceID` or target-node selector.

The same Troubleshooting applet is deliberately made visible on a Node when
`nodes/setup/Setup3` is advertised. More decisively, the MX4200 firmware's
`/usr/sbin/reset_slave_nodes` implements its single-IP reboot by sending the
same empty `core/Reboot` request directly to the chosen Node IP. The HTTP
destination selects the Node, and the reboot stays local to the serving Node.

Linksys' current support documentation likewise exposes **Restart Network** for
the entire system, not an individual online child-node restart:

<https://support.linksys.com/kb/article/99-en/>
<https://support.linksys.com/kb/article/255-en/>

The owner has confirmed from the live system that a direct `core/Reboot` call
to a child Node's IP restarts only that Node.

MeshScope therefore exposes a one-click **Restart current Node** action. It
sends `core/Reboot` directly to the selected known online Node, shows an
in-progress badge on the topology card, and clears it when the Node is observed
offline and back online. There is no typed confirmation page. Duplicate
requests are blocked for 90 seconds, and bounded follow-up polls run alongside
the normal auto-refresh schedule. Unit tests mock the router action; browser
regression uses an in-memory fake endpoint rather than rebooting the live
network.
