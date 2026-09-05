const assert = require("node:assert/strict");
const test = require("node:test");
const { normalize, normalizeEnvelope } = require("../mesh_web/linksys-normalize.js");

function response(output) {
  return { result: "OK", output };
}

const raw = {
  "core/GetDeviceInfo": response({
    manufacturer: "Linksys",
    modelNumber: "MX42",
    firmwareVersion: "1.0.13.216903",
  }),
  "devicelist/GetDevices3": response({
    revision: 7,
    devices: [
      {
        deviceID: "main",
        friendlyName: "Main",
        isAuthority: true,
        nodeType: "Master",
        model: { modelNumber: "MX42" },
        unit: { firmwareVersion: "1.0.13.216903" },
        connections: [{ macAddress: "00:00:00:00:00:01", ipAddress: "10.0.0.1" }],
      },
      {
        deviceID: "child",
        friendlyName: "Atrium",
        nodeType: "Slave",
        model: { modelNumber: "MX5300" },
        unit: { firmwareVersion: "1.1.12.210066" },
        connections: [{ macAddress: "00:00:00:00:00:02", ipAddress: "10.0.0.2" }],
      },
      {
        deviceID: "client",
        friendlyName: "Phone",
        model: { modelNumber: "iPhone" },
        knownMACAddresses: ["00:00:00:00:00:03"],
        connections: [{ macAddress: "00:00:00:00:00:03", ipAddress: "10.0.0.3" }],
      },
      {
        deviceID: "old-client",
        friendlyName: "Old tablet",
        model: { modelNumber: "iPad" },
        connections: [],
      },
    ],
  }),
  "nodes/diagnostics/GetBackhaulInfo": response({
    backhaulDevices: [
      {
        deviceUUID: "child",
        ipAddress: "10.0.0.2",
        parentIPAddress: "10.0.0.1",
        connectionType: "Wireless",
        wirelessConnectionInfo: { radioID: "5GH", channel: 149, stationRSSI: -58 },
        speedMbps: "512.5",
      },
    ],
  }),
  "networkconnections/GetNetworkConnections2": response({ connections: [] }),
  "nodes/networkconnections/GetNodesWirelessNetworkConnections": response({
    nodeWirelessConnections: [
      {
        deviceID: "child",
        connections: [
          {
            macAddress: "00:00:00:00:00:03",
            negotiatedMbps: 433,
            wireless: { band: "5GHz", signalDecibels: -61 },
          },
        ],
      },
    ],
  }),
  "nodes/topologyoptimization/GetTopologyOptimizationSettings2": response({
    isClientSteeringEnabled: true,
    isNodeSteeringEnabled: true,
  }),
  "router/GetWANStatus3": response({
    wanStatus: "Connected",
    wanConnection: { wanType: "DHCP" },
  }),
  "router/GetLANSettings": response({ ipAddress: "10.0.0.1" }),
  "wirelessap/GetRadioInfo3": response({ radios: [{}, {}, {}] }),
};

test("normalizes ESP32 raw JNAP into the existing MeshScope schema", () => {
  const result = normalize("10.0.0.1", raw, {
    updatedAt: "2026-01-01T00:00:00Z",
    generation: 42,
    edgeAddress: "10.0.0.50",
    routerConnected: false,
    clientDetails: "nodes-only",
    topologyLock: { supported: true, enabled: true },
    backhaulPhyLinks: [{
      nodeId: "CHILD",
      rateMbps: 1729.3,
      rawRate: "1.7293 Gb/s",
      observedAt: "2026-01-01T00:00:01Z",
      ageSeconds: 7,
      stale: false,
    }],
  });
  const child = result.nodes.find((item) => item.id === "child");
  const client = result.clients.find((item) => item.id === "client");

  assert.equal(result.meta.source, "Linksys JNAP · ESP32 MeshScope");
  assert.equal(result.meta.generation, 42);
  assert.equal(result.meta.edgeAddress, "10.0.0.50");
  assert.equal(result.meta.routerConnected, false);
  assert.equal(result.meta.clientDetails, "nodes-only");
  assert.equal(result.meta.topologyLock.enabled, true);
  assert.equal(result.summary.nodesOnline, 2);
  assert.equal(result.summary.clientsOnline, 1);
  assert.equal(result.summary.clientsKnown, 2);
  assert.equal(child.parentName, "Main");
  assert.equal(child.band, "5GH");
  assert.equal(child.speedMbps, 512.5);
  assert.equal(child.phyRateMbps, 1729.3);
  assert.equal(child.phyRateRaw, "1.7293 Gb/s");
  assert.equal(child.phyRateAgeSeconds, 7);
  assert.equal(child.phyRateStale, false);
  assert.equal(child.clientCount, 1);
  assert.equal(client.nodeName, "Atrium");
  assert.equal(client.speedMbps, 433);
  assert.equal(result.network.radioCount, 3);
  assert.equal(result.network.nodeSteeringEnabled, true);
});

