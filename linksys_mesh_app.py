#!/usr/bin/env python3
"""Local Linksys Velop mesh dashboard.

The server keeps the router password in memory, binds to localhost by default,
and keeps observation actions read-only. Restart is restricted to a known
online Node selected from the live topology.
"""

from __future__ import annotations

import argparse
import base64
import concurrent.futures
import copy
import ipaddress
import json
import mimetypes
import os
import socket
import ssl
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse


APP_ROOT = Path(__file__).resolve().parent
WEB_ROOT = APP_ROOT / "mesh_web"
DEFAULT_ROUTER = "192.168.1.1"
JNAP_PREFIX = "http://linksys.com/jnap/"
NODE_RESTART_COOLDOWN_SECONDS = 90
READ_ONLY_ACTIONS = {
    "core/CheckAdminPassword": {},
    "core/GetDeviceInfo": {},
    "devicelist/GetDevices3": {},
    "networkconnections/GetNetworkConnections2": {},
    "nodes/diagnostics/GetBackhaulInfo": {},
    "nodes/networkconnections/GetNodesWirelessNetworkConnections": {},
    "nodes/smartmode/GetDeviceMode": {},
    "nodes/topologyoptimization/GetTopologyOptimizationSettings2": {},
    "router/GetWANStatus3": {},
    "router/GetLANSettings": {},
    "wirelessap/GetRadioInfo3": {},
}
MUTATING_ACTIONS = {
    "core/Reboot": {},
}


class RouterError(RuntimeError):
    """A user-safe router communication failure."""


@dataclass
class RouterSession:
    host: str = DEFAULT_ROUTER
    password: str | None = None
    connected_at: str | None = None

    @property
    def connected(self) -> bool:
        return bool(self.password)

    @property
    def base_url(self) -> str:
        return f"https://{self.host}"

    def clear(self) -> None:
        self.password = None
        self.connected_at = None


