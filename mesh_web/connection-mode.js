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
          description: "The router address and credentials are managed by the ESPHome firmware. This page cannot receive or change them.",
          note: "To make changes, edit your local ESPHome YAML and reinstall over USB or OTA.",
          submitLabel: "Reload network",
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
