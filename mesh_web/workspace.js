(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.MeshWorkspace = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const sameId = (a, b) => Boolean(a && b) && String(a).toLowerCase() === String(b).toLowerCase();

  function needsAttention(node, lock = {}, steering = {}) {
    if (!node.online) return true;
    if (["warn", "bad"].includes(node.quality?.tone)) return true;
    const recovery = lock.enabled && lock.nodes?.find((item) => sameId(item.nodeId, node.id));
    if (recovery && !["correct", "wired-manual"].includes(recovery.status)) return true;
    const operation = steering.operation;
    if (sameId(operation?.childId, node.id) && ["failed", "timeout"].includes(operation.state)) return true;
    const wired = node.isWired || /^(wired|ethernet)$/i.test(node.connectionType || "");
    const health = steering.nodeHealth?.find((item) => sameId(item.childId, node.id));
    return !wired && Boolean(health && (health.consecutiveFailures > 0 ||
      ["restart-queued", "parent-restarting", "restart-eligible", "cooldown", "blocked", "restart-failed"].includes(health.state)));
  }

  // Keep each subtree together, including orphaned/offline nodes and bad router
  // data containing a cycle. Filtering never changes the reported parent.
  function orderedNodes(nodes = []) {
    const sorted = [...nodes].sort((a, b) =>
      Number(Boolean(b.isAuthority)) - Number(Boolean(a.isAuthority)) ||
      Number(Boolean(b.online)) - Number(Boolean(a.online)) ||
      String(a.name || "").localeCompare(String(b.name || ""), "en", { numeric: true }));
    const result = [], visited = new Set();
    function visit(node) {
      if (visited.has(node.id)) return;
      visited.add(node.id);
      result.push(node);
      sorted.filter((child) => sameId(child.parentId, node.id)).forEach(visit);
    }
    sorted.filter((node) => node.isAuthority || !nodes.some((parent) => sameId(parent.id, node.parentId))).forEach(visit);
    sorted.forEach(visit);
    return result;
  }

  function visibleNodes(nodes, { query = "", attention = false, lock, steering } = {}) {
    const text = query.trim().toLowerCase();
    return orderedNodes(nodes).filter((node) =>
      (!attention || needsAttention(node, lock, steering)) &&
      (!text || [node.name, node.ipAddress, node.model, node.parentName, node.band]
        .some((value) => String(value || "").toLowerCase().includes(text))));
  }

  function clientMatchesNode(client, nodeId) {
    return !nodeId || sameId(client.nodeId || client.parentId, nodeId);
  }

  function graphScale(width, height, availableWidth, availableHeight, fit) {
    if (!fit || ![width, height, availableWidth, availableHeight].every((n) => Number.isFinite(n) && n > 0)) return 1;
    return Math.min(1, availableWidth / width, availableHeight / height);
  }

  function create(context) {
    const { state, escapeHtml: esc, nodeCardHtml, openDetail, renderClients, renderTopology } = context;
    const $ = (selector) => document.querySelector(selector);
    const $$ = (selector) => [...document.querySelectorAll(selector)];
    const mobile = window.matchMedia("(max-width: 760px)");
    let section = "topology", view = mobile.matches ? "list" : "graph";
    let explicitView = false, attention = false, query = "", fitGraph = false;
    let detailTab = "overview", detailKey = "", graphWidth = 0, graphHeight = 0;
    $(".topology-toolbar").before($("#recoveryWorkspace"));

    function setSection(next, focus = false) {
      section = ["topology", "clients", "recovery"].includes(next) ? next : "topology";
      $("#networkWorkspace").hidden = section === "clients";
      $("#clientsWorkspace").hidden = section !== "clients";
      $("#recoveryWorkspace").hidden = section !== "recovery";
      $$("[data-workspace]").forEach((button) => {
        if (button.dataset.workspace === section) button.setAttribute("aria-current", "page");
        else button.removeAttribute("aria-current");
      });
      if (section !== "clients" && state.topology) renderTopology(state.topology);
      if (focus) {
        const target = section === "recovery" ? $("#recoveryWorkspace") : section === "clients" ? $("#clientSearch") : $("#topologyTitle");
        target.setAttribute("tabindex", "-1");
        target.focus({ preventScroll: true });
        target.scrollIntoView({ block: "start", behavior: "smooth" });
      }
    }

    function applyScale() {
      const scroller = $(".map-scroll");
      const heightLimit = parseFloat(getComputedStyle(scroller).maxHeight) || window.innerHeight;
      // Browser pinch zoom changes the visual viewport, not the layout
      // viewport. Do not multiply by visualViewport.scale/devicePixelRatio or
      // redraw on visual-viewport events: let the browser zoom the vector tree.
      if (!scroller.clientWidth) return;
      const scale = graphScale(graphWidth, graphHeight, scroller.clientWidth - 12, heightLimit - 24, fitGraph);
      const map = $("#meshMap");
      map.style.transformOrigin = "top left";
      map.style.transform = `scale(${scale})`;
      $("#mapStage").style.width = `${Math.ceil(graphWidth * scale)}px`;
      $("#mapStage").style.height = `${Math.ceil(graphHeight * scale)}px`;
      $("#graphFit").setAttribute("aria-pressed", String(fitGraph));
      $("#graphActualSize").setAttribute("aria-pressed", String(!fitGraph));
    }

    function setView(next, explicit = true) {
      view = next === "list" ? "list" : "graph";
      explicitView ||= explicit;
      update();
      if (state.topology) renderTopology(state.topology);
    }

    function update() {
      const data = state.topology;
      if (!data) return;
      const filtered = visibleNodes(data.nodes, { query, attention, lock: state.topologyLock, steering: state.parentSteering });
      const filtering = Boolean(query.trim() || attention);
      const listVisible = view === "list" || filtering;
      $("#nodeList").hidden = !listVisible;
      $(".map-scroll").hidden = listVisible;
      $(".map-scroll-hint").hidden = listVisible;
      $("#graphTools").hidden = listVisible;
      $$("[data-topology-view]").forEach((button) => button.setAttribute("aria-pressed", String(button.dataset.topologyView === (listVisible ? "list" : "graph"))));
      $("#nodeViewSummary").textContent = filtering
        ? `${filtered.length} of ${data.nodes.length} nodes · Filtered list`
        : `${data.nodes.length} nodes · ${data.summary.nodesOnline} online · ${listVisible ? "Grouped by parent" : "Select any node for details"}`;
      $("#clearNodeFilters").hidden = !filtering;
      const count = data.nodes.filter((node) => needsAttention(node, state.topologyLock, state.parentSteering)).length;
      $("#nodeAttentionCount").textContent = count;
      $("#nodeAttentionFilter").setAttribute("aria-pressed", String(attention));
      $("#statAttention").textContent = count;
      const offlineCount = data.nodes.filter((node) => !node.online).length;
      const weakCount = data.nodes.filter((node) => node.online && ["warn", "bad"].includes(node.quality?.tone)).length;
      const otherCount = data.nodes.filter((node) => node.online && !["warn", "bad"].includes(node.quality?.tone) && needsAttention(node, state.topologyLock, state.parentSteering)).length;
      $("#statAttentionSub").textContent = `${weakCount} weak signal · ${offlineCount} offline${otherCount ? ` · ${otherCount} recovery` : ""}`;
      $("#navNodeCount").textContent = data.nodes.length;
      $("#navClientCount").textContent = data.meta?.clientDetails === "nodes-only" ? "—" : data.summary.clientsOnline;
      $("#navRecoveryState").textContent = state.topologyLockEditing ? "Draft" : state.topologyLock.enabled ? "On" : "Off";
      const focusedNode = document.activeElement?.closest("#nodeList [data-node-id]")?.dataset.nodeId;
      // Use the same renderer as the diagram: no second, reduced data model.
      $("#nodeList").innerHTML = filtered.length
        ? filtered.map((node) => nodeCardHtml(state.topologyLockEditing
          ? { ...node, actualParentName: node.parentName, parentName: data.nodes.find((parent) => parent.id === state.topologyLockDraft[node.id])?.name || node.parentName }
          : node, true)).join("")
        : `<div class="list-empty"><strong>${attention ? "No nodes need attention" : "No matching nodes"}</strong><p>${attention ? "Every node matches the current filters. Clear filters to see the full network." : "Try a node name, IP address, model or band."}</p></div>`;
      $("#nodeList").querySelectorAll("[data-node-id]").forEach((button) => {
        button.addEventListener("click", () => openDetail(data.nodes.find((node) => node.id === button.dataset.nodeId), "node"));
        if (button.dataset.nodeId === focusedNode) button.focus({ preventScroll: true });
      });
      const select = $("#clientNodeFilter"), previous = state.clientNodeFilter || "";
      select.innerHTML = '<option value="">All nodes</option>' + orderedNodes(data.nodes).map((node) => `<option value="${esc(node.id)}">${esc(node.name)}</option>`).join("");
      select.value = previous;
      if (select.value !== previous) {
        state.clientNodeFilter = "";
        renderClients();
      }
      const notice = $("#workspaceNotice");
      const degraded = data.meta?.topologyDegraded;
      const offline = data.meta?.routerConnected === false;
      notice.hidden = !degraded && !offline && !state.refreshError;
      notice.textContent = degraded
        ? "Parent data is incomplete. The map may show saved relationships, not verified live links. Automatic recovery is paused while Linksys rebuilds its report."
        : offline || state.refreshError
          ? `Showing the last available snapshot. ${state.refreshError || "The router is unreachable."} MeshScope will retry on the next refresh.` : "";
      applyScale();
    }

    function topologyRendered(layout) {
      graphWidth = layout.width;
      graphHeight = layout.height;
      update();
    }

    function focusRecovery(target = "#topologyLockPanel") {
      setSection("recovery");
      const panel = $(target);
      panel.setAttribute("tabindex", "-1");
      panel.focus({ preventScroll: true });
      panel.scrollIntoView({ behavior: "smooth", block: "center" });
    }

    function expandTopology(expanded) {
      document.body.classList.toggle("focus-topology", expanded);
      $("#focusTopology").textContent = expanded ? "Exit expanded view" : "Expand view";
      $("#focusTopology").setAttribute("aria-pressed", String(expanded));
      if (state.topology) renderTopology(state.topology);
    }

    function selectDetailTab(next) {
      detailTab = next;
      $$("[data-detail-tab]").forEach((button) => {
        const selected = button.dataset.detailTab === next;
        button.setAttribute("aria-selected", String(selected));
        button.tabIndex = selected ? 0 : -1;
      });
      $$("[data-detail-panel]").forEach((panel) => { panel.hidden = panel.dataset.detailPanel !== next; });
    }

    function enhanceDetail(item, kind, fresh) {
      const content = $("#detailContent");
      const key = `${kind}:${item.id}`;
      if (key !== detailKey) { detailKey = key; detailTab = "overview"; }
      $("#detailDrawer").setAttribute("aria-label", `${item.name} ${kind === "node" ? "node" : "client"} details`);
      if (kind === "node") {
        const head = content.querySelector(".detail-head");
        const clients = content.querySelector(".node-clients-section");
        const nodesOnly = state.topology.meta?.clientDetails === "nodes-only";
        const clientCount = nodesOnly ? "—" : clients?.querySelector(".node-clients-heading > span")?.textContent || "0";
        if (nodesOnly && clients) {
          clients.querySelector(".node-clients-heading > span").textContent = "—";
          clients.querySelector(".node-clients-list").innerHTML = '<p class="detail-guide">Client/STA details are not collected in nodes-only mode. This does not mean the node has no connected clients.</p>';
        }
        const tabs = document.createElement("div");
        tabs.className = "detail-tabs";
        tabs.setAttribute("role", "tablist");
        tabs.setAttribute("aria-label", "Node detail sections");
        const sections = [["overview", "Overview"], ["clients", `Clients ${clientCount}`], ["actions", "Actions"], ["diagnostics", "Details"]];
        tabs.innerHTML = sections.map(([name, label]) => `<button type="button" role="tab" id="detailTab-${name}" aria-controls="detailPanel-${name}" data-detail-tab="${name}">${esc(label)}</button>`).join("");
        head.after(tabs);
        const panels = {};
        for (const [name] of sections) {
          const panel = document.createElement("section");
          panel.id = `detailPanel-${name}`;
          panel.dataset.detailPanel = name;
          panel.setAttribute("role", "tabpanel");
          panel.setAttribute("aria-labelledby", `detailTab-${name}`);
          panels[name] = panel;
          content.append(panel);
        }
        for (const selector of [".detail-grid", ".node-lock-detail"]) {
          const element = content.querySelector(selector);
          if (element) panels.overview.append(element);
        }
        for (const selector of [".detail-list", ".node-steering-health-detail"]) {
          const element = content.querySelector(selector);
          if (element) panels.diagnostics.append(element);
        }
        const capabilities = content.querySelector(".node-feasibility");
        if (capabilities) {
          const technical = document.createElement("section");
          technical.className = "detail-technical";
          technical.innerHTML = '<h3>Router capabilities</h3>';
          [...capabilities.querySelectorAll(".node-capability")].slice(0, 2).forEach((element) => technical.append(element));
          capabilities.querySelectorAll(".node-feasibility-note").forEach((element) => technical.append(element));
          panels.diagnostics.append(technical);
          capabilities.querySelector(".section-kicker").textContent = "THIS NODE ONLY";
          capabilities.querySelector("h3").textContent = `${item.name} actions`;
          const move = capabilities.querySelector("#openParentSteeringButton");
          if (move) {
            move.textContent = item.isAuthority ? "Move a node to this gateway" : "Change parent";
            capabilities.prepend(move);
          }
          panels.actions.append(capabilities);
        }
        if (clients) panels.clients.append(clients);
        const links = document.createElement("div");
        links.className = "detail-connections";
        const children = state.topology.nodes.filter((node) => node.online && sameId(node.parentId, item.id));
        const parent = state.topology.nodes.find((node) => sameId(node.id, item.parentId));
        const unverified = item.isWired || /^(wired|ethernet)$/i.test(item.connectionType || "") || /degraded|unavailable|wired-assignment/.test(item.parentSource || "");
        links.innerHTML = `<div><span>${unverified ? "Displayed parent · unverified" : "Current parent"}</span>${parent ? `<button class="text-button" type="button" data-related-node="${esc(parent.id)}">${esc(parent.name)} ↗</button>` : `<strong>${item.isAuthority ? "Internet / WAN" : "Unverified"}</strong>`}</div>
          <div><span>${children.length} online mesh children</span><span>${children.length ? children.map((node) => `<button class="text-button" type="button" data-related-node="${esc(node.id)}">${esc(node.name)} ↗</button>`).join(" ") : "None"}</span></div>
          <button class="button button-quiet" type="button" id="detailClientsShortcut">${nodesOnly ? "Client collection is off" : `View ${clientCount} connected clients →`}</button>
          <p class="detail-guide">Use Actions to change parent, measure hop throughput or restart this node. Details contains all addresses, sample times and steering counters.</p>`;
        panels.overview.prepend(links);
        // A visible preview makes the node → client relationship clear on the
        // first tap, while the Clients tab retains the complete STA list.
        const preview = document.createElement("div");
        preview.className = "detail-client-preview";
        preview.innerHTML = `<h3>Connected clients</h3>${[...clients.querySelectorAll(".node-client")].slice(0, 3).map((button) => button.outerHTML).join("") || `<p>${nodesOnly ? "Client details are not collected in nodes-only mode." : "No online clients reported for this node."}</p>`}`;
        panels.overview.append(preview);
        tabs.querySelectorAll("button").forEach((button, index) => {
          button.addEventListener("click", () => selectDetailTab(button.dataset.detailTab));
          button.addEventListener("keydown", (event) => {
            if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) return;
            event.preventDefault();
            const next = event.key === "Home" ? 0 : event.key === "End" ? 3 : (index + (event.key === "ArrowRight" ? 1 : 3)) % 4;
            selectDetailTab(sections[next][0]);
            tabs.children[next].focus();
          });
        });
        $("#detailClientsShortcut").addEventListener("click", () => {
          selectDetailTab("clients");
          $("#detailTab-clients").focus();
        });
        links.querySelectorAll("[data-related-node]").forEach((button) => button.addEventListener("click", () => {
          openDetail(state.topology.nodes.find((node) => node.id === button.dataset.relatedNode), "node");
        }));
        preview.querySelectorAll("[data-node-client-id]").forEach((button) => button.addEventListener("click", () => {
          openDetail(state.topology.clients.find((client) => client.id === button.dataset.nodeClientId), "client");
        }));
        selectDetailTab(detailTab);
      }
      syncDialogs();
      if (fresh) {
        $("#detailDrawer").scrollTop = 0;
        $("#detailClose").focus({ preventScroll: true });
      }
    }

    function syncDialogs() {
      const connected = !$("#connectModal").classList.contains("hidden");
      const details = $("#detailDrawer").classList.contains("open");
      $(".app-shell").inert = connected || details;
      $("#detailDrawer").inert = !details || connected;
      $("#connectModal").inert = !connected;
      document.body.classList.toggle("dialog-open", connected || details);
    }

    function updateNotice() {
      if (!state.topology) return;
      const meta = state.topology.meta || {};
      const notice = $("#workspaceNotice");
      notice.hidden = !meta.topologyDegraded && meta.routerConnected !== false && !state.refreshError;
      if (state.refreshError && !meta.topologyDegraded) notice.textContent = `Showing the last available snapshot. ${state.refreshError} MeshScope will retry on the next refresh.`;
    }

    $$("[data-workspace]").forEach((button) => button.addEventListener("click", () => setSection(button.dataset.workspace, true)));
    $(".brand").addEventListener("click", (event) => { event.preventDefault(); setSection("topology", true); });
    $$("[data-topology-view]").forEach((button) => button.addEventListener("click", () => {
      if (button.dataset.topologyView === "graph") { query = ""; attention = false; $("#nodeSearch").value = ""; }
      setView(button.dataset.topologyView);
    }));
    $("#nodeSearch").addEventListener("input", (event) => { query = event.target.value; update(); });
    $("#nodeAttentionFilter").addEventListener("click", () => { attention = !attention; update(); });
    $("#clearNodeFilters").addEventListener("click", () => { query = ""; attention = false; $("#nodeSearch").value = ""; setView(view, false); });
    $("#graphFit").addEventListener("click", () => { fitGraph = true; applyScale(); });
    $("#graphActualSize").addEventListener("click", () => { fitGraph = false; applyScale(); });
    $("#focusTopology").addEventListener("click", () => expandTopology(!document.body.classList.contains("focus-topology")));
    $("#summaryNodes").addEventListener("click", () => { query = ""; attention = false; $("#nodeSearch").value = ""; setSection("topology", true); });
    $("#summaryClients").addEventListener("click", () => setSection("clients", true));
    $("#summaryAttention").addEventListener("click", () => { attention = true; query = ""; $("#nodeSearch").value = ""; setSection("topology", true); });
    $("#healthToggle").addEventListener("click", () => {
      const panel = $("#healthPanel");
      panel.hidden = !panel.hidden;
      $("#healthToggle").setAttribute("aria-expanded", String(!panel.hidden));
      if (!panel.hidden) panel.scrollIntoView({ behavior: "smooth", block: "nearest" });
    });
    $("#clientNodeFilter").addEventListener("change", (event) => { state.clientNodeFilter = event.target.value; state.visibleRows = 18; renderClients(); });
    mobile.addEventListener("change", () => { if (!explicitView) setView(mobile.matches ? "list" : "graph", false); });
    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && !document.body.classList.contains("dialog-open")) expandTopology(false);
      if (event.key !== "Tab") return;
      const dialog = !$("#connectModal").classList.contains("hidden") ? $("#connectModal") : $("#detailDrawer").classList.contains("open") ? $("#detailDrawer") : null;
      if (!dialog) return;
      const focusable = [...dialog.querySelectorAll('button:not(:disabled), input:not(:disabled), select:not(:disabled), a[href], [tabindex="0"]')]
        .filter((element) => element.getClientRects().length && !element.closest("[hidden]"));
      const first = focusable[0], last = focusable.at(-1);
      if (event.shiftKey && (document.activeElement === first || !dialog.contains(document.activeElement))) {
        event.preventDefault(); last?.focus();
      } else if (!event.shiftKey && (document.activeElement === last || !dialog.contains(document.activeElement))) {
        event.preventDefault(); first?.focus();
      }
    });
    return { update, updateNotice, topologyRendered, enhanceDetail, syncDialogs, focusRecovery,
      graphVisible: () => section !== "clients" && view === "graph" && !query.trim() && !attention };
  }
  return { needsAttention, orderedNodes, visibleNodes, clientMatchesNode, graphScale, create };
});
