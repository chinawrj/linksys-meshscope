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
import binascii
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
from typing import Any
import uuid


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

BELKIN_MANUFACTURER_PREFIX = b"\x5c\x00"
LINKSYS_MANUFACTURER_PREFIX = b"\xee\x0b"
SETUP_MODE_NAMES = {
    0: "unconfigured-no-limitation",
    1: "configured",
    4: "unconfigured-slave-only",
    8: "unconfigured-master-only",
}
OFFICIAL_APP_SETUP_MODES = frozenset({0, 4, 8})

BLE_AD_TYPE_NAMES = {
    0x01: "flags",
    0x06: "incomplete_128_bit_service_uuids",
    0x07: "complete_128_bit_service_uuids",
    0x08: "shortened_local_name",
    0x09: "complete_local_name",
    0xFF: "manufacturer_specific_data",
}
CONTROL_FRAME_NAMES = {
    JNAP_INIT: "INIT",
    JNAP_START: "START",
    JNAP_STOP: "STOP",
    JNAP_DATA_ACK: "DATA_ACK",
}


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


@dataclass(frozen=True)
class LinksysManufacturerData:
    """Decoded four-byte Linksys/Belkin BLE manufacturer record.

    The Android application calls byte 3 ``modeLimitation``.  Firmware and
    application comments show that value 1 actually means "already
    configured", while 0/4/8 are setup modes accepted by the official app.
    """

    raw: bytes
    company: str
    connectivity_status: int
    setup_mode: int

    def as_dict(self) -> dict[str, Any]:
        mode_name = SETUP_MODE_NAMES.get(self.setup_mode, "unknown")
        return {
            "raw_hex": self.raw.hex(),
            "company": self.company,
            "connectivity_status": self.connectivity_status,
            "setup_mode": self.setup_mode,
            "setup_mode_name": mode_name,
            "configured": self.setup_mode == 1,
            "official_app_3_6_1_accepts": (
                self.setup_mode in OFFICIAL_APP_SETUP_MODES
            ),
        }


def parse_hex_bytes(value: str) -> bytes:
    """Parse hexadecimal bytes with optional spaces, colons, or dashes."""
    compact = re.sub(r"[\s:-]", "", value)
    if not compact or len(compact) % 2 or re.search(r"[^0-9a-fA-F]", compact):
        raise ValueError("hex value must contain complete hexadecimal bytes")
    return bytes.fromhex(compact)


def decode_manufacturer_data(value: bytes) -> LinksysManufacturerData:
    """Decode the four-byte record consumed by Linksys Android 3.6.1."""
    if len(value) != 4:
        raise ValueError("Linksys manufacturer data must be exactly four bytes")
    if value[:2] == BELKIN_MANUFACTURER_PREFIX:
        company = "Belkin"
    elif value[:2] == LINKSYS_MANUFACTURER_PREFIX:
        company = "Linksys"
    else:
        raise ValueError("unknown Linksys/Belkin manufacturer prefix")
    return LinksysManufacturerData(
        raw=value,
        company=company,
        connectivity_status=value[2],
        setup_mode=value[3],
    )


def parse_ble_advertisement(value: bytes) -> list[dict[str, Any]]:
    """Parse an Android raw BLE advertisement into AD structures.

    ``cordova-plugin-bluetoothle`` returns the Android scan record as Base64.
    Each structure begins with a length byte that includes the following type
    byte. A zero length terminates the record.
    """
    records: list[dict[str, Any]] = []
    offset = 0
    while offset < len(value):
        structure_length = value[offset]
        offset += 1
        if structure_length == 0:
            break
        end = offset + structure_length
        if end > len(value):
            raise ValueError("truncated BLE advertisement structure")

        ad_type = value[offset]
        data = value[offset + 1 : end]
        record: dict[str, Any] = {
            "type": ad_type,
            "type_name": BLE_AD_TYPE_NAMES.get(ad_type, "unknown"),
            "data_hex": data.hex(),
        }

        if ad_type in (0x08, 0x09):
            record["text"] = data.decode("utf-8", errors="replace")
        elif ad_type == 0x01 and len(data) == 1:
            record["flags"] = data[0]
        elif ad_type in (0x06, 0x07) and len(data) % 16 == 0:
            record["service_uuids"] = [
                str(uuid.UUID(bytes_le=data[offset : offset + 16]))
                for offset in range(0, len(data), 16)
            ]
        elif ad_type == 0xFF:
            try:
                record["linksys"] = decode_manufacturer_data(data).as_dict()
            except ValueError:
                pass

        records.append(record)
        offset = end
    return records


def decode_android_advertisement_base64(value: str) -> dict[str, Any]:
    """Decode one Base64 scan record returned by the Cordova BLE plugin."""
    try:
        raw = base64.b64decode(value, validate=True)
    except (binascii.Error, ValueError) as error:
        raise ValueError("advertisement must be valid Base64") from error
    return {
        "raw_length": len(raw),
        "raw_hex": raw.hex(),
        "records": parse_ble_advertisement(raw),
    }


