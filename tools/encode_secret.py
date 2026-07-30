#!/usr/bin/env python3
"""Prompt for a secret and print its UTF-8 base64 representation."""

from __future__ import annotations

import base64
import getpass


def encode_secret(value: str) -> str:
    return base64.b64encode(value.encode("utf-8")).decode("ascii")


def main() -> None:
    value = getpass.getpass("Linksys local password: ")
    if not value:
        raise SystemExit("Password cannot be empty.")
    print(encode_secret(value))


if __name__ == "__main__":
    main()
