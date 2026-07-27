#!/usr/bin/env python3
"""Assemble an offline MX4200 OEM-style image around a replacement UBI.

This tool does not create the SquashFS or UBI payload and never accesses an
MTD device. It preserves the original FIT/kernel prefix and Linksys footer,
replaces the UBI region, and recalculates the footer's POSIX cksum field.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile


PEB_SIZE = 0x20000
UBI_OFFSET = 0x600000
FOOTER_SIZE = 256
LINKSYS_MAGIC = b".LINKSYS."
CHECKSUM_SLICE = slice(32, 40)
SKU_SLICE = slice(17, 23)


def _crc_table() -> tuple[int, ...]:
    polynomial = 0x04C11DB7
    table = []
    for value in range(256):
        crc = value << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ polynomial) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
        table.append(crc)
    return tuple(table)


CRC_TABLE = _crc_table()


def posix_cksum(data: bytes) -> int:
    """Return the CRC printed by POSIX cksum for *data*."""
    crc = 0
    for value in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ CRC_TABLE[
            ((crc >> 24) ^ value) & 0xFF
        ]

    length = len(data)
    while length:
        value = length & 0xFF
        length >>= 8
        crc = ((crc << 8) & 0xFFFFFFFF) ^ CRC_TABLE[
            ((crc >> 24) ^ value) & 0xFF
        ]
    return (~crc) & 0xFFFFFFFF


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _validate_ubi(ubi: bytes) -> None:
    if not ubi or len(ubi) % PEB_SIZE:
        raise ValueError("replacement UBI size must be a non-zero PEB multiple")
    if len(ubi) < 3 * PEB_SIZE:
        raise ValueError("replacement UBI is too small")
    for offset in range(0, len(ubi), PEB_SIZE):
        if ubi[offset : offset + 4] != b"UBI#":
            raise ValueError(f"missing UBI EC header at PEB offset 0x{offset:X}")


def assemble_image(
    original_path: Path,
    ubi_path: Path,
    output_path: Path,
    expected_original_sha256: str | None = None,
) -> dict[str, object]:
    original = original_path.read_bytes()
    replacement_ubi = ubi_path.read_bytes()

    if len(original) < UBI_OFFSET + 2 * PEB_SIZE + FOOTER_SIZE:
        raise ValueError("input is too small to be an MX4200 OEM image")
    original_sha256 = _sha256(original)
    if (
        expected_original_sha256
        and original_sha256.lower() != expected_original_sha256.lower()
    ):
        raise ValueError(
            "original SHA-256 mismatch: "
            f"expected {expected_original_sha256}, got {original_sha256}"
        )

    footer = bytearray(original[-FOOTER_SIZE:])
    if footer[: len(LINKSYS_MAGIC)] != LINKSYS_MAGIC:
        raise ValueError("input has no Linksys footer at EOF")
    if footer[SKU_SLICE] != b"MX4200":
        raise ValueError(f"unexpected footer SKU: {footer[SKU_SLICE]!r}")
    if original[UBI_OFFSET : UBI_OFFSET + 4] != b"UBI#":
        raise ValueError("input has no UBI EC header at offset 0x600000")

    original_body = original[:-FOOTER_SIZE]
    embedded_checksum = footer[CHECKSUM_SLICE].decode("ascii")
    calculated_checksum = f"{posix_cksum(original_body):08X}"
    if embedded_checksum != calculated_checksum:
        raise ValueError(
            "original Linksys checksum mismatch: "
            f"footer {embedded_checksum}, calculated {calculated_checksum}"
        )

    _validate_ubi(replacement_ubi)
    body = (
        original[:UBI_OFFSET]
        + replacement_ubi
        + (b"\xFF" * (PEB_SIZE - FOOTER_SIZE))
    )
    footer[CHECKSUM_SLICE] = f"{posix_cksum(body):08X}".encode("ascii")
    output = body + footer

    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists():
        raise FileExistsError(f"refusing to overwrite {output_path}")
    with tempfile.NamedTemporaryFile(
        dir=output_path.parent, prefix=f".{output_path.name}.", delete=False
    ) as temp:
        temp_path = Path(temp.name)
        temp.write(output)
        temp.flush()
        os.fsync(temp.fileno())
    try:
        os.replace(temp_path, output_path)
    finally:
        temp_path.unlink(missing_ok=True)

    return {
        "original_sha256": original_sha256,
        "replacement_ubi_sha256": _sha256(replacement_ubi),
        "replacement_ubi_pebs": len(replacement_ubi) // PEB_SIZE,
        "output_sha256": _sha256(output),
        "output_size": len(output),
        "footer_checksum": footer[CHECKSUM_SLICE].decode("ascii"),
        "ubi_offset": UBI_OFFSET,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Assemble an offline MX4200 OEM-style IMG with a replacement UBI; "
            "this tool never flashes a router."
        )
    )
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--ubi", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--expected-original-sha256")
    args = parser.parse_args()

    manifest = assemble_image(
        args.original,
        args.ubi,
        args.output,
        args.expected_original_sha256,
    )
    print(json.dumps(manifest, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
