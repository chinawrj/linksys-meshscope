(function (root, factory) {
  const engine = factory();
  if (typeof module === "object" && module.exports) module.exports = engine;
  else root.MeshNodeRestartState = engine;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const OPERATION_TIMEOUT_MS = 90_000;
  const POLL_DELAYS_MS = [8_000, 20_000, 45_000, 90_000];

  function begin(operations, node, now = Date.now()) {
    const operation = {
      nodeId: node.id,
      name: node.name,
      requestedAt: now,
      sawOffline: false,
      phase: "requested",
    };
    operations.set(node.id, operation);
    return operation;
  }

  function label(operation) {
    if (!operation) return "";
    return operation.sawOffline ? "正在恢复上线" : "重启请求已发送";
  }

  function requestBody(node) {
    return { nodeId: node.id };
  }

  function reconcile(operations, nodes, now = Date.now()) {
    const events = [];
    for (const [nodeId, operation] of operations) {
      const node = nodes.find((candidate) => candidate.id === nodeId);
      if (node && !node.online) {
        operation.sawOffline = true;
        operation.phase = "offline";
        continue;
      }
      if (node?.online && operation.sawOffline) {
        operations.delete(nodeId);
        events.push({ type: "recovered", nodeId, name: operation.name });
        continue;
      }
      if (now - operation.requestedAt >= OPERATION_TIMEOUT_MS) {
        operations.delete(nodeId);
        events.push({
          type: node?.online ? "online-timeout" : "observation-timeout",
          nodeId,
          name: operation.name,
        });
      }
    }
    return events;
  }

  return {
    OPERATION_TIMEOUT_MS,
    POLL_DELAYS_MS,
    begin,
    label,
    reconcile,
    requestBody,
  };
});
