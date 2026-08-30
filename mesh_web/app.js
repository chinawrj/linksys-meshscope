const state = {
  topology: null,
  filter: "online",
  search: "",
  visibleRows: 18,
  refreshTimer: null,
  countdownTimer: null,
  topologyLockTimer: null,
  refreshInterval: MeshRefreshState.normalizeInterval(
    localStorage.getItem("meshscopeRefreshInterval") ?? MeshRefreshState.DEFAULT_INTERVAL,
  ),
  nextRefreshAt: null,
  refreshDue: false,
  refreshError: null,
  refreshing: false,
  topologyAnimationFrame: null,
  topologyResizeObserver: null,
  topologyViewportWidth: 0,
  topologyLock: MeshTopologyLock.normalize(null),
  topologyLockReceivedAt: Date.now(),
  topologyLockEditing: false,
  topologyLockDraft: {},
  topologyLockDraftError: "",
  topologyLockAcknowledged: false,
  topologyLockSelectedNodeId: null,
  topologyLockBusy: false,
  parentSteering: MeshMqttParentSteering.normalize(null),
  parentSteeringLoaded: false,
  parentSteeringLoading: false,
  parentSteeringBusy: false,
  parentSteeringPollTimer: null,
  parentSteeringSelectedChildId: null,
  parentSteeringSelectedParentId: null,
  parentSteeringBand: "5GH",
  draggedNodeId: null,
  suppressNodeClickUntil: 0,
  detailToken: 0,
  detailSelection: null,
  nodeRestarts: new Map(),
  managedConnection: false,
  modalReturnFocus: null,
  detailReturnFocus: null,
};

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => Array.from(document.querySelectorAll(selector));

const typeIcons = {
  phone: "▯",
  tablet: "▤",
  computer: "⌨",
  camera: "◉",
  media: "▶",
  wearable: "◌",
  iot: "⌁",
  device: "◇",
  node: "◎",
};

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function compactNumber(value, digits = 0) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) return "—";
  return Number(value).toLocaleString("en-US", {
    maximumFractionDigits: digits,
    minimumFractionDigits: digits,
  });
}

function formatLinkRate(value) {
  const rate = Number(value);
  if (!Number.isFinite(rate)) return "—";
  if (rate >= 1000) return `${compactNumber(rate / 1000, 2)} Gbps`;
  return `${compactNumber(rate)} Mbps`;
}

function formatTime(value) {
  if (!value) return "—";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return "—";
  return new Intl.DateTimeFormat("en-US", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  }).format(date);
}

function signalLevel(rssi) {
  if (rssi === null || rssi === undefined) return 0;
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -75) return 2;
  return 1;
}

function signalBars(rssi, tone = "") {
  const level = signalLevel(rssi);
  return `<span class="signal-bars level-${level} ${escapeHtml(tone)}" aria-label="Signal ${rssi ?? "unknown"} dBm"><i></i><i></i><i></i><i></i></span>`;
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  let payload = {};
  try {
    payload = await response.json();
  } catch {
    payload = { error: "The local service returned an unrecognized response." };
  }
  if (!response.ok) {
    const error = new Error(payload.error || `Request failed (${response.status})`);
    error.status = response.status;
    error.code = payload.code;
    throw error;
  }
  return window.MeshLinksysNormalize?.normalizeEnvelope(payload) ?? payload;
}

function openConnectModal() {
  if ($("#connectModal").classList.contains("hidden")) {
    state.modalReturnFocus = document.activeElement;
  }
  $("#connectModal").classList.remove("hidden");
  requestAnimationFrame(() => {
    (state.managedConnection ? $("#connectButton") : $("#passwordInput")).focus();
  });
}

function closeConnectModal() {
  if (!state.topology) return;
  $("#connectModal").classList.add("hidden");
  $("#connectError").textContent = "";
  state.modalReturnFocus?.focus?.();
  state.modalReturnFocus = null;
}

function setConnectionStatus(connected) {
  $("#liveChip").classList.toggle("connected", connected);
  if (!connected) $("#liveText").textContent = "Waiting to connect";
}

function configureConnectionMode(status) {
  const mode = MeshConnectionMode.view(status);
  state.managedConnection = mode.managed;
  $("#settingsButton").textContent = mode.settingsLabel;
  $("#settingsButton").setAttribute("aria-label", mode.settingsLabel);
  $("#connectTitle").textContent = mode.title;
  $("#connectDescription").textContent = mode.description;
  $("#connectNoteText").textContent = mode.note;
  $("#connectButtonLabel").textContent = mode.submitLabel;
  $("#editableConnectionFields").hidden = mode.managed;
  $("#managedConnectionPanel").hidden = !mode.managed;
  $("#managedRouterHost").textContent = status.router || "—";
  $("#hostInput").required = !mode.managed;
  $("#passwordInput").required = !mode.managed;
}

function updateRefreshLabel() {
  const status = MeshRefreshState.view({
    hasTopology: Boolean(state.topology),
    refreshing: state.refreshing,
    interval: state.refreshInterval,
    nextRefreshAt: state.nextRefreshAt,
    now: Date.now(),
    visible: document.visibilityState === "visible",
    refreshDue: state.refreshDue,
    error: state.refreshError,
  });
  const chip = $("#liveChip");
  for (const mode of ["refreshing", "paused", "stale", "error"]) {
    chip.classList.toggle(mode, status.mode === mode);
  }
  chip.classList.toggle("demo", state.topology?.meta?.demo === true);
  const routerOffline = state.topology?.meta?.routerConnected === false;
  chip.classList.toggle("error", routerOffline || status.mode === "error");
  $("#liveText").textContent = routerOffline
    ? "Router offline · Last cached data"
    : state.topology?.meta?.demo
      ? `Demo · ${status.text}`
      : status.text;
  updateTopologyLockCountdown();
}

function scheduleAutoRefresh() {
  clearTimeout(state.refreshTimer);
  clearInterval(state.countdownTimer);
  state.refreshTimer = null;
  state.countdownTimer = null;
  state.refreshDue = false;
  if (!state.topology || !state.refreshInterval) {
    state.nextRefreshAt = null;
    updateRefreshLabel();
    return;
  }
  state.nextRefreshAt = Date.now() + state.refreshInterval * 1000;
  state.countdownTimer = setInterval(updateRefreshLabel, 1000);
  state.refreshTimer = setTimeout(async () => {
    if (document.visibilityState === "visible") {
      await refresh(true);
    } else {
      state.refreshTimer = null;
      state.refreshDue = true;
      updateRefreshLabel();
    }
  }, state.refreshInterval * 1000);
  updateRefreshLabel();
}

function toast(message) {
  const el = $("#toast");
  el.textContent = message;
  el.classList.add("show");
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => el.classList.remove("show"), 2300);
}

function parentSteeringModeHelp(mode) {
  if (mode === "force-on") {
    return "Force on attempts steering even when the broker capability cannot be confirmed. All topology safety checks still apply.";
  }
  if (mode === "force-off") {
    return "Force off does not probe or send new MQTT Parent-steering messages. An already accepted request may still be verified.";
  }
  return "Auto safely probes the local broker and enables steering only after a fresh infrastructure round trip.";
}

function parentSteeringRequest() {
  const child = state.topology?.nodes?.find(
    (node) => MeshMqttParentSteering.sameId(node.id, state.parentSteeringSelectedChildId),
  );
  const parent = state.topology?.nodes?.find(
    (node) => MeshMqttParentSteering.sameId(node.id, state.parentSteeringSelectedParentId),
  );
  return {
    childId: child?.id || state.parentSteeringSelectedChildId || "",
    childName: child?.name || "",
    parentId: parent?.id || state.parentSteeringSelectedParentId || "",
    parentName: parent?.name || "",
    band: MeshMqttParentSteering.cleanBand(state.parentSteeringBand) || "5GH",
  };
}

