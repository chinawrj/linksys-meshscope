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
    { id: "door", name: "DoorCorner" },
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
  assert.equal(nodeForClient(topology, topology.clients[0]).name, "DoorCorner");
  assert.equal(nodeForClient(topology, { nodeId: "missing" }), null);
});

test("preserves every existing node metric and identity field", () => {
  const rows = nodeDetailRows(
    {
      online: true,
      clientCount: 3,
      speedMbps: 199.4,
      rssi: -66,
      quality: { label: "良好" },
      model: "WHW03",
      ipAddress: "10.37.1.208",
      macAddress: "AA:BB:CC:DD:EE:FF",
      parentName: "YardEast-Wi-Fi6",
      band: "5GL",
      channel: 48,
      firmwareVersion: "2.1.20.216892",
      hardwareVersion: "2",
      serialNumber: "test-serial",
    },
    (value) => Number(value).toFixed(0),
  );

  assert.deepEqual(
    rows.metrics.map(([label]) => label),
    ["当前状态", "接入客户端", "回程速率", "回程信号"],
  );
  assert.deepEqual(
    rows.details.map(([label]) => label),
    ["型号", "IP 地址", "MAC 地址", "父节点", "回程频段", "信道", "固件版本", "硬件版本", "序列号"],
  );
  assert.equal(rows.metrics[2][1], "199 Mbps");
  assert.equal(rows.details[3][1], "YardEast-Wi-Fi6");
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
        name: "BigTree",
        online: true,
        parentId: "main",
        ipAddress: "10.37.1.208",
        managementUrl: "https://10.37.1.208/ca",
      },
      { id: "road", name: "RoadSouth", online: true, parentId: "big" },
    ],
  };
  const report = nodeCapabilityReport(mesh, mesh.nodes[1]);
  assert.equal(report.parentRole.status, "confirmed");
  assert.deepEqual(report.children.map((node) => node.name), ["RoadSouth"]);
  assert.equal(report.manualTarget.status, "internal");
  assert.match(report.manualTarget.label, /内部已确认/);
  assert.equal(report.individualRestart.status, "available");
  assert.equal(report.localManagement.status, "available");
  assert.equal(report.localManagement.url, "https://10.37.1.208/ca");
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
        name: "BigTree",
        online: true,
        ipAddress: "10.37.1.208",
        managementUrl: "https://10.37.1.208/ca",
      },
    ],
  };
  const report = nodeCapabilityReport(mesh, mesh.nodes[0]);
  assert.equal(report.manualTarget.status, "internal");
  assert.equal(report.individualRestart.status, "unverified");
  assert.equal(report.localManagement.status, "unverified");
  assert.equal(report.localManagement.url, null);
});
