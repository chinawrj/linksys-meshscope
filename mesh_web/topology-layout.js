(function (root, factory) {
  const engine = factory();
  if (typeof module === "object" && module.exports) module.exports = engine;
  else root.MeshTopologyLayout = engine;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  function compute(nodes, options = {}) {
    const nodeWidth = options.nodeWidth || 188;
    const nodeHeight = options.nodeHeight || 124;
    const columnGap = options.columnGap || 128;
    const rowGap = options.rowGap || 30;
    const left = options.left || 194;
    const top = options.top || 24;
    const bottomSpace = options.bottomSpace || 78;
    const online = nodes.filter((node) => node.online);
    const rootNode = online.find((node) => node.isAuthority);
    if (!rootNode) {
      return { positions: [], edges: [], width: 820, height: 520, nodeWidth, nodeHeight };
    }

    const byId = new Map(online.map((node) => [node.id, node]));
    const parentById = new Map();
    const createsCycle = (nodeId, parentId) => {
      const seen = new Set([nodeId]);
      let cursor = parentId;
      while (cursor && cursor !== rootNode.id) {
        if (seen.has(cursor)) return true;
        seen.add(cursor);
        cursor = byId.get(cursor)?.parentId;
      }
      return false;
    };

    for (const node of online) {
      if (node.id === rootNode.id) continue;
      const candidate = byId.has(node.parentId) ? node.parentId : rootNode.id;
      parentById.set(node.id, createsCycle(node.id, candidate) ? rootNode.id : candidate);
    }

    const children = new Map();
    for (const node of online) children.set(node.id, []);
    for (const [nodeId, parentId] of parentById.entries()) {
      children.get(parentId).push(byId.get(nodeId));
    }
    for (const list of children.values()) {
      list.sort((a, b) => a.name.localeCompare(b.name, "zh-CN", { numeric: true }));
    }

    let leafCursor = top;
    let maxDepth = 0;
    const placed = new Map();

    function place(node, depth) {
      maxDepth = Math.max(maxDepth, depth);
      const childNodes = children.get(node.id) || [];
      let y;
      if (!childNodes.length) {
        y = leafCursor;
        leafCursor += nodeHeight + rowGap;
      } else {
        const childPositions = childNodes.map((child) => place(child, depth + 1));
        const first = childPositions[0];
        const last = childPositions[childPositions.length - 1];
        y = (first.y + last.y) / 2;
      }
      const value = {
        ...node,
        parentId: parentById.get(node.id) || node.parentId,
        depth,
        x: left + depth * (nodeWidth + columnGap),
        y,
      };
      placed.set(node.id, value);
      return value;
    }

    place(rootNode, 0);
    const positions = online.map((node) => placed.get(node.id)).filter(Boolean);
    const edges = positions
      .filter((node) => !node.isAuthority)
      .map((node) => ({
        id: `${node.parentId}->${node.id}`,
        source: placed.get(node.parentId) || placed.get(rootNode.id),
        target: node,
        band: node.band || node.connectionType || "Mesh",
        speedMbps: node.speedMbps,
        rssi: node.rssi,
        channel: node.channel,
        tone: node.quality?.tone || "",
      }));
    const usedHeight = Math.max(
      nodeHeight + top * 2,
      ...positions.map((node) => node.y + nodeHeight + top),
    );
    return {
      positions,
      edges,
      root: placed.get(rootNode.id),
      nodeWidth,
      nodeHeight,
      width: Math.max(900, left + (maxDepth + 1) * (nodeWidth + columnGap) + 36),
      height: usedHeight + bottomSpace,
    };
  }

  return { compute };
});