function renderParentSteering() {
  const panel = $("#parentSteeringPanel");
  if (!panel) return;
  const report = state.parentSteering;
  const capability = MeshMqttParentSteering.capabilityPresentation(report);
  panel.className = `parent-steering-panel ${escapeHtml(capability.tone)}`;
  $("#parentSteeringCapability").textContent = capability.label;
  $("#parentSteeringDescription").textContent = capability.title + ". " + capability.detail;
  $("#parentSteeringMode").value = report.mode;
  $("#parentSteeringMode").disabled = state.parentSteeringBusy;
  $("#parentSteeringModeHelp").innerHTML = `<strong>${escapeHtml(
    report.mode === "force-on" ? "Force on" : report.mode === "force-off" ? "Force off" : "Auto",
  )}</strong> ${escapeHtml(parentSteeringModeHelp(report.mode).replace(/^(Auto|Force on|Force off) /, ""))}`;
  $("#parentSteeringTestedAt").textContent = report.testedAt
    ? `Checked ${formatTime(report.testedAt)}`
    : "Not checked yet";
  $("#parentSteeringProbeButton").disabled =
    report.mode === "force-off" || state.parentSteeringLoading || state.parentSteeringBusy;
  $("#parentSteeringProbeButton").textContent = state.parentSteeringLoading
    ? "Checking…"
    : "Check again";

  const children = MeshMqttParentSteering.eligibleChildren(state.topology?.nodes || []);
  if (!children.some((node) => MeshMqttParentSteering.sameId(node.id, state.parentSteeringSelectedChildId))) {
    state.parentSteeringSelectedChildId = children[0]?.id || null;
  }
  const child = children.find((node) =>
    MeshMqttParentSteering.sameId(node.id, state.parentSteeringSelectedChildId));
  const childSelect = $("#parentSteeringChild");
  childSelect.innerHTML = children.length
    ? children.map((node) => `<option value="${escapeHtml(node.id)}">${escapeHtml(node.name)} · ${escapeHtml(node.band || "Mesh")}</option>`).join("")
    : '<option value="">No eligible wireless child nodes</option>';
  childSelect.value = child?.id || "";

  const parents = child
    ? MeshMqttParentSteering.eligibleParents(state.topology.nodes, child.id)
    : [];
  if (!parents.some((node) => MeshMqttParentSteering.sameId(node.id, state.parentSteeringSelectedParentId))) {
    state.parentSteeringSelectedParentId = parents[0]?.id || null;
  }
  const parent = parents.find((node) =>
    MeshMqttParentSteering.sameId(node.id, state.parentSteeringSelectedParentId));
  const parentSelect = $("#parentSteeringParent");
  parentSelect.innerHTML = parents.length
    ? parents.map((node) => `<option value="${escapeHtml(node.id)}">${escapeHtml(node.name)}${node.isAuthority ? " · Gateway" : ""}</option>`).join("")
    : '<option value="">No eligible online Parent</option>';
  parentSelect.value = parent?.id || "";

  if (child && ["5GH", "5GL"].includes(String(child.band || "").toUpperCase()) &&
      !state.parentSteeringBand) {
    state.parentSteeringBand = String(child.band).toUpperCase();
  }
  $("#parentSteeringBand").value = state.parentSteeringBand;

  const operationActive = MeshMqttParentSteering.isOperationActive(report.operation);
  const controlsDisabled = !report.effectiveEnabled || state.parentSteeringBusy || operationActive;
  childSelect.disabled = controlsDisabled || !children.length;
  parentSelect.disabled = controlsDisabled || !parents.length;
  $("#parentSteeringBand").disabled = controlsDisabled || !children.length;
  const validation = MeshMqttParentSteering.validateRequest(
    state.topology?.nodes || [],
    parentSteeringRequest(),
    state.topologyLock,
    report,
  );
  $("#parentSteeringSubmit").disabled = controlsDisabled || !validation.valid;
  $("#parentSteeringSubmit").textContent = state.parentSteeringBusy
    ? "Sending…"
    : operationActive
      ? "Request in progress"
      : "Move node";
  $("#parentSteeringValidation").textContent =
    report.effectiveEnabled && !operationActive && children.length ? (validation.valid ? "" : validation.error) : "";

  const operationView = MeshMqttParentSteering.operationPresentation(report.operation);
  const operationElement = $("#parentSteeringOperation");
  operationElement.hidden = !operationView;
  if (operationView) {
    operationElement.className = `parent-steering-operation ${escapeHtml(operationView.tone)}`;
    const icon = operationView.tone === "verified" ? "✓" : operationView.tone === "failed" ? "!" : "↻";
    operationElement.innerHTML = `<i aria-hidden="true">${icon}</i><span><strong>${escapeHtml(operationView.label)}</strong><small>${escapeHtml(operationView.detail)}</small></span>`;
  }

  if ($("#manualSteering")) {
    $("#manualSteering").textContent = report.mode === "force-off"
      ? "MQTT · Disabled"
      : report.available && report.roundTrip
        ? "MQTT · Available"
        : report.mode === "force-on"
          ? "MQTT · Forced on"
          : "MQTT · Not detected";
  }
}

function scheduleParentSteeringPoll() {
  clearTimeout(state.parentSteeringPollTimer);
  state.parentSteeringPollTimer = null;
  const healthNeedsPolling = (state.parentSteering.nodeHealth || []).some((health) =>
    ["restart-queued", "parent-restarting", "cooldown", "restart-eligible"].includes(health.state));
  if (!state.parentSteeringLoaded ||
      (!state.parentSteeringLoading &&
       !MeshMqttParentSteering.isOperationActive(state.parentSteering.operation) &&
       state.parentSteering.state !== "detecting" &&
       !healthNeedsPolling)) return;
  state.parentSteeringPollTimer = setTimeout(() => loadParentSteering(false), 2000);
}

async function loadParentSteering(force = false) {
  if (state.parentSteeringLoading) return;
  state.parentSteeringLoading = true;
  renderParentSteering();
  try {
    const report = await api(`/api/mqtt-parent-steering${force ? "?refresh=1" : ""}`);
    state.parentSteering = MeshMqttParentSteering.normalize(report, state.parentSteering);
    state.parentSteeringLoaded = true;
  } catch (error) {
    state.parentSteering = MeshMqttParentSteering.normalize({
      state: "error",
      available: false,
      roundTrip: false,
      reason: error.message,
      testedAt: new Date().toISOString(),
    }, state.parentSteering);
    state.parentSteeringLoaded = true;
  } finally {
    state.parentSteeringLoading = false;
    renderParentSteering();
    if (state.topology) renderTopology(state.topology);
    scheduleParentSteeringPoll();
  }
}

async function setParentSteeringMode(mode) {
  if (state.parentSteeringBusy) return;
  state.parentSteeringBusy = true;
  $("#parentSteeringValidation").textContent = "";
  renderParentSteering();
  try {
    const report = await api("/api/mqtt-parent-steering", {
      method: "POST",
      body: JSON.stringify({ mode: MeshMqttParentSteering.cleanMode(mode) }),
    });
    state.parentSteering = MeshMqttParentSteering.normalize(report, {
      ...state.parentSteering,
      mode: MeshMqttParentSteering.cleanMode(mode),
    });
    toast(`Exact Parent Steering mode set to ${state.parentSteering.mode.replace("-", " ")}`);
  } catch (error) {
    $("#parentSteeringValidation").textContent = error.message;
    toast(error.message);
  } finally {
    state.parentSteeringBusy = false;
    renderParentSteering();
    scheduleParentSteeringPoll();
  }
}

function scheduleParentSteeringRefreshes() {
  [5000, 15000, 30000, 60000, 120000].forEach((delay) => {
    window.setTimeout(() => {
      if (MeshMqttParentSteering.isOperationActive(state.parentSteering.operation)) refresh(true);
    }, delay);
  });
}

async function submitParentSteering(event) {
  event.preventDefault();
  if (state.parentSteeringBusy) return;
  const request = parentSteeringRequest();
  const validation = MeshMqttParentSteering.validateRequest(
    state.topology?.nodes || [],
    request,
    state.topologyLock,
    state.parentSteering,
  );
  if (!validation.valid) {
    $("#parentSteeringValidation").textContent = validation.error;
    return;
  }
  state.parentSteeringBusy = true;
  state.parentSteering.operation = MeshMqttParentSteering.operationFromResponse(
    { queued: true }, request,
  );
  renderParentSteering();
  renderTopology(state.topology);
  try {
    const result = await api("/api/steer-node-parent", {
      method: "POST",
      body: JSON.stringify({
        childId: request.childId,
        parentId: request.parentId,
        band: request.band,
      }),
    });
    state.parentSteering.operation = MeshMqttParentSteering.operationFromResponse(
      result,
      request,
    );
    const view = MeshMqttParentSteering.operationPresentation(state.parentSteering.operation);
    toast(view?.label || "Parent steering request queued");
    scheduleParentSteeringRefreshes();
  } catch (error) {
    state.parentSteering.operation = MeshMqttParentSteering.normalizeOperation({
      state: "failed",
      ...request,
      requestedAt: new Date().toISOString(),
      error: error.message,
    });
    $("#parentSteeringValidation").textContent = error.message;
    toast(error.message);
  } finally {
    state.parentSteeringBusy = false;
    renderParentSteering();
    renderTopology(state.topology);
    scheduleParentSteeringPoll();
  }
}

function focusParentSteering(node) {
  if (node && !node.isAuthority) state.parentSteeringSelectedChildId = node.id;
  if (node?.isAuthority) state.parentSteeringSelectedParentId = node.id;
  closeDetail();
  renderParentSteering();
  $("#parentSteeringPanel").scrollIntoView({ behavior: "smooth", block: "center" });
  requestAnimationFrame(() => $(node?.isAuthority ? "#parentSteeringChild" : "#parentSteeringParent").focus());
}

function healthScore(data) {
  if (!data?.summary?.nodesOnline) return 0;
  const onlineRatio = data.summary.nodesOnline / Math.max(data.summary.nodesTotal, 1);
  const backhaulNodes = data.nodes.filter((node) => node.online && !node.isAuthority);
  const signalAverage = backhaulNodes.length
    ? backhaulNodes.reduce((sum, node) => sum + (node.quality.score ?? 35), 0) / backhaulNodes.length
    : 100;
  const wan = data.network.wanStatus === "Connected" ? 100 : 35;
  return Math.round(onlineRatio * 25 + signalAverage * 0.55 + wan * 0.2);
}

