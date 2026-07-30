# Linksys Velop Node Steering findings

This note records the read-only capability investigation performed against a
Linksys Velop MX42 authority node running firmware `1.0.13.216903`.

## Result

The live JNAP/UI investigation found only automatic Client Steering and
automatic Node Steering, both enabled on the inspected network.

Later offline analyses of WHW03 2.1.19 and MX4200 1.0.13.210200 firmware found
the same undocumented shell-level data path that moves one child UUID toward
an exact Parent radio BSSID/channel. WHW03 wraps it with `topomgmt` and
`nodes.topology.steerer.change_node_parent`; MX4200 omits those wrappers but
retains `nodes.util.steer_node_to_parent`, the byte-identical
`pub_bh_config`, and the full child backhaul consumer.

No public JNAP action or web form exposes that operation. MeshScope therefore
still cannot offer it through its current authenticated web transport.

This distinction matters:

- **Node Steering** is a global automatic policy. Nodes select the strongest
  available upstream signal and self-heal when topology changes.
- **Manual Parent request** exists internally as a runtime backhaul steering
  command. It is not a documented permanent pin, and it is not exposed by the
  local UI or public JNAP calls.

## Evidence

The authority node advertises these services:

```text
http://linksys.com/jnap/nodes/topologyoptimization/TopologyOptimization
http://linksys.com/jnap/nodes/topologyoptimization/TopologyOptimization2
```

The read-only action:

```text
http://linksys.com/jnap/nodes/topologyoptimization/GetTopologyOptimizationSettings2
```

returned:

```json
{
  "isClientSteeringEnabled": true,
  "isNodeSteeringEnabled": true
}
```

The firmware's Wi-Fi Advanced page uses this service only to read and update
the two global Boolean settings. Its Network Map reads parent relationships
from backhaul observations but does not offer a Parent selector.

Offline firmware evidence shows a separate internal path:

```text
topomgmt steerer.change_node_parent
  -> pub_bh_config
  -> network/<child UUID>/BH/config
  -> child backhaul reconnect to requested BSSID/channel
```

The requested Parent's exact tuple comes from
`/tmp/msg/DEVINFO/<uuid>` fields such as `userAp5GL_bssid/channel` and
`userAp5GH_bssid/channel`.

The preference is runtime `sysevent` state and can be cleared by auto-channel
processing. It should be described as **Steer now**, not as a durable pin.

## Safety boundary

MeshScope reads and displays the automatic settings. It does not expose the
global write action or the undocumented internal Parent command.

The internal function lacks target ownership, reachability, descendant-cycle,
and concurrency checks. A future implementation must resolve BSSID/channel
server-side, reject the selected child's entire descendant subtree, serialize
mutations, observe success from fresh backhaul state, and provide a bounded
recovery path.

Full offline evidence and the hidden-entry inventory are in
[Linksys hidden interfaces and Node Parent steering](hidden-firmware-interfaces.md)
and [MX4200 firmware analysis](mx4200-firmware-analysis.md).