test("normalizes only ESP32 envelopes and preserves Python responses", () => {
  const existing = { meta: { source: "Linksys JNAP · Local read-only access" }, nodes: [], clients: [] };
  assert.equal(normalizeEnvelope(existing), existing);

  const result = normalizeEnvelope({
    router: "10.0.0.1",
    meta: { updatedAt: "2026-01-01T00:00:00Z" },
    rawJnap: raw,
  });
  assert.equal(result.meta.edgeHosted, true);
  assert.equal(result.meta.router, "10.0.0.1");
});

test("uses a saved wired layout instead of trusting an ambiguous LLDP parent", () => {
  const wiredRaw = structuredClone(raw);
  wiredRaw["devicelist/GetDevices3"].output.devices.push({
    deviceID: "wired",
    friendlyName: "LivingRoom",
    nodeType: "Slave",
    model: { modelNumber: "MX5300" },
    connections: [{ macAddress: "00:00:00:00:00:04", ipAddress: "10.0.0.4" }],
  });
  wiredRaw["nodes/diagnostics/GetBackhaulInfo"].output.backhaulDevices.push({
    deviceUUID: "wired",
    ipAddress: "10.0.0.4",
    parentIPAddress: "10.0.0.2",
    connectionType: "Wired",
    speedMbps: "1024",
  });

  const result = normalize("10.0.0.1", wiredRaw, {
    topologyLock: {
      enabled: true,
      nodes: [{ nodeId: "wired", expectedParentId: "main" }],
    },
    backhaulPhyLinks: [{ nodeId: "WIRED", rateMbps: 1000, rawRate: "1 Gb/s" }],
  });
  const wired = result.nodes.find((item) => item.id === "wired");

  assert.equal(wired.parentId, "main");
  assert.equal(wired.parentName, "Main");
  assert.equal(wired.parentIpAddress, "10.0.0.1");
  assert.equal(wired.reportedParentId, "child");
  assert.equal(wired.reportedParentIpAddress, "10.0.0.2");
  assert.equal(wired.parentSource, "topology-lock-wired-assignment");
  assert.equal(wired.parentConfidence, "manual");
  assert.equal(wired.linkType, "Ethernet");
  assert.equal(wired.quality.label, "Wired");
  assert.equal(wired.speedMbps, 1024);
  assert.equal(wired.phyRateMbps, 1000);
});

test("preserves the Linksys LLDP parent as unverified when no wired layout is saved", () => {
  const wiredRaw = structuredClone(raw);
  wiredRaw["devicelist/GetDevices3"].output.devices.push({
    deviceID: "wired",
    friendlyName: "LivingRoom",
    nodeType: "Slave",
    model: { modelNumber: "MX5300" },
    connections: [{ ipAddress: "10.0.0.4" }],
  });
  wiredRaw["nodes/diagnostics/GetBackhaulInfo"].output.backhaulDevices.push({
    deviceUUID: "wired",
    ipAddress: "10.0.0.4",
    parentIPAddress: "10.0.0.2",
    connectionType: "Wired",
  });
  const wired = normalize("10.0.0.1", wiredRaw).nodes.find((item) => item.id === "wired");
  assert.equal(wired.parentId, "child");
  assert.equal(wired.parentSource, "linksys-lldp-reported");
  assert.equal(wired.parentConfidence, "firmware-reported-unverified");
});