function renderSummary(data) {
  const { summary, network, meta } = data;
  const routerOffline = meta.routerConnected === false;
  $("#routerLabel").textContent = meta.router;
  $("#heroCopy").textContent = meta.demo
    ? "Explore a complete offline topology, backhaul quality, and Client/STA interactions without connecting to or changing a router."
    : routerOffline
      ? "The router is currently unreachable. MeshScope is showing the last complete topology and will update automatically after recovery."
      : meta.clientDetails === "nodes-only"
        ? "Low-memory mode is showing online mesh nodes. Client/STA details and offline nodes are not collected; select full mode when the device has enough memory."
      : "See every Velop node, backhaul link, and connected device in real time. Your data stays between this device and your router.";
  $("#networkStatus").textContent = meta.demo
    ? "Offline demo data"
    : routerOffline
      ? "Router offline"
      : network.wanStatus === "Connected"
        ? "Operating normally"
        : network.wanStatus || "Status unknown";
  $("#lastUpdated").textContent = routerOffline
    ? `Last cached at ${formatTime(meta.updatedAt)}`
    : `${meta.demo ? "No router connection · " : ""}Updated at ${formatTime(meta.updatedAt)}`;
  $("#statNodes").textContent = `${summary.nodesOnline}/${summary.nodesTotal}`;
  $("#statNodesSub").textContent = `${summary.nodesTotal - summary.nodesOnline} offline nodes`;
  $("#statClients").textContent = compactNumber(summary.clientsOnline);
  $("#statClientsSub").textContent = `${summary.clientsKnown} known device records`;
  $("#statBackhaul").textContent = summary.backhaulMbps ? `${compactNumber(summary.backhaulMbps)}M` : "—";
  $("#statBackhaulSub").textContent = "Sum of latest child-to-parent hop measurements";
  $("#statAttention").textContent = compactNumber(summary.weakNodes + (summary.nodesTotal - summary.nodesOnline));
  $("#statAttentionSub").textContent = `${summary.weakNodes} weak signal · ${summary.nodesTotal - summary.nodesOnline} offline`;

  $("#networkModel").textContent = network.model || "—";
  $("#networkFirmware").textContent = network.firmwareVersion || "—";
  $("#networkWan").textContent = network.wanStatus === "Connected" ? `Connected · ${network.wanType || "WAN"}` : network.wanStatus || "Unknown";
  $("#nodeSteering").textContent =
    network.nodeSteeringEnabled === true
      ? "Automatic · On"
      : network.nodeSteeringEnabled === false
        ? "Automatic · Off"
        : "Not reported by firmware";
  $("#manualSteering").textContent = state.parentSteeringLoaded
    ? "MQTT · " + (state.parentSteering.effectiveEnabled ? "Available" : "Unavailable")
    : "MQTT · Checking";

  const score = healthScore(data);
  $("#healthRing").style.setProperty("--score", score);
  $("#healthPercent").textContent = `${score}%`;
  $("#healthScore").textContent = score >= 85 ? "Healthy" : score >= 65 ? "Check recommended" : "Needs attention";
  $("#healthScore").style.background = score >= 85 ? "var(--mint-soft)" : score >= 65 ? "var(--amber-soft)" : "var(--coral-soft)";
  $("#healthScore").style.color = score >= 85 ? "var(--mint)" : score >= 65 ? "var(--amber)" : "var(--coral)";
  $("#healthSummary").textContent =
    score >= 85
      ? "The network is stable. Backhaul links and online clients are responding."
      : "Some nodes are offline or have weak backhaul signals. Review the affected nodes below.";

  const weakest = data.nodes
    .filter((node) => node.online && !node.isAuthority)
    .sort((a, b) => (a.rssi ?? -100) - (b.rssi ?? -100))[0];
  const strongest = data.nodes
    .filter((node) => node.online && !node.isAuthority)
    .sort((a, b) => (b.speedMbps ?? 0) - (a.speedMbps ?? 0))[0];
  $("#healthList").innerHTML = [
    {
      tone: network.wanStatus === "Connected" ? "" : "bad",
      label: "Internet connection",
      value: network.wanStatus === "Connected" ? "Online" : network.wanStatus || "Issue detected",
    },
    {
      tone: summary.nodesTotal === summary.nodesOnline ? "" : "warn",
      label: "Mesh nodes",
      value: `${summary.nodesOnline}/${summary.nodesTotal} online`,
    },
    {
      tone: weakest?.quality?.tone === "bad" ? "bad" : weakest?.quality?.tone === "warn" ? "warn" : "",
      label: "Weakest backhaul",
      value: weakest ? `${weakest.name} · ${weakest.rssi ?? "—"} dBm` : "—",
    },
    {
      tone: "",
      label: "Fastest backhaul",
      value: strongest?.speedMbps ? `${strongest.name} · ${compactNumber(strongest.speedMbps)} Mbps` : "—",
    },
  ]
    .map((item) => `<div class="health-item ${item.tone}"><i></i><span>${escapeHtml(item.label)}</span><strong>${escapeHtml(item.value)}</strong></div>`)
    .join("");
}

function topologyLockRemaining() {
  return MeshTopologyLock.remainingSeconds(
    state.topologyLock,
    state.topologyLockReceivedAt,
  );
}

function updateTopologyLockCountdown() {
  const remaining = topologyLockRemaining();
  $$("[data-topology-lock-countdown]").forEach((element) => {
    element.textContent = remaining > 0
      ? `Move in ${MeshTopologyLock.duration(remaining)}`
      : "MQTT move on next confirmed refresh";
  });
  const countdown = $("#topologyLockCountdown");
  if (countdown) {
    const hasMismatch = Number(state.topologyLock.summary?.mismatch || 0) > 0;
    countdown.textContent = !hasMismatch
      ? "No action queued"
      : remaining > 0
        ? `Next action ${MeshTopologyLock.duration(remaining)}`
        : "Action window open";
  }
  const chip = $("#topologyRecoveryChip");
  if (chip && state.topologyLock.enabled && Number(state.topologyLock.summary?.mismatch || 0) > 0) {
    chip.textContent = "Recovery: Monitoring · " +
      (remaining ? MeshTopologyLock.duration(remaining) : "Action ready");
  }
}

function updateParentHealthCountdown() {
  $$('[data-parent-health-countdown]').forEach((element) => {
    const health = MeshMqttParentSteering.healthForNode(
      state.parentSteering,
      element.dataset.parentHealthChild,
    );
    const view = MeshMqttParentSteering.healthPresentation(health);
    if (view) element.textContent = element.dataset.parentHealthCountdownKind === "detail"
      ? view.remaining ? `${view.remaining}s` : "Ready / inactive"
      : view.label;
  });
}

function setTopologyLock(value) {
  state.topologyLock = MeshTopologyLock.normalize(value);
  state.topologyLockReceivedAt = Date.now();
  if (state.topology?.meta) state.topology.meta.topologyLock = state.topologyLock;
}

function topologyLockNodeOptions() {
  return (state.topology?.nodes || []).filter((node) => node.online);
}

function renderTopologyLockEditor() {
  const online = topologyLockNodeOptions();
  const children = online.filter((node) => !node.isAuthority);
  if (!children.length) return;
  if (!children.some((node) => node.id === state.topologyLockSelectedNodeId)) {
    state.topologyLockSelectedNodeId = children[0].id;
  }
  const child = children.find((node) => node.id === state.topologyLockSelectedNodeId);
  const nodeSelect = $("#topologyLockNodeSelect");
  const parentSelect = $("#topologyLockParentSelect");
  nodeSelect.innerHTML = children
    .map((node) => `<option value="${escapeHtml(node.id)}">${escapeHtml(node.name)}</option>`)
    .join("");
  nodeSelect.value = child.id;
  parentSelect.innerHTML = online
    .filter((node) => node.id !== child.id)
    .map((node) => `<option value="${escapeHtml(node.id)}">${escapeHtml(node.name)}${node.isAuthority ? " · Gateway" : ""}</option>`)
    .join("");
  parentSelect.value = state.topologyLockDraft[child.id] || "";
  const valid = MeshTopologyLock.validate(state.topology.nodes, state.topologyLockDraft);
  $("#topologyLockValidation").textContent = state.topologyLockDraftError || (valid.valid ? "" : valid.error);
  $("#applyTopologyLockButton").disabled =
    !valid.valid || !state.topologyLockAcknowledged || state.topologyLockBusy;
}

function renderTopologyLock() {
  const panel = $("#topologyLockPanel");
  if (!panel || !state.topology) return;
  const lock = state.topologyLock;
  const recoveryChip = $("#topologyRecoveryChip");
  const supported = lock.supported && state.topology.meta?.edgeHosted === true;
  const editing = supported && state.topologyLockEditing;
  panel.className = `topology-lock-panel ${lock.enabled ? "enabled" : ""} ${editing ? "editing" : ""}`;
  $("#topologyLockEditor").hidden = !editing;
  $("#editTopologyLockButton").hidden = !supported || editing;
  $("#unlockTopologyButton").hidden = !supported || !lock.enabled || editing;
  $("#topologyLockHistory").hidden = !supported || !lock.history.length || editing;
  $("#topologyLockStats").hidden = !supported || !lock.enabled || editing;
  if (!supported) {
    recoveryChip.textContent = "Recovery: ESP32 required";
    recoveryChip.className = "topology-recovery-chip unavailable";
    $("#topologyLockIcon").textContent = "◇";
    $("#topologyLockTitle").textContent = "Topology lock requires ESP32 hosting";
    $("#topologyLockMode").textContent = "UNAVAILABLE";
    $("#topologyLockDescription").textContent = "Open the page hosted by a supported MeshScope ESP32 to save and enforce parent relationships.";
    return;
  }
  if (editing) {
    recoveryChip.textContent = "Recovery: Draft · Review warning";
    recoveryChip.className = "topology-recovery-chip editing";
    $("#topologyLockIcon").textContent = "✣";
    $("#topologyLockTitle").textContent = "Editing desired topology";
    $("#topologyLockMode").textContent = "DRAFT";
    $("#topologyLockDescription").textContent = "Drag child nodes onto a desired parent. Current backhaul details remain visible while the map previews the desired structure.";
    renderTopologyLockEditor();
    return;
  }
  if (!lock.enabled) {
    recoveryChip.textContent = "Recovery: Off · Set up";
    recoveryChip.className = "topology-recovery-chip";
    $("#topologyLockIcon").textContent = "◇";
    $("#topologyLockTitle").textContent = "Topology lock is off";
    $("#topologyLockMode").textContent = "UNLOCKED";
    $("#topologyLockDescription").textContent = "Save the current parent structure or edit it by dragging nodes before enabling continuous monitoring.";
    $("#editTopologyLockButton").textContent = "Edit & lock topology";
    return;
  }
  const summary = lock.summary;
  const attention = Number(summary.mismatch || 0) + Number(summary.blocked || 0) + Number(summary.offline || 0);
  const remaining = topologyLockRemaining();
  recoveryChip.textContent = summary.mismatch
    ? "Recovery: Monitoring · " + (remaining ? MeshTopologyLock.duration(remaining) : "Action ready")
    : attention
      ? "Recovery: Monitoring · " + attention + " issue" + (attention === 1 ? "" : "s")
      : "Recovery: Monitoring · All parents match";
  recoveryChip.className = "topology-recovery-chip active " + (attention ? "attention" : "correct");
  $("#topologyLockIcon").textContent = attention ? "!" : "◆";
  $("#topologyLockTitle").textContent = attention
    ? `MQTT recovery is monitoring ${attention} node${attention === 1 ? "" : "s"}`
    : "All monitored nodes match the saved parent map";
  $("#topologyLockMode").textContent = "MONITORING";
  $("#topologyLockDescription").textContent = summary.blocked
    ? "At least one desired parent is offline or MQTT recovery is disabled, so affected moves are blocked."
    : summary.mismatch
      ? "Parent differences are being confirmed. Eligible child nodes are moved with exact MQTT Parent Steering, one at a time under the five-minute global limit."
      : "Every monitored online node currently has the desired parent.";
  $("#editTopologyLockButton").textContent = "Edit desired topology";
  $("#topologyLockStats").innerHTML = `
    <span class="correct"><strong>${summary.correct}</strong> correct</span>
    <span class="mismatch"><strong>${summary.mismatch}</strong> mismatch</span>
    <span class="blocked"><strong>${summary.blocked}</strong> blocked</span>
    <span class="offline"><strong>${summary.offline}</strong> offline</span>
    <span id="topologyLockCountdown"><strong>↻</strong> ${summary.mismatch ? (topologyLockRemaining() ? `Next action ${MeshTopologyLock.duration(topologyLockRemaining())}` : "Action window open") : "No action queued"}</span>`;
  $("#topologyLockHistory").innerHTML = `
    <strong>Recent automatic actions</strong>
    ${lock.history.slice(0, 3).map((action) => `
      <span><i class="${action.accepted ? "accepted" : "failed"}"></i><b>${escapeHtml(action.name)}</b> · ${escapeHtml(formatTime(action.requestedAt))} · ${action.accepted ? "MQTT move queued" : "not queued"} · ${escapeHtml(action.currentParentName || action.currentParentId || "unknown")} → ${escapeHtml(action.expectedParentName || action.expectedParentId)}</span>`).join("")}`;
  updateTopologyLockCountdown();
}

