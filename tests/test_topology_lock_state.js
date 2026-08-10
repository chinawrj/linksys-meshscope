const test = require("node:test");
const assert = require("node:assert/strict");

const MeshTopologyLock = require("../mesh_web/topology-lock-state.js");

const nodes = [
  { id: "main", name: "Main", online: true, isAuthority: true, parentId: null },
  { id: "yard", name: "Yard", online: true, isAuthority: false, parentId: "main", parentName: "Main" },
  { id: "tree", name: "Studio", online: true, isAuthority: false, parentId: "yard", parentName: "Yard" },
  { id: "road", name: "Patio", online: true, isAuthority: false, parentId: "tree", parentName: "Studio" },
  { id: "old", name: "Old node", online: false, isAuthority: false, parentId: null },
];

test("captures one desired parent for every online child node", () => {
  const draft = MeshTopologyLock.draftFrom(nodes, null);
  assert.deepEqual(draft, { yard: "main", tree: "yard", road: "tree" });
  assert.equal(MeshTopologyLock.validate(nodes, draft).valid, true);
  assert.deepEqual(MeshTopologyLock.mappings(draft), [
    { nodeId: "road", parentId: "tree" },
    { nodeId: "tree", parentId: "yard" },
    { nodeId: "yard", parentId: "main" },
  ]);
});

test("editing a saved map fills new online children and drops offline-only entries", () => {
  const changed = [
    ...nodes,
    { id: "new", name: "Loft", online: true, isAuthority: false, parentId: "main", parentName: "Main" },
  ];
  const draft = MeshTopologyLock.draftFrom(changed, {
    enabled: true,
    nodes: [
      { nodeId: "tree", expectedParentId: "main" },
      { nodeId: "road", expectedParentId: "tree" },
      { nodeId: "offline-old", expectedParentId: "main" },
    ],
  });
  assert.deepEqual(draft, {
    yard: "main",
    tree: "main",
    road: "tree",
    new: "main",
  });
  assert.equal(MeshTopologyLock.validate(changed, draft).valid, true);
});

test("dragging changes only the desired topology preview", () => {
  const draft = MeshTopologyLock.draftFrom(nodes, null);
  const result = MeshTopologyLock.move(nodes, draft, "road", "yard");
  assert.equal(result.valid, true);
  assert.equal(result.draft.road, "yard");
  assert.equal(nodes.find((node) => node.id === "road").parentId, "tree");
  const preview = MeshTopologyLock.applyDraft(nodes, result.draft);
  const road = preview.find((node) => node.id === "road");
  assert.equal(road.parentId, "yard");
  assert.equal(road.actualParentId, "tree");
});

test("rejects self-parent and descendant cycles", () => {
  const draft = MeshTopologyLock.draftFrom(nodes, null);
  assert.equal(MeshTopologyLock.move(nodes, draft, "tree", "tree").valid, false);
  const cycle = MeshTopologyLock.move(nodes, draft, "yard", "road");
  assert.equal(cycle.valid, false);
  assert.match(cycle.error, /cycle/i);
});

test("countdown and node presentation remain visible on the topology card", () => {
  const lock = MeshTopologyLock.normalize({
    supported: true,
    enabled: true,
    nextActionInSeconds: 300,
    nodes: [{
      nodeId: "road",
      expectedParentName: "Studio",
      currentParentName: "Main",
      status: "cooldown",
      confirmations: 3,
    }],
  });
  assert.equal(MeshTopologyLock.remainingSeconds(lock, 10_000, 70_000), 240);
  assert.equal(MeshTopologyLock.duration(240), "04:00");
  assert.deepEqual(MeshTopologyLock.presentation(lock.nodes[0], 240), {
    tone: "lock-cooldown",
    label: "Move in 04:00",
    detail: "Expected Studio · Current Main",
  });
  assert.match(
    MeshTopologyLock.presentation({
      status: "confirming",
      confirmations: 2,
      expectedParentName: "Studio",
      currentParentName: "Main",
    }, 0, 4).label,
    /2\/4/,
  );
});

test("shows exact MQTT steering and disabled recovery states", () => {
  assert.match(MeshTopologyLock.presentation({
    status: "steering",
    expectedParentName: "Studio",
  }).label, /MQTT move/i);
  assert.match(MeshTopologyLock.presentation({
    status: "mqtt-disabled",
    expectedParentName: "Studio",
  }).detail, /Enable Parent Steering/i);
  assert.match(MeshTopologyLock.presentation({
    status: "mqtt-unavailable",
    expectedParentName: "Studio",
  }).label, /MQTT capability/i);
});

test("shows a blocked state when the desired parent is offline", () => {
  const view = MeshTopologyLock.presentation({
    status: "parent-offline",
    expectedParentName: "Studio",
  });
  assert.equal(view.tone, "lock-blocked");
  assert.match(view.label, /Studio/);
  assert.match(view.detail, /blocked/i);
});
