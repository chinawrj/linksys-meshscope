# Linksys Velop Node Steering findings

This note records the read-only capability investigation performed against a
Linksys Velop MX42 authority node running firmware `1.0.13.216903`.

## Result

The firmware supports automatic Client Steering and automatic Node Steering.
Both are enabled on the inspected network.

No supported control was found for selecting a child node and pinning it to a
specific parent node.

This distinction matters:

- **Node Steering** is a global automatic policy. Nodes select the strongest
  available upstream signal and self-heal when topology changes.
- **Manual parent assignment** would require a per-child target parent or
  uplink action. The firmware's local UI and exposed JNAP calls do not present
  such a control.

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
from backhaul observations but does not offer a parent selector or invoke a
per-node steering action.

Linksys' public support documentation describes Node Steering as connecting
nodes to the strongest signal and automatically self-healing when a node moves
or goes offline. It does not document manual parent pinning.

## Safety boundary

MeshScope reads and displays the settings. It does not expose the firmware's
write action for enabling or disabling Steering, and it does not attempt
undocumented commands.

If a future firmware adds a documented per-node parent action, it should be
treated as a disruptive network mutation and implemented behind explicit node,
parent, and confirmation gates.
