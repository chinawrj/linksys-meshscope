(function (root, factory) {
  const engine = factory();
  if (typeof module === "object" && module.exports) module.exports = engine;
  else root.MeshDetailData = engine;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  function clientNodeId(client) {
    return client.nodeId || client.parentId || null;
  }

  function clientsForNode(topology, node, { onlineOnly = true } = {}) {
    if (!topology?.clients || !node?.id) return [];
    return topology.clients
      .filter((client) => clientNodeId(client) === node.id)
      .filter((client) => !onlineOnly || client.online)
      .sort((a, b) => {
        if (Boolean(a.online) !== Boolean(b.online)) return a.online ? -1 : 1;
        return String(a.name || "").localeCompare(String(b.name || ""), "zh-CN", {
          numeric: true,
        });
      });
  }

  function nodeForClient(topology, client) {
    const nodeId = clientNodeId(client);
    return topology?.nodes?.find((node) => node.id === nodeId) || null;
  }

  function nodeCapabilityReport(topology, node) {
    const children = (topology?.nodes || [])
      .filter((candidate) => candidate.online && candidate.parentId === node?.id)
      .sort((a, b) => String(a.name || "").localeCompare(String(b.name || ""), "zh-CN", {
        numeric: true,
      }));
    const manualParentAvailable =
      topology?.network?.manualParentSelectionAvailable === true;
    const individualRestartAvailable =
      topology?.network?.individualNodeRestartAvailable === true;

    return {
      children,
      parentRole: node?.isAuthority
        ? {
            status: "gateway",
            label: "主网关",
            detail: children.length
              ? `${children.map((child) => child.name).join("、")} 当前直接上联此节点`
              : "当前没有直接下游 Mesh 节点",
          }
        : children.length
          ? {
              status: "confirmed",
              label: "自动 Parent · 已验证",
              detail: `${children.map((child) => child.name).join("、")} 当前直接上联此节点`,
            }
          : {
              status: "automatic",
              label: "由固件自动决定",
              detail: "当前没有下游节点；这不代表该节点不能成为 Parent",
            },
      manualTarget: manualParentAvailable
        ? {
            status: "available",
            label: "固件报告支持",
            detail: "需要另行设计明确的 Child、Parent 与确认步骤",
          }
        : {
            status: "unsupported",
            label: "未发现指定接口",
            detail: "Topology Optimization 仅提供全局自动 Node Steering",
          },
      individualRestart: individualRestartAvailable
        ? {
            status: "available",
            label: "固件报告支持",
            detail: "执行前仍需确认影响范围",
          }
        : {
            status: "unverified",
            label: "不开放 · 范围未证实",
            detail: "官方动作会重启整个 Mesh；未执行任何破坏性探测",
          },
      localManagement: node?.managementUrl
        ? {
            status: "available",
            label: "CA Support 入口",
            detail: `${node.ipAddress} · 使用与 Main 同步的本地凭证`,
            url: node.managementUrl,
          }
        : {
            status: "unavailable",
            label: "当前不可用",
            detail: "Node 没有在线管理地址",
            url: null,
          },
    };
  }

  return {
    clientNodeId,
    clientsForNode,
    nodeForClient,
    nodeCapabilityReport,
  };
});
