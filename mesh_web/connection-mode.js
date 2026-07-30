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
          settingsLabel: "设备配置",
          title: "ESP32 设备配置",
          description: "路由器地址与登录凭证由 ESPHome 固件管理，网页不会接收或修改它们。",
          note: "如需修改，请编辑本地 ESPHome YAML 后通过 USB 或 OTA 重新安装。",
          submitLabel: "重新读取网络",
        }
      : {
          managed,
          settingsLabel: "连接设置",
          title: "连接 Linksys Mesh",
          description: "输入主 Linksys 路由器的局域网地址和本地管理密码。",
          note: "密码仅保存在本地 Python 进程内存中；服务停止后清除。",
          submitLabel: "连接并读取网络",
        };
  }

  return { view };
});
