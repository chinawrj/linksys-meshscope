const assert = require("node:assert/strict");
const test = require("node:test");
const {
  clientNodeId,
  clientsForNode,
  nodeCapabilityReport,
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

test("reports a node already proven as an automatic parent without claiming manual control", () => {
  const mesh = {
    network: {
      manualParentSelectionAvailable: false,
      individualNodeRestartAvailable: false,
      documentedRestartScope: "whole-network",
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
  assert.equal(report.manualTarget.status, "unsupported");
  assert.equal(report.individualRestart.status, "unverified");
  assert.equal(report.localManagement.status, "available");
  assert.equal(report.localManagement.url, "https://10.37.1.208/ca");
});