class MeshState:
    def __init__(self) -> None:
        self.session = RouterSession()
        self.demo = False
        self.cache: dict[str, Any] | None = None
        self.cache_at = 0.0
        self.node_probe_cache: dict[str, tuple[float, dict[str, Any]]] = {}
        self.node_restart_cooldowns: dict[str, float] = {}
        self.lock = threading.RLock()

    def status(self) -> dict[str, Any]:
        with self.lock:
            return {
                "connected": self.demo or self.session.connected,
                "demo": self.demo,
                "router": self.session.host,
                "connectedAt": self.session.connected_at,
                "cachedAt": self.cache.get("meta", {}).get("updatedAt") if self.cache else None,
            }

    def enable_demo(self, host: str = DEFAULT_ROUTER) -> dict[str, Any]:
        """Enable a synthetic, mutation-free topology for offline UI review."""
        with self.lock:
            self.demo = True
            self.session = RouterSession(host=host, connected_at=now_iso())
            self.cache = build_demo_topology(host)
            self.cache_at = time.monotonic()
            self.node_probe_cache.clear()
            self.node_restart_cooldowns.clear()
            return copy.deepcopy(self.cache)

    def connect(self, host: str, password: str) -> dict[str, Any]:
        if self.demo:
            raise RouterError("Demo mode does not connect to or authenticate with a real router.")
        clean_host = validate_router_host(host)
        if not password:
            raise RouterError("Enter the local router password.")
        candidate = RouterSession(host=clean_host, password=password)
        response = jnap_call(candidate, "core/CheckAdminPassword")
        if response.get("result") != "OK":
            if response.get("result") == "_ErrorUnauthorized":
                raise RouterError("The password is incorrect. Try again.")
            raise RouterError(
                f"The router rejected authentication: {response.get('result', 'unknown error')}"
            )
        with self.lock:
            self.session = candidate
            self.session.connected_at = now_iso()
            self.cache = None
            self.cache_at = 0.0
            self.node_probe_cache.clear()
            self.node_restart_cooldowns.clear()
        return self.refresh(force=True)

    def disconnect(self) -> None:
        with self.lock:
            if self.demo:
                return
            self.session.clear()
            self.cache = None
            self.cache_at = 0.0
            self.node_probe_cache.clear()
            self.node_restart_cooldowns.clear()

    def refresh(self, force: bool = False) -> dict[str, Any]:
        with self.lock:
            if self.demo:
                topology = copy.deepcopy(self.cache or build_demo_topology(self.session.host))
                topology["meta"]["updatedAt"] = now_iso()
                self.cache = topology
                self.cache_at = time.monotonic()
                return copy.deepcopy(topology)
            if not self.session.connected:
                raise RouterError("No router is connected.")
            if not force and self.cache and time.monotonic() - self.cache_at < 4:
                return self.cache
            session = RouterSession(
                host=self.session.host,
                password=self.session.password,
                connected_at=self.session.connected_at,
            )

        action_names = [name for name in READ_ONLY_ACTIONS if name != "core/CheckAdminPassword"]
        raw: dict[str, dict[str, Any]] = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(action_names)) as pool:
            futures = {pool.submit(jnap_call, session, name): name for name in action_names}
            for future in concurrent.futures.as_completed(futures):
                name = futures[future]
                try:
                    response = future.result()
                except Exception as exc:
                    raw[name] = {"result": "_LocalError", "error": str(exc)}
                else:
                    raw[name] = response

        device_response = raw.get("devicelist/GetDevices3", {})
        if device_response.get("result") == "_ErrorUnauthorized":
            with self.lock:
                self.session.clear()
            raise RouterError("The router session has expired. Reconnect to continue.")
        if device_response.get("result") != "OK":
            raise RouterError(
                "Unable to read the device list: "
                + str(
                    device_response.get("error")
                    or device_response.get("result")
                    or "unknown error"
                )
            )

        normalized = normalize_topology(session.host, raw)
        with self.lock:
            self.cache = normalized
            self.cache_at = time.monotonic()
        return normalized

    def probe_node(self, node_id: str) -> dict[str, Any]:
        """Probe one known node through safe, read-only local JNAP actions."""
        clean_id = (node_id or "").strip()
        if not clean_id:
            raise RouterError("Node ID is required.")

        with self.lock:
            if self.demo:
                topology = self.cache or build_demo_topology(self.session.host)
                node = next(
                    (item for item in topology.get("nodes", []) if item.get("id") == clean_id),
                    None,
                )
                if not node:
                    raise RouterError("The selected node was not found.")
                return demo_node_probe(node)
            if not self.session.connected:
                raise RouterError("No router is connected.")
            cached_probe = self.node_probe_cache.get(clean_id)
            if cached_probe and time.monotonic() - cached_probe[0] < 30:
                return cached_probe[1]
            topology = self.cache
            password = self.session.password

        if not topology:
            topology = self.refresh()
        node = next((item for item in topology.get("nodes", []) if item.get("id") == clean_id), None)
        if not node:
            raise RouterError("The selected node was not found.")
        if not node.get("online"):
            raise RouterError("The selected node is offline and cannot be probed.")
        node_host = validate_router_host(str(node.get("ipAddress") or ""))
        session = RouterSession(host=node_host, password=password)
        actions = (
            "core/CheckAdminPassword",
            "core/GetDeviceInfo",
            "nodes/smartmode/GetDeviceMode",
            "nodes/topologyoptimization/GetTopologyOptimizationSettings2",
        )
        raw: dict[str, dict[str, Any]] = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(actions)) as pool:
            futures = {pool.submit(jnap_call, session, action): action for action in actions}
            for future in concurrent.futures.as_completed(futures):
                action = futures[future]
                try:
                    raw[action] = future.result()
                except Exception as exc:
                    raw[action] = {"result": "_LocalError", "error": str(exc)}

        password_check = raw["core/CheckAdminPassword"]
        if password_check.get("result") == "_ErrorUnauthorized":
            raise RouterError(
                "The selected node did not accept the credentials synchronized from Main."
            )
        identity = output_of(raw, "core/GetDeviceInfo")
        mode = output_of(raw, "nodes/smartmode/GetDeviceMode")
        optimization = output_of(
            raw, "nodes/topologyoptimization/GetTopologyOptimizationSettings2"
        )
        services = [str(item) for item in identity.get("services") or []]
        report = {
            "nodeId": clean_id,
            "name": node.get("name"),
            "ipAddress": node_host,
            "managementUrl": f"https://{node_host}/ca",
            "managementEntry": "ca-support",
            "credentialsSynchronized": password_check.get("result") == "OK",
            "deviceMode": mode.get("mode"),
            "identity": {
                "model": identity.get("modelNumber"),
                "hardwareVersion": identity.get("hardwareVersion"),
                "firmwareVersion": identity.get("firmwareVersion"),
                "serialNumber": identity.get("serialNumber"),
            },
            "services": {
                "coreReboot": any(value.startswith(JNAP_PREFIX + "core/Core") for value in services),
                "nodesSetup3": JNAP_PREFIX + "nodes/setup/Setup3" in services,
                "topologyOptimization2": (
                    JNAP_PREFIX + "nodes/topologyoptimization/TopologyOptimization2" in services
                ),
            },
            "topologyOptimization": {
                "clientSteeringEnabled": optimization.get("isClientSteeringEnabled"),
                "nodeSteeringEnabled": optimization.get("isNodeSteeringEnabled"),
            },
            "individualRestart": {
                "visibleInCaSupportUi": JNAP_PREFIX + "nodes/setup/Setup3" in services,
                "action": "core/Reboot",
                "hasTargetDeviceId": False,
                "scope": "single-node",
                "executed": False,
            },
            "manualParentSelection": {
                "available": False,
                "transport": "not-available",
                "firmwareInternalPathDiscovered": True,
                "reason": (
                    "MX4200 and WHW03 firmware contain the exact internal Parent "
                    "steering data path, but ordinary admin JNAP exposes no safe transport."
                ),
            },
            "observedAt": now_iso(),
        }
        with self.lock:
            self.node_probe_cache[clean_id] = (time.monotonic(), report)
        return report

    def restart_node(self, node_id: str) -> dict[str, Any]:
        """Restart one selected online Node through its local CA JNAP endpoint."""
        clean_id = (node_id or "").strip()
        if not clean_id:
            raise RouterError("Node ID is required.")
        with self.lock:
            if self.demo:
                raise RouterError("Demo mode never sends restart requests to a node.")
            if not self.session.connected:
                raise RouterError("No router is connected.")
            topology = self.cache
            password = self.session.password
        if not topology:
            topology = self.refresh()
        node = next((item for item in topology.get("nodes", []) if item.get("id") == clean_id), None)
        if not node:
            raise RouterError("The selected node was not found.")
        if not node.get("online"):
            raise RouterError("The selected node is offline and cannot be restarted.")
        node_host = validate_router_host(str(node.get("ipAddress") or ""))
        with self.lock:
            now = time.monotonic()
            last_restart = self.node_restart_cooldowns.get(clean_id)
            if (
                last_restart is not None
                and now - last_restart < NODE_RESTART_COOLDOWN_SECONDS
            ):
                raise RouterError(
                    "The selected node is already restarting. Wait for it to come back online."
                )
            self.node_restart_cooldowns[clean_id] = now
        try:
            response = jnap_mutating_call(
                RouterSession(host=node_host, password=password),
                "core/Reboot",
            )
        except Exception:
            with self.lock:
                self.node_restart_cooldowns.pop(clean_id, None)
            raise
        if response.get("result") != "OK":
            with self.lock:
                self.node_restart_cooldowns.pop(clean_id, None)
            raise RouterError(
                "The node did not accept the restart request: "
                + str(response.get("error") or response.get("result") or "unknown error")
            )
        with self.lock:
            self.node_probe_cache.clear()
        return {
            "accepted": True,
            "requestedThroughNode": {
                "id": clean_id,
                "name": node.get("name"),
                "ipAddress": node_host,
            },
            "action": "core/Reboot",
            "scope": "single-node",
            "message": "The restart request was sent only to the selected node's local endpoint.",
            "requestedAt": now_iso(),
        }


