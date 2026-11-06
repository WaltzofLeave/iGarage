"""Package only the built app, approved assets/services, samples and documentation."""
from __future__ import annotations

import argparse
import ctypes
import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import uuid
import zipfile

from fetch_pyrefly import prepare

ROOT = Path(__file__).resolve().parents[1]


def desktop_folder() -> Path:
    """Resolve the current Windows Desktop, including OneDrive/redirected profiles."""
    class Guid(ctypes.Structure):
        _fields_ = [("data1", ctypes.c_uint32), ("data2", ctypes.c_uint16),
                    ("data3", ctypes.c_uint16), ("data4", ctypes.c_ubyte * 8)]
    desktop_id = Guid.from_buffer_copy(uuid.UUID("B4BFCC3A-DB2C-424C-B029-7FE99A87C641").bytes_le)
    shell = ctypes.WinDLL("shell32")
    shell.SHGetKnownFolderPath.argtypes = [ctypes.POINTER(Guid), ctypes.c_uint32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    shell.SHGetKnownFolderPath.restype = ctypes.c_long
    memory = ctypes.c_void_p()
    result = shell.SHGetKnownFolderPath(ctypes.byref(desktop_id), 0, None, ctypes.byref(memory))
    ole = ctypes.WinDLL("ole32")
    ole.CoTaskMemFree.argtypes = [ctypes.c_void_p]
    try:
        if result != 0 or not memory.value:
            raise OSError(f"Cannot resolve Desktop (HRESULT 0x{result & 0xffffffff:08x})")
        return Path(ctypes.wstring_at(memory)).resolve()
    finally:
        ole.CoTaskMemFree(memory)


def extract_portable(archive_path: Path, parent: Path) -> Path:
    """Deliver into a fresh version directory; existing releases/projects stay intact."""
    parent = parent.resolve()
    with zipfile.ZipFile(archive_path) as archive:
        seen = set()
        entries = []
        total = 0
        for member in archive.infolist():
            if member.orig_filename != member.filename:
                raise ValueError("Archive path was normalized or truncated: " + member.orig_filename)
            name = member.filename.rstrip("/")
            path = PurePosixPath(name)
            if not name or path.is_absolute() or "\\" in name or ":" in name or any(p in {"", ".", ".."} for p in name.split("/")):
                raise ValueError("Unsafe archive path: " + name)
            key = name.casefold()
            if key in seen:
                raise ValueError("Duplicate archive path: " + name)
            seen.add(key)
            total += member.file_size
            if total > 512 * 1024 * 1024:
                raise ValueError("Portable archive exceeds its extraction budget")
            entries.append((member, path))
        if archive.testzip() is not None:
            raise ValueError("Portable archive failed its CRC check")
        binary_hash = hashlib.sha256(archive.read("OrbitNative.exe")).hexdigest()
        name = f"Orbit-Windows-{datetime.date.today():%Y%m%d}-{binary_hash[:8]}"
        parent.mkdir(parents=True, exist_ok=True)
        destination = parent / name
        suffix = 2
        while destination.exists():
            destination = parent / f"{name}-{suffix}"
            suffix += 1
        destination.mkdir()
        destination = destination.resolve()
        if destination.parent != parent:
            raise ValueError("Extraction destination escaped the release directory")
        for member, relative in entries:
            target = destination.joinpath(*relative.parts).resolve()
            if not target.is_relative_to(destination):
                raise ValueError("Archive path escaped the release directory")
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member) as source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
        if hashlib.sha256((destination / "OrbitNative.exe").read_bytes()).hexdigest() != binary_hash:
            raise ValueError("Delivered application checksum mismatch")
    return destination


def package(output: Path) -> dict:
    app = ROOT / "app"
    service = app / "services/pyrefly"
    prepared = prepare(service, offline=True)
    manifest = json.loads((service / "manifest.json").read_text(encoding="utf-8"))
    files = {"OrbitNative.exe": app / "OrbitNative.exe"}
    for name in ["assets", "sample-project"]:
        for path in (app / name).rglob("*"):
            if path.is_file() and "__pycache__" not in path.parts and path.suffix != ".pyc":
                files[path.relative_to(app).as_posix()] = path
    for path in app.glob("*LICENSE.txt"):
        files[path.name] = path
    for name in ["pyrefly.exe", "manifest.json", *manifest["files"]]:
        files["services/pyrefly/" + name] = service / name
    for name in ["docs", "qa"]:
        for path in (ROOT / name).rglob("*"):
            if path.is_file():
                files[path.relative_to(ROOT).as_posix()] = path
    for name in ["README.md", "QA.md"]:
        files[name] = ROOT / name
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, path in sorted(files.items()):
            archive.write(path, name)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None, "Archive CRC failed"
        assert "data/session.json" not in archive.namelist()
        assert not any("fixture" in name for name in archive.namelist())
        assert "sample-project/.gitignore" in archive.namelist()
        assert archive.read("OrbitNative.exe") == (app / "OrbitNative.exe").read_bytes()
        assert hashlib.sha256(archive.read("services/pyrefly/pyrefly.exe")).hexdigest() == manifest["binary_sha256"]
        for name, expected in manifest["files"].items():
            assert hashlib.sha256(archive.read("services/pyrefly/" + name)).hexdigest() == expected, name
        icons = json.loads(archive.read("assets/icons/manifest.json"))
        assert {n for n in archive.namelist() if n.startswith("assets/icons/") and n.endswith(".svg")} == {
            "assets/icons/" + item["file"] for item in icons["assets"]}
        for item in icons["assets"]:
            assert hashlib.sha256(archive.read("assets/icons/" + item["file"])).hexdigest() == item["sha256"]
        for name in ["Apache-2.0.txt", "JetBrains-NOTICE.txt", "Python-logo-NOTICE.txt"]:
            assert archive.read("assets/icons/licenses/" + name)
    return {"archive_bytes": output.stat().st_size,
            "archive_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
            "binary_sha256": hashlib.sha256((app / "OrbitNative.exe").read_bytes()).hexdigest(),
            "language_service": prepared, "approved_icon_files": len(icons["assets"]),
            "checksums_notices_and_archive_integrity": "passed"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/Orbit-Workbench-Windows-x64.zip")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--no-desktop", action="store_true", help="Package only; skip the normal Desktop extraction (CI/verification)")
    args = parser.parse_args()
    result = package(args.output.resolve())
    if os.name == "nt" and not args.no_desktop:
        result["desktop_folder"] = str(extract_portable(args.output.resolve(), desktop_folder() / "Orbit IDE"))
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
