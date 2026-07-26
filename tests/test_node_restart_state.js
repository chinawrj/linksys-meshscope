const assert = require("node:assert/strict");
const test = require("node:test");
const {
  OPERATION_TIMEOUT_MS,
  POLL_DELAYS_MS,
  begin,
  label,
  reconcile,
  requestBody,
} = require("../mesh_web/node-restart-state.js");

test("tracks requested, offline, and recovered phases", () => {
  const operations = new Map();
  const operation = begin(operations, { id: "big", name: "BigTree" }, 1_000);

  assert.equal(label(operation), "重启请求已发送");
  assert.deepEqual(reconcile(operations, [{ id: "big", online: true }], 5_000), []);

  assert.deepEqual(reconcile(operations, [{ id: "big", online: false }], 10_000), []);
  assert.equal(operation.phase, "offline");
  assert.equal(label(operation), "正在恢复上线");

  assert.deepEqual(
    reconcile(operations, [{ id: "big", online: true }], 20_000),
    [{ type: "recovered", nodeId: "big", name: "BigTree" }],
  );
  assert.equal(operations.has("big"), false);
});

test("clears an operation after the observation timeout", () => {
  const operations = new Map();
  begin(operations, { id: "big", name: "BigTree" }, 0);

  assert.deepEqual(
    reconcile(
      operations,
      [{ id: "big", online: true }],
      OPERATION_TIMEOUT_MS,
    ),
    [{ type: "online-timeout", nodeId: "big", name: "BigTree" }],
  );
  assert.equal(operations.size, 0);
});

test("uses bounded follow-up polls without hiding normal auto refresh", () => {
  assert.deepEqual(POLL_DELAYS_MS, [8_000, 20_000, 45_000, 90_000]);
});

test("builds a direct restart request with no confirmation field", () => {
  assert.deepEqual(requestBody({ id: "big", name: "BigTree" }), { nodeId: "big" });
  assert.equal("confirmation" in requestBody({ id: "big" }), false);
});
