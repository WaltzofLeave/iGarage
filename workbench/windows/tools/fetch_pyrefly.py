"""Prepare the pinned, unmodified native language server and its redistribution notices.

First build downloads a checksum-verified upstream archive; subsequent builds work
offline. No package installation, PATH modification, or runtime download occurs.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "vendor/pyrefly"
NOTICE_FILES = {"LICENSE.txt", "THIRD-PARTY-LICENSES.txt", "dependency-inventory.json"}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def replace(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as target:
            target.write(content)
        os.replace(name, path)
    finally:
        if Path(name).exists():
            Path(name).unlink()


def prepare(destination: Path, offline: bool) -> dict:
    manifest = json.loads((VENDOR / "manifest.json").read_text(encoding="utf-8"))
    if set(manifest["files"]) != NOTICE_FILES:
        raise ValueError("Unexpected redistribution notice inventory")
    for name, expected in manifest["files"].items():
        if digest((VENDOR / name).read_bytes()) != expected:
            raise ValueError("Redistribution notice checksum mismatch: " + name)
    inventory = json.loads((VENDOR / "dependency-inventory.json").read_text(encoding="utf-8"))
    approved = {"MIT", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause", "0BSD", "BSL-1.0",
                "CC0-1.0", "ISC", "MPL-2.0", "Unicode-3.0", "Unicode-DFS-2016", "Unlicense", "Zlib"}
    # The reviewed inventory records one selected grant (and required conjuncts),
    # so no precedence-sensitive parsing of upstream OR expressions is needed.
    for item in inventory["packages"]:
        selected = item.get("selected_license", "")
        if not selected or not all(part in approved or part == "Apache-2.0 WITH LLVM-exception" for part in selected.split(" AND ")):
            raise ValueError("Unreviewed dependency license: " + item["name"] + ": " + item["license"])
    executable = destination / "pyrefly.exe"
    if not executable.exists() or digest(executable.read_bytes()) != manifest["binary_sha256"]:
        cached = ROOT / "build/downloads" / ("pyrefly-" + manifest["version"] + "-windows-x86_64.zip")
        if not cached.exists() or digest(cached.read_bytes()) != manifest["archive_sha256"]:
            if offline:
                raise FileNotFoundError("Verified Pyrefly binary/archive missing. Run this script once with network access.")
            request = urllib.request.Request(manifest["url"], headers={"User-Agent": "Orbit-native-build"})
            with urllib.request.urlopen(request, timeout=90) as response:
                archive = response.read(64 * 1024 * 1024 + 1)
            if len(archive) > 64 * 1024 * 1024 or digest(archive) != manifest["archive_sha256"]:
                raise ValueError("Pyrefly download checksum mismatch")
            replace(cached, archive)
        with zipfile.ZipFile(io.BytesIO(cached.read_bytes())) as archive:
            entry = archive.getinfo("pyrefly.exe")
            if entry.file_size != manifest["binary_bytes"]:
                raise ValueError("Unexpected language-server binary size")
            binary = archive.read(entry)
        if digest(binary) != manifest["binary_sha256"]:
            raise ValueError("Pyrefly executable checksum mismatch")
        replace(executable, binary)
    destination.mkdir(parents=True, exist_ok=True)
    for name in sorted(NOTICE_FILES | {"manifest.json"}):
        shutil.copyfile(VENDOR / name, destination / name)
    return {"version": manifest["version"], "sha256": manifest["binary_sha256"],
            "license_packages_reviewed": len(inventory["packages"]), "notices": "verified"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true", help="Require an existing verified binary or cached archive")
    parser.add_argument("--destination", type=Path, default=ROOT / "app/services/pyrefly")
    args = parser.parse_args()
    print(json.dumps(prepare(args.destination.resolve(), args.offline), indent=2))
