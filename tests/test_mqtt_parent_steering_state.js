const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const Steering = require("../mesh_web/mqtt-parent-steering-state.js");

const WEB_ROOT = path.join(__dirname, "..", "mesh_web");

const nodes = [
  { id: "MAIN", name: "Main", online: true, isAuthority: true, parentId: null },
  { id: "YARD", name: "Yard", online: true, isAuthority: false, parentId: "MAIN", band: "5GH", connectionType: "Wireless" },
  { id: "TREE", name: "Tree", online: true, isAuthority: false, parentId: "YARD", band: "5GL", connectionType: "Wireless" },
  { id: "ROAD", name: "Road", online: true, isAuthority: false, parentId: "TREE", band: "5GH", connectionType: "Wireless" },
  { id: "WIRE", name: "Wired", online: true, isAuthority: false, parentId: "MAIN", connectionType: "Ethernet" },
  { id: "OFF", name: "Offline", online: false, isAuthority: false, parentId: "MAIN", band: "5GH" },
];

test("implements Auto, Force on, and Force off without bypassing request safety", () => {
  const autoUnavailable = Steering.normalize({ mode: "auto", available: false, roundTrip: false, testedAt: "now" });
  assert.equal(autoUnavailable.effectiveEnabled, false);
  assert.equal(autoUnavailable.state, "unavailable");

  const autoAvailable = Steering.normalize({ mode: "auto", available: true, roundTrip: true });
  assert.equal(autoAvailable.effectiveEnabled, true);
  assert.equal(autoAvailable.state, "available");

  const forced = Steering.normalize({ mode: "force-on", available: false, roundTrip: false });
  assert.equal(forced.effectiveEnabled, true);
  assert.equal(forced.state, "forced");
  assert.equal(
    Steering.validateRequest(nodes, { childId: "WIRE", parentId: "YARD", band: "5GH" }, null, forced).code,
    "wired_child",
  );

  const off = Steering.normalize({ mode: "force-off", available: true, roundTrip: true });
  assert.equal(off.effectiveEnabled, false);
  assert.equal(off.state, "disabled");
});

test("offers only eligible online wireless children and non-descendant parents", () => {
  assert.deepEqual(
    Steering.eligibleChildren(nodes).map((node) => node.id),
    ["YARD", "TREE", "ROAD"],
  );
  assert.deepEqual(
    Steering.eligibleParents(nodes, "YARD").map((node) => node.id),
    ["WIRE"],
  );
});

test("validates current Parent, descendant cycles, offline nodes, and exact bands", () => {
  const enabled = { mode: "force-on" };
  assert.equal(
    Steering.validateRequest(nodes, { childId: "TREE", parentId: "YARD", band: "5GH" }, null, enabled).code,
    "current_parent",
  );
  assert.equal(
    Steering.validateRequest(nodes, { childId: "YARD", parentId: "ROAD", band: "5GH" }, null, enabled).code,
    "descendant_parent",
  );
  assert.equal(
    Steering.validateRequest(nodes, { childId: "TREE", parentId: "OFF", band: "5GH" }, null, enabled).code,
    "offline_node",
  );
  assert.equal(
    Steering.validateRequest(nodes, { childId: "TREE", parentId: "MAIN", band: "6G" }, null, enabled).code,
    "invalid_band",
  );
  assert.equal(
    Steering.validateRequest(nodes, { childId: "TREE", parentId: "MAIN", band: "5gh" }, null, enabled).valid,
    true,
  );
});

test("blocks a request that would fight an enabled Topology Lock", () => {
  const lock = {
    enabled: true,
    nodes: [{ nodeId: "TREE", expectedParentId: "YARD", expectedParentName: "Yard" }],
  };
  const result = Steering.validateRequest(
    nodes,
    { childId: "TREE", parentId: "MAIN", band: "5GH" },
    lock,
    { mode: "force-on" },
  );
  assert.equal(result.code, "topology_lock_conflict");
  assert.match(result.error, /Topology Lock expects Yard/);
});

test("normalizes synchronous and asynchronous steering responses", () => {
  const sync = Steering.operationFromResponse(
    { accepted: true, target: { id: "MAIN", name: "Main", band: "5GH" }, requestedAt: "2026-01-01T00:00:00Z" },
    { childId: "TREE", childName: "Tree", parentId: "MAIN", parentName: "Main", band: "5GH" },
  );
  assert.equal(sync.state, "accepted");
  assert.equal(sync.accepted, true);
  assert.equal(sync.requestedParentId, "MAIN");

  const asyncOperation = Steering.operationFromResponse(
    { queued: true, operation: { id: "op-1", state: "resolving-target", childId: "TREE", requestedParentId: "MAIN" } },
  );
  assert.equal(asyncOperation.state, "resolving-target");
  assert.equal(asyncOperation.queued, true);
});