function beginTopologyLockEdit() {
  if (!state.topologyLock.supported || state.topologyLockBusy) return;
  state.topologyLockDraft = MeshTopologyLock.draftFrom(
    state.topology.nodes,
    state.topologyLock,
  );
  state.topologyLockDraftError = "";
  state.topologyLockAcknowledged = false;
  $("#topologyLockAcknowledgement").checked = false;
  state.topologyLockEditing = true;
  state.topologyLockSelectedNodeId = Object.keys(state.topologyLockDraft)[0] || null;
  $("#topologyLockDraftStatus").textContent = state.topologyLock.enabled
    ? "Saved structure"
    : "Current live structure";
  $("#cancelTopologyLockButton").textContent = state.topologyLock.enabled
    ? "Cancel"
    : "Keep recovery off";
  renderTopologyLock();
  renderTopology(state.topology);
}

function cancelTopologyLockEdit() {
  state.topologyLockEditing = false;
  state.topologyLockDraft = {};
  state.topologyLockDraftError = "";
  state.topologyLockAcknowledged = false;
  state.topologyLockSelectedNodeId = null;
  renderTopologyLock();
  renderTopology(state.topology);
}

function moveTopologyLockDraft(nodeId, parentId) {
  const result = MeshTopologyLock.move(
    state.topology.nodes,
    state.topologyLockDraft,
    nodeId,
    parentId,
  );
  if (!result.valid) {
    state.topologyLockDraftError = result.error;
    $("#topologyLockDraftStatus").textContent = "Invalid change · draft unchanged";
    toast(result.error);
    renderTopologyLockEditor();
    return false;
  }
  state.topologyLockDraft = result.draft;
  state.topologyLockDraftError = "";
  state.topologyLockSelectedNodeId = nodeId;
  $("#topologyLockDraftStatus").textContent = "Desired structure changed";
  renderTopologyLock();
  renderTopology(state.topology);
  return true;
}

async function applyTopologyLock() {
  const result = MeshTopologyLock.validate(
    state.topology.nodes,
    state.topologyLockDraft,
  );
  const activeSteering = MeshMqttParentSteering.isOperationActive(
    state.parentSteering.operation,
  ) ? state.parentSteering.operation : null;
  const activeSteeringDraftParent = activeSteering
    ? Object.entries(state.topologyLockDraft).find(([nodeId]) =>
        MeshMqttParentSteering.sameId(nodeId, activeSteering.childId))?.[1]
    : null;
  if (activeSteering && !MeshMqttParentSteering.sameId(
    activeSteeringDraftParent,
    activeSteering.requestedParentId,
  )) {
    $("#topologyLockValidation").textContent =
      `Exact Parent Steering is moving ${activeSteering.childName || "this node"} to ${activeSteering.requestedParentName || "the requested Parent"}. Match that Parent in the draft or wait for the operation to finish.`;
    return;
  }
  if (!result.valid || !state.topologyLockAcknowledged || state.topologyLockBusy) {
    $("#topologyLockValidation").textContent = result.error ||
      "Acknowledge the MQTT steering behavior before enabling automatic recovery.";
    return;
  }
  state.topologyLockBusy = true;
  $("#applyTopologyLockButton").disabled = true;
  $("#applyTopologyLockButton").textContent = "Enabling recovery…";
  try {
    const lock = await api("/api/topology-lock", {
      method: "POST",
      body: JSON.stringify({
        enabled: true,
        nodes: MeshTopologyLock.mappings(state.topologyLockDraft),
      }),
    });
    setTopologyLock(lock);
    state.topologyLockEditing = false;
    state.topologyLockDraft = {};
    state.topologyLockDraftError = "";
    toast("MQTT topology recovery enabled · Monitoring starts with the next refresh");
    renderTopologyLock();
    renderParentSteering();
    renderTopology(state.topology);
  } catch (error) {
    $("#topologyLockValidation").textContent = error.message;
    toast(error.message);
  } finally {
    state.topologyLockBusy = false;
    const button = $("#applyTopologyLockButton");
    button.textContent = "Enable MQTT recovery";
    renderTopologyLockEditor();
  }
}

async function disableTopologyLock() {
  if (state.topologyLockBusy) return;
  state.topologyLockBusy = true;
  $("#unlockTopologyButton").disabled = true;
  try {
    setTopologyLock(await api("/api/topology-lock", {
      method: "POST",
      body: JSON.stringify({ enabled: false }),
    }));
    toast("Topology lock disabled · Automatic MQTT moves stopped");
    renderTopologyLock();
    renderParentSteering();
    renderTopology(state.topology);
  } catch (error) {
    toast(error.message);
  } finally {
    state.topologyLockBusy = false;
    $("#unlockTopologyButton").disabled = false;
  }
}

function layoutNodes(nodes, availableWidth = 0) {
  const lockExpanded = state.topologyLockEditing || state.topologyLock.enabled;
  const steeringExpanded = Boolean(
    state.parentSteering.operation &&
    nodes.some((node) => MeshMqttParentSteering.sameId(
      node.id,
      state.parentSteering.operation.childId,
    )),
  );
  const healthExpanded = nodes.some((node) => {
    const health = MeshMqttParentSteering.healthForNode(
      state.parentSteering,
      node.id,
    );
    return Boolean(MeshMqttParentSteering.healthCardPresentation(health));
  });
  const nodeHeight = 124 + Number(lockExpanded) * 60 +
    Number(steeringExpanded) * 60 + Number(healthExpanded) * 82;
  return MeshTopologyLayout.compute(nodes, {
    nodeHeight,
    rowGap: nodeHeight > 124 ? 38 : 30,
    availableWidth,
  });
}

function observeTopologyWidth() {
  const scroller = $(".map-scroll");
  if (!scroller || typeof ResizeObserver === "undefined") return;
  state.topologyResizeObserver?.disconnect();
  state.topologyResizeObserver = new ResizeObserver((entries) => {
    const width = Math.max(0, Math.floor(entries[0]?.contentRect?.width || 0));
    if (!width || Math.abs(width - state.topologyViewportWidth) < 3) return;
    state.topologyViewportWidth = width;
    if (!state.topology) return;
    cancelAnimationFrame(state.topologyAnimationFrame);
    state.topologyAnimationFrame = requestAnimationFrame(() => {
      state.topologyAnimationFrame = null;
      renderTopology(state.topology);
    });
  });
  state.topologyResizeObserver.observe(scroller);
}

function applyTopologyLayoutPositions(map) {
  map.querySelectorAll("[data-layout-left][data-layout-top]").forEach((element) => {
    const left = Number(element.dataset.layoutLeft);
    const top = Number(element.dataset.layoutTop);
    if (!Number.isFinite(left) || !Number.isFinite(top)) return;
    // Direct CSSOM assignment is compatible with MeshScope's strict
    // style-src CSP. Inline style attributes inserted through innerHTML are
    // intentionally rejected by the browser and would stack every node at 0,0.
    element.style.left = `${left}px`;
    element.style.top = `${top}px`;
  });
}

