const test = require("node:test");
const assert = require("node:assert/strict");
const { view } = require("../mesh_web/connection-mode.js");

test("desktop mode accepts a router address and password", () => {
  const mode = view({ managedConnection: false });
  assert.equal(mode.managed, false);
  assert.equal(mode.settingsLabel, "Connection settings");
  assert.equal(mode.submitLabel, "Connect and load network");
});

test("ESPHome mode explains that connection values are firmware-managed", () => {
  const mode = view({ managedConnection: true });
  assert.equal(mode.managed, true);
  assert.equal(mode.settingsLabel, "Device configuration");
  assert.equal(mode.submitLabel, "Save & reload network");
  assert.match(mode.description, /credentials stay in ESPHome firmware/);
  assert.match(mode.note, /survives restarts/);
});
