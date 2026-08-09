"""Minimal Linksys MQTT Parent-steering client used by MeshScope.

The Linksys broker is a local MQTT 3.1.1 service on the primary Node.  This
module intentionally implements only the small protocol surface MeshScope
needs so desktop users do not have to install a separate MQTT client package.
"""

from __future__ import annotations

import json
import socket
import struct
import time
import uuid
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from typing import Any, Iterable


MQTT_PORT = 1883
ALLOWED_BANDS = {"5GL", "5GH"}
DEVINFO_TOPIC = "network/+/DEVINFO"
STATUS_REFRESH_TOPICS = (
    "network/status_resend_all",
    "network/DEVINFO/status_resend_all",
    "network/BH/status_resend_all",
)


class MQTTParentError(RuntimeError):
    """A safe, user-facing MQTT Parent-steering error."""


@dataclass(frozen=True)
class RadioTarget:
    parent_id: str
    parent_name: str
    band: str
    channel: int
    bssid: str
    source: str = "fresh MQTT DEVINFO"


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _mqtt_varint(value: int) -> bytes:
    if not 0 <= value <= 268_435_455:
        raise ValueError("MQTT remaining length is out of range")
    encoded = bytearray()
    while True:
        byte = value % 128
        value //= 128
        if value:
            byte |= 0x80
        encoded.append(byte)
        if not value:
            return bytes(encoded)


