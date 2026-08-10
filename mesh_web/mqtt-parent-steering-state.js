(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  root.MeshMqttParentSteering = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const MODES = Object.freeze(["auto", "force-on", "force-off"]);
  const BANDS = Object.freeze(["5GH", "5GL"]);
  const ACTIVE_OPERATION_STATES = new Set([
    "queued",
    "probing",
    "resolving-target",
    "publishing",
    "accepted",
    "verifying",
  ]);

  const DEFAULT_REPORT = Object.freeze({
    mode: "auto",
    state: "detecting",
    available: false,
    effectiveEnabled: false,
    roundTrip: false,
    transport: "mqtt-1883",
    reason: "Checking the local Linksys MQTT broker.",
    testedAt: "",
    operation: null,
    failureThreshold: 2,
    parentRestartCooldownSeconds: 300,
    nodeHealth: [],
  });

  function cleanMode(value) {
    const mode = String(value || "").trim().toLowerCase();
    return MODES.includes(mode) ? mode : "auto";
  }

  function cleanBand(value) {
    const band = String(value || "").trim().toUpperCase();
    return BANDS.includes(band) ? band : "";
  }

  function sameId(left, right) {
    return Boolean(left) && Boolean(right) &&
      String(left).trim().toUpperCase() === String(right).trim().toUpperCase();
  }

  function operationState(value) {
    const state = String(value || "").trim().toLowerCase();
    if (state === "discovering-target") return "resolving-target";
    if (state === "verification-pending") return "verifying";
    if (state === "cancelled") return "failed";
    return state || "queued";
  }

  function normalizeOperation(value) {
    if (!value || typeof value !== "object") return null;
    const target = value.target && typeof value.target === "object" ? value.target : {};
    const verification = value.verification && typeof value.verification === "object"
      ? value.verification
      : {};
    const accepted = value.accepted === true;
    const state = operationState(
      value.state || value.phase || verification.state ||
      (accepted ? "accepted" : verification.status || "queued"),
    );
    return {
      ...value,
      id: value.id || value.operationId || "",
      state,
      childId: value.childId || value.nodeId || value.child?.id || "",
      childName: value.childName || value.nodeName || value.child?.name || "",
      requestedParentId:
        value.requestedParentId || value.parentId || target.id || target.uuid ||
        target.parentId || target.parent_id || "",
      requestedParentName:
        value.requestedParentName || value.parentName || target.name ||
        target.parentName || target.parent_name || "",
      band: cleanBand(value.band || target.band),
      requestedAt: value.requestedAt || "",
      acceptedAt: value.acceptedAt || (accepted ? value.requestedAt || "" : ""),
      lastCheckedAt: value.lastCheckedAt || "",
      lastGeneration: value.lastGeneration ?? verification.generation ?? null,
      accepted: accepted || ["accepted", "verifying", "verified"].includes(state),
      queued: value.queued === true || ["queued", "probing", "resolving-target", "publishing"].includes(state),
      error: value.error || "",
      verification: {
        ...verification,
        status: verification.status || (state === "verified" ? "verified" : "pending"),
        observations: Math.max(0, Number(
          verification.observations ?? value.consecutiveMatches ?? 0,
        )),
        required: Math.max(1, Number(
          verification.required ?? value.requiredMatches ?? 2,
        )),
        observedParentId:
          verification.observedParentId || verification.currentParentId || "",
        observedParentName:
          verification.observedParentName || verification.currentParentName || "",
        generation: verification.generation ?? value.lastGeneration ?? null,
      },
    };
  }

  function normalizeHealth(value, defaults = {}) {
    if (!value || typeof value !== "object") return null;
    return {
      ...value,
      childId: value.childId || value.nodeId || "",
      childName: value.childName || value.nodeName || "",
      targetParentId: value.targetParentId || value.parentId || "",
      targetParentName: value.targetParentName || value.parentName || "",
      band: cleanBand(value.band),
      state: String(value.state || "idle").trim().toLowerCase(),
      reason: String(value.reason || ""),
      failureThreshold: Math.max(1, Number(value.failureThreshold ?? defaults.failureThreshold ?? 2)),
      consecutiveFailures: Math.max(0, Number(value.consecutiveFailures || 0)),
      totalFailures: Math.max(0, Number(value.totalFailures || 0)),
      successfulMoves: Math.max(0, Number(value.successfulMoves || 0)),
      parentRestartCount: Math.max(0, Number(value.parentRestartCount || 0)),
      lastTriggerFailures: Math.max(0, Number(value.lastTriggerFailures || 0)),
      targetParentOnlineChildren: Math.max(0, Number(value.targetParentOnlineChildren || 0)),
      targetParentOnline: value.targetParentOnline === true,
      restartQueued: value.restartQueued === true,
      restartInSeconds: Math.max(0, Number(value.restartInSeconds || 0)),
      lastOperationId: value.lastOperationId || 0,
      lastRequestPublished: value.lastRequestPublished === true,
      lastCommandEchoed: value.lastCommandEchoed === true,
      lastTargetBssid: value.lastTargetBssid || "",
      lastTargetChannel: Math.max(0, Number(value.lastTargetChannel || 0)),
      lastTargetSource: value.lastTargetSource || "",
      lastFailureAt: value.lastFailureAt || "",
      lastSuccessAt: value.lastSuccessAt || "",
      lastParentRestartAt: value.lastParentRestartAt || "",
      receivedAt: Number(value.receivedAt) || Date.now(),
    };
  }

  function effectiveEnabled(mode, available, roundTrip) {
    if (mode === "force-off") return false;
    if (mode === "force-on") return true;
    return available && roundTrip;
  }

  function normalize(value, previous = null) {
    const source = value && typeof value === "object" ? value : {};
    const prior = previous && typeof previous === "object" ? previous : DEFAULT_REPORT;
    const probe = source.probe && typeof source.probe === "object" ? source.probe : {};
    const mode = cleanMode(source.mode ?? prior.mode);
    const hasAvailable = Object.prototype.hasOwnProperty.call(source, "available") ||
      Object.prototype.hasOwnProperty.call(probe, "available");
    const hasRoundTrip = Object.prototype.hasOwnProperty.call(source, "roundTrip") ||
      Object.prototype.hasOwnProperty.call(probe, "roundTrip");
    const available = hasAvailable
      ? source.available === true || probe.available === true
      : prior.available === true;
    const roundTrip = hasRoundTrip
      ? source.roundTrip === true || probe.roundTrip === true
      : prior.roundTrip === true;
    let state = String(source.state || probe.state || "").trim().toLowerCase();
    if (mode === "force-off") state = "disabled";
    else if (mode === "force-on" && !(available && roundTrip)) state = "forced";
    else if (available && roundTrip) state = "available";
    else if (!state) state = source.testedAt || probe.testedAt ? "unavailable" : "detecting";
    const hasOperation = Object.prototype.hasOwnProperty.call(source, "operation");
    const failureThreshold = Math.max(1, Number(
      source.failureThreshold ?? prior.failureThreshold ?? 2,
    ));
    const hasHealth = Object.prototype.hasOwnProperty.call(source, "nodeHealth");
    const healthSource = hasHealth ? source.nodeHealth : prior.nodeHealth;
    return {
      ...DEFAULT_REPORT,
      ...prior,
      ...source,
      mode,
      state,
      available,
      roundTrip,
      effectiveEnabled: effectiveEnabled(mode, available, roundTrip),
      transport: source.transport || probe.transport || prior.transport || "mqtt-1883",
      reason: source.reason || probe.reason || prior.reason || DEFAULT_REPORT.reason,
      testedAt: source.testedAt || probe.testedAt || prior.testedAt || "",
      operation: normalizeOperation(hasOperation ? source.operation : prior.operation),
      failureThreshold,
      parentRestartCooldownSeconds: Math.max(1, Number(
        source.parentRestartCooldownSeconds ?? prior.parentRestartCooldownSeconds ?? 300,
      )),
      nodeHealth: Array.isArray(healthSource)
        ? healthSource.map((item) => normalizeHealth(item, { failureThreshold })).filter(Boolean)
        : [],
    };
  }

  function operationFromResponse(payload, request = {}, now = new Date().toISOString()) {
    if (payload?.operation) return normalizeOperation(payload.operation);
    const target = payload?.target || {};
    const accepted = payload?.accepted === true;
    const queued = payload?.queued === true || !accepted;
    return normalizeOperation({
      ...payload,
      state: accepted ? "accepted" : queued ? "queued" : "failed",
      childId: payload?.childId || request.childId,
      childName: payload?.childName || request.childName,
      requestedParentId: payload?.parentId || target.id || request.parentId,
      requestedParentName: payload?.parentName || target.name || request.parentName,
      band: payload?.band || target.band || request.band,
      requestedAt: payload?.requestedAt || now,
      accepted,
      queued,
      verification: payload?.verification || { status: "pending", observations: 0, required: 2 },
    });
  }

  function isWiredNode(node) {
    const connection = String(node?.connectionType || "").toLowerCase();
    const band = String(node?.band || "").toLowerCase();
    return connection.includes("wired") || connection.includes("ethernet") ||
      band === "wired" || band === "ethernet";
  }

  function onlineNodes(nodes) {
    return (nodes || []).filter((node) => node?.online === true);
  }

  function eligibleChildren(nodes) {
    return onlineNodes(nodes).filter((node) => !node.isAuthority && !isWiredNode(node));
  }

  function descendantIds(nodes, nodeId) {
    const descendants = new Set();
    let changed = true;
    while (changed) {
      changed = false;
      for (const node of nodes || []) {
        if (!node?.id || descendants.has(String(node.id).toUpperCase())) continue;
        if (sameId(node.parentId, nodeId) || descendants.has(String(node.parentId || "").toUpperCase())) {
          descendants.add(String(node.id).toUpperCase());
          changed = true;
        }
      }
    }
    return descendants;
  }

  function eligibleParents(nodes, childId) {
    const child = (nodes || []).find((node) => sameId(node?.id, childId));
    const descendants = descendantIds(nodes, childId);
    return onlineNodes(nodes).filter((node) =>
      !sameId(node.id, childId) &&
      !sameId(node.id, child?.parentId) &&
      !descendants.has(String(node.id || "").toUpperCase()),
    );
  }

  function lockConflict(lockValue, childId, parentId) {
    const lock = lockValue && typeof lockValue === "object" ? lockValue : {};
    if (lock.enabled !== true) return null;
    const item = (lock.nodes || []).find((candidate) => sameId(candidate.nodeId, childId));
    if (!item || sameId(item.expectedParentId, parentId)) return null;
    return {
      expectedParentId: item.expectedParentId,
      expectedParentName: item.expectedParentName || item.expectedParentId || "the saved Parent",
    };
  }

  function validateRequest(nodes, request, lockValue = null, reportValue = null) {
    const child = (nodes || []).find((node) => sameId(node.id, request?.childId));
    const parent = (nodes || []).find((node) => sameId(node.id, request?.parentId));
    const band = cleanBand(request?.band);
    const report = reportValue ? normalize(reportValue) : null;
    if (report && !report.effectiveEnabled) {
      return { valid: false, code: "mqtt_parent_disabled", error: "Exact Parent Steering is not enabled in the current mode." };
    }
    if (!child || !parent) {
      return { valid: false, code: "unknown_node", error: "Choose a known child node and requested Parent." };
    }
    if (!child.online || !parent.online) {
      return { valid: false, code: "offline_node", error: "The child node and requested Parent must both be online." };
    }
    if (child.isAuthority) {
      return { valid: false, code: "gateway_child", error: "The primary gateway cannot be moved to another Parent." };
    }
    if (isWiredNode(child)) {
      return { valid: false, code: "wired_child", error: `${child.name} uses a wired backhaul and cannot be moved by wireless Parent steering.` };
    }
    if (sameId(child.id, parent.id)) {
      return { valid: false, code: "self_parent", error: "A node cannot be its own Parent." };
    }
    if (sameId(child.parentId, parent.id)) {
      return { valid: false, code: "current_parent", error: `${child.name} is already connected to ${parent.name}.` };
    }
    if (descendantIds(nodes, child.id).has(String(parent.id).toUpperCase())) {
      return { valid: false, code: "descendant_parent", error: "A downstream node cannot become this child's Parent." };
    }
    if (!band) {
      return { valid: false, code: "invalid_band", error: "Choose either the 5GH or 5GL backhaul radio." };
    }
    const conflict = lockConflict(lockValue, child.id, parent.id);
    if (conflict) {
      return {
        valid: false,
        code: "topology_lock_conflict",
        error: `Topology Lock expects ${conflict.expectedParentName}. Edit the saved map or stop recovery before moving this node.`,
      };
    }
    return { valid: true, code: "", error: "", child, parent, band };
  }

  function topologyGeneration(topology) {
    return topology?.meta?.generation ?? topology?.meta?.revision ?? topology?.meta?.updatedAt ?? null;
  }

  function reconcileOperation(value, topology, now = Date.now(), timeoutMs = 180_000) {
    const operation = normalizeOperation(value);
    if (!operation || !["accepted", "verifying"].includes(operation.state)) return operation;
    const requestedAt = Date.parse(operation.requestedAt || "");
    if (Number.isFinite(requestedAt) && now - requestedAt >= timeoutMs) {
      return { ...operation, state: "timed-out", verification: { ...operation.verification, status: "timed-out" } };
    }
    const child = (topology?.nodes || []).find((node) => sameId(node.id, operation.childId));
    const generation = topologyGeneration(topology);
    if (!child || generation === null || String(generation) === String(operation.lastGeneration)) {
      return operation;
    }
    const matched = sameId(child.parentId, operation.requestedParentId);
    const observations = matched ? operation.verification.observations + 1 : 0;
    const required = operation.verification.required || 2;
    const verified = observations >= required;
    return {
      ...operation,
      state: verified ? "verified" : "verifying",
      lastCheckedAt: topology?.meta?.updatedAt || new Date(now).toISOString(),
      lastGeneration: generation,
      verification: {
        ...operation.verification,
        status: verified ? "verified" : "pending",
        observations,
        required,
        observedParentId: child.parentId || "",
        observedParentName: child.parentName || child.parentId || "unknown",
        generation,
      },
    };
  }

  function isOperationActive(value) {
    const operation = normalizeOperation(value);
    return Boolean(operation && ACTIVE_OPERATION_STATES.has(operation.state));
  }

  function operationForNode(value, nodeId) {
    const operation = normalizeOperation(value);
    return operation && sameId(operation.childId, nodeId) ? operation : null;
  }

  function healthForNode(report, nodeId) {
    const items = Array.isArray(report?.nodeHealth) ? report.nodeHealth : [];
    return items.find((health) => sameId(health.childId, nodeId)) || null;
  }

  function healthRestartRemaining(health, now = Date.now()) {
    if (!health) return 0;
    const elapsed = Math.max(0, Math.floor((now - Number(health.receivedAt || now)) / 1000));
    return Math.max(0, Number(health.restartInSeconds || 0) - elapsed);
  }

  function healthPresentation(value, now = Date.now()) {
    const health = normalizeHealth(value);
    if (!health) return null;
    const target = health.targetParentName || health.targetParentId || "requested Parent";
    const failures = `${health.consecutiveFailures}/${health.failureThreshold}`;
    const childCount = health.targetParentOnlineChildren;
    const remaining = healthRestartRemaining(health, now);
    let tone = "watching";
    let label = `Steering health · ${failures} failures`;
    if (["restart-queued", "parent-restarting"].includes(health.state)) {
      tone = "restarting";
      label = health.state === "restart-queued"
        ? `Restart queued · ${target}`
        : `Restarting Parent · ${target}`;
    } else if (health.state === "cooldown") {
      tone = "cooldown";
      label = `Parent restart cooldown · ${remaining}s`;
    } else if (["blocked", "restart-failed"].includes(health.state)) {
      tone = "blocked";
      label = `Parent restart blocked · ${failures}`;
    } else if (health.state === "recovered") {
      tone = "healthy";
      label = `Steering recovered · ${target}`;
    } else if (health.consecutiveFailures >= health.failureThreshold) {
      tone = "attention";
      label = `Restart threshold reached · ${failures}`;
    }
    const published = health.lastRequestPublished
      ? health.lastCommandEchoed ? "MQTT published + echoed" : "MQTT published"
      : "No qualifying MQTT publish";
    return {
      tone,
      label,
      detail: `${target} · ${childCount} online mesh child${childCount === 1 ? "" : "ren"} · ${published}`,
      reason: health.reason,
      remaining,
      health,
    };
  }

  function capabilityPresentation(value) {
    const report = normalize(value);
    if (report.mode === "force-off") {
      return { tone: "disabled", label: "OFF", title: "Exact Parent Steering is off", detail: "MQTT probing and new steering requests are disabled." };
    }
    if (report.mode === "force-on" && !(report.available && report.roundTrip)) {
      return { tone: "forced", label: "FORCED ON", title: "Steering is forced on", detail: report.reason || "The broker has not been verified; requests may fail." };
    }
    if (report.state === "detecting") {
      return { tone: "detecting", label: "CHECKING", title: "Checking MQTT steering capability", detail: report.reason || "Waiting for a fresh Linksys infrastructure record." };
    }
    if (report.available && report.roundTrip) {
      return { tone: "available", label: "AVAILABLE", title: "Exact Parent Steering is available", detail: "The local broker returned a fresh Linksys infrastructure record." };
    }
    return { tone: "unavailable", label: "UNAVAILABLE", title: "MQTT steering was not detected", detail: report.reason || "The broker or required ACL permissions could not be confirmed." };
  }

  function operationPresentation(value) {
    const operation = normalizeOperation(value);
    if (!operation) return null;
    const target = operation.requestedParentName || operation.requestedParentId || "requested Parent";
    const observed = operation.verification.observedParentName || operation.verification.observedParentId || "current topology";
    switch (operation.state) {
      case "queued":
      case "probing":
      case "resolving-target":
      case "publishing":
        return { tone: "preparing", label: `Preparing request for ${target}`, detail: "No MQTT acceptance has been reported yet." };
      case "accepted":
        return { tone: "verifying", label: "Broker accepted · waiting for topology", detail: `Requested ${target} on ${operation.band || "the selected radio"}.` };
      case "verifying":
        return {
          tone: "verifying",
          label: `Observed ${observed} · ${operation.verification.observations}/${operation.verification.required}`,
          detail: `Two consecutive fresh topology generations must show ${target}.`,
        };
      case "verified":
        return { tone: "verified", label: `Parent verified · ${target} ${operation.verification.required}/${operation.verification.required}`, detail: "The requested Parent was observed in consecutive fresh topology updates." };
      case "timed-out":
        return { tone: "failed", label: "Parent was not verified", detail: `Last observed Parent: ${observed}.` };
      default:
        return { tone: "failed", label: "Steering request failed", detail: operation.error || "The request did not complete." };
    }
  }

  return {
    BANDS,
    MODES,
    capabilityPresentation,
    cleanBand,
    cleanMode,
    descendantIds,
    eligibleChildren,
    eligibleParents,
    healthForNode,
    healthPresentation,
    healthRestartRemaining,
    isOperationActive,
    lockConflict,
    normalize,
    normalizeHealth,
    normalizeOperation,
    operationForNode,
    operationFromResponse,
    operationPresentation,
    reconcileOperation,
    sameId,
    validateRequest,
  };
});
