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

test("normalizes per-child Parent steering health and exposes restart diagnostics", () => {
  const report = Steering.normalize({
    failureThreshold: 2,
    parentRestartCooldownSeconds: 300,
    nodeHealth: [{
      childId: "YARD",
      childName: "YardFront",
      targetParentId: "TREE",
      targetParentName: "BigTree",
      band: "5GL",
      state: "restart-eligible",
      consecutiveFailures: 2,
      totalFailures: 4,
      successfulMoves: 1,
      targetParentOnline: true,
      targetParentOnlineChildren: 0,
      lastRequestPublished: true,
      lastCommandEchoed: true,
      lastTargetBssid: "E8:9F:80:56:6D:90",
      lastTargetChannel: 36,
      lastOperationId: 17,
    }],
  });
  const health = Steering.healthForNode(report, "yard");
  assert.equal(health.failureThreshold, 2);
  assert.equal(health.targetParentOnlineChildren, 0);
  assert.equal(health.lastRequestPublished, true);
  assert.equal(health.lastTargetChannel, 36);
  const presentation = Steering.healthPresentation(health);
  assert.equal(presentation.tone, "attention");
  assert.match(presentation.label, /2\/2/);
  assert.match(presentation.detail, /0 online mesh children/);
  assert.match(presentation.detail, /published \+ echoed/);
});

test("Parent restart countdown decreases without hiding persisted statistics", () => {
  const receivedAt = Date.parse("2026-08-10T10:00:00Z");
  const health = Steering.normalizeHealth({
    childId: "YARD",
    targetParentId: "TREE",
    targetParentName: "BigTree",
    state: "cooldown",
    consecutiveFailures: 0,
    lastTriggerFailures: 2,
    restartInSeconds: 300,
    receivedAt,
  });
  assert.equal(
    Steering.healthRestartRemaining(health, receivedAt + 125_000),
    175,
  );
  const view = Steering.healthPresentation(health, receivedAt + 125_000);
  assert.equal(view.tone, "cooldown");
  assert.match(view.label, /175s/);
  assert.equal(view.health.lastTriggerFailures, 2);
});

test("keeps healthy steering history in details without showing persistent topology warnings", () => {
  const healthy = Steering.normalizeHealth({
    childId: "YARD",
    targetParentId: "TREE",
    targetParentName: "BigTree",
    state: "idle",
    reason: "No unresolved steering failures",
    consecutiveFailures: 0,
    totalFailures: 2,
    successfulMoves: 5,
  });
  const detail = Steering.healthPresentation(healthy);
  assert.equal(detail.tone, "healthy");
  assert.equal(detail.health.totalFailures, 2);
  assert.equal(detail.health.successfulMoves, 5);
  assert.equal(Steering.healthCardPresentation(healthy), null);

  const recovered = Steering.normalizeHealth({
    ...healthy,
    state: "recovered",
  });
  assert.equal(Steering.healthCardPresentation(recovered), null);
});

test("keeps unresolved and active Parent health states visible on topology cards", () => {
  const watching = Steering.normalizeHealth({
    childId: "YARD",
    targetParentId: "TREE",
    state: "watching",
    consecutiveFailures: 1,
    failureThreshold: 2,
  });
  assert.equal(Steering.healthCardPresentation(watching).tone, "watching");

  const restarting = Steering.normalizeHealth({
    ...watching,
    state: "parent-restarting",
    consecutiveFailures: 0,
  });
  assert.equal(Steering.healthCardPresentation(restarting).tone, "restarting");
});

test("passive recovery removes current alerts but retains all historical evidence", () => {
  const history = {
    childId: "YARD", targetParentId: "PARENT", targetParentName: "Patio",
    lastOperationId: 4, state: "recovered", consecutiveFailures: 0,
    totalFailures: 13, successfulMoves: 4, lastCommandEchoed: true,
    lastFailureAt: "2026-09-06T01:45:07Z", lastSuccessAt: "2026-09-05T03:46:19Z",
    lastRecoveredAt: "2026-09-06T13:00:00Z",
  };
  const health = Steering.normalizeHealth(history);
  const operation = {id: 4, childId: "yard", parentId: "parent", state: "failed",
    requestedAt: "2026-09-06T01:42:00Z"};
  assert.equal(Steering.healthCardPresentation(health), null);
  assert.equal(Steering.operationCardPresentation(operation, health), null);
  assert.equal(Steering.operationPresentation(operation).tone, "failed", "operation history is unchanged");
  const details = Steering.healthPresentation(health);
  for (const key of ["totalFailures", "successfulMoves", "lastFailureAt", "lastSuccessAt", "lastRecoveredAt", "lastCommandEchoed"]) {
    assert.equal(details.health[key], history[key]);
  }
  assert.match(details.label, /Parent recovered/);
  for (const change of [{id: 5}, {childId: "other"}, {parentId: "other"}, {state: "verification-pending"}, {id: 0},
    {requestedAt: "2026-09-07T00:00:00Z"}, {requestedAt: ""}]) {
    assert.notEqual(Steering.operationCardPresentation({...operation, ...change}, health), null);
  }
  assert.notEqual(Steering.operationCardPresentation(operation, {...health, consecutiveFailures: 1}), null);
});

test("first recovery observation remains visible as confirmation, not a restart alarm", () => {
  const view = Steering.healthCardPresentation({
    childId: "YARD", targetParentId: "PARENT", state: "confirming",
    consecutiveFailures: 2, recoveryMatches: 1, recoveryRequired: 2,
  });
  assert.equal(view.tone, "watching");
  assert.equal(view.label, "Confirming Parent recovery · 1/2");
  assert.equal(view.health.consecutiveFailures, 2);
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
  assert.match(app, /PARENT STEERING HEALTH/);
  assert.match(app, /Parent online children/);
  assert.match(app, /MQTT publish \/ echo/);
  assert.match(app, /healthCardPresentation/);
  assert.match(app, /parent-health-expanded/);
});