def _mqtt_text(value: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > 65_535:
        raise ValueError("MQTT string is too long")
    return struct.pack("!H", len(encoded)) + encoded


class MQTTClient:
    """Small synchronous MQTT 3.1.1 client for one local operation."""

    def __init__(self, host: str, timeout: float = 5.0, client_prefix: str = "meshscope"):
        self.host = host
        self.timeout = timeout
        self.client_id = (client_prefix + "-" + uuid.uuid4().hex[:10])[:23]
        self.socket: socket.socket | None = None
        self.packet_id = 0
        self.pending_messages: list[tuple[str, bytes]] = []

    def __enter__(self) -> "MQTTClient":
        try:
            sock = socket.create_connection((self.host, MQTT_PORT), timeout=self.timeout)
            sock.settimeout(self.timeout)
        except OSError as exc:
            raise MQTTParentError(f"Unable to connect to MQTT at {self.host}:1883: {exc}") from exc
        self.socket = sock
        variable = _mqtt_text("MQTT") + bytes((4, 2, 0, 30))
        payload = _mqtt_text(self.client_id)
        self._send_packet(0x10, variable + payload)
        packet_type, _flags, body = self._read_packet(self.timeout)
        if packet_type != 2 or len(body) != 2 or body[1] != 0:
            self.close()
            reason = body[1] if len(body) > 1 else "invalid response"
            raise MQTTParentError(f"MQTT connection was rejected: {reason}")
        return self

    def __exit__(self, *_args: Any) -> None:
        if self.socket is not None:
            try:
                self._send_packet(0xE0, b"")
            except OSError:
                pass
        self.close()

    def close(self) -> None:
        if self.socket is not None:
            try:
                self.socket.close()
            finally:
                self.socket = None

    def _next_packet_id(self) -> int:
        self.packet_id = self.packet_id % 65_535 + 1
        return self.packet_id

    def _send_packet(self, header: int, body: bytes) -> None:
        if self.socket is None:
            raise MQTTParentError("MQTT client is not connected")
        self.socket.sendall(bytes((header,)) + _mqtt_varint(len(body)) + body)

    def _read_exact(self, size: int, timeout: float) -> bytes:
        if self.socket is None:
            raise MQTTParentError("MQTT client is not connected")
        self.socket.settimeout(max(0.05, timeout))
        chunks = bytearray()
        while len(chunks) < size:
            try:
                chunk = self.socket.recv(size - len(chunks))
            except socket.timeout as exc:
                raise TimeoutError("MQTT receive timed out") from exc
            if not chunk:
                raise MQTTParentError("MQTT broker closed the connection")
            chunks.extend(chunk)
        return bytes(chunks)

    def _read_packet(self, timeout: float) -> tuple[int, int, bytes]:
        deadline = time.monotonic() + timeout
        first = self._read_exact(1, deadline - time.monotonic())[0]
        multiplier = 1
        remaining = 0
        for _ in range(4):
            byte = self._read_exact(1, deadline - time.monotonic())[0]
            remaining += (byte & 0x7F) * multiplier
            if not byte & 0x80:
                break
            multiplier *= 128
        else:
            raise MQTTParentError("MQTT packet has an invalid remaining length")
        body = self._read_exact(remaining, deadline - time.monotonic()) if remaining else b""
        return first >> 4, first & 0x0F, body

    def subscribe(self, topics: Iterable[str]) -> None:
        packet_id = self._next_packet_id()
        body = struct.pack("!H", packet_id) + b"".join(_mqtt_text(topic) + b"\x00" for topic in topics)
        self._send_packet(0x82, body)
        deadline = time.monotonic() + self.timeout
        while True:
            packet_type, flags, payload = self._read_packet(deadline - time.monotonic())
            if packet_type == 9:
                if len(payload) < 3 or struct.unpack("!H", payload[:2])[0] != packet_id:
                    raise MQTTParentError("MQTT SUBACK did not match the request")
                if any(code == 0x80 for code in payload[2:]):
                    raise MQTTParentError("MQTT subscription was denied by the router ACL")
                return
            self._handle_packet(packet_type, flags, payload)

    def publish(self, topic: str, payload: bytes | str, qos: int = 1) -> None:
        if qos not in (0, 1):
            raise ValueError("MeshScope supports MQTT QoS 0 or 1")
        raw_payload = payload.encode("utf-8") if isinstance(payload, str) else payload
        packet_id = self._next_packet_id() if qos else 0
        body = _mqtt_text(topic)
        if qos:
            body += struct.pack("!H", packet_id)
        self._send_packet(0x30 | (qos << 1), body + raw_payload)
        if not qos:
            return
        deadline = time.monotonic() + self.timeout
        while True:
            packet_type, flags, response = self._read_packet(deadline - time.monotonic())
            if packet_type == 4:
                if len(response) != 2 or struct.unpack("!H", response)[0] != packet_id:
                    raise MQTTParentError("MQTT PUBACK did not match the request")
                return
            self._handle_packet(packet_type, flags, response)

    def _handle_packet(self, packet_type: int, flags: int, payload: bytes) -> None:
        if packet_type == 3:
            if len(payload) < 2:
                raise MQTTParentError("MQTT PUBLISH packet is truncated")
            topic_size = struct.unpack("!H", payload[:2])[0]
            position = 2 + topic_size
            if position > len(payload):
                raise MQTTParentError("MQTT PUBLISH Topic is truncated")
            topic = payload[2:position].decode("utf-8")
            qos = (flags >> 1) & 0x03
            if qos:
                if position + 2 > len(payload):
                    raise MQTTParentError("MQTT PUBLISH packet ID is truncated")
                packet_id = payload[position : position + 2]
                position += 2
                if qos == 1:
                    self._send_packet(0x40, packet_id)
            self.pending_messages.append((topic, payload[position:]))
        elif packet_type == 13:
            return

    def receive(self, timeout: float) -> tuple[str, bytes]:
        if self.pending_messages:
            return self.pending_messages.pop(0)
        deadline = time.monotonic() + timeout
        while True:
            packet_type, flags, payload = self._read_packet(deadline - time.monotonic())
            self._handle_packet(packet_type, flags, payload)
            if self.pending_messages:
                return self.pending_messages.pop(0)


def probe_acl(host: str, timeout: float = 4.0) -> dict[str, Any]:
    report: dict[str, Any] = {"available": False, "host": host, "port": MQTT_PORT}
    try:
        with MQTTClient(host, timeout=timeout, client_prefix="meshscope-probe") as client:
            client.subscribe((DEVINFO_TOPIC,))
            for refresh_topic in STATUS_REFRESH_TOPICS:
                client.publish(refresh_topic, b"", qos=1)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                received_topic, received_payload = client.receive(deadline - time.monotonic())
                if not received_topic.endswith("/DEVINFO"):
                    continue
                try:
                    document = json.loads(received_payload.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    continue
                if document.get("uuid") and isinstance(document.get("data"), dict):
                    report.update(
                        {
                            "available": True,
                            "roundTrip": True,
                            "proof": "fresh DEVINFO received after status refresh",
                        }
                    )
                    return report
        report.update(
            {
                "roundTrip": False,
                "reason": "The broker accepted the probe but returned no fresh DEVINFO.",
            }
        )
    except (MQTTParentError, TimeoutError, OSError) as exc:
        report.update({"roundTrip": False, "reason": str(exc)})
    return report


def collect_devinfo(host: str, seconds: float = 20.0) -> dict[str, dict[str, Any]]:
    records: dict[str, dict[str, Any]] = {}
    with MQTTClient(host, timeout=max(4.0, seconds), client_prefix="meshscope-state") as client:
        client.subscribe((DEVINFO_TOPIC,))
        # Firmware generations differ in which refresh Topic wakes DEVINFO.
        # Publishing all three is idempotent and mirrors Linksys's own status
        # refresh behavior; none is a configuration command.
        for refresh_topic in STATUS_REFRESH_TOPICS:
            client.publish(refresh_topic, b"", qos=1)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                topic, payload = client.receive(deadline - time.monotonic())
            except TimeoutError:
                break
            if not topic.endswith("/DEVINFO"):
                continue
            try:
                document = json.loads(payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            node_id = str(document.get("uuid") or "").lower()
            if node_id and isinstance(document.get("data"), dict):
                records[node_id] = document
    return records


def _valid_bssid(value: Any) -> str:
    clean = str(value or "").upper()
    pieces = clean.split(":")
    if len(pieces) != 6 or any(len(piece) != 2 for piece in pieces):
        raise MQTTParentError("The Parent DEVINFO contains an invalid BSSID")
    try:
        octets = [int(piece, 16) for piece in pieces]
    except ValueError as exc:
        raise MQTTParentError("The Parent DEVINFO contains an invalid BSSID") from exc
    if clean == "00:00:00:00:00:00" or octets[0] & 1:
        raise MQTTParentError("The Parent DEVINFO BSSID is not unicast")
    return clean


def radio_target(parent: dict[str, Any], band: str, records: dict[str, dict[str, Any]]) -> RadioTarget:
    if band not in ALLOWED_BANDS:
        raise MQTTParentError("Parent steering supports only 5GL or 5GH")
    parent_id = str(parent.get("id") or "").lower()
    record = records.get(parent_id)
    if not record:
        raise MQTTParentError(f"No fresh MQTT DEVINFO was received for {parent.get('name') or parent_id}")
    data = record["data"]
    prefix = "userAp5GL" if band == "5GL" else "userAp5GH"
    try:
        channel = int(data.get(f"{prefix}_channel"))
    except (TypeError, ValueError) as exc:
        raise MQTTParentError(f"The Parent has no live {band} channel") from exc
    if not 1 <= channel <= 196:
        raise MQTTParentError(f"The Parent has an invalid {band} channel")
    return RadioTarget(
        parent_id=parent_id,
        parent_name=str(parent.get("name") or parent_id),
        band=band,
        channel=channel,
        bssid=_valid_bssid(data.get(f"{prefix}_bssid")),
    )


def _find_node(topology: dict[str, Any], node_id: str) -> dict[str, Any]:
    clean = node_id.casefold()
    matches = [node for node in topology.get("nodes") or [] if str(node.get("id") or "").casefold() == clean]
    if len(matches) != 1:
        raise MQTTParentError("The selected Node is not present in the current topology")
    return matches[0]


def _descendants(topology: dict[str, Any], node_id: str) -> set[str]:
    children: dict[str, list[str]] = {}
    for node in topology.get("nodes") or []:
        parent_id = str(node.get("parentId") or "").lower()
        child_id = str(node.get("id") or "").lower()
        if parent_id and child_id:
            children.setdefault(parent_id, []).append(child_id)
    found: set[str] = set()
    pending = list(children.get(node_id.lower(), ()))
    while pending:
        child_id = pending.pop()
        if child_id in found:
            continue
        found.add(child_id)
        pending.extend(children.get(child_id, ()))
    return found


def preflight(topology: dict[str, Any], child_id: str, parent_id: str) -> tuple[dict[str, Any], dict[str, Any]]:
    child = _find_node(topology, child_id)
    parent = _find_node(topology, parent_id)
    child_key = str(child.get("id") or "").lower()
    parent_key = str(parent.get("id") or "").lower()
    if child.get("isAuthority"):
        raise MQTTParentError("The primary Node cannot be steered as a child")
    if not child.get("online") or not parent.get("online"):
        raise MQTTParentError("Both the child and requested Parent must be online")
    if child_key == parent_key:
        raise MQTTParentError("A Node cannot be its own Parent")
    if parent_key in _descendants(topology, child_key):
        raise MQTTParentError("The requested Parent is a descendant of the child")
    if str(child.get("connectionType") or "").casefold() == "wired":
        raise MQTTParentError("A wired-backhaul Node cannot be steered over BH/config")
    if str(child.get("parentId") or "").casefold() == parent_key:
        raise MQTTParentError("The Node is already connected to the requested Parent")
    return child, parent


def publish_parent_request(host: str, child_id: str, target: RadioTarget) -> dict[str, Any]:
    wire_uuid = child_id.upper()
    topic = f"network/{wire_uuid}/BH/config"
    document = {
        "uuid": wire_uuid,
        "type": "set",
        "TS": utc_now(),
        "data": {
            "band": target.band,
            "bssid": target.bssid.lower(),
            "channel": str(target.channel),
        },
    }
    with MQTTClient(host, timeout=6.0, client_prefix="meshscope-steer") as client:
        client.publish(topic, json.dumps(document, separators=(",", ":")), qos=1)
    return {
        "accepted": True,
        "topic": topic,
        "payload": document,
        "target": asdict(target),
        "requestedAt": document["TS"],
    }


def steer_parent(
    host: str,
    topology: dict[str, Any],
    child_id: str,
    parent_id: str,
    band: str,
    state_wait: float = 20.0,
) -> dict[str, Any]:
    child, parent = preflight(topology, child_id, parent_id)
    capability = probe_acl(host)
    if not capability.get("available"):
        raise MQTTParentError(str(capability.get("reason") or "MQTT Parent steering is unavailable"))
    try:
        target = radio_target(parent, band, collect_devinfo(host, seconds=state_wait))
        result = publish_parent_request(host, str(child["id"]), target)
    except (TimeoutError, OSError) as exc:
        raise MQTTParentError(f"MQTT Parent request failed: {exc}") from exc
    result["child"] = {"id": child.get("id"), "name": child.get("name")}
    result["previousParent"] = {"id": child.get("parentId"), "name": child.get("parentName")}
    return result