function renderTopology(data) {
  const map = $("#meshMap");
  const scroller = $(".map-scroll");
  if (state.topologyAnimationFrame) {
    cancelAnimationFrame(state.topologyAnimationFrame);
    state.topologyAnimationFrame = null;
  }
  const displayNodes = state.topologyLockEditing
    ? MeshTopologyLock.applyDraft(data.nodes, state.topologyLockDraft)
    : data.nodes;
  const availableWidth = Math.max(
    0,
    Math.floor((scroller?.clientWidth || state.topologyViewportWidth || 0) - 12),
  );
  state.topologyViewportWidth = scroller?.clientWidth || state.topologyViewportWidth;
  const layout = layoutNodes(displayNodes, availableWidth);
  const { positions, edges, root, nodeWidth, nodeHeight } = layout;
  const offline = data.nodes.filter((node) => !node.online);
  if (!positions.length || !root) return;
  map.style.width = `${layout.width}px`;
  map.style.minWidth = `${layout.width}px`;
  map.style.height = `${layout.height}px`;
  $(".map-scroll-hint")?.classList.toggle(
    "needed",
    layout.contentWidth > availableWidth + 2,
  );
  const internetY = root.y + nodeHeight / 2 - 38;
  const wanEdge = {
    id: "internet->gateway",
    sourcePoint: { x: 110, y: root.y + nodeHeight / 2 },
    targetPoint: { x: root.x, y: root.y + nodeHeight / 2 },
    band: "WAN",
    speedMbps: null,
    tone: "wan",
  };
  const positionById = new Map(positions.map((node) => [node.id, node]));
  const currentPreviewEdges = [];
  if (state.topologyLockEditing) {
    for (const edge of edges) {
      const actualParentId = edge.target.actualParentId;
      if (!actualParentId || actualParentId === edge.target.parentId) continue;
      edge.kind = "desired";
      const actualSource = positionById.get(actualParentId);
      if (actualSource) {
        currentPreviewEdges.push({
          ...edge,
          id: `${actualParentId}->${edge.target.id}:current`,
          source: actualSource,
          kind: "current",
        });
      }
    }
  }
  let html = `<canvas class="topology-canvas" id="topologyCanvas" aria-hidden="true"></canvas>`;
  html += `<div class="map-internet" data-layout-left="34" data-layout-top="${internetY}"><span>⌁</span><small>INTERNET</small></div>`;
  html += edgeLabelHtml(wanEdge);
  for (const edge of edges) html += edgeLabelHtml(edge, nodeWidth, nodeHeight);
  for (const edge of currentPreviewEdges) html += edgeLabelHtml(edge, nodeWidth, nodeHeight);
  for (const node of positions) {
    const tone = node.quality?.tone || "";
    const restart = state.nodeRestarts.get(node.id);
    const lockItem = state.topologyLockEditing
      ? null
      : MeshTopologyLock.statusForNode(state.topologyLock, node.id);
    const lockPresentation = MeshTopologyLock.presentation(
      lockItem,
      topologyLockRemaining(),
      state.topologyLock.confirmationsRequired,
    );
    const currentParentName = node.actualParentName || node.parentName || "Main";
    const desiredParentName = node.parentName || "Main";
    const draggable = state.topologyLockEditing && !node.isAuthority;
    const steeringOperation = MeshMqttParentSteering.operationForNode(
      state.parentSteering.operation,
      node.id,
    );
    const steeringPresentation = MeshMqttParentSteering.operationPresentation(steeringOperation);
    const steeringTone = steeringPresentation?.tone || "";
    const steeringHealth = MeshMqttParentSteering.healthForNode(
      state.parentSteering,
      node.id,
    );
    const healthPresentation = MeshMqttParentSteering.healthCardPresentation(steeringHealth);
    const healthTone = healthPresentation?.tone || "";
    const phyAge = node.phyRateAgeSeconds !== null && node.phyRateAgeSeconds !== undefined
      ? `${compactNumber(node.phyRateAgeSeconds)} seconds old`
      : "sample age unavailable";
    const phyTitle = node.phyRateMbps !== null && node.phyRateMbps !== undefined
      ? `Child backhaul PHY from MQTT BH/status · ${node.phyRateRaw || formatLinkRate(node.phyRateMbps)} · ${phyAge}${node.phyRateStale ? " · stale" : ""}`
      : "Waiting for the child Node's MQTT BH/status PHY sample";
    html += `
      <button class="mesh-node ${node.isAuthority ? "master" : ""} ${tone === "warn" || tone === "bad" ? "weak" : ""} ${restart ? "restarting" : ""} ${lockPresentation?.tone || ""} ${draggable ? "lock-draggable" : ""} ${state.topologyLockEditing || state.topologyLock.enabled ? "lock-expanded" : ""} ${steeringPresentation ? `parent-steering-expanded parent-steering-${escapeHtml(steeringTone)}` : ""} ${healthPresentation ? `parent-health-expanded parent-health-${escapeHtml(healthTone)}` : ""}"
        data-layout-left="${node.x}" data-layout-top="${node.y}" data-node-id="${escapeHtml(node.id)}" type="button" ${draggable ? 'draggable="true"' : ""}>
        <div class="node-title">
          <strong>${escapeHtml(node.name)}</strong>
          <span class="node-role">${node.isAuthority ? "GATEWAY" : "NODE"}</span>
        </div>
        <div class="node-meta">${escapeHtml(node.model)} · ${escapeHtml(node.ipAddress || "No IP")}</div>
        ${node.isAuthority ? "" : state.topologyLockEditing
          ? `<div class="node-parent desired">Desired ↳ ${escapeHtml(desiredParentName)}</div><div class="node-parent current">Current ↳ ${escapeHtml(currentParentName)} · ${escapeHtml(node.band || "Mesh")}${node.channel ? ` ch ${node.channel}` : ""}</div>`
          : `<div class="node-parent">↳ ${escapeHtml(node.parentName || "Main")} · ${escapeHtml(node.band || "Mesh")}${node.channel ? ` ch ${node.channel}` : ""}</div>`}
        <div class="node-stats">
          <div><span>Clients</span><strong>${node.clientCount}</strong></div>
          <div><span>${node.isAuthority ? "Status" : "Hop throughput"}</span><strong>${node.isAuthority ? "Online" : `${compactNumber(node.speedMbps)} Mbps`}</strong></div>
          <div class="node-phy ${node.phyRateStale ? "stale" : ""}" title="${escapeHtml(phyTitle)}"><span>PHY rate</span><strong>${node.isAuthority ? "—" : formatLinkRate(node.phyRateMbps)}</strong></div>
          <div class="node-signal">${node.isAuthority ? '<span class="signal-bars level-4"><i></i><i></i><i></i><i></i></span>' : signalBars(node.rssi, tone)}<small>${node.isAuthority ? "WAN" : `${node.rssi ?? "—"} dBm`}</small></div>
        </div>
        ${lockPresentation ? `<div class="node-lock-status ${escapeHtml(lockPresentation.tone)}"><i aria-hidden="true">${lockItem.status === "correct" ? "◆" : lockItem.status === "parent-offline" ? "◇" : "↻"}</i><span><strong ${lockItem.status === "cooldown" ? "data-topology-lock-countdown" : ""}>${escapeHtml(lockPresentation.label)}</strong><small>${escapeHtml(lockPresentation.detail)}</small></span></div>` : ""}
        ${steeringPresentation ? `<div class="node-parent-steering-status ${escapeHtml(steeringTone)}"><i aria-hidden="true">${steeringTone === "verified" ? "✓" : steeringTone === "failed" ? "!" : "↻"}</i><span><strong>${escapeHtml(steeringPresentation.label)}</strong><small>${escapeHtml(steeringPresentation.detail)}</small></span></div>` : ""}
        ${healthPresentation ? `<div class="node-parent-health-status ${escapeHtml(healthTone)}"><i aria-hidden="true">${healthTone === "healthy" ? "✓" : healthTone === "blocked" ? "!" : healthTone === "restarting" ? "↻" : "#"}</i><span><strong ${healthPresentation.remaining ? `data-parent-health-countdown data-parent-health-child="${escapeHtml(node.id)}"` : ""}>${escapeHtml(healthPresentation.label)}</strong><small>${escapeHtml(healthPresentation.detail)}</small><small class="health-reason">${escapeHtml(healthPresentation.reason)}</small></span></div>` : ""}
        ${draggable ? `<div class="node-drag-hint">Drag onto desired parent</div>` : ""}
        ${restart ? `<div class="node-operation"><i aria-hidden="true">↻</i><span>${escapeHtml(MeshNodeRestartState.label(restart))}</span></div>` : ""}
      </button>`;
  }
  if (offline.length) {
    html += `<div class="offline-strip"><strong>${offline.length} offline nodes</strong>${offline
      .slice(0, 5)
      .map((node) => {
        const lockItem = MeshTopologyLock.statusForNode(state.topologyLock, node.id);
        const lockView = MeshTopologyLock.presentation(
          lockItem,
          topologyLockRemaining(),
          state.topologyLock.confirmationsRequired,
        );
        return `<button class="offline-node-chip text-button ${lockView?.tone || ""}" data-node-id="${escapeHtml(node.id)}" type="button"><b>${escapeHtml(node.name)}</b>${lockView ? `<small>${escapeHtml(lockView.label)}</small>` : ""}</button>`;
      })
      .join("")}${offline.length > 5 ? `<span>${offline.length - 5} more</span>` : ""}</div>`;
  }
  map.innerHTML = html;
  applyTopologyLayoutPositions(map);
  const canvasEdges = [
    wanEdge,
    ...[...currentPreviewEdges, ...edges].map((edge) => ({
      ...edge,
      sourcePoint: {
        x: edge.source.x + nodeWidth,
        y: edge.source.y + nodeHeight / 2,
      },
      targetPoint: {
        x: edge.target.x,
        y: edge.target.y + nodeHeight / 2,
      },
    })),
  ];
  requestAnimationFrame(() => startTopologyCanvas(canvasEdges, layout.width, layout.height));
  $$("[data-node-id]").forEach((button) => {
    button.addEventListener("click", () => {
      if (Date.now() < state.suppressNodeClickUntil) return;
      const node = data.nodes.find((item) => item.id === button.dataset.nodeId);
      if (node) openDetail(node, "node");
    });
  });
  if (state.topologyLockEditing) wireTopologyLockDragDrop(data);
  updateTopologyLockCountdown();
  updateParentHealthCountdown();
}

function wireTopologyLockDragDrop(data) {
  $$(".mesh-node").forEach((card) => {
    const nodeId = card.dataset.nodeId;
    const node = data.nodes.find((item) => item.id === nodeId);
    if (!node) return;
    if (!node.isAuthority) {
      card.addEventListener("dragstart", (event) => {
        state.draggedNodeId = nodeId;
        state.topologyLockSelectedNodeId = nodeId;
        card.classList.add("dragging");
        event.dataTransfer.effectAllowed = "move";
        event.dataTransfer.setData("text/plain", nodeId);
      });
      card.addEventListener("dragend", () => {
        state.draggedNodeId = null;
        card.classList.remove("dragging");
        $$(".mesh-node.drop-target").forEach((item) => item.classList.remove("drop-target"));
      });
    }
    card.addEventListener("dragover", (event) => {
      const dragged = state.draggedNodeId;
      if (!dragged || dragged === nodeId) return;
      event.preventDefault();
      event.dataTransfer.dropEffect = "move";
      card.classList.add("drop-target");
    });
    card.addEventListener("dragleave", () => card.classList.remove("drop-target"));
    card.addEventListener("drop", (event) => {
      event.preventDefault();
      card.classList.remove("drop-target");
      const dragged = state.draggedNodeId || event.dataTransfer.getData("text/plain");
      if (!dragged || dragged === nodeId) return;
      state.suppressNodeClickUntil = Date.now() + 500;
      moveTopologyLockDraft(dragged, nodeId);
    });
  });
}

