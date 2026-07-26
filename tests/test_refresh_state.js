const assert = require("node:assert/strict");
const test = require("node:test");
const {
  normalizeInterval,
  shouldStartRefresh,
  visibilityAction,
  view,
} = require("../mesh_web/refresh-state.js");

test("normalizes stored refresh preferences to supported values", () => {
  assert.equal(normalizeInterval("10"), 10);
  assert.equal(normalizeInterval(0), 0);
  assert.equal(normalizeInterval("15"), 30);
  assert.equal(normalizeInterval("garbage"), 30);
});

test("exposes waiting, refreshing, paused, countdown, and error states", () => {
  const base = {
    hasTopology: true,
    refreshing: false,
    interval: 30,
    nextRefreshAt: 35_000,
    now: 10_500,
    visible: true,
    refreshDue: false,
    error: null,
  };
  assert.deepEqual(view({ ...base, hasTopology: false }), {
    mode: "waiting",
    text: "等待连接",
  });
  assert.deepEqual(view({ ...base, refreshing: true }), {
    mode: "refreshing",
    text: "正在刷新",
  });
  assert.deepEqual(view({ ...base, interval: 0 }), {
    mode: "paused",
    text: "自动刷新已暂停",
  });
  assert.deepEqual(view(base), {
    mode: "live",
    text: "本地实时 · 25s",
  });
  assert.deepEqual(view({ ...base, error: "timeout" }), {
    mode: "error",
    text: "刷新失败 · 25s 后重试",
  });
});

test("marks an expired hidden page stale and refreshes immediately on return", () => {
  const timing = {
    hasTopology: true,
    refreshing: false,
    refreshDue: true,
    nextRefreshAt: 20_000,
    now: 25_000,
  };
  assert.deepEqual(
    view({
      ...timing,
      interval: 30,
      visible: false,
      error: null,
    }),
    { mode: "stale", text: "后台暂停 · 返回即刷新" },
  );
  assert.equal(visibilityAction({ ...timing, visible: true }), "refresh");
  assert.equal(
    visibilityAction({
      ...timing,
      refreshDue: false,
      nextRefreshAt: 30_000,
      visible: true,
    }),
    "keep",
  );
});

test("prevents refresh overlap and refresh without a topology", () => {
  assert.equal(shouldStartRefresh({ hasTopology: true, refreshing: false }), true);
  assert.equal(shouldStartRefresh({ hasTopology: true, refreshing: true }), false);
  assert.equal(shouldStartRefresh({ hasTopology: false, refreshing: false }), false);
});
