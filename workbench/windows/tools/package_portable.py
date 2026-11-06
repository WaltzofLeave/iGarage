"""Package only the built app, approved assets/services, samples and documentation."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import zipfile

from fetch_pyrefly import prepare

ROOT = Path(__file__).resolve().parents[1]


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
    args = parser.parse_args()
    result = package(args.output.resolve())
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
