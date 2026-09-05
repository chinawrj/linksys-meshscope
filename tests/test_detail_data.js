const assert = require("node:assert/strict");
const test = require("node:test");
const {
  clientNodeId,
  clientsForNode,
  nodeCapabilityReport,
  nodeDetailRows,
  nodeForClient,
} = require("../mesh_web/detail-data.js");

const topology = {
  nodes: [
    { id: "main", name: "Main" },
    { id: "door", name: "Office" },
  ],
  clients: [
    { id: "b", name: "Tablet 2", online: true, nodeId: "door" },
    { id: "a", name: "Tablet 1", online: true, parentId: "door" },
    { id: "c", name: "Old phone", online: false, nodeId: "door" },
    { id: "d", name: "Camera", online: true, nodeId: "main" },
  ],
};

test("associates online clients with a node by nodeId or legacy parentId", () => {
  assert.equal(clientNodeId(topology.clients[0]), "door");
  assert.equal(clientNodeId(topology.clients[1]), "door");
  assert.deepEqual(
    clientsForNode(topology, topology.nodes[1]).map((client) => client.id),
    ["a", "b"],
  );
});

test("does not mix historical clients into the live STA list", () => {
  assert.equal(clientsForNode(topology, topology.nodes[1]).length, 2);
  assert.equal(
    clientsForNode(topology, topology.nodes[1], { onlineOnly: false }).length,
    3,
  );
});

test("finds the parent node when opening a client detail", () => {
  assert.equal(nodeForClient(topology, topology.clients[0]).name, "Office");
  assert.equal(nodeForClient(topology, { nodeId: "missing" }), null);
});

test("preserves every existing node metric and identity field", () => {
  const rows = nodeDetailRows(
    {
      online: true,
      clientCount: 3,
      speedMbps: 199.4,
      phyRateMbps: 1729.3,
      phyRateRaw: "1.7293 Gb/s",
      phyRateAgeSeconds: 8,
      phyRateStale: false,
      rssi: -66,
      quality: { label: "Good" },
      model: "WHW03",
      ipAddress: "192.168.1.10",
      macAddress: "AA:BB:CC:DD:EE:FF",
      parentName: "Patio",
      band: "5GL",
      channel: 48,
      timestamp: "2026-08-30T08:00:00Z",
      firmwareVersion: "2.1.20.216892",
      hardwareVersion: "2",
      serialNumber: "test-serial",
    },
    (value, digits = 0) => Number(value).toFixed(digits),
  );

  assert.deepEqual(
    rows.metrics.map(([label]) => label),
    ["Current status", "Connected clients", "Hop throughput", "Link PHY rate", "Backhaul signal"],
  );
  assert.deepEqual(
    rows.details.map(([label]) => label),
    ["Model", "IP address", "MAC address", "Connection type", "Parent node", "Backhaul band", "Channel", "Hop source", "Hop sample time", "PHY source", "PHY raw value", "PHY sample age", "Firmware version", "Hardware version", "Serial number"],
  );
  assert.equal(rows.metrics[2][1], "199 Mbps");
  assert.equal(rows.metrics[3][1], "1.73 Gbps");
  assert.equal(rows.details[4][1], "Patio");
  assert.match(rows.details[7][1], /Child → Parent Thrulay/);
  assert.equal(rows.details[8][1], "2026-08-30T08:00:00Z");
});

test("missing PHY data stays unknown instead of displaying a zero rate", () => {
  for (const value of [null, undefined, ""]) {
    const row = nodeDetailRows({phyRateMbps:value}).metrics.find(([label]) => label === "Link PHY rate");
    assert.equal(row[1], "—");
  }
});

test("labels wired rate fields and preserves the unverified Linksys parent evidence", () => {
  const rows = nodeDetailRows({
    online: true,
    clientCount: 1,
    connectionType: "Wired",
    isWired: true,
    speedMbps: 1024,
    phyRateMbps: 1000,
    quality: { label: "Wired" },
    model: "MX5300",
    ipAddress: "10.37.1.122",
    parentName: "Main",
    parentIpAddress: "10.37.1.1",
    reportedParentIpAddress: "10.37.1.12",
    parentSource: "topology-lock-wired-assignment",
  });
  assert.equal(rows.metrics[2][0], "Ethernet link");
  assert.equal(rows.metrics[3][0], "Port PHY rate");
  assert.equal(rows.metrics[4][1], "Ethernet · No RF signal");
  assert.equal(rows.details.find(([label]) => label === "Parent node")[1], "Main");
  assert.match(rows.details.find(([label]) => label === "Wired parent basis")[1], /Manual/);
  assert.match(rows.details.find(([label]) => label === "Linksys-reported wired parent IP")[1], /10\.37\.1\.12/);
});

test("reports automatic parent status and internal steering evidence without claiming web control", () => {
  const mesh = {
    network: {
      manualParentSelectionAvailable: false,
      manualParentSelectionEvidence: "firmware-internal-confirmed",
      individualNodeRestartAvailable: true,
      documentedRestartScope: "single-node",
    },
    nodes: [
      { id: "main", name: "Main", online: true, isAuthority: true },
      {
        id: "big",
        name: "Atrium",
        online: true,
        parentId: "main",
        ipAddress: "192.168.1.10",
        managementUrl: "https://192.168.1.10/ca",
      },
      { id: "road", name: "Garage", online: true, parentId: "big" },
    ],
  };
  const report = nodeCapabilityReport(mesh, mesh.nodes[1]);
  assert.equal(report.parentRole.status, "confirmed");
  assert.deepEqual(report.children.map((node) => node.name), ["Garage"]);
  assert.equal(report.manualTarget.status, "internal");
  assert.match(report.manualTarget.label, /Confirmed in firmware/);
  assert.equal(report.individualRestart.status, "available");
  assert.equal(report.localManagement.status, "available");
  assert.equal(report.localManagement.url, "https://192.168.1.10/ca");
});

test("demo mode keeps capability information but disables live node actions", () => {
  const mesh = {
    meta: { demo: true },
    network: {
      manualParentSelectionAvailable: false,
      manualParentSelectionEvidence: "firmware-internal-confirmed",
      individualNodeRestartAvailable: true,
    },
    nodes: [
      {
        id: "big",
        name: "Atrium",
        online: true,
        ipAddress: "192.168.1.10",
        managementUrl: "https://192.168.1.10/ca",
      },
    ],
  };
  const report = nodeCapabilityReport(mesh, mesh.nodes[0]);
  assert.equal(report.manualTarget.status, "internal");
  assert.equal(report.individualRestart.status, "unverified");
  assert.equal(report.localManagement.status, "unverified");
  assert.equal(report.localManagement.url, null);
});
