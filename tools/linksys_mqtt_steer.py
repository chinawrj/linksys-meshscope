#!/usr/bin/env python3
"""Safely exercise Linksys exact-Parent steering over the stock MQTT path.

The command obtains identities and observed Parent relationships from JNAP,
refreshes infrastructure MQTT state, resolves a Parent radio tuple from fresh
DEVINFO or a currently observed child link, publishes one BH/config request,
and declares success only after JNAP reports the requested Parent twice.
"""

from __future__ import annotations

import argparse
import fcntl
import getpass
import json
import os
import re
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

from linksys_mesh_app import (  # noqa: E402
    RouterSession,
    jnap_call,
    normalize_topology,
    output_of,
)
import linksys_mqtt_parent as mqtt_parent  # noqa: E402


MAC_RE = re.compile(r"^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")
UUID_RE = re.compile(
    r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
    r"[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
)
ALLOWED_BANDS = {"5GL", "5GH"}
TOPOLOGY_ACTIONS = (
    "core/GetDeviceInfo",
    "devicelist/GetDevices3",
    "nodes/diagnostics/GetBackhaulInfo",
)


class SteeringError(RuntimeError):
    pass


@dataclass(frozen=True)
class RadioTuple:
    parent_id: str
    parent_name: str
    band: str
    channel: int
    bssid: str
    source: str


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def safe_slug(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-") or "node"


def load_password(args: argparse.Namespace) -> str:
    env_value = os.environ.get(args.password_env)
    if env_value:
        return env_value

    if args.credentials and args.credentials.exists():
        document = json.loads(args.credentials.read_text(encoding="utf-8"))
        matches: list[dict[str, Any]] = []
        default = document.get("default")
        if isinstance(default, dict):
            matches.append(default)
        hosts = document.get("hosts")
        if isinstance(hosts, dict) and isinstance(hosts.get(args.router), dict):
            matches.append(hosts[args.router])
        for item in document.get("routers") or []:
            if isinstance(item, dict) and str(item.get("host")) == args.router:
                matches.append(item)
        for item in reversed(matches):
            if item.get("password"):
                return str(item["password"])

    if not sys.stdin.isatty():
        raise SteeringError(
            f"Set {args.password_env} or add host {args.router} to {args.credentials}."
        )
    return getpass.getpass("Linksys local router password: ")


def mqtt_acl_probe(host: str, timeout: int = 4) -> dict[str, Any]:
    report = mqtt_parent.probe_acl(host, timeout=timeout)
    return {"ok": bool(report.get("available")), **report}


def fetch_topology(session: RouterSession) -> tuple[dict[str, Any], dict[str, Any]]:
    raw = {action: jnap_call(session, action) for action in TOPOLOGY_ACTIONS}
    for action, response in raw.items():
        if response.get("result") != "OK":
            raise SteeringError(f"JNAP {action} failed: {response.get('result')}")
    return normalize_topology(session.host, raw), raw


def find_node(topology: dict[str, Any], selector: str) -> dict[str, Any]:
    selector_folded = selector.casefold()
    matches = [
        node
        for node in topology.get("nodes") or []
        if str(node.get("id", "")).casefold() == selector_folded
        or str(node.get("name", "")).casefold() == selector_folded
    ]
    if len(matches) != 1:
        names = ", ".join(str(node.get("name")) for node in matches) or "none"
        raise SteeringError(f"Node selector {selector!r} matched {names}.")
    return matches[0]


def descendant_ids(topology: dict[str, Any], root_id: str) -> set[str]:
    children: dict[str, list[str]] = {}
    for node in topology.get("nodes") or []:
        parent_id = node.get("parentId")
        node_id = node.get("id")
        if parent_id and node_id:
            children.setdefault(str(parent_id), []).append(str(node_id))
    found: set[str] = set()
    pending = list(children.get(root_id, []))
    while pending:
        node_id = pending.pop()
        if node_id in found:
            continue
        found.add(node_id)
        pending.extend(children.get(node_id, []))
    return found


def parse_mqtt_lines(stdout: str) -> list[tuple[str, dict[str, Any]]]:
    messages: list[tuple[str, dict[str, Any]]] = []
    for line in stdout.splitlines():
        topic = ""
        payload = ""
        try:
            formatted = json.loads(line)
        except json.JSONDecodeError:
            formatted = None
        if isinstance(formatted, dict) and isinstance(formatted.get("topic"), str):
            topic = formatted["topic"]
            raw_payload = formatted.get("payload")
            payload = raw_payload if isinstance(raw_payload, str) else ""
        else:
            topic, separator, payload = line.partition(" ")
            if not separator:
                continue
        try:
            value = json.loads(payload)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            messages.append((topic, value))
    return messages


def collect_infrastructure_messages(host: str, seconds: int = 7) -> list[tuple[str, dict[str, Any]]]:
    try:
        records = mqtt_parent.collect_devinfo(host, seconds=seconds)
    except (mqtt_parent.MQTTParentError, TimeoutError, OSError) as exc:
        raise SteeringError(str(exc)) from exc
    return [
        (f"network/{node_id.upper()}/DEVINFO", payload)
        for node_id, payload in records.items()
    ]


def devinfo_by_uuid(messages: list[tuple[str, dict[str, Any]]]) -> dict[str, dict[str, Any]]:
    records: dict[str, dict[str, Any]] = {}
    for topic, payload in messages:
        if not topic.endswith("/DEVINFO"):
            continue
        node_id = str(payload.get("uuid") or "").lower()
        if UUID_RE.fullmatch(node_id) and isinstance(payload.get("data"), dict):
            records[node_id] = payload
    return records


def validate_radio_values(band: str, channel: Any, bssid: Any) -> tuple[int, str]:
    if band not in ALLOWED_BANDS:
        raise SteeringError(f"Unsupported backhaul band {band!r}.")
    try:
        channel_number = int(channel)
    except (TypeError, ValueError) as exc:
        raise SteeringError(f"Invalid channel {channel!r}.") from exc
    if not 1 <= channel_number <= 196:
        raise SteeringError(f"Invalid channel {channel_number}.")
    clean_bssid = str(bssid or "").upper()
    if not MAC_RE.fullmatch(clean_bssid) or clean_bssid == "00:00:00:00:00:00":
        raise SteeringError(f"Invalid BSSID {bssid!r}.")
    if int(clean_bssid.split(":")[0], 16) & 1:
        raise SteeringError(f"BSSID {clean_bssid} is multicast.")
    return channel_number, clean_bssid


def tuple_from_devinfo(
    parent: dict[str, Any], band: str, records: dict[str, dict[str, Any]]
) -> RadioTuple | None:
    record = records.get(str(parent.get("id")).lower())
    if not record:
        return None
    data = record.get("data") or {}
    prefix = "userAp5GL" if band == "5GL" else "userAp5GH"
    try:
        channel, bssid = validate_radio_values(
            band, data.get(f"{prefix}_channel"), data.get(f"{prefix}_bssid")
        )
    except SteeringError:
        return None
    return RadioTuple(
        parent_id=str(parent["id"]),
        parent_name=str(parent["name"]),
        band=band,
        channel=channel,
        bssid=bssid,
        source="fresh MQTT DEVINFO",
    )


def tuple_from_observed_links(
    parent: dict[str, Any], band: str, raw: dict[str, Any]
) -> RadioTuple | None:
    backhaul = output_of(raw, "nodes/diagnostics/GetBackhaulInfo").get(
        "backhaulDevices"
    ) or []
    candidates: set[tuple[int, str]] = set()
    for item in backhaul:
        if str(item.get("parentIPAddress")) != str(parent.get("ipAddress")):
            continue
        wireless = item.get("wirelessConnectionInfo") or {}
        if str(wireless.get("radioID")) != band:
            continue
        try:
            candidates.add(
                validate_radio_values(
                    band, wireless.get("channel"), wireless.get("apBSSID")
                )
            )
        except SteeringError:
            continue
    if len(candidates) != 1:
        return None
    channel, bssid = candidates.pop()
    return RadioTuple(
        parent_id=str(parent["id"]),
        parent_name=str(parent["name"]),
        band=band,
        channel=channel,
        bssid=bssid,
        source="fresh JNAP observed child link",
    )


def resolve_parent_tuple(
    parent: dict[str, Any],
    band: str,
    records: dict[str, dict[str, Any]],
    raw: dict[str, Any],
) -> RadioTuple:
    result = tuple_from_devinfo(parent, band, records)
    if result:
        return result
    result = tuple_from_observed_links(parent, band, raw)
    if result:
        return result
    raise SteeringError(
        f"No trustworthy live {band} BSSID/channel tuple is available for {parent['name']}."
    )


def publish_steering(host: str, child_id: str, target: RadioTuple) -> dict[str, Any]:
    if not UUID_RE.fullmatch(child_id):
        raise SteeringError("Child UUID is invalid.")
    try:
        result = mqtt_parent.publish_parent_request(
            host,
            child_id,
            mqtt_parent.RadioTarget(
                parent_id=target.parent_id,
                parent_name=target.parent_name,
                band=target.band,
                channel=target.channel,
                bssid=target.bssid,
                source=target.source,
            ),
        )
    except (mqtt_parent.MQTTParentError, TimeoutError, OSError) as exc:
        raise SteeringError(str(exc)) from exc
    return {
        "topic": result["topic"],
        "payload": result["payload"],
        "publisherReturnCode": 0,
    }


def observe_parent(
    session: RouterSession,
    child_id: str,
    requested_parent_id: str,
    timeout: int,
    poll_seconds: float,
) -> tuple[bool, list[dict[str, Any]]]:
    deadline = time.monotonic() + timeout
    consecutive = 0
    observations: list[dict[str, Any]] = []
    last_signature: tuple[Any, ...] | None = None
    while time.monotonic() < deadline:
        try:
            topology, _raw = fetch_topology(session)
            child = find_node(topology, child_id)
            signature = (
                bool(child.get("online")),
                child.get("parentId"),
                child.get("parentName"),
                child.get("band"),
                child.get("channel"),
            )
            observation = {
                "at": utc_now(),
                "online": signature[0],
                "parentId": signature[1],
                "parentName": signature[2],
                "band": signature[3],
                "channel": signature[4],
            }
            if signature != last_signature:
                observations.append(observation)
                print(json.dumps({"observation": observation}, ensure_ascii=False), flush=True)
                last_signature = signature
            if child.get("online") and child.get("parentId") == requested_parent_id:
                consecutive += 1
                if consecutive >= 2:
                    return True, observations
            else:
                consecutive = 0
        except Exception as exc:  # transient loss is expected during reconnect
            observation = {"at": utc_now(), "error": str(exc)}
            observations.append(observation)
            print(json.dumps({"observation": observation}, ensure_ascii=False), flush=True)
            consecutive = 0
        time.sleep(poll_seconds)
    return False, observations


def write_journal(path: Path, document: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(document, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def preflight(
    topology: dict[str, Any], child: dict[str, Any], parent: dict[str, Any]
) -> None:
    if child.get("isAuthority"):
        raise SteeringError("Main cannot be selected as the child.")
    if not child.get("online") or not parent.get("online"):
        raise SteeringError("Both child and requested Parent must be online.")
    if child.get("id") == parent.get("id"):
        raise SteeringError("A Node cannot be its own Parent.")
    if parent.get("id") in descendant_ids(topology, str(child.get("id"))):
        raise SteeringError("The requested Parent is a descendant of the child.")
    if str(child.get("connectionType", "")).casefold() == "wired":
        raise SteeringError("Wired-backhaul Nodes cannot be steered with BH/config.")
    if child.get("parentId") == parent.get("id"):
        raise SteeringError(f"{child['name']} is already connected to {parent['name']}.")


def print_topology(topology: dict[str, Any]) -> None:
    for node in topology.get("nodes") or []:
        if node.get("online"):
            print(
                json.dumps(
                    {
                        "name": node.get("name"),
                        "id": node.get("id"),
                        "ip": node.get("ipAddress"),
                        "parent": node.get("parentName"),
                        "band": node.get("band"),
                        "channel": node.get("channel"),
                    },
                    ensure_ascii=False,
                )
            )


def command_probe(args: argparse.Namespace) -> int:
    report = mqtt_acl_probe(args.mqtt_host)
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 2


def command_topology(args: argparse.Namespace) -> int:
    password = load_password(args)
    topology, _raw = fetch_topology(RouterSession(host=args.router, password=password))
    print_topology(topology)
    return 0


def command_steer(args: argparse.Namespace) -> int:
    password = load_password(args)
    session = RouterSession(host=args.router, password=password)
    lock_path = Path("/tmp/meshscope-linksys-mqtt-steer.lock")
    with lock_path.open("w", encoding="utf-8") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise SteeringError("Another steering operation is already running.") from exc

        probe = mqtt_acl_probe(args.mqtt_host)
        if not probe["ok"]:
            raise SteeringError(
                "MQTT ACL round-trip failed; refusing to publish BH/config. "
                + str(probe.get("reason") or "No fresh DEVINFO was received.")
            )

        topology, raw = fetch_topology(session)
        child = find_node(topology, args.child)
        parent = find_node(topology, args.parent)
        preflight(topology, child, parent)

        messages = collect_infrastructure_messages(args.mqtt_host, args.state_wait)
        records = devinfo_by_uuid(messages)
        target = resolve_parent_tuple(parent, args.band, records, raw)

        journal_path = args.journal or (
            REPO_ROOT
            / "firmware-analysis"
            / "work"
            / "steering-runs"
            / (
                datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
                + f"-{safe_slug(str(child['name']))}-to-{safe_slug(str(parent['name']))}.json"
            )
        )
        journal: dict[str, Any] = {
            "status": "prepared",
            "preparedAt": utc_now(),
            "router": args.router,
            "mqttHost": args.mqtt_host,
            "child": {
                "id": child.get("id"),
                "name": child.get("name"),
                "previousParentId": child.get("parentId"),
                "previousParentName": child.get("parentName"),
                "previousBand": child.get("band"),
                "previousChannel": child.get("channel"),
            },
            "requestedParent": asdict(target),
            "mqttAclProbe": probe,
            "capturedDevinfoUuids": sorted(records),
        }
        write_journal(journal_path, journal)
        print(json.dumps({"prepared": journal, "journal": str(journal_path)}, indent=2))

        if args.dry_run:
            return 0

        published = publish_steering(args.mqtt_host, str(child["id"]), target)
        journal.update({"status": "published", "publishedAt": utc_now(), "mqtt": published})
        write_journal(journal_path, journal)

        success, observations = observe_parent(
            session,
            str(child["id"]),
            target.parent_id,
            args.timeout,
            args.poll,
        )
        journal.update(
            {
                "status": "verified" if success else "not-verified",
                "finishedAt": utc_now(),
                "observations": observations,
            }
        )
        write_journal(journal_path, journal)
        print(json.dumps({"success": success, "journal": str(journal_path)}, indent=2))
        return 0 if success else 3


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--router", default="192.168.1.1")
    parser.add_argument("--mqtt-host", default=None)
    parser.add_argument(
        "--credentials", type=Path, default=REPO_ROOT / "router_credentials.json"
    )
    parser.add_argument("--password-env", default="LINKSYS_PASSWORD")
    subparsers = parser.add_subparsers(dest="command", required=True)

    probe = subparsers.add_parser("probe-acl", help="Verify publish/subscribe round-trip")
    probe.set_defaults(handler=command_probe)

    topology = subparsers.add_parser("topology", help="Print the online Node topology")
    topology.set_defaults(handler=command_topology)

    steer = subparsers.add_parser("steer", help="Publish and verify one exact-Parent request")
    steer.add_argument("--child", required=True)
    steer.add_argument("--parent", required=True)
    steer.add_argument("--band", choices=sorted(ALLOWED_BANDS), required=True)
    steer.add_argument("--dry-run", action="store_true")
    steer.add_argument("--state-wait", type=int, default=20)
    steer.add_argument("--timeout", type=int, default=150)
    steer.add_argument("--poll", type=float, default=5.0)
    steer.add_argument("--journal", type=Path)
    steer.set_defaults(handler=command_steer)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    args.mqtt_host = args.mqtt_host or args.router
    try:
        return int(args.handler(args))
    except (SteeringError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
