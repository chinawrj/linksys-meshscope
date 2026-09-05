(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  root.MeshLinksysNormalize = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  function outputOf(raw, action) {
    const response = raw?.[action] || {};
    return response.result === "OK" && response.output && typeof response.output === "object"
      ? response.output
      : {};
  }

  function propertyMap(device) {
    const result = {};
    for (const prop of device?.properties || []) {
      if (prop && prop.name && prop.value !== null && prop.value !== undefined && prop.value !== "") {
        result[String(prop.name)] = String(prop.value);
      }
    }
    return result;
  }

  function allMacs(device) {
    const values = [];
    for (const value of device?.knownMACAddresses || []) {
      if (value) values.push(String(value).toUpperCase());
    }
    for (const item of [...(device?.knownInterfaces || []), ...(device?.connections || [])]) {
      if (item?.macAddress) values.push(String(item.macAddress).toUpperCase());
    }
    return [...new Set(values)];
  }

  function friendlyName(device) {
    const props = propertyMap(device);
    const model = device?.model || {};
    return String(
      props.userDeviceName ||
        device?.friendlyName ||
        model.modelNumber ||
        "Unnamed device",
    );
  }

  function signalQuality(rssi) {
    if (rssi === null || rssi === undefined || rssi === "") {
      return { label: "Unknown", score: null, tone: "muted" };
    }
    const value = Number(rssi);
    const score = Math.max(0, Math.min(100, Math.round(2 * (value + 100))));
    if (value >= -55) return { label: "Excellent", score, tone: "good" };
    if (value >= -67) return { label: "Good", score, tone: "good" };
    if (value >= -75) return { label: "Fair", score, tone: "warn" };
    return { label: "Weak", score, tone: "bad" };
  }

  function isWiredConnection(value) {
    const normalized = String(value || "").trim().toLowerCase();
    return normalized === "wired" || normalized === "ethernet";
  }

  function deviceType(device) {
    const model = device?.model || {};
    const values = [
      model.deviceType,
      model.modelNumber,
      model.manufacturer,
      device?.friendlyName,
      device?.unit?.operatingSystem,
    ];
    const haystack = values.map((value) => String(value || "")).join(" ").toLowerCase();
    if (["iphone", "phone", "android", "mobile"].some((word) => haystack.includes(word))) {
      return "phone";
    }
    if (["ipad", "tablet"].some((word) => haystack.includes(word))) return "tablet";
    if (["macbook", "laptop", "computer", "desktop", "windows"].some((word) => haystack.includes(word))) {
      return "computer";
    }
    if (["camera", "cam"].some((word) => haystack.includes(word))) return "camera";
    if (["tv", "player", "chromecast"].some((word) => haystack.includes(word))) return "media";
    if (haystack.includes("watch")) return "wearable";
    if (["iot", "midea", "cooker", "lwip", "esp"].some((word) => haystack.includes(word))) {
      return "iot";
    }
    return "device";
  }

  function normalize(host, raw, edgeMeta = {}) {
    const deviceInfo = outputOf(raw, "core/GetDeviceInfo");
    const deviceOutput = outputOf(raw, "devicelist/GetDevices3");
    const devices = (deviceOutput.devices || []).filter((item) => item && typeof item === "object");
    const backhaulResponse = raw?.["nodes/diagnostics/GetBackhaulInfo"] || {};
    const topologyDegraded = edgeMeta.topologyDegraded === true || backhaulResponse.result !== "OK";
    const backhaul = (outputOf(raw, "nodes/diagnostics/GetBackhaulInfo").backhaulDevices || [])
      .filter((item) => item && typeof item === "object");
    const mainConnections = (
      outputOf(raw, "networkconnections/GetNetworkConnections2").connections || []
    ).filter((item) => item && typeof item === "object");
    const nodeWireless = (
      outputOf(raw, "nodes/networkconnections/GetNodesWirelessNetworkConnections")
        .nodeWirelessConnections || []
    ).filter((item) => item && typeof item === "object");
    const wan = outputOf(raw, "router/GetWANStatus3");
    const lan = outputOf(raw, "router/GetLANSettings");
    const radioInfo = outputOf(raw, "wirelessap/GetRadioInfo3");
    const optimization = outputOf(
      raw,
      "nodes/topologyoptimization/GetTopologyOptimizationSettings2",
    );
    const backhaulPhyById = new Map(
      (Array.isArray(edgeMeta.backhaulPhyLinks) ? edgeMeta.backhaulPhyLinks : [])
        .filter((item) => item && item.nodeId)
        .map((item) => [String(item.nodeId).toUpperCase(), item]),
    );
    const lockedParentById = new Map(
      edgeMeta.topologyLock?.enabled === true && Array.isArray(edgeMeta.topologyLock.nodes)
        ? edgeMeta.topologyLock.nodes
            .filter((item) => item?.nodeId && item?.expectedParentId)
            .map((item) => [String(item.nodeId).toUpperCase(), String(item.expectedParentId)])
        : [],
    );

    const backhaulById = new Map(backhaul.map((item) => [item.deviceUUID, item]));
    const nodeIds = new Set(
      devices
        .filter((item) => item.nodeType || item.isAuthority || backhaulById.has(item.deviceID))
        .map((item) => item.deviceID)
        .filter(Boolean),
    );
    const masterDevice = devices.find((item) => item.isAuthority);
    const masterId = masterDevice?.deviceID || null;

    const liveByMac = new Map();
    for (const connection of mainConnections) {
      const mac = String(connection.macAddress || "").toUpperCase();
      if (mac) liveByMac.set(mac, { ...connection, parentDeviceID: masterId });
    }
    for (const group of nodeWireless) {
      for (const connection of group.connections || []) {
        const mac = String(connection?.macAddress || "").toUpperCase();
        if (mac) liveByMac.set(mac, { ...connection, parentDeviceID: group.deviceID });
      }
    }

    const devicesById = new Map(devices.map((item) => [item.deviceID, item]));
    const ipToNode = new Map();
    for (const deviceId of nodeIds) {
      const device = devicesById.get(deviceId) || {};
      for (const connection of device.connections || []) {
        if (connection?.ipAddress) ipToNode.set(String(connection.ipAddress), String(deviceId));
      }
    }
    for (const item of backhaul) {
      if (item.ipAddress && item.deviceUUID) {
        ipToNode.set(String(item.ipAddress), String(item.deviceUUID));
      }
    }

    const nodes = [];
    for (const deviceId of nodeIds) {
      const device = devicesById.get(deviceId) || {};
      const props = propertyMap(device);
      const model = device.model || {};
      const unit = device.unit || {};
      const deviceConnections = device.connections || [];
      const backhaulItem = backhaulById.get(deviceId) || {};
      const connectionWithIp = deviceConnections.find((item) => item?.ipAddress);
      const ipAddress = String(connectionWithIp?.ipAddress || backhaulItem.ipAddress || "");
      const reportedParentIp = backhaulItem.parentIPAddress;
      const connectionType = backhaulItem.connectionType || (device.isAuthority ? "Gateway" : null);
      const isWired = !device.isAuthority && isWiredConnection(connectionType);
      const reportedParentId = reportedParentIp
        ? ipToNode.get(String(reportedParentIp)) || null
        : null;
      // Wired parentIPAddress is produced from the firmware's LLDP helper, but
      // on switched LANs it can select a merely root-accessible peer (or remain
      // stale). A saved Topology Lock mapping therefore doubles as the user's
      // explicit wired-layout assignment. It is display-only for Ethernet and
      // never triggers MQTT wireless steering.
      const lockedParentId = (isWired || topologyDegraded)
        ? lockedParentById.get(String(deviceId).toUpperCase()) || null
        : null;
      const parentId = lockedParentId || reportedParentId;
      const resolvedParent = parentId ? devicesById.get(parentId) : null;
      const parentIp = resolvedParent?.connections?.find((item) => item?.ipAddress)?.ipAddress
        || (lockedParentId ? null : reportedParentIp);
      const wireless = backhaulItem.wirelessConnectionInfo || {};
      const backhaulPhy = backhaulPhyById.get(String(deviceId).toUpperCase()) || {};
      let rssi = wireless.stationRSSI;
      if (rssi === null || rssi === undefined || Number(rssi) === 0) rssi = wireless.apRSSI;
      const connectionWithMac = deviceConnections.find((item) => item?.macAddress);
      nodes.push({
        id: deviceId,
        name: friendlyName(device),
        location: props.userDeviceLocation || friendlyName(device),
        role: device.isAuthority ? "Primary node" : "Child node",
        isAuthority: Boolean(device.isAuthority),
        online: Boolean(
          device.isAuthority ||
          backhaulById.has(deviceId) ||
          (topologyDegraded && deviceConnections.length),
        ),
        model: model.modelNumber || "Linksys Velop",
        description: model.description || "",
        hardwareVersion: model.hardwareVersion ?? null,
        firmwareVersion: unit.firmwareVersion ?? null,
        firmwareDate: unit.firmwareDate ?? null,
        serialNumber: unit.serialNumber ?? null,
        macAddress: connectionWithMac?.macAddress ?? null,
        ipAddress: ipAddress || null,
        parentId,
        parentIpAddress: parentIp ?? null,
        reportedParentIpAddress: reportedParentIp ?? null,
        reportedParentId,
        parentSource: lockedParentId
          ? topologyDegraded
            ? "topology-lock-degraded-assignment"
            : "topology-lock-wired-assignment"
          : topologyDegraded
            ? "linksys-backhaul-unavailable"
          : isWired
            ? "linksys-lldp-reported"
            : "linksys-parent-ip",
        parentConfidence: lockedParentId
          ? topologyDegraded
            ? "desired-unverified"
            : "manual"
          : topologyDegraded
            ? "unavailable"
          : isWired
            ? "firmware-reported-unverified"
            : "firmware-reported",
        connectionType,
        isWired,
        linkType: isWired ? "Ethernet" : wireless.radioID ?? connectionType ?? null,
        band: wireless.radioID ?? null,
        channel: wireless.channel ?? null,
        rssi: rssi ?? null,
        quality: isWired
          ? { label: "Wired", score: 100, tone: "wired" }
          : signalQuality(rssi),
        speedMbps: backhaulItem.speedMbps ? Number(backhaulItem.speedMbps) : null,
        phyRateMbps: Number.isFinite(Number(backhaulPhy.rateMbps))
          ? Number(backhaulPhy.rateMbps)
          : null,
        phyRateRaw: backhaulPhy.rawRate ?? null,
        phyRateObservedAt: backhaulPhy.observedAt ?? null,
        phyRateAgeSeconds: Number.isFinite(Number(backhaulPhy.ageSeconds))
          ? Number(backhaulPhy.ageSeconds)
          : null,
        phyRateStale: backhaulPhy.stale === true,
        timestamp: backhaulItem.timestamp ?? null,
        clientCount: 0,
        managementUrl: ipAddress ? `https://${ipAddress}/ca` : null,
        managementEntry: ipAddress ? "ca-support" : null,
      });
    }

    const clients = [];
    for (const device of devices) {
      if (nodeIds.has(device.deviceID)) continue;
      const deviceConnections = (device.connections || []).filter(
        (item) => item && typeof item === "object",
      );
      const deviceMacs = allMacs(device);
      const live = deviceMacs.map((mac) => liveByMac.get(mac)).find(Boolean) || {};
      const primary = { ...(deviceConnections[0] || {}), ...live };
      const wireless = primary.wireless || {};
      const parentId = primary.parentDeviceID || masterId;
      const props = propertyMap(device);
      const model = device.model || {};
      const unit = device.unit || {};
      clients.push({
        id: device.deviceID,
        name: friendlyName(device),
        online: Boolean(deviceConnections.length || Object.keys(live).length),
        type: deviceType(device),
        model: model.modelNumber ?? null,
        manufacturer: model.manufacturer ?? null,
        operatingSystem: unit.operatingSystem ?? null,
        macAddress: primary.macAddress || deviceMacs[0] || null,
        ipAddress: primary.ipAddress ?? null,
        parentId,
        nodeId: parentId,
        band: wireless.band ?? null,
        radioId: wireless.radioID ?? null,
        rssi: wireless.signalDecibels ?? null,
        quality: signalQuality(wireless.signalDecibels),
        speedMbps: primary.negotiatedMbps ?? null,
        isGuest: Boolean(wireless.isGuest),
        lastSeen: primary.timestamp ?? null,
        userLabel: props.userDeviceName ?? null,
      });
    }

    const onlineCounts = new Map();
    for (const client of clients) {
      if (client.online && client.nodeId) {
        onlineCounts.set(client.nodeId, (onlineCounts.get(client.nodeId) || 0) + 1);
      }
    }
    for (const node of nodes) node.clientCount = onlineCounts.get(node.id) || 0;

    nodes.sort((a, b) => {
      if (a.isAuthority !== b.isAuthority) return a.isAuthority ? -1 : 1;
      if (a.online !== b.online) return a.online ? -1 : 1;
      return a.name.localeCompare(b.name);
    });
    clients.sort((a, b) => {
      if (a.online !== b.online) return a.online ? -1 : 1;
      return a.name.localeCompare(b.name);
    });
    const nodeNameById = new Map(nodes.map((item) => [item.id, item.name]));
    for (const node of nodes) node.parentName = nodeNameById.get(node.parentId) || null;
    for (const client of clients) client.nodeName = nodeNameById.get(client.nodeId) || "Main";

    const onlineNodes = nodes.filter((item) => item.online);
    const onlineClients = clients.filter((item) => item.online);
    const weakNodes = onlineNodes.filter(
      (item) => !item.isAuthority && ["warn", "bad"].includes(item.quality?.tone),
    );
    const backhaulMbps = onlineNodes
      .filter((item) => !item.isAuthority)
      .reduce((sum, item) => sum + Number(item.speedMbps || 0), 0);

    return {
      meta: {
        source: "Linksys JNAP · ESP32 MeshScope",
        router: host,
        updatedAt: edgeMeta.updatedAt || new Date().toISOString(),
        generation: edgeMeta.generation ?? null,
        revision: deviceOutput.revision,
        edgeHosted: true,
        edgeAddress: edgeMeta.edgeAddress || null,
        routerConnected: edgeMeta.routerConnected !== false,
        topologyDegraded,
        clientDetails: edgeMeta.clientDetails || "full",
        topologyLock: edgeMeta.topologyLock || null,
        backhaulPhyLinks: edgeMeta.backhaulPhyLinks || [],
      },
      network: {
        manufacturer: deviceInfo.manufacturer || "Linksys",
        model: deviceInfo.modelNumber ?? null,
        description: deviceInfo.description ?? null,
        firmwareVersion: deviceInfo.firmwareVersion ?? null,
        firmwareDate: deviceInfo.firmwareDate ?? null,
        serialNumber: deviceInfo.serialNumber ?? null,
        wanStatus: wan.wanStatus ?? null,
        wanType: wan.wanConnection?.wanType ?? null,
        wanIpAddress: wan.wanConnection?.ipAddress ?? null,
        lanIpAddress: lan.ipAddress || host,
        hostName: lan.hostName ?? null,
        radioCount: (radioInfo.radios || []).length,
        clientSteeringEnabled: optimization.isClientSteeringEnabled ?? null,
        nodeSteeringEnabled: optimization.isNodeSteeringEnabled ?? null,
        nodeSteeringMode: "automatic",
        manualParentSelectionAvailable: false,
        manualParentSelectionEvidence: "firmware-internal-confirmed",
        manualParentSelectionTransport: "not-available",
        documentedRestartScope: "single-node",
        individualNodeRestartAvailable: true,
        individualNodeRestartProbe: "firmware-confirmed",
        individualNodeRestartEvidence: "reset_slave_nodes-direct-jnap",
      },
      summary: {
        nodesOnline: onlineNodes.length,
        nodesTotal: nodes.length,
        clientsOnline: onlineClients.length,
        clientsKnown: clients.length,
        weakNodes: weakNodes.length,
        backhaulMbps: Math.round(backhaulMbps * 10) / 10,
      },
      nodes,
      clients,
    };
  }

  function normalizeEnvelope(payload) {
    if (!payload?.rawJnap) return payload;
    return normalize(
      payload.router || payload.meta?.router || "192.168.1.1",
      payload.rawJnap,
      payload.meta || {},
    );
  }

  return {
    allMacs,
    deviceType,
    friendlyName,
    normalize,
    normalizeEnvelope,
    outputOf,
    propertyMap,
    signalQuality,
    isWiredConnection,
  };
});
