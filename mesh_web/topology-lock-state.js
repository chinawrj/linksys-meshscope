(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  root.MeshTopologyLock = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const DEFAULT_LOCK = Object.freeze({
    supported: false,
    enabled: false,
    state: "unavailable",
    recoveryTransport: "mqtt",
    recoveryMode: "auto",
    recoveryEnabled: true,
    recoveryAvailable: false,
    lockedAt: "",
    cooldownSeconds: 300,
    nextActionInSeconds: 0,
    confirmationsRequired: 3,
    monitorIntervalSeconds: 10,
    summary: { total: 0, correct: 0, mismatch: 0, blocked: 0, offline: 0 },
    nodes: [],
    history: [],
  });

  function normalize(value) {
    const source = value && typeof value === "object" ? value : {};
    return {
      ...DEFAULT_LOCK,
      ...source,
      summary: { ...DEFAULT_LOCK.summary, ...(source.summary || {}) },
      nodes: Array.isArray(source.nodes) ? source.nodes : [],
      history: Array.isArray(source.history) ? source.history : [],
    };
  }

  function onlineTopology(nodes) {
    return (nodes || []).filter((node) => node.online);
  }

  function draftFrom(nodes, lockValue) {
    const lock = normalize(lockValue);
    const draft = {};
    const online = onlineTopology(nodes);
    const onlineIds = new Set(online.map((node) => node.id));
    const saved = new Map(
      lock.enabled
        ? lock.nodes.map((item) => [item.nodeId, item.expectedParentId])
        : [],
    );
    for (const node of online) {
      if (node.isAuthority) continue;
      const savedParent = saved.get(node.id);
      draft[node.id] = savedParent && onlineIds.has(savedParent)
        ? savedParent
        : node.parentId;
    }
    return draft;
  }

  function validate(nodes, draft) {
    const online = onlineTopology(nodes);
    const byId = new Map(online.map((node) => [node.id, node]));
    const root = online.find((node) => node.isAuthority);
    if (!root) return { valid: false, error: "The online gateway is unavailable." };
    const children = online.filter((node) => !node.isAuthority);
    if (Object.keys(draft || {}).length !== children.length) {
      return { valid: false, error: "Choose one desired parent for every online child node." };
    }
    for (const child of children) {
      const parentId = draft?.[child.id];
      if (!parentId || !byId.has(parentId)) {
        return { valid: false, error: `${child.name} needs an online desired parent.` };
      }
      if (parentId === child.id) {
        return { valid: false, error: `${child.name} cannot be its own parent.` };
      }
    }
    for (const child of children) {
      const seen = new Set([child.id]);
      let cursor = draft[child.id];
      while (cursor !== root.id) {
        if (seen.has(cursor)) {
          return { valid: false, error: "The desired topology contains a parent cycle." };
        }
        seen.add(cursor);
        cursor = draft[cursor];
        if (!cursor) {
          return { valid: false, error: "Every desired parent path must lead to the gateway." };
        }
      }
    }
    return { valid: true, error: "" };
  }

  function move(nodes, draft, nodeId, parentId) {
    const next = { ...(draft || {}), [nodeId]: parentId };
    const result = validate(nodes, next);
    return result.valid ? { ...result, draft: next } : { ...result, draft };
  }

  function applyDraft(nodes, draft) {
    const names = new Map((nodes || []).map((node) => [node.id, node.name]));
    return (nodes || []).map((node) => {
      const desiredParentId = !node.isAuthority && draft?.[node.id]
        ? draft[node.id]
        : node.parentId;
      return {
        ...node,
        actualParentId: node.parentId,
        actualParentName: node.parentName,
        parentId: desiredParentId,
        parentName: names.get(desiredParentId) || node.parentName,
      };
    });
  }

  function mappings(draft) {
    return Object.entries(draft || {})
      .sort(([left], [right]) => left.localeCompare(right))
      .map(([nodeId, parentId]) => ({ nodeId, parentId }));
  }

  function statusForNode(lockValue, nodeId) {
    return normalize(lockValue).nodes.find((item) => item.nodeId === nodeId) || null;
  }

  function remainingSeconds(lockValue, receivedAt, now = Date.now()) {
    const lock = normalize(lockValue);
    const elapsed = Math.max(0, Math.floor((now - Number(receivedAt || now)) / 1000));
    return Math.max(0, Number(lock.nextActionInSeconds || 0) - elapsed);
  }

  function duration(seconds) {
    const value = Math.max(0, Math.ceil(Number(seconds) || 0));
    const minutes = Math.floor(value / 60);
    const remainder = value % 60;
    return `${String(minutes).padStart(2, "0")}:${String(remainder).padStart(2, "0")}`;
  }

  function presentation(item, globalRemaining = 0, confirmationsRequired = 3) {
    if (!item) return null;
    switch (item.status) {
      case "correct":
        return { tone: "lock-correct", label: "Parent correct", detail: `Locked to ${item.expectedParentName}` };
      case "confirming":
        return {
          tone: "lock-confirming",
          label: `Parent mismatch · ${item.confirmations || 0}/${confirmationsRequired}`,
          detail: `Expected ${item.expectedParentName} · Current ${item.currentParentName || "unknown"}`,
        };
      case "cooldown":
        return {
          tone: "lock-cooldown",
          label: `Move in ${duration(globalRemaining || item.actionInSeconds)}`,
          detail: `Expected ${item.expectedParentName} · Current ${item.currentParentName || "unknown"}`,
        };
      case "steering-ready":
        return {
          tone: "lock-ready",
          label: "MQTT move queued",
          detail: `Expected ${item.expectedParentName} · Current ${item.currentParentName || "unknown"}`,
        };
      case "steering":
        return {
          tone: "lock-recovering",
          label: "MQTT move in progress",
          detail: `Moving to ${item.expectedParentName} · Waiting for topology verification`,
        };
      case "mqtt-disabled":
        return {
          tone: "lock-blocked",
          label: "MQTT recovery is off",
          detail: `Enable Parent Steering to restore ${item.expectedParentName}`,
        };
      case "mqtt-unavailable":
        return {
          tone: "lock-blocked",
          label: "Waiting for MQTT capability",
          detail: `Auto mode will restore ${item.expectedParentName} after a safe broker probe`,
        };
      case "parent-offline":
        return {
          tone: "lock-blocked",
          label: `Waiting for ${item.expectedParentName}`,
          detail: "Expected parent is offline · MQTT move blocked",
        };
      default:
        return { tone: "lock-offline", label: "Node offline", detail: `Expected ${item.expectedParentName}` };
    }
  }

  return {
    normalize,
    draftFrom,
    validate,
    move,
    applyDraft,
    mappings,
    statusForNode,
    remainingSeconds,
    duration,
    presentation,
  };
});