def _redact_jnap_authorization(text: str) -> str:
    return re.sub(
        r"(?im)^(X-JNAP-Authorization\s*:)[^\r\n]*",
        r"\1<redacted>",
        text,
    )


def _redact_sensitive_json(value: Any) -> Any:
    sensitive_keys = {
        "adminpassword",
        "authorization",
        "password",
        "passphrase",
        "secret",
        "srppassword",
        "token",
        "username",
    }
    if isinstance(value, dict):
        return {
            key: (
                "<redacted>"
                if key.lower() in sensitive_keys
                else _redact_sensitive_json(child)
            )
            for key, child in value.items()
        }
    if isinstance(value, list):
        return [_redact_sensitive_json(child) for child in value]
    return value


def decode_cordova_value(value: str) -> dict[str, Any]:
    """Decode a Base64 ``value`` field from a Cordova BLE mock recording."""
    try:
        raw = base64.b64decode(value, validate=True)
    except (binascii.Error, ValueError) as error:
        raise ValueError("Cordova BLE value must be valid Base64") from error

    decoded: dict[str, Any] = {
        "byte_length": len(raw),
        "hex": raw.hex(),
    }
    frame_name = CONTROL_FRAME_NAMES.get(raw)
    if frame_name is not None:
        decoded["kind"] = "control_frame"
        decoded["name"] = frame_name
        return decoded

    if raw.startswith(JNAP_REQUEST_PREFIX) and len(raw) == 4:
        decoded["kind"] = "request_length"
        decoded["request_length"] = int.from_bytes(raw[2:], "big")
        return decoded

    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        decoded["kind"] = "binary"
        return decoded

    redacted_text = _redact_jnap_authorization(text)
    decoded.pop("hex")
    decoded["sha256"] = hashlib.sha256(raw).hexdigest()
    try:
        parsed_json = json.loads(text)
    except json.JSONDecodeError:
        decoded["kind"] = (
            "jnap_request"
            if "X-JNAP-Action:" in text
            else "utf8_text"
        )
        decoded["text"] = redacted_text
    else:
        decoded["kind"] = "json"
        decoded["json"] = _redact_sensitive_json(parsed_json)
    return decoded


def decode_cordova_recording(path: Path) -> dict[str, Any]:
    """Decode a Linksys ``MockData.writeResponses`` JSON recording.

    The application writes an array of callback objects to its external data
    directory. Scan callbacks use ``advertisement``; GATT callbacks use
    ``value``. Other fields are retained unchanged.
    """
    try:
        document = json.loads(path.read_text())
    except json.JSONDecodeError as error:
        raise ValueError("Cordova recording must contain valid JSON") from error
    entries = document if isinstance(document, list) else [document]
    if not all(isinstance(entry, dict) for entry in entries):
        raise ValueError("Cordova recording entries must be JSON objects")

    decoded_entries: list[dict[str, Any]] = []
    for entry in entries:
        decoded_entry = {
            key: value
            for key, value in entry.items()
            if key not in {"advertisement", "value"}
        }
        if "advertisement" in entry:
            if not isinstance(entry["advertisement"], str):
                raise ValueError("Cordova advertisement field must be a string")
            decoded_entry["advertisement"] = (
                decode_android_advertisement_base64(entry["advertisement"])
            )
        if "value" in entry:
            if not isinstance(entry["value"], str):
                raise ValueError("Cordova value field must be a string")
            decoded_entry["value"] = decode_cordova_value(entry["value"])
        decoded_entries.append(decoded_entry)

    return {
        "source": path.name,
        "entry_count": len(decoded_entries),
        "entries": decoded_entries,
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
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--action")
    operation.add_argument(
        "--manufacturer-data-hex",
        help=(
            "decode a captured four-byte Linksys/Belkin manufacturer record "
            "without using a Bluetooth adapter"
        ),
    )
    operation.add_argument(
        "--android-advertisement-base64",
        help=(
            "decode one Android Cordova raw scan record from Base64 without "
            "using a Bluetooth adapter"
        ),
    )
    operation.add_argument(
        "--cordova-recording",
        type=Path,
        help=(
            "decode a Linksys MockData startScan/read/subscribe/write JSON "
            "recording created by the official application"
        ),
    )
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

    if args.manufacturer_data_hex is not None:
        try:
            decoded = decode_manufacturer_data(
                parse_hex_bytes(args.manufacturer_data_hex)
            )
        except ValueError as error:
            parser.error(str(error))
        print(json.dumps(decoded.as_dict(), indent=2, sort_keys=True))
        return 0

    if args.android_advertisement_base64 is not None:
        try:
            decoded_advertisement = decode_android_advertisement_base64(
                args.android_advertisement_base64
            )
        except ValueError as error:
            parser.error(str(error))
        print(json.dumps(decoded_advertisement, indent=2, sort_keys=True))
        return 0

    if args.cordova_recording is not None:
        try:
            decoded_recording = decode_cordova_recording(
                args.cordova_recording
            )
        except (OSError, ValueError) as error:
            parser.error(str(error))
        print(json.dumps(decoded_recording, indent=2, sort_keys=True))
        return 0

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
