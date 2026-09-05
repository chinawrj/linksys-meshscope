(function (root, factory) {
  const engine = factory();
  if (typeof module === "object" && module.exports) module.exports = engine;
  else root.MeshDetailData = engine;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  function clientNodeId(client) {
    return client.nodeId || client.parentId || null;
  }

  function clientsForNode(topology, node, { onlineOnly = true } = {}) {
    if (!topology?.clients || !node?.id) return [];
    return topology.clients
      .filter((client) => clientNodeId(client) === node.id)
      .filter((client) => !onlineOnly || client.online)
      .sort((a, b) => {
        if (Boolean(a.online) !== Boolean(b.online)) return a.online ? -1 : 1;
        return String(a.name || "").localeCompare(String(b.name || ""), "en-US", {
          numeric: true,
        });
      });
  }

  function nodeForClient(topology, client) {
    const nodeId = clientNodeId(client);
    return topology?.nodes?.find((node) => node.id === nodeId) || null;
  }

  function nodeDetailRows(node, formatNumber = (value) => String(value ?? "—")) {
    const connection = String(node.connectionType || "").trim().toLowerCase();
    const isWired = node.isWired === true || connection === "wired" || connection === "ethernet";
    const phyRate = Number(node.phyRateMbps);
    const formattedPhyRate = Number.isFinite(phyRate)
      ? phyRate >= 1000
        ? `${formatNumber(phyRate / 1000, 2)} Gbps`
        : `${formatNumber(phyRate)} Mbps`
      : node.isAuthority
        ? "Gateway"
        : "—";
    return {
      metrics: [
        ["Current status", node.online ? "Online" : "Offline"],
        ["Connected clients", `${node.clientCount}`],
        [
          isWired ? "Ethernet link" : "Hop throughput",
          node.speedMbps !== null && node.speedMbps !== undefined
            ? `${formatNumber(node.speedMbps)} Mbps`
            : node.isAuthority
              ? "Gateway"
              : "—",
        ],
        [
          isWired ? "Port PHY rate" : "Link PHY rate",
          `${formattedPhyRate}${node.phyRateStale ? " · Stale sample" : ""}`,
        ],
        [
          isWired ? "Backhaul medium" : "Backhaul signal",
          isWired
            ? "Ethernet · No RF signal"
            : node.rssi !== null && node.rssi !== undefined
            ? `${node.rssi} dBm · ${node.quality?.label || "Unknown"}`
            : "—",
        ],
      ],
      details: [
        ["Model", node.model],
        ["IP address", node.ipAddress],
        ["MAC address", node.macAddress],
        ["Connection type", isWired ? "Ethernet" : node.connectionType || "Wireless"],
        ["Parent node", node.parentName || (node.isAuthority ? "Internet / WAN" : "—")],
        ...(isWired
          ? [[
              "Wired parent basis",
              node.parentSource === "topology-lock-wired-assignment"
                ? "Manual Topology Lock assignment · Not automatically verified"
                : "Linksys LLDP report · Treat as unverified on switched LANs",
            ]]
          : []),
        ["Backhaul band", isWired ? "Not applicable · Ethernet" : node.band],
        ["Channel", isWired ? "Not applicable" : node.channel],
        [
          "Hop source",
          node.isAuthority
            ? "—"
            : isWired
              ? "JNAP GetBackhaulInfo · Ethernet link status"
              : "JNAP GetBackhaulInfo · Child → Parent Thrulay",
        ],
        ["Hop sample time", node.isAuthority ? "—" : node.timestamp || "—"],
        ["PHY source", node.isAuthority ? "—" : isWired ? "MQTT BH/status · Ethernet interface" : "MQTT BH/status · Child backhaul interface"],
        ["PHY raw value", node.phyRateRaw || "—"],
        [
          "PHY sample age",
          node.phyRateAgeSeconds !== null && node.phyRateAgeSeconds !== undefined
            ? `${formatNumber(node.phyRateAgeSeconds)} seconds`
            : "—",
        ],
        ...(isWired && node.reportedParentIpAddress
          ? [["Linksys-reported wired parent IP", `${node.reportedParentIpAddress}${node.reportedParentIpAddress !== node.parentIpAddress ? " · Differs from manual layout" : ""}`]]
          : []),
        ["Firmware version", node.firmwareVersion],
        ["Hardware version", node.hardwareVersion],
        ["Serial number", node.serialNumber],
      ],
    };
  }

  function nodeCapabilityReport(topology, node) {
    const isDemo = topology?.meta?.demo === true;
    const children = (topology?.nodes || [])
      .filter((candidate) => candidate.online && candidate.parentId === node?.id)
      .sort((a, b) => String(a.name || "").localeCompare(String(b.name || ""), "en-US", {
        numeric: true,
      }));
    const manualParentAvailable =
      topology?.network?.manualParentSelectionAvailable === true;
    const individualRestartAvailable =
      topology?.network?.individualNodeRestartAvailable === true;

    return {
      children,
      parentRole: node?.isAuthority
        ? {
            status: "gateway",
            label: "Primary gateway",
            detail: children.length
              ? `Direct downstream: ${children.map((child) => child.name).join(", ")}`
              : "No mesh nodes currently connect directly downstream",
          }
        : children.length
          ? {
              status: "confirmed",
              label: "Automatic parent · Verified",
              detail: `Direct downstream: ${children.map((child) => child.name).join(", ")}`,
            }
          : {
              status: "automatic",
              label: "Selected automatically by firmware",
              detail: "There are no downstream nodes now; this does not mean the node cannot become a parent",
            },
      manualTarget: manualParentAvailable
        ? {
            status: "available",
            label: "Reported as supported",
            detail: "A separate workflow with explicit child, parent, and confirmation steps is required",
          }
        : {
            status: topology?.network?.manualParentSelectionEvidence
              === "firmware-internal-confirmed"
              ? "internal"
              : "unsupported",
            label: topology?.network?.manualParentSelectionEvidence
              === "firmware-internal-confirmed"
              ? "Confirmed in firmware · Not exposed"
              : "No target interface found",
            detail: topology?.network?.manualParentSelectionEvidence
              === "firmware-internal-confirmed"
              ? "MX4200/WHW03 firmware contains an exact-parent data path, but current Web/JNAP interfaces expose no supported transport"
              : "Topology Optimization provides only global automatic Node Steering",
          },
      individualRestart: isDemo
        ? {
            status: "unverified",
            label: "Demo mode · Disabled",
            detail: "Requests are sent to a selected node's local endpoint only during a live connection",
          }
        : individualRestartAvailable
        ? {
            status: "available",
            label: "Selected node · Available",
            detail: "The request goes directly to the selected node's local endpoint",
          }
        : {
            status: "unverified",
            label: "Unavailable · Scope unverified",
            detail: "The official action restarts the entire mesh; no destructive probe was performed",
          },
      localManagement: isDemo
        ? {
            status: "unverified",
            label: "Demo data · No direct connection",
            detail: "A live connection verifies the node IP with local credentials synchronized from Main",
            url: null,
          }
        : node?.managementUrl
        ? {
            status: "available",
            label: "CA Support entry point",
            detail: `${node.ipAddress} · Uses local credentials synchronized from Main`,
            url: node.managementUrl,
          }
        : {
            status: "unavailable",
            label: "Currently unavailable",
            detail: "The node has no online management address",
            url: null,
          },
    };
  }

  return {
    clientNodeId,
    clientsForNode,
    nodeDetailRows,
    nodeForClient,
    nodeCapabilityReport,
  };
});