STATE = MeshState()


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def validate_router_host(value: str) -> str:
    value = (value or "").strip()
    if "://" in value:
        value = urlparse(value).hostname or ""
    value = value.strip("[]").rstrip(".")
    if not value:
        raise RouterError("Enter a router address.")
    if value.lower().endswith(".local"):
        return value
    try:
        addresses = socket.getaddrinfo(value, 443, type=socket.SOCK_STREAM)
    except socket.gaierror as exc:
        raise RouterError("The router address could not be resolved.") from exc
    for address in {item[4][0] for item in addresses}:
        ip = ipaddress.ip_address(address)
        if not (ip.is_private or ip.is_link_local or ip.is_loopback):
            raise RouterError("For safety, MeshScope only connects to private LAN addresses.")
    return value


def jnap_call(session: RouterSession, action: str) -> dict[str, Any]:
    if action not in READ_ONLY_ACTIONS:
        raise RouterError("A non-read-only router action was blocked.")
    return _jnap_request(session, action, READ_ONLY_ACTIONS[action])


def jnap_mutating_call(session: RouterSession, action: str) -> dict[str, Any]:
    if action not in MUTATING_ACTIONS:
        raise RouterError("An unapproved router action was blocked.")
    return _jnap_request(session, action, MUTATING_ACTIONS[action])


def _jnap_request(
    session: RouterSession, action: str, data: dict[str, Any]
) -> dict[str, Any]:
    if not session.password:
        raise RouterError("No router password is available.")
    auth = base64.b64encode(f"admin:{session.password}".encode()).decode()
    body = json.dumps(data, separators=(",", ":")).encode()
    request = urllib.request.Request(
        f"{session.base_url}/JNAP/",
        data=body,
        method="POST",
        headers={
            "Content-Type": "application/json; charset=UTF-8",
            "X-JNAP-Action": JNAP_PREFIX + action,
            "X-JNAP-Authorization": "Basic " + auth,
            "Cache-Control": "no-cache",
        },
    )
    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    try:
        with urllib.request.urlopen(request, timeout=12, context=context) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        raise RouterError(f"The router returned HTTP {exc.code}.") from exc
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        raise RouterError(f"Unable to connect to {session.host}: {exc}") from exc


def output_of(raw: dict[str, dict[str, Any]], action: str) -> dict[str, Any]:
    response = raw.get(action) or {}
    output = response.get("output")
    return output if response.get("result") == "OK" and isinstance(output, dict) else {}


def property_map(device: dict[str, Any]) -> dict[str, str]:
    result: dict[str, str] = {}
    for prop in device.get("properties") or []:
        if isinstance(prop, dict) and prop.get("name") and prop.get("value") not in (None, ""):
            result[str(prop["name"])] = str(prop["value"])
    return result


