#!/usr/bin/env python3
"""Compare extracted firmware trees, including filesystem metadata.

Only file content and size may differ for explicitly allowed paths. File type,
mode, owner, group, timestamps, links, device numbers, hardlink relationships,
and extended attributes must remain identical for every path.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import stat
import sys
from typing import Any


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def entry_type(mode: int) -> str:
    if stat.S_ISREG(mode):
        return "file"
    if stat.S_ISDIR(mode):
        return "directory"
    if stat.S_ISLNK(mode):
        return "symlink"
    if stat.S_ISCHR(mode):
        return "character-device"
    if stat.S_ISBLK(mode):
        return "block-device"
    if stat.S_ISFIFO(mode):
        return "fifo"
    if stat.S_ISSOCK(mode):
        return "socket"
    return "unknown"


def read_xattrs(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    try:
        names = os.listxattr(path, follow_symlinks=False)
    except (AttributeError, OSError):
        return result
    for name in sorted(names):
        try:
            value = os.getxattr(path, name, follow_symlinks=False)
        except OSError as exc:
            result[name] = f"<unreadable:{exc.errno}>"
        else:
            result[name] = base64.b64encode(value).decode("ascii")
    return result


def list_paths(root: Path) -> list[Path]:
    paths = [root]
    for directory, dirnames, filenames in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        dirnames.sort()
        filenames.sort()
        paths.extend(directory_path / name for name in dirnames)
        paths.extend(directory_path / name for name in filenames)
    return paths


def scan_tree(root: Path) -> dict[str, dict[str, Any]]:
    root = root.resolve()
    records: dict[str, dict[str, Any]] = {}
    inode_paths: dict[tuple[int, int], list[str]] = {}

    for path in list_paths(root):
        relative = "." if path == root else path.relative_to(root).as_posix()
        metadata = path.lstat()
        kind = entry_type(metadata.st_mode)
        record: dict[str, Any] = {
            "type": kind,
            "mode": f"{stat.S_IMODE(metadata.st_mode):04o}",
            "uid": metadata.st_uid,
            "gid": metadata.st_gid,
            "xattrs": read_xattrs(path),
        }
        # UBI Reader creates the extraction-root directory itself and does not
        # apply the UBIFS root inode's timestamp to that wrapper directory.
        # Every real child path, including all files, is still checked.
        if relative != ".":
            record["mtime_ns"] = metadata.st_mtime_ns
        if kind == "file":
            record["size"] = metadata.st_size
            record["sha256"] = sha256_file(path)
            if metadata.st_nlink > 1:
                inode_paths.setdefault(
                    (metadata.st_dev, metadata.st_ino), []
                ).append(relative)
        elif kind == "symlink":
            record["target"] = os.readlink(path)
        elif kind in {"character-device", "block-device"}:
            record["major"] = os.major(metadata.st_rdev)
            record["minor"] = os.minor(metadata.st_rdev)
        records[relative] = record

    for paths in inode_paths.values():
        group = sorted(paths)
        for relative in group:
            records[relative]["hardlinks"] = group
    return records


def compare_trees(
    baseline_root: Path,
    candidate_root: Path,
    allowed_content: set[str],
) -> dict[str, Any]:
    baseline = scan_tree(baseline_root)
    candidate = scan_tree(candidate_root)
    baseline_paths = set(baseline)
    candidate_paths = set(candidate)
    missing = sorted(baseline_paths - candidate_paths)
    added = sorted(candidate_paths - baseline_paths)
    changes: list[dict[str, Any]] = []

    for relative in sorted(baseline_paths & candidate_paths):
        before = baseline[relative]
        after = candidate[relative]
        fields = sorted(set(before) | set(after))
        for field in fields:
            if before.get(field) == after.get(field):
                continue
            if relative in allowed_content and field in {"size", "sha256"}:
                continue
            changes.append(
                {
                    "path": relative,
                    "field": field,
                    "baseline": before.get(field),
                    "candidate": after.get(field),
                }
            )

    allowed_observed = {
        relative: {
            "baseline_size": baseline[relative].get("size"),
            "candidate_size": candidate[relative].get("size"),
            "baseline_sha256": baseline[relative].get("sha256"),
            "candidate_sha256": candidate[relative].get("sha256"),
        }
        for relative in sorted(allowed_content)
        if relative in baseline and relative in candidate
    }
    return {
        "baseline": str(baseline_root),
        "candidate": str(candidate_root),
        "baseline_entries": len(baseline),
        "candidate_entries": len(candidate),
        "allowed_content_paths": sorted(allowed_content),
        "allowed_content_differences": allowed_observed,
        "missing": missing,
        "added": added,
        "unexpected_changes": changes,
        "ok": not missing and not added and not changes,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument(
        "--allow-content",
        action="append",
        default=[],
        help="Relative path allowed to differ in regular-file size and content",
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    result = compare_trees(
        args.baseline,
        args.candidate,
        {Path(value).as_posix().lstrip("./") for value in args.allow_content},
    )
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    sys.stdout.write(encoded)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