function edgeLabelHtml(edge, nodeWidth = 0, nodeHeight = 0) {
  const from = edge.sourcePoint || {
    x: edge.source.x + nodeWidth,
    y: edge.source.y + nodeHeight / 2,
  };
  const to = edge.targetPoint || {
    x: edge.target.x,
    y: edge.target.y + nodeHeight / 2,
  };
  const midX = (from.x + to.x) / 2;
  const midY = (from.y + to.y) / 2;
  const displayBand = edge.kind === "desired" ? "LOCK" : edge.band;
  const details = edge.kind === "desired"
    ? `Desired → ${edge.source.name}`
    : edge.kind === "current"
      ? `${edge.speedMbps ? `${compactNumber(edge.speedMbps)}M` : "—"} · CURRENT`
    : edge.band === "WAN"
    ? "UPLINK"
    : `${edge.speedMbps ? `${compactNumber(edge.speedMbps)}M` : "—"}${edge.phyRateMbps !== null && edge.phyRateMbps !== undefined ? ` · PHY ${formatLinkRate(edge.phyRateMbps)}${edge.phyRateStale ? " (stale)" : ""}` : ""}${edge.rssi !== null && edge.rssi !== undefined ? ` · ${edge.rssi}dBm` : ""}`;
  return `
    <span class="edge-label band-${escapeHtml(String(displayBand).toLowerCase())} ${edge.kind ? `edge-${escapeHtml(edge.kind)}` : ""}" data-layout-left="${midX - 70}" data-layout-top="${midY - 20}">
      <strong>${escapeHtml(displayBand)}</strong><small>${escapeHtml(details)}</small>
    </span>`;
}

function startTopologyCanvas(edges, width, height) {
  const canvas = $("#topologyCanvas");
  if (!canvas) return;
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  canvas.width = Math.round(width * ratio);
  canvas.height = Math.round(height * ratio);
  canvas.style.width = `${width}px`;
  canvas.style.height = `${height}px`;
  const context = canvas.getContext("2d");
  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const palettes = {
    "5GH": { line: "#3f7f9a", glow: "rgba(63,127,154,.17)" },
    "5GL": { line: "#3d856d", glow: "rgba(61,133,109,.17)" },
    "WAN": { line: "#7a9b90", glow: "rgba(122,155,144,.14)" },
    "Ethernet": { line: "#b9772b", glow: "rgba(185,119,43,.15)" },
    "LOCK": { line: "#6f55b5", glow: "rgba(111,85,181,.2)" },
  };
  context.scale(ratio, ratio);

  function pointOnCurve(from, to, progress) {
    const control = Math.max(42, (to.x - from.x) * 0.48);
    const p0 = from;
    const p1 = { x: from.x + control, y: from.y };
    const p2 = { x: to.x - control, y: to.y };
    const p3 = to;
    const inverse = 1 - progress;
    return {
      x: inverse ** 3 * p0.x + 3 * inverse ** 2 * progress * p1.x + 3 * inverse * progress ** 2 * p2.x + progress ** 3 * p3.x,
      y: inverse ** 3 * p0.y + 3 * inverse ** 2 * progress * p1.y + 3 * inverse * progress ** 2 * p2.y + progress ** 3 * p3.y,
    };
  }

  function draw(timestamp = 0) {
    context.clearRect(0, 0, width, height);
    edges.forEach((edge, edgeIndex) => {
      const from = edge.sourcePoint;
      const to = edge.targetPoint;
      const control = Math.max(42, (to.x - from.x) * 0.48);
      const palette = edge.kind === "desired"
        ? palettes.LOCK
        : palettes[edge.band] || palettes.WAN;
      context.save();
      if (edge.kind === "current") context.setLineDash([7, 6]);
      context.beginPath();
      context.moveTo(from.x, from.y);
      context.bezierCurveTo(from.x + control, from.y, to.x - control, to.y, to.x, to.y);
      context.lineWidth = 7;
      context.strokeStyle = palette.glow;
      context.stroke();
      context.lineWidth = 2;
      context.strokeStyle = palette.line;
      context.stroke();
      for (const point of [from, to]) {
        context.beginPath();
        context.arc(point.x, point.y, 4, 0, Math.PI * 2);
        context.fillStyle = palette.line;
        context.fill();
      }
      if (!reduceMotion && edge.kind !== "current") {
        const progress = ((timestamp / 4200) + edgeIndex * 0.173) % 1;
        const packet = pointOnCurve(from, to, progress);
        context.beginPath();
        context.arc(packet.x, packet.y, 4.2, 0, Math.PI * 2);
        context.shadowBlur = 10;
        context.shadowColor = palette.line;
        context.fillStyle = "#fffefa";
        context.fill();
        context.lineWidth = 2;
        context.strokeStyle = palette.line;
        context.stroke();
      }
      context.restore();
    });
    if (!reduceMotion) state.topologyAnimationFrame = requestAnimationFrame(draw);
  }
  draw();
}

function filteredClients() {
  if (!state.topology) return [];
  const query = state.search.trim().toLowerCase();
  return state.topology.clients.filter((client) => {
    if (state.filter === "online" && !client.online) return false;
    if (!query) return true;
    return [client.name, client.ipAddress, client.macAddress, client.model, client.nodeName]
      .some((value) => String(value || "").toLowerCase().includes(query));
  });
}

function renderClients() {
  const clients = filteredClients();
  const visible = clients.slice(0, state.visibleRows);
  $("#clientRows").innerHTML = visible.length
    ? visible.map(clientRowHtml).join("")
    : `<tr><td colspan="7" class="empty-row">No matching clients.</td></tr>`;
  $("#clientCountLabel").textContent = `Showing ${visible.length} of ${clients.length} devices`;
  $("#loadMoreButton").hidden = visible.length >= clients.length;
  $$("[data-client-id]").forEach((button) => {
    button.addEventListener("click", () => {
      const client = state.topology.clients.find((item) => item.id === button.dataset.clientId);
      if (client) openDetail(client, "client");
    });
  });
}

function clientRowHtml(client) {
  const signal = client.online && client.rssi !== null
    ? `${signalBars(client.rssi, client.quality?.tone)}<span>${client.rssi} dBm</span>`
    : `<span>${client.online ? "Wired" : "Offline"}</span>`;
  return `
    <tr>
      <td>
        <div class="device-cell">
          <span class="device-icon" aria-hidden="true">${typeIcons[client.type] || typeIcons.device}</span>
          <div><strong>${escapeHtml(client.name)}</strong><span>${escapeHtml(client.model || client.manufacturer || client.type)}</span></div>
        </div>
      </td>
      <td><span class="node-chip">${escapeHtml(client.nodeName || "—")}</span></td>
      <td><span class="connection-chip">${escapeHtml(client.online ? client.band || (client.rssi === null ? "Ethernet" : "Wi‑Fi") : "History")}</span></td>
      <td><div class="signal-cell">${signal}</div></td>
      <td>${client.speedMbps !== null && client.speedMbps !== undefined ? `${compactNumber(client.speedMbps)} Mbps` : "—"}</td>
      <td>${escapeHtml(client.ipAddress || "—")}</td>
      <td><button class="row-detail" data-client-id="${escapeHtml(client.id)}" type="button" aria-label="View details for ${escapeHtml(client.name)}">›</button></td>
    </tr>`;
}

function nodeClientHtml(client) {
  const connection = client.online
    ? client.band || (client.rssi === null ? "Ethernet" : "Wi‑Fi")
    : "History";
  const signal = client.rssi !== null && client.rssi !== undefined
    ? `${client.rssi} dBm`
    : client.online
      ? "Wired"
      : "Offline";
  return `
    <button class="node-client" data-node-client-id="${escapeHtml(client.id)}" type="button">
      <span class="device-icon" aria-hidden="true">${typeIcons[client.type] || typeIcons.device}</span>
      <span class="node-client-copy">
        <strong>${escapeHtml(client.name)}</strong>
        <small>${escapeHtml(client.model || client.manufacturer || client.type || "Network device")}</small>
        <code>${escapeHtml(client.ipAddress || client.macAddress || "No address")}</code>
      </span>
      <span class="node-client-link">
        <strong>${escapeHtml(connection)}</strong>
        <small>${escapeHtml(signal)}</small>
        <small>${client.speedMbps !== null && client.speedMbps !== undefined ? `${compactNumber(client.speedMbps)} Mbps` : "—"}</small>
      </span>
      <span class="node-client-chevron" aria-hidden="true">›</span>
    </button>`;
}