def all_macs(device: dict[str, Any]) -> list[str]:
    values: list[str] = []
    for value in device.get("knownMACAddresses") or []:
        if value:
            values.append(str(value).upper())
    for interface in device.get("knownInterfaces") or []:
        if isinstance(interface, dict) and interface.get("macAddress"):
            values.append(str(interface["macAddress"]).upper())
    for connection in device.get("connections") or []:
        if isinstance(connection, dict) and connection.get("macAddress"):
            values.append(str(connection["macAddress"]).upper())
    return list(dict.fromkeys(values))


def friendly_name(device: dict[str, Any]) -> str:
    props = property_map(device)
    model = device.get("model") or {}
    return str(
        props.get("userDeviceName")
        or device.get("friendlyName")
        or model.get("modelNumber")
        or "Unnamed device"
    )


def signal_quality(rssi: int | float | None) -> dict[str, Any]:
    if rssi is None:
        return {"label": "Unknown", "score": None, "tone": "muted"}
    value = float(rssi)
    score = max(0, min(100, round(2 * (value + 100))))
    if value >= -55:
        return {"label": "Excellent", "score": score, "tone": "good"}
    if value >= -67:
        return {"label": "Good", "score": score, "tone": "good"}
    if value >= -75:
        return {"label": "Fair", "score": score, "tone": "warn"}
    return {"label": "Weak", "score": score, "tone": "bad"}


