const test = require("node:test");
const assert = require("node:assert/strict");
const { view } = require("../mesh_web/connection-mode.js");

test("desktop mode accepts a router address and password", () => {
  const mode = view({ managedConnection: false });
  assert.equal(mode.managed, false);
  assert.equal(mode.settingsLabel, "连接设置");
  assert.equal(mode.submitLabel, "连接并读取网络");
});

test("ESPHome mode explains that connection values are firmware-managed", () => {
  const mode = view({ managedConnection: true });
  assert.equal(mode.managed, true);
  assert.equal(mode.settingsLabel, "设备配置");
  assert.equal(mode.submitLabel, "重新读取网络");
  assert.match(mode.description, /ESPHome 固件管理/);
});