function openDetail(item, kind) {
  if (!$("#detailDrawer").classList.contains("open")) {
    state.detailReturnFocus = document.activeElement;
  }
  state.detailSelection = { id: item.id, kind };
  const detailToken = ++state.detailToken;
  const isNode = kind === "node";
  const attachedClients = isNode
    ? MeshDetailData.clientsForNode(state.topology, item)
    : [];
  const capabilityReport = isNode
    ? MeshDetailData.nodeCapabilityReport(state.topology, item)
    : null;
  const topologyLockItem = isNode
    ? MeshTopologyLock.statusForNode(state.topologyLock, item.id)
    : null;
  const topologyLockPresentation = MeshTopologyLock.presentation(
    topologyLockItem,
    topologyLockRemaining(),
    state.topologyLock.confirmationsRequired,
  );
  const steeringHealth = isNode
    ? MeshMqttParentSteering.healthForNode(state.parentSteering, item.id)
    : null;
  const steeringHealthPresentation =
    MeshMqttParentSteering.healthPresentation(steeringHealth);
  const parentNode = isNode ? null : MeshDetailData.nodeForClient(state.topology, item);
  const nodeRows = isNode ? MeshDetailData.nodeDetailRows(item, compactNumber) : null;
  const metrics = isNode
    ? nodeRows.metrics
    : [
        ["Current status", item.online ? "Online" : "Historical device"],
        ["Connected node", item.nodeName || "—"],
        ["Negotiated rate", item.speedMbps !== null && item.speedMbps !== undefined ? `${compactNumber(item.speedMbps)} Mbps` : "—"],
        ["Signal quality", item.rssi !== null && item.rssi !== undefined ? `${item.rssi} dBm · ${item.quality.label}` : "—"],
      ];
  const details = isNode
    ? nodeRows.details
    : [
        ["Device type", item.type],
        ["Model", item.model],
        ["Manufacturer", item.manufacturer],
        ["Operating system", item.operatingSystem],
        ["IP address", item.ipAddress],
        ["MAC address", item.macAddress],
        ["Connection band", item.band],
        ["Radio", item.radioId],
        ["Last seen", item.lastSeen ? new Date(item.lastSeen).toLocaleString("en-US") : null],
      ];
  $("#detailContent").innerHTML = `
    ${parentNode ? `<button class="detail-back" id="detailBackToNode" type="button">← Back to ${escapeHtml(parentNode.name)}</button>` : ""}
    <div class="detail-head">
      <span class="detail-type-icon" aria-hidden="true">${isNode ? typeIcons.node : typeIcons[item.type] || typeIcons.device}</span>
      <p class="section-kicker">${isNode ? "MESH NODE" : "CLIENT DEVICE"}</p>
      <h2>${escapeHtml(item.name)}</h2>
      <p>${escapeHtml(isNode ? item.description || item.role : item.model || item.manufacturer || "Network device")}</p>
      <span class="detail-status ${item.online ? "" : "offline"}">${item.online ? "● Online" : "○ Offline"}</span>
    </div>
    <div class="detail-grid">${metrics
      .map(([label, value]) => `<div class="detail-metric"><span>${escapeHtml(label)}</span><strong>${escapeHtml(value ?? "—")}</strong></div>`)
      .join("")}</div>
    <div class="detail-list">${details
      .filter(([, value]) => value !== null && value !== undefined && value !== "")
      .map(([label, value]) => `<div><span>${escapeHtml(label)}</span><strong>${escapeHtml(value)}</strong></div>`)
      .join("")}</div>
    ${topologyLockPresentation ? `<section class="node-lock-detail ${escapeHtml(topologyLockPresentation.tone)}"><span aria-hidden="true">◆</span><div><small>TOPOLOGY LOCK</small><strong ${topologyLockItem.status === "cooldown" ? "data-topology-lock-countdown" : ""}>${escapeHtml(topologyLockPresentation.label)}</strong><p>${escapeHtml(topologyLockPresentation.detail)}</p></div></section>` : ""}
    ${steeringHealthPresentation ? `<section class="node-steering-health-detail ${escapeHtml(steeringHealthPresentation.tone)}">
      <div class="node-steering-health-heading"><span aria-hidden="true">#</span><div><small>PARENT STEERING HEALTH</small><strong>${escapeHtml(steeringHealthPresentation.label)}</strong><p>${escapeHtml(steeringHealthPresentation.reason)}</p></div></div>
      <div class="node-steering-health-grid">
        <div><span>Requested Parent</span><strong>${escapeHtml(steeringHealth.targetParentName || steeringHealth.targetParentId || "—")}</strong></div>
        <div><span>Band / target</span><strong>${escapeHtml(steeringHealth.band || "—")}${steeringHealth.lastTargetChannel ? ` · ch ${steeringHealth.lastTargetChannel}` : ""}</strong></div>
        <div><span>Consecutive failures</span><strong>${steeringHealth.consecutiveFailures}/${steeringHealth.failureThreshold}</strong></div>
        <div><span>Total failures</span><strong>${steeringHealth.totalFailures}</strong></div>
        <div><span>Successful moves</span><strong>${steeringHealth.successfulMoves}</strong></div>
        <div><span>Parent online children</span><strong>${steeringHealth.targetParentOnlineChildren}</strong></div>
        <div><span>MQTT publish / echo</span><strong>${steeringHealth.lastRequestPublished ? "Published" : "No"} / ${steeringHealth.lastCommandEchoed ? "Seen" : "Not seen"}</strong></div>
        <div><span>Parent restarts</span><strong>${steeringHealth.parentRestartCount}</strong></div>
        <div><span>Last operation</span><strong>#${steeringHealth.lastOperationId || "—"}</strong></div>
        <div><span>Restart countdown</span><strong data-parent-health-countdown data-parent-health-countdown-kind="detail" data-parent-health-child="${escapeHtml(item.id)}">${steeringHealthPresentation.remaining ? `${steeringHealthPresentation.remaining}s` : "Ready / inactive"}</strong></div>
      </div>
      <div class="node-steering-health-evidence">
        <span>Target BSSID</span><code>${escapeHtml(steeringHealth.lastTargetBssid || "—")}</code>
        <span>Target source</span><strong>${escapeHtml(steeringHealth.lastTargetSource || "—")}</strong>
        <span>Last failure</span><strong>${escapeHtml(steeringHealth.lastFailureAt || "—")}</strong>
        <span>Last success</span><strong>${escapeHtml(steeringHealth.lastSuccessAt || "—")}</strong>
        <span>Last Parent restart</span><strong>${escapeHtml(steeringHealth.lastParentRestartAt || "—")}</strong>
      </div>
    </section>` : ""}
    ${isNode ? `
      <section class="node-feasibility" aria-labelledby="nodeFeasibilityTitle">
        <div class="node-feasibility-heading">
          <div>
            <p class="section-kicker">NODE CAPABILITIES</p>
            <h3 id="nodeFeasibilityTitle">${escapeHtml(item.name)} node capabilities</h3>
          </div>
          <span>Per-node operations</span>
        </div>
        <div class="node-capability-list">
          <article class="node-capability ${escapeHtml(capabilityReport.parentRole.status)}">
            <i aria-hidden="true">↳</i>
            <div><span>As an upstream parent</span><strong>${escapeHtml(capabilityReport.parentRole.label)}</strong><small>${escapeHtml(capabilityReport.parentRole.detail)}</small></div>
          </article>
          <article class="node-capability ${escapeHtml(capabilityReport.manualTarget.status)}">
            <i aria-hidden="true">⌁</i>
            <div><span>Manually target this parent</span><strong>${escapeHtml(capabilityReport.manualTarget.label)}</strong><small>${escapeHtml(capabilityReport.manualTarget.detail)}</small></div>
          </article>
          <article class="node-capability ${escapeHtml(capabilityReport.individualRestart.status)}">
            <i aria-hidden="true">↻</i>
            <div>
              <span>Restart this node</span>
              <strong id="nodeRestartProbeLabel">${escapeHtml(capabilityReport.individualRestart.label)}</strong>
              <small id="nodeRestartProbeDetail">${escapeHtml(capabilityReport.individualRestart.detail)}</small>
              <button class="node-restart-button" id="restartMeshButton" type="button" hidden>Restart ${escapeHtml(item.name)} now</button>
            </div>
          </article>
          <article class="node-capability ${escapeHtml(capabilityReport.localManagement.status)}" id="nodeDirectProbe">
            <i aria-hidden="true">⌘</i>
            <div>
              <span>Node Web / JNAP</span>
              <strong id="nodeProbeLabel">${escapeHtml(item.online ? "Running read-only probe…" : capabilityReport.localManagement.label)}</strong>
              <small id="nodeProbeDetail">${escapeHtml(capabilityReport.localManagement.detail)}</small>
              ${capabilityReport.localManagement.url ? `<a class="node-management-link" href="${escapeHtml(capabilityReport.localManagement.url)}" target="_blank" rel="noopener noreferrer">Open ${escapeHtml(item.name)} CA page ↗</a>` : ""}
            </div>
          </article>
        </div>
        ${item.online ? `<button class="node-parent-steering-button" id="openParentSteeringButton" type="button">${item.isAuthority ? "Use this gateway in Exact Parent Steering" : "Move this node with Exact Parent Steering"}</button>` : ""}
        <p class="node-feasibility-note">The hidden entry point is <code>https://&lt;node-ip&gt;/ca</code>; after login, the firmware opens <code>#casupport</code>. Restart requests go directly to the selected node's local endpoint.</p>
      </section>` : ""}
    ${isNode ? `
      <section class="node-clients-section" aria-labelledby="nodeClientsTitle">
        <div class="node-clients-heading">
          <div>
            <p class="section-kicker">ASSOCIATED STATIONS</p>
            <h3 id="nodeClientsTitle">Current clients / STAs</h3>
          </div>
          <span>${attachedClients.length}</span>
        </div>
        <div class="node-clients-list">
          ${attachedClients.length
            ? attachedClients.map(nodeClientHtml).join("")
            : `<div class="node-clients-empty"><span>◇</span><strong>No online clients or STAs</strong><small>Historical devices are not incorrectly assigned to this node.</small></div>`}
        </div>
      </section>` : ""}`;
  $$("[data-node-client-id]").forEach((button) => {
    button.addEventListener("click", () => {
      const client = state.topology.clients.find(
        (candidate) => candidate.id === button.dataset.nodeClientId,
      );
      if (client) openDetail(client, "client");
    });
  });
  $("#detailBackToNode")?.addEventListener("click", () => openDetail(parentNode, "node"));
  $("#openParentSteeringButton")?.addEventListener("click", () => focusParentSteering(item));
  $("#detailBackdrop").classList.add("open");
  $("#detailDrawer").classList.add("open");
  $("#detailDrawer").setAttribute("aria-hidden", "false");
  if (isNode && item.online && item.ipAddress) loadNodeProbe(item, detailToken);
}

async function loadNodeProbe(node, detailToken) {
  try {
    const report = await api(`/api/node-capabilities?nodeId=${encodeURIComponent(node.id)}`);
    if (detailToken !== state.detailToken) return;
    const probe = $("#nodeDirectProbe");
    const probeLabel = $("#nodeProbeLabel");
    const probeDetail = $("#nodeProbeDetail");
    const restartLabel = $("#nodeRestartProbeLabel");
    const restartDetail = $("#nodeRestartProbeDetail");
    if (!probe || !probeLabel || !probeDetail || !restartLabel || !restartDetail) return;
    probe.classList.remove("unavailable");
    probe.classList.add(report.credentialsSynchronized ? "confirmed" : "unverified");
    probeLabel.textContent = report.demo
      ? "Demo mode · No live node connection"
      : report.credentialsSynchronized
        ? `${report.deviceMode || "Local"} · Synchronized credentials verified`
        : `${report.deviceMode || "Local"} · Credential status unknown`;
    probeDetail.textContent = report.demo
      ? `${report.identity.model || node.model || "Linksys Node"} synthetic identity · No network request made`
      : `${report.identity.model || node.model || "Linksys Node"} direct identity verified · No control action performed`;
    if (report.individualRestart.visibleInCaSupportUi) {
      const restartButton = $("#restartMeshButton");
      const activeRestart = state.nodeRestarts.get(node.id);
      restartLabel.textContent = activeRestart ? "Selected node · Restart in progress" : "Selected node · Available";
      restartDetail.textContent = activeRestart
        ? `${MeshNodeRestartState.label(activeRestart)}; MeshScope is continuing to monitor it`
        : node.isAuthority
          ? `One click immediately sends core/Reboot to ${node.ipAddress}; restarting Main may briefly interrupt the entire mesh`
          : `One click immediately sends core/Reboot to ${node.ipAddress}; this node and its clients will briefly disconnect`;
      if (restartButton) {
        restartButton.hidden = false;
        if (activeRestart) {
          restartButton.disabled = true;
          restartButton.textContent = `${MeshNodeRestartState.label(activeRestart)}…`;
        } else {
          restartButton.addEventListener("click", () => restartNode(node, restartButton));
        }
      }
    }
  } catch (error) {
    if (detailToken !== state.detailToken) return;
    const probe = $("#nodeDirectProbe");
    probe?.classList.remove("available");
    probe?.classList.add("unverified");
    if ($("#nodeProbeLabel")) $("#nodeProbeLabel").textContent = "Read-only probe did not complete";
    if ($("#nodeProbeDetail")) $("#nodeProbeDetail").textContent = error.message;
  }
}