def build_demo_topology(host: str = DEFAULT_ROUTER) -> dict[str, Any]:
    """Return a full synthetic topology used only by the explicit --demo mode."""
    node_specs = [
        {
            "id": "demo-main",
            "name": "Main",
            "ipAddress": host,
            "model": "MX42",
            "isAuthority": True,
            "parentId": None,
            "band": None,
            "channel": None,
            "rssi": None,
            "speedMbps": None,
            "clientCount": 6,
        },
        {
            "id": "demo-big-tree",
            "name": "Atrium",
            "ipAddress": "192.168.1.10",
            "model": "WHW03",
            "isAuthority": False,
            "parentId": "demo-main",
            "band": "5GH",
            "channel": 149,
            "rssi": -62,
            "speedMbps": 119,
            "clientCount": 0,
        },
        {
            "id": "demo-door-corner",
            "name": "Office",
            "ipAddress": "192.168.1.11",
            "model": "MX5300",
            "isAuthority": False,
            "parentId": "demo-main",
            "band": "5GL",
            "channel": 44,
            "rssi": -57,
            "speedMbps": 444,
            "clientCount": 10,
        },
        {
            "id": "demo-yard-east",
            "name": "Patio",
            "ipAddress": "192.168.1.12",
            "model": "MX42",
            "isAuthority": False,
            "parentId": "demo-main",
            "band": "5GH",
            "channel": 149,
            "rssi": -65,
            "speedMbps": 241,
            "clientCount": 3,
        },
        {
            "id": "demo-parent-room",
            "name": "Bedroom",
            "ipAddress": "192.168.1.13",
            "model": "MX5300",
            "isAuthority": False,
            "parentId": "demo-door-corner",
            "band": "5GH",
            "channel": 149,
            "rssi": -54,
            "speedMbps": 308,
            "clientCount": 2,
        },
        {
            "id": "demo-road-south",
            "name": "Garage",
            "ipAddress": "192.168.1.14",
            "model": "WHW03",
            "isAuthority": False,
            "parentId": "demo-big-tree",
            "band": "5GL",
            "channel": 44,
            "rssi": -69,
            "speedMbps": 122,
            "clientCount": 1,
        },
    ]
    node_names = {item["id"]: item["name"] for item in node_specs}
    nodes: list[dict[str, Any]] = []
    for index, spec in enumerate(node_specs, start=1):
        quality = signal_quality(spec["rssi"])
        nodes.append(
            {
                **spec,
                "location": spec["name"],
                "role": "Primary node" if spec["isAuthority"] else "Child node",
                "online": True,
                "description": "Linksys Velop Mesh Node",
                "hardwareVersion": "2" if spec["model"] == "WHW03" else "1",
                "firmwareVersion": (
                    "2.1.20.216892" if spec["model"] == "WHW03" else "1.0.13.216903"
                ),
                "firmwareDate": "2026-07-01",
                "serialNumber": f"DEMO-NODE-{index:02d}",
                "macAddress": f"02:00:00:00:10:{index:02X}",
                "parentIpAddress": next(
                    (
                        parent["ipAddress"]
                        for parent in node_specs
                        if parent["id"] == spec["parentId"]
                    ),
                    None,
                ),
                "parentName": node_names.get(spec["parentId"]),
                "connectionType": "Gateway" if spec["isAuthority"] else "Wireless",
                "quality": quality,
                "timestamp": now_iso(),
                "managementUrl": (
                    f"https://{spec['ipAddress']}/ca" if spec["ipAddress"] else None
                ),
                "managementEntry": "ca-support",
            }
        )

    client_names = {
        "demo-main": [
            "Work-MacBook",
            "LivingRoom-TV",
            "Phone-01",
            "HomePod",
            "Gate-Camera",
            "ESP-Sensor",
        ],
        "demo-door-corner": [
            "Office-PC",
            "Tablet-01",
            "Kitchen-Speaker",
            "Doorbell",
            "Printer",
            "Phone-02",
            "Watch",
            "Camera-East",
            "Air-Purifier",
            "ESP-Meter",
        ],
        "demo-yard-east": ["Patio-Camera", "Irrigation", "Weather-Station"],
        "demo-parent-room": ["Parent-iPad", "Bedroom-TV"],
        "demo-road-south": ["Road-Camera"],
    }
    def demo_client_type(name: str) -> str:
        normalized = name.lower()
        if any(token in normalized for token in ("macbook", "pc", "printer")):
            return "computer"
        if any(token in normalized for token in ("ipad", "tablet")):
            return "tablet"
        if any(token in normalized for token in ("phone",)):
            return "phone"
        if any(token in normalized for token in ("watch",)):
            return "wearable"
        if any(token in normalized for token in ("tv", "homepod", "speaker")):
            return "media"
        if any(token in normalized for token in ("camera", "doorbell")):
            return "camera"
        return "iot"

    clients: list[dict[str, Any]] = []
    client_index = 0
    for node_id, names in client_names.items():
        for local_index, name in enumerate(names):
            client_index += 1
            rssi = -47 - (client_index * 3 % 28)
            band = "5GHz" if client_index % 3 else "2.4GHz"
            clients.append(
                {
                    "id": f"demo-client-{client_index:02d}",
                    "name": name,
                    "online": True,
                    "type": demo_client_type(name),
                    "model": "Synthetic Client",
                    "manufacturer": "Demo Fixture",
                    "operatingSystem": "Demo OS",
                    "macAddress": f"02:00:00:20:{client_index // 256:02X}:{client_index % 256:02X}",
                    "ipAddress": f"192.168.1.{150 + client_index}",
                    "parentId": node_id,
                    "nodeId": node_id,
                    "nodeName": node_names[node_id],
                    "band": band,
                    "radioId": "RADIO_5GHz" if band == "5GHz" else "RADIO_2.4GHz",
                    "rssi": rssi,
                    "quality": signal_quality(rssi),
                    "speedMbps": 72 + (client_index * 37 % 540),
                    "isGuest": False,
                    "lastSeen": now_iso(),
                    "userLabel": name,
                }
            )

    online_backhaul = [node for node in nodes if not node["isAuthority"]]
    return {
        "meta": {
            "source": "MeshScope demo data · No router connection",
            "router": host,
            "updatedAt": now_iso(),
            "revision": "demo",
            "demo": True,
        },
        "network": {
            "manufacturer": "Linksys",
            "model": "MX42",
            "description": "Synthetic offline MeshScope topology",
            "firmwareVersion": "1.0.13.216903",
            "firmwareDate": "2026-07-01",
            "serialNumber": "DEMO-AUTHORITY",
            "wanStatus": "Connected",
            "wanType": "DHCP",
            "wanIpAddress": "203.0.113.10",
            "lanIpAddress": host,
            "hostName": "MeshScope-Demo",
            "radioCount": 3,
            "clientSteeringEnabled": True,
            "nodeSteeringEnabled": True,
            "nodeSteeringMode": "automatic",
            "manualParentSelectionAvailable": False,
            "manualParentSelectionEvidence": "firmware-internal-confirmed",
            "manualParentSelectionTransport": "not-available",
            "documentedRestartScope": "single-node",
            "individualNodeRestartAvailable": True,
            "individualNodeRestartProbe": "firmware-confirmed",
            "individualNodeRestartEvidence": "reset_slave_nodes-direct-jnap",
        },
        "summary": {
            "nodesOnline": len(nodes),
            "nodesTotal": len(nodes),
            "clientsOnline": len(clients),
            "clientsKnown": len(clients),
            "weakNodes": sum(
                1 for node in online_backhaul if node["quality"]["tone"] in ("warn", "bad")
            ),
            "backhaulMbps": sum(node["speedMbps"] for node in online_backhaul),
        },
        "nodes": nodes,
        "clients": clients,
    }


def demo_node_probe(node: dict[str, Any]) -> dict[str, Any]:
    """Return an explicit non-live capability report for one demo Node."""
    return {
        "demo": True,
        "nodeId": node.get("id"),
        "name": node.get("name"),
        "ipAddress": node.get("ipAddress"),
        "managementUrl": node.get("managementUrl"),
        "managementEntry": "ca-support",
        "credentialsSynchronized": False,
        "deviceMode": "Demo",
        "identity": {
            "model": node.get("model"),
            "hardwareVersion": node.get("hardwareVersion"),
            "firmwareVersion": node.get("firmwareVersion"),
            "serialNumber": node.get("serialNumber"),
        },
        "services": {
            "coreReboot": True,
            "nodesSetup3": True,
            "topologyOptimization2": True,
        },
        "topologyOptimization": {
            "clientSteeringEnabled": True,
            "nodeSteeringEnabled": True,
        },
        "individualRestart": {
            "visibleInCaSupportUi": False,
            "action": "core/Reboot",
            "hasTargetDeviceId": False,
            "scope": "single-node",
            "executed": False,
            "disabledInDemo": True,
        },
        "manualParentSelection": {
            "available": False,
            "transport": "not-available",
            "firmwareInternalPathDiscovered": True,
            "reason": "Demo mode shows capability evidence without connecting to or controlling a router.",
        },
        "observedAt": now_iso(),
    }


