(function (root, factory) {
  const engine = factory();
  if (typeof module === "object" && module.exports) module.exports = engine;
  else root.MeshRefreshState = engine;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const DEFAULT_INTERVAL = 30;
  const SUPPORTED_INTERVALS = [0, 10, 30, 60];

  function normalizeInterval(value) {
    const parsed = Number(value);
    return SUPPORTED_INTERVALS.includes(parsed) ? parsed : DEFAULT_INTERVAL;
  }

  function shouldStartRefresh({ hasTopology, refreshing }) {
    return Boolean(hasTopology && !refreshing);
  }

  function visibilityAction({
    hasTopology,
    refreshing,
    visible,
    refreshDue,
    nextRefreshAt,
    now,
  }) {
    if (!hasTopology || refreshing || !visible) return "none";
    if (refreshDue || (nextRefreshAt !== null && nextRefreshAt <= now)) return "refresh";
    return "keep";
  }

  function view({
    hasTopology,
    refreshing,
    interval,
    nextRefreshAt,
    now,
    visible,
    refreshDue,
    error,
  }) {
    if (!hasTopology) return { mode: "waiting", text: "等待连接" };
    if (refreshing) return { mode: "refreshing", text: "正在刷新" };
    if (!interval) return { mode: "paused", text: "自动刷新已暂停" };
    if (!visible && (refreshDue || (nextRefreshAt !== null && nextRefreshAt <= now))) {
      return { mode: "stale", text: "后台暂停 · 返回即刷新" };
    }
    const seconds = nextRefreshAt === null
      ? interval
      : Math.max(0, Math.ceil((nextRefreshAt - now) / 1000));
    if (error) return { mode: "error", text: `刷新失败 · ${seconds}s 后重试` };
    return { mode: "live", text: `本地实时 · ${seconds}s` };
  }

  return {
    DEFAULT_INTERVAL,
    SUPPORTED_INTERVALS,
    normalizeInterval,
    shouldStartRefresh,
    visibilityAction,
    view,
  };
});