async function restartNode(node, button) {
  if (state.nodeRestarts.has(node.id)) return;
  button.disabled = true;
  button.textContent = `Restarting ${node.name}…`;
  try {
    const result = await api("/api/restart-node", {
      method: "POST",
      body: JSON.stringify(MeshNodeRestartState.requestBody(node)),
    });
    MeshNodeRestartState.begin(state.nodeRestarts, node);
    closeDetail();
    state.refreshError = null;
    renderTopology(state.topology);
    toast(`${result.requestedThroughNode.name} is restarting`);
    MeshNodeRestartState.POLL_DELAYS_MS.forEach((delay) => {
      window.setTimeout(() => refresh(true), delay);
    });
  } catch (error) {
    button.disabled = false;
    button.textContent = `Restart ${node.name} now`;
    toast(error.message);
  }
}

function reconcileNodeRestarts(data) {
  const events = MeshNodeRestartState.reconcile(state.nodeRestarts, data.nodes);
  for (const event of events) {
    if (event.type === "recovered") toast(`${event.name} is back online`);
    else if (event.type === "online-timeout") toast(`${event.name} is still online`);
    else toast(`Monitoring ended for ${event.name}; refresh manually to check again`);
  }
}

function closeDetail() {
  state.detailToken += 1;
  state.detailSelection = null;
  $("#detailBackdrop").classList.remove("open");
  $("#detailDrawer").classList.remove("open");
  $("#detailDrawer").setAttribute("aria-hidden", "true");
  state.detailReturnFocus?.focus?.();
  state.detailReturnFocus = null;
}

function render(data) {
  const detailSelection = state.detailSelection;
  const detailScrollTop = $("#detailDrawer").scrollTop;
  reconcileNodeRestarts(data);
  state.topology = data;
  setTopologyLock(data.meta?.topologyLock);
  state.parentSteering.operation = MeshMqttParentSteering.reconcileOperation(
    state.parentSteering.operation,
    data,
  );
  setConnectionStatus(data.meta?.routerConnected !== false);
  renderSummary(data);
  renderTopologyLock();
  renderParentSteering();
  renderTopology(data);
  renderClients();
  if (detailSelection && $("#detailDrawer").classList.contains("open")) {
    const collection = detailSelection.kind === "node" ? data.nodes : data.clients;
    const refreshedItem = collection.find((item) => item.id === detailSelection.id);
    if (refreshedItem) {
      openDetail(refreshedItem, detailSelection.kind);
      $("#detailDrawer").scrollTop = detailScrollTop;
    } else {
      closeDetail();
    }
  }
  $("#connectModal").classList.add("hidden");
  scheduleAutoRefresh();
  // Parent health can change in the ESP32 worker without a browser-originated
  // operation. Refresh the small state document with every topology refresh so
  // external/automatic failures and Parent restarts appear on Node cards.
  if (!state.parentSteeringLoading) {
    void loadParentSteering(false);
  }
}

async function refresh(silent = false) {
  if (!MeshRefreshState.shouldStartRefresh({
    hasTopology: Boolean(state.topology),
    refreshing: state.refreshing,
  })) {
    if (state.refreshing) return;
    openConnectModal();
    return;
  }
  state.refreshing = true;
  state.refreshError = null;
  state.refreshDue = false;
  updateRefreshLabel();
  const button = $("#refreshButton");
  button.classList.add("loading");
  try {
    render(await api("/api/refresh", { method: "POST", body: "{}" }));
    state.refreshError = null;
    if (!silent) toast("Network data refreshed");
  } catch (error) {
    state.refreshError = error.message;
    setConnectionStatus(false);
    if (!silent) toast(error.message);
  } finally {
    state.refreshing = false;
    button.classList.remove("loading");
    scheduleAutoRefresh();
  }
}

async function loadInitialData() {
  const status = await api("/api/status");
  configureConnectionMode(status);
  $("#hostInput").value = status.router || "192.168.1.1";
  if (status.connected || status.snapshotReady) {
    render(await api("/api/topology"));
  } else {
    openConnectModal();
  }
}

async function initialize() {
  wireEvents();
  observeTopologyWidth();
  state.topologyLockTimer = setInterval(() => {
    updateTopologyLockCountdown();
    updateParentHealthCountdown();
  }, 1000);
  try {
    await loadInitialData();
  } catch (error) {
    $("#connectError").textContent = error.message;
    openConnectModal();
  }
  $("#refreshInterval").value = String(state.refreshInterval);
  scheduleAutoRefresh();
}

function wireEvents() {
  $("#connectForm").addEventListener("submit", async (event) => {
    event.preventDefault();
    const button = $("#connectButton");
    const original = button.innerHTML;
    button.disabled = true;
    button.innerHTML = "<span>Connecting and loading…</span><i>↻</i>";
    $("#connectError").textContent = "";
    try {
      const body = state.managedConnection
        ? {}
        : {
            host: $("#hostInput").value,
            password: $("#passwordInput").value,
          };
      const data = await api("/api/connect", {
        method: "POST",
        body: JSON.stringify(body),
      });
      $("#passwordInput").value = "";
      render(data);
      toast("Connected to Linksys Mesh");
    } catch (error) {
      $("#connectError").textContent = error.message;
      (state.managedConnection ? button : $("#passwordInput")).focus();
    } finally {
      button.disabled = false;
      button.innerHTML = original;
    }
  });
  $("#settingsButton").addEventListener("click", openConnectModal);
  $("#connectClose").addEventListener("click", closeConnectModal);
  $("#refreshButton").addEventListener("click", () => refresh(false));
  $("#editTopologyLockButton").addEventListener("click", beginTopologyLockEdit);
  $("#cancelTopologyLockButton").addEventListener("click", cancelTopologyLockEdit);
  $("#applyTopologyLockButton").addEventListener("click", applyTopologyLock);
  $("#unlockTopologyButton").addEventListener("click", disableTopologyLock);
  $("#topologyRecoveryChip").addEventListener("click", () => {
    $("#topologyLockPanel").scrollIntoView({ behavior: "smooth", block: "center" });
  });
  $("#topologyLockAcknowledgement").addEventListener("change", (event) => {
    state.topologyLockAcknowledged = event.target.checked;
    renderTopologyLockEditor();
  });
  $("#topologyLockNodeSelect").addEventListener("change", (event) => {
    state.topologyLockSelectedNodeId = event.target.value;
    state.topologyLockDraftError = "";
    renderTopologyLockEditor();
  });
  $("#topologyLockParentSelect").addEventListener("change", (event) => {
    moveTopologyLockDraft(
      state.topologyLockSelectedNodeId,
      event.target.value,
    );
  });
  $("#parentSteeringMode").addEventListener("change", (event) => {
    setParentSteeringMode(event.target.value);
  });
  $("#parentSteeringProbeButton").addEventListener("click", () => {
    loadParentSteering(true);
  });
  $("#parentSteeringForm").addEventListener("submit", submitParentSteering);
  $("#parentSteeringChild").addEventListener("change", (event) => {
    state.parentSteeringSelectedChildId = event.target.value;
    state.parentSteeringSelectedParentId = null;
    const child = state.topology?.nodes?.find((node) =>
      MeshMqttParentSteering.sameId(node.id, event.target.value));
    const band = MeshMqttParentSteering.cleanBand(child?.band);
    if (band) state.parentSteeringBand = band;
    renderParentSteering();
  });
  $("#parentSteeringParent").addEventListener("change", (event) => {
    state.parentSteeringSelectedParentId = event.target.value;
    renderParentSteering();
  });
  $("#parentSteeringBand").addEventListener("change", (event) => {
    state.parentSteeringBand = MeshMqttParentSteering.cleanBand(event.target.value) || "5GH";
    renderParentSteering();
  });
  $("#refreshInterval").addEventListener("change", (event) => {
    state.refreshInterval = MeshRefreshState.normalizeInterval(event.target.value);
    state.refreshError = null;
    localStorage.setItem("meshscopeRefreshInterval", String(state.refreshInterval));
    scheduleAutoRefresh();
    toast(state.refreshInterval ? `Auto-refresh set to ${state.refreshInterval} seconds` : "Auto-refresh paused");
  });
  document.addEventListener("visibilitychange", () => {
    const action = MeshRefreshState.visibilityAction({
      hasTopology: Boolean(state.topology),
      refreshing: state.refreshing,
      visible: document.visibilityState === "visible",
      refreshDue: state.refreshDue,
      nextRefreshAt: state.nextRefreshAt,
      now: Date.now(),
    });
    if (action === "refresh") refresh(true);
    else updateRefreshLabel();
  });
  $("#togglePassword").addEventListener("click", () => {
    const input = $("#passwordInput");
    const visible = input.type === "text";
    input.type = visible ? "password" : "text";
    $("#togglePassword").textContent = visible ? "Show" : "Hide";
  });
  $$(".segmented button").forEach((button) => {
    button.addEventListener("click", () => {
      $$(".segmented button").forEach((item) => item.classList.remove("active"));
      button.classList.add("active");
      state.filter = button.dataset.filter;
      state.visibleRows = 18;
      renderClients();
    });
  });
  $("#clientSearch").addEventListener("input", (event) => {
    state.search = event.target.value;
    state.visibleRows = 18;
    renderClients();
  });
  $("#loadMoreButton").addEventListener("click", () => {
    state.visibleRows += 25;
    renderClients();
  });
  $("#detailClose").addEventListener("click", closeDetail);
  $("#detailBackdrop").addEventListener("click", closeDetail);
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      closeDetail();
      closeConnectModal();
    }
  });
}

initialize();
