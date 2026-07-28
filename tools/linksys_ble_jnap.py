#!/usr/bin/env python3
"""Build Linksys BLE-JNAP frames without accessing a Bluetooth adapter.

The protocol was reconstructed from the Linksys Android application and the
MX4200/MX5300 ``btsetup`` binaries.  This module intentionally performs no BLE
scan, connection, or write operation; it only produces an offline transcript
that a separately reviewed client can use.
"""

from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any


JNAP_SERVICE_UUID = "00002080-8eab-46c2-b788-0e9440016fd1"
CONTROL_POINT_UUID = "00002081-8eab-46c2-b788-0e9440016fd1"
JNAP_DATA_UUID = "00002082-8eab-46c2-b788-0e9440016fd1"

JNAP_INIT = b"\x00\x01"
JNAP_START = b"\x00\x02"
JNAP_STOP = b"\x00\x05"
JNAP_DATA_ACK = b"\x04\x00\x00\x00"
JNAP_REQUEST_PREFIX = b"\x00\x03"

ACTION_BASE = "http://linksys.com/jnap"
DEFAULT_SETUP_AUTHORIZATION = "Basic YWRtaW46YWRtaW4="


def _b64(value: bytes) -> str:
    return base64.b64encode(value).decode("ascii")


@dataclass(frozen=True)
class BLEJNAPRequest:
    """One complete JNAP request and its BLE length-control frame."""

    action: str
    body: str
    authorization: str
    request: bytes
    length_frame: bytes

    def transcript(self, include_request: bool = True) -> dict[str, Any]:
        """Return the observed Control Point/JNAP Data handshake."""
        steps: list[dict[str, str]] = [
            {
                "operation": "subscribe",
                "characteristic": CONTROL_POINT_UUID,
            },
            {
                "operation": "write",
                "characteristic": CONTROL_POINT_UUID,
                "value_base64": _b64(JNAP_INIT),
                "after_notification_base64": _b64(JNAP_INIT),
            },
            {
                "operation": "write",
                "characteristic": CONTROL_POINT_UUID,
                "value_base64": _b64(JNAP_START),
                "after_notification_base64": _b64(JNAP_START),
            },
            {
                "operation": "write",
                "characteristic": CONTROL_POINT_UUID,
                "value_base64": _b64(self.length_frame),
                "after_notification_base64": _b64(self.length_frame),
            },
            {
                "operation": "write",
                "characteristic": JNAP_DATA_UUID,
                "after_notification_base64": _b64(JNAP_DATA_ACK),
            },
            {
                "operation": "write",
                "characteristic": CONTROL_POINT_UUID,
                "value_base64": _b64(JNAP_STOP),
                "after_notification_base64": _b64(JNAP_STOP),
            },
            {
                "operation": "unsubscribe",
                "characteristic": CONTROL_POINT_UUID,
            },
            {
                "operation": "read",
                "characteristic": JNAP_DATA_UUID,
            },
        ]
        if include_request:
            steps[4]["value_base64"] = _b64(self.request)

        return {
            "service_uuid": JNAP_SERVICE_UUID,
            "action": self.action,
            "request_length": len(self.request),
            "length_frame_hex": self.length_frame.hex(),
            "steps": steps,
        }


def basic_authorization(username: str, password: str) -> str:
    """Return an RFC 7617-style Basic authorization value."""
    if any(character in username for character in "\r\n:"):
        raise ValueError("username may not contain a colon or newline")
    if any(character in password for character in "\r\n"):
        raise ValueError("password may not contain a newline")
    token = base64.b64encode(f"{username}:{password}".encode()).decode("ascii")
    return f"Basic {token}"


def build_request(
    action: str,
    payload: dict[str, Any] | None = None,
    authorization: str = DEFAULT_SETUP_AUTHORIZATION,
) -> BLEJNAPRequest:
    """Encode one BLE-JNAP request exactly as the Linksys app does.

    ``json.dumps(..., ensure_ascii=True)`` keeps the body ASCII so the byte
    length and the JavaScript application's string length are identical.
    """
    if not action.startswith("/") or action.startswith("//"):
        raise ValueError("action must be a JNAP path beginning with one slash")
    if any(character in action for character in "\r\n"):
        raise ValueError("action may not contain a newline")
    if not authorization.startswith("Basic "):
        raise ValueError("authorization must start with 'Basic '")
    if any(character in authorization for character in "\r\n"):
        raise ValueError("authorization may not contain a newline")

    body = json.dumps(payload or {}, separators=(",", ":"), ensure_ascii=True)
    request_text = (
        "Host:www.linksyssmartwifi.com\n"
        f"X-JNAP-Action:{ACTION_BASE}{action}\n"
        f"X-JNAP-Authorization:{authorization}\n"
        "Content-Type:application/json; charset=utf-8\n"
        f"{body}\n"
    )
    request = request_text.encode("ascii")
    if len(request) > 0xFFFF:
        raise ValueError("BLE-JNAP request exceeds the 16-bit length field")
    length_frame = JNAP_REQUEST_PREFIX + len(request).to_bytes(2, "big")
    return BLEJNAPRequest(
        action=action,
        body=body,
        authorization=authorization,
        request=request,
        length_frame=length_frame,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Build an offline Linksys BLE-JNAP handshake transcript. "
            "This command never uses a Bluetooth adapter."
        )
    )
    parser.add_argument("--action", required=True)
    parser.add_argument("--payload-json", default="{}")
    parser.add_argument(
        "--authorization-file",
        type=Path,
        help=(
            "file containing a complete 'Basic ...' value; otherwise use the "
            "Linksys application's unconfigured-device admin/admin value"
        ),
    )
    parser.add_argument(
        "--redact-request",
        action="store_true",
        help="omit the request frame, which contains the authorization value",
    )
    args = parser.parse_args()

    payload = json.loads(args.payload_json)
    if not isinstance(payload, dict):
        parser.error("--payload-json must decode to a JSON object")

    authorization = DEFAULT_SETUP_AUTHORIZATION
    if args.authorization_file:
        authorization = args.authorization_file.read_text().strip()

    request = build_request(args.action, payload, authorization)
    print(
        json.dumps(
            request.transcript(include_request=not args.redact_request),
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
