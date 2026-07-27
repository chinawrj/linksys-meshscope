const state = {
  topology: null,
  filter: "online",
  search: "",
  visibleRows: 18,
  refreshTimer: null,
  countdownTimer: null,
  refreshInterval: MeshRefreshState.normalizeInterval(
    localStorage.getItem("meshscopeRefreshInterval") ?? MeshRefreshState.DEFAULT_INTERVAL,
  ),
  nextRefreshAt: null,
  refreshDue: false,
  refreshError: null,
  refreshing: false,
  topologyAnimationFrame: null,
  detailToken: 0,
  detailSelection: null,
  nodeRestarts: new Map(),
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
  return Number(value).toLocaleString("zh-CN", {
    maximumFractionDigits: digits,
    minimumFractionDigits: digits,
  });
}

function formatTime(value) {
  if (!value) return "—";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return "—";
  return new Intl.DateTimeFormat("zh-CN", {
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
  return `<span class="signal-bars level-${level} ${escapeHtml(tone)}" aria-label="信号 ${rssi ?? "未知"} dBm"><i></i><i></i><i></i><i></i></span>`;
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
    payload = { error: "本地服务返回了无法识别的数据。" };
  }
  if (!response.ok) {
    throw new Error(payload.error || `请求失败 (${response.status})`);
  }
  return payload;
}

function openConnectModal() {
  $("#connectModal").classList.remove("hidden");
  requestAnimationFrame(() => $("#passwordInput").focus());
}

function closeConnectModal() {
  if (!state.topology) return;
  $("#connectModal").classList.add("hidden");
  $("#connectError").textContent = "";
}

function setConnectionStatus(connected) {
  $("#liveChip").classList.toggle("connected", connected);
  if (!connected) $("#liveText").textContent = "等待连接";
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
  $("#liveText").textContent = state.topology?.meta?.demo
    ? `演示 · ${status.text}`
    : status.text;
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
  $("#routerLabel").textContent = meta.router;
  $("#heroCopy").textContent = meta.demo
    ? "用完整离线演示数据检查节点层级、回程质量与 Client / STA 交互；不会连接或修改路由器。"
    : "实时查看每个 Velop 节点、回程质量与当前接入设备。所有数据仅在本机与路由器之间流动。";
  $("#networkStatus").textContent = meta.demo
    ? "离线演示数据"
    : network.wanStatus === "Connected"
      ? "运行正常"
      : network.wanStatus || "状态未知";
  $("#lastUpdated").textContent = `${meta.demo ? "未连接路由器 · " : ""}更新于 ${formatTime(meta.updatedAt)}`;
  $("#statNodes").textContent = `${summary.nodesOnline}/${summary.nodesTotal}`;
  $("#statNodesSub").textContent = `${summary.nodesTotal - summary.nodesOnline} 个离线节点`;
  $("#statClients").textContent = compactNumber(summary.clientsOnline);
  $("#statClientsSub").textContent = `${summary.clientsKnown} 个已知设备记录`;
  $("#statBackhaul").textContent = summary.backhaulMbps ? `${compactNumber(summary.backhaulMbps)}M` : "—";
  $("#statBackhaulSub").textContent = "在线节点协商速率合计";
  $("#statAttention").textContent = compactNumber(summary.weakNodes + (summary.nodesTotal - summary.nodesOnline));
  $("#statAttentionSub").textContent = `${summary.weakNodes} 个弱信号 · ${summary.nodesTotal - summary.nodesOnline} 个离线`;

  $("#networkModel").textContent = network.model || "—";
  $("#networkFirmware").textContent = network.firmwareVersion || "—";
  $("#networkWan").textContent = network.wanStatus === "Connected" ? `已连接 · ${network.wanType || "WAN"}` : network.wanStatus || "未知";
  $("#nodeSteering").textContent =
    network.nodeSteeringEnabled === true
      ? "自动 · 已开启"
      : network.nodeSteeringEnabled === false
        ? "自动 · 已关闭"
        : "固件未报告";
  $("#manualSteering").textContent = network.manualParentSelectionAvailable
    ? "支持"
    : network.manualParentSelectionEvidence === "firmware-internal-confirmed"
      ? "内部已确认 · 未开放"
      : "未发现接口";
  $("#steeringTitle").textContent =
    network.nodeSteeringEnabled === true ? "自动 Node Steering 已开启" : "自动 Node Steering 未开启";
  $("#steeringDescription").textContent = network.manualParentSelectionAvailable
    ? "固件报告了手动 Parent 选择能力。"
    : network.manualParentSelectionEvidence === "firmware-internal-confirmed"
      ? "MX4200/WHW03 固件内部已确认指定 Parent 数据路径；当前 Web/JNAP 没有安全传输入口，因此保持只读。"
      : "固件会自动选择最强信号并自愈；未发现可把子节点锁定到指定 Parent 的受支持接口。";
  $("#steeringMode").textContent = network.manualParentSelectionAvailable
    ? "MANUAL AVAILABLE"
    : network.manualParentSelectionEvidence === "firmware-internal-confirmed"
      ? "INTERNAL · NO TRANSPORT"
      : "AUTO ONLY";

  const score = healthScore(data);
  $("#healthRing").style.setProperty("--score", score);
  $("#healthPercent").textContent = `${score}%`;
  $("#healthScore").textContent = score >= 85 ? "状态良好" : score >= 65 ? "建议检查" : "需要关注";
  $("#healthScore").style.background = score >= 85 ? "var(--mint-soft)" : score >= 65 ? "var(--amber-soft)" : "var(--coral-soft)";
  $("#healthScore").style.color = score >= 85 ? "var(--mint)" : score >= 65 ? "var(--amber)" : "var(--coral)";
  $("#healthSummary").textContent =
    score >= 85
      ? "网络整体稳定。回程链路与在线客户端都在持续响应。"
      : "部分节点离线或回程信号偏弱，建议查看下方具体节点。";

  const weakest = data.nodes
    .filter((node) => node.online && !node.isAuthority)
    .sort((a, b) => (a.rssi ?? -100) - (b.rssi ?? -100))[0];
  const strongest = data.nodes
    .filter((node) => node.online && !node.isAuthority)
    .sort((a, b) => (b.speedMbps ?? 0) - (a.speedMbps ?? 0))[0];
  $("#healthList").innerHTML = [
    {
      tone: network.wanStatus === "Connected" ? "" : "bad",
      label: "互联网连接",
      value: network.wanStatus === "Connected" ? "在线" : network.wanStatus || "异常",
    },
    {
      tone: summary.nodesTotal === summary.nodesOnline ? "" : "warn",
      label: "Mesh 节点",
      value: `${summary.nodesOnline}/${summary.nodesTotal} 在线`,
    },
    {
      tone: weakest?.quality?.tone === "bad" ? "bad" : weakest?.quality?.tone === "warn" ? "warn" : "",
      label: "最弱回程",
      value: weakest ? `${weakest.name} · ${weakest.rssi ?? "—"} dBm` : "—",
    },
    {
      tone: "",
      label: "最快回程",
      value: strongest?.speedMbps ? `${strongest.name} · ${compactNumber(strongest.speedMbps)} Mbps` : "—",
    },
  ]
    .map((item) => `<div class="health-item ${item.tone}"><i></i><span>${escapeHtml(item.label)}</span><strong>${escapeHtml(item.value)}</strong></div>`)
    .join("");
}

function layoutNodes(nodes) {
  return MeshTopologyLayout.compute(nodes);
}

function renderTopology(data) {
  const map = $("#meshMap");
  if (state.topologyAnimationFrame) {
    cancelAnimationFrame(state.topologyAnimationFrame);
    state.topologyAnimationFrame = null;
  }
  const layout = layoutNodes(data.nodes);
  const { positions, edges, root, nodeWidth, nodeHeight } = layout;
  const offline = data.nodes.filter((node) => !node.online);
  if (!positions.length || !root) return;
  map.style.minWidth = `${layout.width}px`;
  map.style.height = `${layout.height}px`;
  const internetY = root.y + nodeHeight / 2 - 38;
  const wanEdge = {
    id: "internet->gateway",
    sourcePoint: { x: 110, y: root.y + nodeHeight / 2 },
    targetPoint: { x: root.x, y: root.y + nodeHeight / 2 },
    band: "WAN",
    speedMbps: null,
    tone: "wan",
  };
  let html = `<canvas class="topology-canvas" id="topologyCanvas" aria-hidden="true"></canvas>`;
  html += `<div class="map-internet" style="left:34px;top:${internetY}px"><span>⌁</span><small>INTERNET</small></div>`;
  html += edgeLabelHtml(wanEdge);
  for (const edge of edges) html += edgeLabelHtml(edge, nodeWidth, nodeHeight);
  for (const node of positions) {
    const tone = node.quality?.tone || "";
    const restart = state.nodeRestarts.get(node.id);
    html += `
      <button class="mesh-node ${node.isAuthority ? "master" : ""} ${tone === "warn" || tone === "bad" ? "weak" : ""} ${restart ? "restarting" : ""}"
        style="left:${node.x}px;top:${node.y}px" data-node-id="${escapeHtml(node.id)}" type="button">
        <div class="node-title">
          <strong>${escapeHtml(node.name)}</strong>
          <span class="node-role">${node.isAuthority ? "GATEWAY" : "NODE"}</span>
        </div>
        <div class="node-meta">${escapeHtml(node.model)} · ${escapeHtml(node.ipAddress || "无 IP")}</div>
        ${node.isAuthority ? "" : `<div class="node-parent">↳ ${escapeHtml(node.parentName || "Main")} · ${escapeHtml(node.band || "Mesh")}${node.channel ? ` ch ${node.channel}` : ""}</div>`}
        <div class="node-stats">
          <div><span>客户端</span><strong>${node.clientCount}</strong></div>
          <div><span>${node.isAuthority ? "状态" : "回程"}</span><strong>${node.isAuthority ? "在线" : `${compactNumber(node.speedMbps)} Mbps`}</strong></div>
          <div class="node-signal">${node.isAuthority ? '<span class="signal-bars level-4"><i></i><i></i><i></i><i></i></span>' : signalBars(node.rssi, tone)}<small>${node.isAuthority ? "WAN" : `${node.rssi ?? "—"} dBm`}</small></div>
        </div>
        ${restart ? `<div class="node-operation"><i aria-hidden="true">↻</i><span>${escapeHtml(MeshNodeRestartState.label(restart))}</span></div>` : ""}
      </button>`;
  }
  if (offline.length) {
    html += `<div class="offline-strip"><strong>${offline.length} 个离线节点</strong>${offline
      .slice(0, 5)
      .map((node) => `<button class="offline-node-chip text-button" data-node-id="${escapeHtml(node.id)}" type="button">${escapeHtml(node.name)}</button>`)
      .join("")}${offline.length > 5 ? `<span>还有 ${offline.length - 5} 个</span>` : ""}</div>`;
  }
  map.innerHTML = html;
  const canvasEdges = [
    wanEdge,
    ...edges.map((edge) => ({
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
      const node = data.nodes.find((item) => item.id === button.dataset.nodeId);
      if (node) openDetail(node, "node");
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
  const details = edge.band === "WAN"
    ? "UPLINK"
    : `${edge.speedMbps ? `${compactNumber(edge.speedMbps)}M` : "—"}${edge.rssi !== null && edge.rssi !== undefined ? ` · ${edge.rssi}dBm` : ""}`;
  return `
    <span class="edge-label band-${escapeHtml(String(edge.band).toLowerCase())}" style="left:${midX - 42}px;top:${midY - 20}px">
      <strong>${escapeHtml(edge.band)}</strong><small>${escapeHtml(details)}</small>
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
      const palette = palettes[edge.band] || palettes.WAN;
      context.save();
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
      if (!reduceMotion) {
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
    : `<tr><td colspan="7" class="empty-row">没有匹配的客户端。</td></tr>`;
  $("#clientCountLabel").textContent = `显示 ${visible.length} / ${clients.length} 台设备`;
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
    : `<span>${client.online ? "有线" : "离线"}</span>`;
  return `
    <tr>
      <td>
        <div class="device-cell">
          <span class="device-icon" aria-hidden="true">${typeIcons[client.type] || typeIcons.device}</span>
          <div><strong>${escapeHtml(client.name)}</strong><span>${escapeHtml(client.model || client.manufacturer || client.type)}</span></div>
        </div>
      </td>
      <td><span class="node-chip">${escapeHtml(client.nodeName || "—")}</span></td>
      <td><span class="connection-chip">${escapeHtml(client.online ? client.band || (client.rssi === null ? "Ethernet" : "Wi‑Fi") : "历史")}</span></td>
      <td><div class="signal-cell">${signal}</div></td>
      <td>${client.speedMbps !== null && client.speedMbps !== undefined ? `${compactNumber(client.speedMbps)} Mbps` : "—"}</td>
      <td>${escapeHtml(client.ipAddress || "—")}</td>
      <td><button class="row-detail" data-client-id="${escapeHtml(client.id)}" type="button" aria-label="查看 ${escapeHtml(client.name)} 详情">›</button></td>
    </tr>`;
}

function nodeClientHtml(client) {
  const connection = client.online
    ? client.band || (client.rssi === null ? "Ethernet" : "Wi‑Fi")
    : "历史";
  const signal = client.rssi !== null && client.rssi !== undefined
    ? `${client.rssi} dBm`
    : client.online
      ? "有线"
      : "离线";
  return `
    <button class="node-client" data-node-client-id="${escapeHtml(client.id)}" type="button">
      <span class="device-icon" aria-hidden="true">${typeIcons[client.type] || typeIcons.device}</span>
      <span class="node-client-copy">
        <strong>${escapeHtml(client.name)}</strong>
        <small>${escapeHtml(client.model || client.manufacturer || client.type || "网络设备")}</small>
        <code>${escapeHtml(client.ipAddress || client.macAddress || "无地址")}</code>
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
  state.detailSelection = { id: item.id, kind };
  const detailToken = ++state.detailToken;
  const isNode = kind === "node";
  const attachedClients = isNode
    ? MeshDetailData.clientsForNode(state.topology, item)
    : [];
  const capabilityReport = isNode
    ? MeshDetailData.nodeCapabilityReport(state.topology, item)
    : null;
  const parentNode = isNode ? null : MeshDetailData.nodeForClient(state.topology, item);
  const nodeRows = isNode ? MeshDetailData.nodeDetailRows(item, compactNumber) : null;
  const metrics = isNode
    ? nodeRows.metrics
    : [
        ["当前状态", item.online ? "在线" : "历史设备"],
        ["接入节点", item.nodeName || "—"],
        ["协商速率", item.speedMbps !== null && item.speedMbps !== undefined ? `${compactNumber(item.speedMbps)} Mbps` : "—"],
        ["信号质量", item.rssi !== null && item.rssi !== undefined ? `${item.rssi} dBm · ${item.quality.label}` : "—"],
      ];
  const details = isNode
    ? nodeRows.details
    : [
        ["设备类型", item.type],
        ["型号", item.model],
        ["厂商", item.manufacturer],
        ["操作系统", item.operatingSystem],
        ["IP 地址", item.ipAddress],
        ["MAC 地址", item.macAddress],
        ["连接频段", item.band],
        ["Radio", item.radioId],
        ["最后观测", item.lastSeen ? new Date(item.lastSeen).toLocaleString("zh-CN") : null],
      ];
  $("#detailContent").innerHTML = `
    ${parentNode ? `<button class="detail-back" id="detailBackToNode" type="button">← 返回 ${escapeHtml(parentNode.name)}</button>` : ""}
    <div class="detail-head">
      <span class="detail-type-icon" aria-hidden="true">${isNode ? typeIcons.node : typeIcons[item.type] || typeIcons.device}</span>
      <p class="section-kicker">${isNode ? "MESH NODE" : "CLIENT DEVICE"}</p>
      <h2>${escapeHtml(item.name)}</h2>
      <p>${escapeHtml(isNode ? item.description || item.role : item.model || item.manufacturer || "网络设备")}</p>
      <span class="detail-status ${item.online ? "" : "offline"}">${item.online ? "● 在线" : "○ 离线"}</span>
    </div>
    <div class="detail-grid">${metrics
      .map(([label, value]) => `<div class="detail-metric"><span>${escapeHtml(label)}</span><strong>${escapeHtml(value ?? "—")}</strong></div>`)
      .join("")}</div>
    <div class="detail-list">${details
      .filter(([, value]) => value !== null && value !== undefined && value !== "")
      .map(([label, value]) => `<div><span>${escapeHtml(label)}</span><strong>${escapeHtml(value)}</strong></div>`)
      .join("")}</div>
    ${isNode ? `
      <section class="node-feasibility" aria-labelledby="nodeFeasibilityTitle">
        <div class="node-feasibility-heading">
          <div>
            <p class="section-kicker">NODE CAPABILITIES</p>
            <h3 id="nodeFeasibilityTitle">${escapeHtml(item.name)} 节点能力</h3>
          </div>
          <span>单节点操作</span>
        </div>
        <div class="node-capability-list">
          <article class="node-capability ${escapeHtml(capabilityReport.parentRole.status)}">
            <i aria-hidden="true">↳</i>
            <div><span>作为上游 Parent</span><strong>${escapeHtml(capabilityReport.parentRole.label)}</strong><small>${escapeHtml(capabilityReport.parentRole.detail)}</small></div>
          </article>
          <article class="node-capability ${escapeHtml(capabilityReport.manualTarget.status)}">
            <i aria-hidden="true">⌁</i>
            <div><span>手动指定到此 Parent</span><strong>${escapeHtml(capabilityReport.manualTarget.label)}</strong><small>${escapeHtml(capabilityReport.manualTarget.detail)}</small></div>
          </article>
          <article class="node-capability ${escapeHtml(capabilityReport.individualRestart.status)}">
            <i aria-hidden="true">↻</i>
            <div>
              <span>重启当前 Node</span>
              <strong id="nodeRestartProbeLabel">${escapeHtml(capabilityReport.individualRestart.label)}</strong>
              <small id="nodeRestartProbeDetail">${escapeHtml(capabilityReport.individualRestart.detail)}</small>
              <button class="node-restart-button" id="restartMeshButton" type="button" hidden>立即重启 ${escapeHtml(item.name)}</button>
            </div>
          </article>
          <article class="node-capability ${escapeHtml(capabilityReport.localManagement.status)}" id="nodeDirectProbe">
            <i aria-hidden="true">⌘</i>
            <div>
              <span>Node Web / JNAP</span>
              <strong id="nodeProbeLabel">${escapeHtml(item.online ? "正在只读探测…" : capabilityReport.localManagement.label)}</strong>
              <small id="nodeProbeDetail">${escapeHtml(capabilityReport.localManagement.detail)}</small>
              ${capabilityReport.localManagement.url ? `<a class="node-management-link" href="${escapeHtml(capabilityReport.localManagement.url)}" target="_blank" rel="noopener noreferrer">打开 ${escapeHtml(item.name)} CA 页面 ↗</a>` : ""}
            </div>
          </article>
        </div>
        <p class="node-feasibility-note">隐藏入口为 <code>https://&lt;Node-IP&gt;/ca</code>；登录后固件进入 <code>#casupport</code>。重启请求直接发送到当前 Node 的本地端点。</p>
      </section>` : ""}
    ${isNode ? `
      <section class="node-clients-section" aria-labelledby="nodeClientsTitle">
        <div class="node-clients-heading">
          <div>
            <p class="section-kicker">ASSOCIATED STATIONS</p>
            <h3 id="nodeClientsTitle">当前 Client / STA</h3>
          </div>
          <span>${attachedClients.length}</span>
        </div>
        <div class="node-clients-list">
          ${attachedClients.length
            ? attachedClients.map(nodeClientHtml).join("")
            : `<div class="node-clients-empty"><span>◇</span><strong>当前没有在线 Client / STA</strong><small>历史设备不会被错误归到该节点。</small></div>`}
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
      ? "演示模式 · 未连接真实 Node"
      : report.credentialsSynchronized
        ? `${report.deviceMode || "本地"} · 同步凭证已验证`
        : `${report.deviceMode || "本地"} · 凭证状态未知`;
    probeDetail.textContent = report.demo
      ? `${report.identity.model || node.model || "Linksys Node"} 合成身份 · 不执行网络请求`
      : `${report.identity.model || node.model || "Linksys Node"} 直连身份已确认 · 未执行控制`;
    if (report.individualRestart.visibleInCaSupportUi) {
      const restartButton = $("#restartMeshButton");
      const activeRestart = state.nodeRestarts.get(node.id);
      restartLabel.textContent = activeRestart ? "当前 Node · 重启进行中" : "当前 Node · 可用";
      restartDetail.textContent = activeRestart
        ? `${MeshNodeRestartState.label(activeRestart)}；页面正在继续观测`
        : `点击后直接向 ${node.ipAddress} 发送 core/Reboot`;
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
    if ($("#nodeProbeLabel")) $("#nodeProbeLabel").textContent = "只读探测未完成";
    if ($("#nodeProbeDetail")) $("#nodeProbeDetail").textContent = error.message;
  }
}

async function restartNode(node, button) {
  if (state.nodeRestarts.has(node.id)) return;
  button.disabled = true;
  button.textContent = `正在重启 ${node.name}…`;
  try {
    const result = await api("/api/restart-node", {
      method: "POST",
      body: JSON.stringify(MeshNodeRestartState.requestBody(node)),
    });
    MeshNodeRestartState.begin(state.nodeRestarts, node);
    closeDetail();
    state.refreshError = null;
    renderTopology(state.topology);
    toast(`${result.requestedThroughNode.name} 正在重启`);
    MeshNodeRestartState.POLL_DELAYS_MS.forEach((delay) => {
      window.setTimeout(() => refresh(true), delay);
    });
  } catch (error) {
    button.disabled = false;
    button.textContent = `立即重启 ${node.name}`;
    toast(error.message);
  }
}

function reconcileNodeRestarts(data) {
  const events = MeshNodeRestartState.reconcile(state.nodeRestarts, data.nodes);
  for (const event of events) {
    if (event.type === "recovered") toast(`${event.name} 已恢复在线`);
    else if (event.type === "online-timeout") toast(`${event.name} 当前在线`);
    else toast(`${event.name} 状态观测已结束，可手动刷新`);
  }
}

function closeDetail() {
  state.detailToken += 1;
  state.detailSelection = null;
  $("#detailBackdrop").classList.remove("open");
  $("#detailDrawer").classList.remove("open");
  $("#detailDrawer").setAttribute("aria-hidden", "true");
}

function render(data) {
  const detailSelection = state.detailSelection;
  const detailScrollTop = $("#detailDrawer").scrollTop;
  reconcileNodeRestarts(data);
  state.topology = data;
  setConnectionStatus(true);
  renderSummary(data);
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
    if (!silent) toast("网络数据已刷新");
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

async function initialize() {
  wireEvents();
  try {
    const status = await api("/api/status");
    $("#hostInput").value = status.router || "10.37.1.1";
    if (status.connected) {
      render(await api("/api/topology"));
    } else {
      openConnectModal();
    }
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
    button.innerHTML = "<span>正在连接并读取…</span><i>↻</i>";
    $("#connectError").textContent = "";
    try {
      const data = await api("/api/connect", {
        method: "POST",
        body: JSON.stringify({
          host: $("#hostInput").value,
          password: $("#passwordInput").value,
        }),
      });
      $("#passwordInput").value = "";
      render(data);
      toast("已连接 Linksys Mesh");
    } catch (error) {
      $("#connectError").textContent = error.message;
      $("#passwordInput").focus();
    } finally {
      button.disabled = false;
      button.innerHTML = original;
    }
  });
  $("#settingsButton").addEventListener("click", openConnectModal);
  $("#connectClose").addEventListener("click", closeConnectModal);
  $("#refreshButton").addEventListener("click", () => refresh(false));
  $("#refreshInterval").addEventListener("change", (event) => {
    state.refreshInterval = MeshRefreshState.normalizeInterval(event.target.value);
    state.refreshError = null;
    localStorage.setItem("meshscopeRefreshInterval", String(state.refreshInterval));
    scheduleAutoRefresh();
    toast(state.refreshInterval ? `自动刷新已设为 ${state.refreshInterval} 秒` : "自动刷新已暂停");
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
    $("#togglePassword").textContent = visible ? "显示" : "隐藏";
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