def device_type(device: dict[str, Any]) -> str:
    model = device.get("model") or {}
    haystack = " ".join(
        str(value or "")
        for value in (
            model.get("deviceType"),
            model.get("modelNumber"),
            model.get("manufacturer"),
            device.get("friendlyName"),
            (device.get("unit") or {}).get("operatingSystem"),
        )
    ).lower()
    if any(word in haystack for word in ("iphone", "phone", "android", "mobile")):
        return "phone"
    if any(word in haystack for word in ("ipad", "tablet")):
        return "tablet"
    if any(word in haystack for word in ("macbook", "laptop", "computer", "desktop", "windows")):
        return "computer"
    if any(word in haystack for word in ("camera", "cam")):
        return "camera"
    if any(word in haystack for word in ("tv", "player", "chromecast")):
        return "media"
    if any(word in haystack for word in ("watch",)):
        return "wearable"
    if any(word in haystack for word in ("iot", "midea", "cooker", "lwip", "esp")):
        return "iot"
    return "device"


def normalize_topology(host: str, raw: dict[str, dict[str, Any]]) -> dict[str, Any]:
    device_info = output_of(raw, "core/GetDeviceInfo")
    device_output = output_of(raw, "devicelist/GetDevices3")
    devices = [item for item in device_output.get("devices") or [] if isinstance(item, dict)]
    backhaul = [
        item
        for item in output_of(raw, "nodes/diagnostics/GetBackhaulInfo").get("backhaulDevices") or []
        if isinstance(item, dict)
    ]
    main_connections = [
        item
        for item in output_of(raw, "networkconnections/GetNetworkConnections2").get("connections") or []
        if isinstance(item, dict)
    ]
    node_wireless = [
        item
        for item in output_of(
            raw, "nodes/networkconnections/GetNodesWirelessNetworkConnections"
        ).get("nodeWirelessConnections")
        or []
        if isinstance(item, dict)
    ]
    wan = output_of(raw, "router/GetWANStatus3")
    lan = output_of(raw, "router/GetLANSettings")
    radio_info = output_of(raw, "wirelessap/GetRadioInfo3")
    topology_optimization = output_of(
        raw, "nodes/topologyoptimization/GetTopologyOptimizationSettings2"
    )

    backhaul_by_id = {item.get("deviceUUID"): item for item in backhaul}
    node_ids = {
        item.get("deviceID")
        for item in devices
        if item.get("nodeType") or item.get("isAuthority") or item.get("deviceID") in backhaul_by_id
    }
    node_ids.discard(None)
    master_device = next((item for item in devices if item.get("isAuthority")), None)
    master_id = master_device.get("deviceID") if master_device else None

    live_by_mac: dict[str, dict[str, Any]] = {}
    for connection in main_connections:
        mac = str(connection.get("macAddress") or "").upper()
        if mac:
            live_by_mac[mac] = {**connection, "parentDeviceID": master_id}
    for node_group in node_wireless:
        parent_id = node_group.get("deviceID")
        for connection in node_group.get("connections") or []:
            if not isinstance(connection, dict):
                continue
            mac = str(connection.get("macAddress") or "").upper()
            if mac:
                live_by_mac[mac] = {**connection, "parentDeviceID": parent_id}

    devices_by_id = {item.get("deviceID"): item for item in devices}
    ip_to_node: dict[str, str] = {}
    for device_id in node_ids:
        device = devices_by_id.get(device_id) or {}
        for connection in device.get("connections") or []:
            if connection.get("ipAddress"):
                ip_to_node[str(connection["ipAddress"])] = str(device_id)
    for item in backhaul:
        if item.get("ipAddress") and item.get("deviceUUID"):
            ip_to_node[str(item["ipAddress"])] = str(item["deviceUUID"])

    nodes: list[dict[str, Any]] = []
    for device_id in node_ids:
        device = devices_by_id.get(device_id) or {}
        props = property_map(device)
        model = device.get("model") or {}
        unit = device.get("unit") or {}
        device_connections = device.get("connections") or []
        backhaul_item = backhaul_by_id.get(device_id) or {}
        ip_address = next(
            (str(item.get("ipAddress")) for item in device_connections if item.get("ipAddress")),
            str(backhaul_item.get("ipAddress") or ""),
        )
        parent_ip = backhaul_item.get("parentIPAddress")
        parent_id = ip_to_node.get(str(parent_ip)) if parent_ip else None
        wireless = backhaul_item.get("wirelessConnectionInfo") or {}
        rssi = wireless.get("stationRSSI")
        if rssi in (None, 0):
            rssi = wireless.get("apRSSI")
        online = bool(device.get("isAuthority") or backhaul_item)
        nodes.append(
            {
                "id": device_id,
                "name": friendly_name(device),
                "location": props.get("userDeviceLocation") or friendly_name(device),
                "role": "Primary node" if device.get("isAuthority") else "Child node",
                "isAuthority": bool(device.get("isAuthority")),
                "online": online,
                "model": model.get("modelNumber") or "Linksys Velop",
                "description": model.get("description") or "",
                "hardwareVersion": model.get("hardwareVersion"),
                "firmwareVersion": unit.get("firmwareVersion"),
                "firmwareDate": unit.get("firmwareDate"),
                "serialNumber": unit.get("serialNumber"),
                "macAddress": next(
                    (item.get("macAddress") for item in device_connections if item.get("macAddress")),
                    None,
                ),
                "ipAddress": ip_address or None,
                "parentId": parent_id,
                "parentIpAddress": parent_ip,
                "connectionType": backhaul_item.get("connectionType") or (
                    "Gateway" if device.get("isAuthority") else None
                ),
                "band": wireless.get("radioID"),
                "channel": wireless.get("channel"),
                "rssi": rssi,
                "quality": signal_quality(rssi),
                "speedMbps": float(backhaul_item["speedMbps"])
                if backhaul_item.get("speedMbps")
                else None,
                "timestamp": backhaul_item.get("timestamp"),
                "clientCount": 0,
                "managementUrl": f"https://{ip_address}/ca" if ip_address else None,
                "managementEntry": "ca-support" if ip_address else None,
            }
        )

    clients: list[dict[str, Any]] = []
    for device in devices:
        if device.get("deviceID") in node_ids:
            continue
        device_connections = [item for item in device.get("connections") or [] if isinstance(item, dict)]
        device_macs = all_macs(device)
        live = next((live_by_mac[mac] for mac in device_macs if mac in live_by_mac), {})
        primary = dict(device_connections[0]) if device_connections else {}
        primary.update(live)
        mac_address = primary.get("macAddress") or (device_macs[0] if device_macs else None)
        wireless = primary.get("wireless") or {}
        rssi = wireless.get("signalDecibels")
        parent_id = primary.get("parentDeviceID") or master_id
        props = property_map(device)
        model = device.get("model") or {}
        unit = device.get("unit") or {}
        online = bool(device_connections or live)
        clients.append(
            {
                "id": device.get("deviceID"),
                "name": friendly_name(device),
                "online": online,
                "type": device_type(device),
                "model": model.get("modelNumber"),
                "manufacturer": model.get("manufacturer"),
                "operatingSystem": unit.get("operatingSystem"),
                "macAddress": mac_address,
                "ipAddress": primary.get("ipAddress"),
                "parentId": parent_id,
                "nodeId": parent_id,
                "band": wireless.get("band"),
                "radioId": wireless.get("radioID"),
                "rssi": rssi,
                "quality": signal_quality(rssi),
                "speedMbps": primary.get("negotiatedMbps"),
                "isGuest": bool(wireless.get("isGuest")),
                "lastSeen": primary.get("timestamp"),
                "userLabel": props.get("userDeviceName"),
            }
        )

    online_counts: dict[str, int] = {}
    for client in clients:
        if client["online"] and client.get("nodeId"):
            node_id = str(client["nodeId"])
            online_counts[node_id] = online_counts.get(node_id, 0) + 1
    for node in nodes:
        node["clientCount"] = online_counts.get(str(node["id"]), 0)

    nodes.sort(key=lambda item: (not item["isAuthority"], not item["online"], item["name"].lower()))
    clients.sort(key=lambda item: (not item["online"], item["name"].lower()))
    node_name_by_id = {item["id"]: item["name"] for item in nodes}
    for node in nodes:
        node["parentName"] = node_name_by_id.get(node.get("parentId"))
    for client in clients:
        client["nodeName"] = node_name_by_id.get(client.get("nodeId"), "Main")

    online_nodes = [item for item in nodes if item["online"]]
    online_clients = [item for item in clients if item["online"]]
    weak_nodes = [
        item
        for item in online_nodes
        if not item["isAuthority"] and item.get("quality", {}).get("tone") in ("warn", "bad")
    ]
    return {
        "meta": {
            "source": "Linksys JNAP · Local read-only access",
            "router": host,
            "updatedAt": now_iso(),
            "revision": device_output.get("revision"),
        },
        "network": {
            "manufacturer": device_info.get("manufacturer", "Linksys"),
            "model": device_info.get("modelNumber"),
            "description": device_info.get("description"),
            "firmwareVersion": device_info.get("firmwareVersion"),
            "firmwareDate": device_info.get("firmwareDate"),
            "serialNumber": device_info.get("serialNumber"),
            "wanStatus": wan.get("wanStatus"),
            "wanType": (wan.get("wanConnection") or {}).get("wanType"),
            "wanIpAddress": (wan.get("wanConnection") or {}).get("ipAddress"),
            "lanIpAddress": lan.get("ipAddress") or host,
            "hostName": lan.get("hostName"),
            "radioCount": len(radio_info.get("radios") or []),
            "clientSteeringEnabled": topology_optimization.get("isClientSteeringEnabled"),
            "nodeSteeringEnabled": topology_optimization.get("isNodeSteeringEnabled"),
            "nodeSteeringMode": "automatic",
            "manualParentSelectionAvailable": False,
            "manualParentSelectionEvidence": "firmware-internal-confirmed",
            "manualParentSelectionTransport": "not-available",
            "documentedRestartScope": "single-node",
            "individualNodeRestartAvailable": True,
            "individualNodeRestartProbe": "firmware-confirmed",
            "individualNodeRestartEvidence": "reset_slave_nodes-direct-jnap",
        },
        "summary": {
            "nodesOnline": len(online_nodes),
            "nodesTotal": len(nodes),
            "clientsOnline": len(online_clients),
            "clientsKnown": len(clients),
            "weakNodes": len(weak_nodes),
            "backhaulMbps": round(
                sum(float(item.get("speedMbps") or 0) for item in online_nodes if not item["isAuthority"]),
                1,
            ),
        },
        "nodes": nodes,
        "clients": clients,
    }


