(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  root.MeshConnectionMode = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  function view(status = {}) {
    const managed = status.managedConnection === true;
    return managed
      ? {
          managed,
          settingsLabel: "Device configuration",
          title: "ESP32 device configuration",
          description: "Router credentials stay in ESPHome firmware. Runtime behavior can be configured here.",
          note: "The action rate limit is saved on the ESP32 and survives restarts. Credentials are never returned to this page.",
          submitLabel: "Save & reload network",
        }
      : {
          managed,
          settingsLabel: "Connection settings",
          title: "Connect to Linksys Mesh",
          description: "Enter the LAN address and local admin password for your primary Linksys router.",
          note: "The password stays in this local Python process and is cleared when the service stops.",
          submitLabel: "Connect and load network",
        };
  }

  return { view };
});