test("requires two distinct topology generations to verify the requested Parent", () => {
  let operation = Steering.operationFromResponse(
    { accepted: true, requestedAt: "2026-01-01T00:00:00Z" },
    { childId: "TREE", parentId: "MAIN", parentName: "Main", band: "5GH" },
  );
  const first = {
    meta: { generation: 7, updatedAt: "2026-01-01T00:00:10Z" },
    nodes: [{ id: "tree", parentId: "main", parentName: "Main" }],
  };
  operation = Steering.reconcileOperation(operation, first, Date.parse("2026-01-01T00:00:10Z"));
  assert.equal(operation.state, "verifying");
  assert.equal(operation.verification.observations, 1);

  operation = Steering.reconcileOperation(operation, first, Date.parse("2026-01-01T00:00:11Z"));
  assert.equal(operation.verification.observations, 1);
  assert.equal(operation.state, "verifying");

  operation = Steering.reconcileOperation(operation, {
    ...first,
    meta: { generation: 8, updatedAt: "2026-01-01T00:00:20Z" },
  }, Date.parse("2026-01-01T00:00:20Z"));
  assert.equal(operation.state, "verified");
  assert.equal(operation.verification.observations, 2);
});

test("a mismatching fresh generation resets verification and timeout never reports success", () => {
  let operation = Steering.normalizeOperation({
    state: "verifying",
    childId: "TREE",
    requestedParentId: "MAIN",
    requestedAt: "2026-01-01T00:00:00Z",
    lastGeneration: 7,
    verification: { observations: 1, required: 2 },
  });
  operation = Steering.reconcileOperation(operation, {
    meta: { generation: 8 },
    nodes: [{ id: "TREE", parentId: "YARD", parentName: "Yard" }],
  }, Date.parse("2026-01-01T00:00:20Z"));
  assert.equal(operation.verification.observations, 0);
  assert.equal(operation.verification.observedParentName, "Yard");

  operation = Steering.reconcileOperation(
    operation,
    { meta: { generation: 9 }, nodes: [{ id: "TREE", parentId: "YARD" }] },
    Date.parse("2026-01-01T00:03:00Z"),
  );
  assert.equal(operation.state, "timed-out");
  assert.equal(Steering.operationPresentation(operation).tone, "failed");
});

test("keeps a local operation when capability responses omit operation state", () => {
  const operation = { state: "accepted", childId: "TREE", requestedParentId: "MAIN" };
  const report = Steering.normalize(
    { mode: "auto", available: true, roundTrip: true },
    { mode: "auto", operation },
  );
  assert.equal(report.operation.state, "accepted");
  assert.equal(report.operation.childId, "TREE");
});

test("normalizes ESP32 capability and operation wire names", () => {
  const report = Steering.normalize({
    mode: "auto",
    available: true,
    roundTrip: true,
    testedAt: "2026-08-09T12:00:00Z",
    operation: {
      id: 9,
      state: "verification-pending",
      childId: "child",
      parentId: "parent",
      consecutiveMatches: 1,
      requiredMatches: 2,
    },
  });

  assert.equal(report.effectiveEnabled, true);
  assert.equal(report.state, "available");
  assert.equal(report.operation.state, "verifying");
  assert.equal(report.operation.verification.observations, 1);
  assert.equal(report.operation.verification.required, 2);
  assert.equal(Steering.isOperationActive(report.operation), true);

  const resolving = Steering.normalizeOperation({ state: "discovering-target" });
  assert.equal(resolving.state, "resolving-target");
  assert.equal(Steering.isOperationActive(resolving), true);
});

test("renders an accessible steering form without removing existing topology and client information", () => {
  const html = fs.readFileSync(path.join(WEB_ROOT, "index.html"), "utf8");
  const app = fs.readFileSync(path.join(WEB_ROOT, "app.js"), "utf8");
  const helper = fs.readFileSync(path.join(WEB_ROOT, "mqtt-parent-steering-state.js"), "utf8");
  for (const fragment of [
    'id="parentSteeringPanel"',
    'id="parentSteeringMode"',
    '<option value="auto" selected>Auto</option>',
    '<option value="force-on">Force on</option>',
    '<option value="force-off">Force off</option>',
    'id="parentSteeringChild"',
    'id="parentSteeringParent"',
    'id="parentSteeringBand"',
    'id="parentSteeringOperation" role="status" aria-live="polite"',
    "5GH · high band",
    "5GL · low band",
    "Mesh Topology",
    "Current clients / STAs",
    "Clients",
    'id="topologyLockPanel"',
    'id="restartMeshButton"',
  ]) {
    assert.ok(html.includes(fragment) || app.includes(fragment), fragment);
  }
  assert.match(app, /MeshMqttParentSteering\.reconcileOperation/);
  assert.match(helper, /Topology Lock expects/);
  assert.match(app, /parent-steering-expanded/);
});