class MeshRequestHandler(BaseHTTPRequestHandler):
    server_version = "MeshScope/1.0"

    def log_message(self, message: str, *args: Any) -> None:
        print(f"[mesh] {self.address_string()} {message % args}")

    def send_json(self, payload: Any, status: int = 200) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def read_json(self) -> dict[str, Any]:
        try:
            length = min(int(self.headers.get("Content-Length", "0")), 16_384)
            value = json.loads(self.rfile.read(length).decode("utf-8") or "{}")
        except (ValueError, json.JSONDecodeError) as exc:
            raise RouterError("The request body is invalid.") from exc
        return value if isinstance(value, dict) else {}

    def do_GET(self) -> None:
        route = urlparse(self.path).path
        if route == "/api/status":
            self.send_json(STATE.status())
            return
        if route == "/api/topology":
            try:
                self.send_json(STATE.refresh(force=self.path.endswith("refresh=1")))
            except RouterError as exc:
                self.send_json({"error": str(exc), **STATE.status()}, HTTPStatus.UNAUTHORIZED)
            return
        if route == "/api/node-capabilities":
            try:
                query = parse_qs(urlparse(self.path).query)
                self.send_json(STATE.probe_node(str((query.get("nodeId") or [""])[0])))
            except RouterError as exc:
                self.send_json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)
            return
        self.serve_static(route)

    def do_POST(self) -> None:
        route = urlparse(self.path).path
        try:
            if route == "/api/connect":
                body = self.read_json()
                topology = STATE.connect(str(body.get("host") or DEFAULT_ROUTER), str(body.get("password") or ""))
                self.send_json(topology)
                return
            if route == "/api/disconnect":
                STATE.disconnect()
                self.send_json({"connected": False})
                return
            if route == "/api/refresh":
                self.send_json(STATE.refresh(force=True))
                return
            if route == "/api/restart-node":
                body = self.read_json()
                self.send_json(STATE.restart_node(str(body.get("nodeId") or "")))
                return
            self.send_json({"error": "Endpoint not found."}, HTTPStatus.NOT_FOUND)
        except RouterError as exc:
            self.send_json({"error": str(exc), **STATE.status()}, HTTPStatus.BAD_REQUEST)
        except Exception as exc:
            self.send_json(
                {"error": f"The local service encountered an error: {exc}"},
                HTTPStatus.INTERNAL_SERVER_ERROR,
            )

    def serve_static(self, route: str) -> None:
        if route in ("", "/"):
            route = "/index.html"
        candidate = (WEB_ROOT / route.lstrip("/")).resolve()
        if WEB_ROOT not in candidate.parents or not candidate.is_file():
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        content = candidate.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", mimetypes.guess_type(candidate.name)[0] or "application/octet-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the local Linksys Mesh Topology dashboard.")
    parser.add_argument("--host", default="127.0.0.1", help="Local bind address. Default: 127.0.0.1")
    parser.add_argument("--port", default=8765, type=int, help="Local web port. Default: 8765")
    parser.add_argument("--router", default=DEFAULT_ROUTER, help=f"Router address. Default: {DEFAULT_ROUTER}")
    parser.add_argument(
        "--password-env",
        default="LINKSYS_PASSWORD",
        help="Optional environment variable used to connect at startup.",
    )
    parser.add_argument(
        "--demo",
        action="store_true",
        help="Serve a synthetic read-only topology without connecting to a router.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    STATE.session.host = validate_router_host(args.router)
    if args.demo:
        STATE.enable_demo(args.router)
        print("MeshScope demo mode enabled; no router calls or mutations are allowed.")
    else:
        startup_password = os.environ.get(args.password_env)
        if startup_password:
            try:
                STATE.connect(args.router, startup_password)
                print(f"Connected to Linksys router at {args.router}.")
            except RouterError as exc:
                print(f"Startup connection failed: {exc}")
    server = ThreadingHTTPServer((args.host, args.port), MeshRequestHandler)
    print(f"MeshScope is running at http://{args.host}:{args.port}")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        STATE.disconnect()
        server.server_close()


if __name__ == "__main__":
    main()
