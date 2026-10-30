#!/usr/bin/env python3
"""Offline, deterministic packaging of a previously validated Windows artifact.

Never downloads a user's PDF. Input is a successful GitHub Actions Portable folder;
output is one ZIP and a SHA256 release manifest consumed by LUNA-Update.ps1.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

def prepare(root: Path, destination: Path, tag: str, source_run: int, commit: str):
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:\.\d+)?", tag):
        raise ValueError("Stable numeric tag required")
    if not root.is_dir() or root.is_symlink():
        raise ValueError("Portable missing")
    info = json.loads((root / "build-info.json").read_text(encoding="utf-8-sig"))
    if int(info["run"]) != source_run or info["commit"] != commit:
        raise ValueError("Portable source build mismatch")
    if info["validation"]["windowsCpp"] != "passed":
        raise ValueError("Portable not Windows-validated")
    exe_digest = hashlib.sha256((root / "LUNA.exe").read_bytes()).hexdigest()
    if exe_digest != info["exeSha256"]:
        raise ValueError("Portable executable checksum mismatch")
    from tools.verify_portable import check
    failures = check(root)
    if failures:
        raise ValueError("Unsafe portable: " + ", ".join(failures))
    paths = sorted((f for f in root.rglob("*") if f.is_file()),
                   key=lambda p: p.relative_to(root).as_posix())
    if any(p.is_symlink() for p in root.rglob("*")):
        raise ValueError("Symlinks not allowed")
    destination.mkdir(parents=True, exist_ok=True)
    zip_path = destination / "LUNA-Windows-Portable.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for file in paths:
            relative = file.relative_to(root).as_posix()
            # Archive paths cannot be absolute or escape the root.
            if relative.startswith("/") or ".." in Path(relative).parts:
                raise ValueError("Unsafe archive entry")
            item = zipfile.ZipInfo(relative, date_time=(2026, 1, 1, 0, 0, 0))
            item.compress_type = zipfile.ZIP_DEFLATED
            item.external_attr = 0o100644 << 16
            archive.writestr(item, file.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    manifest = {"schema": 1, "tag": tag, "source_run": source_run,
                "commit": commit, "zip": zip_path.name,
                "sha256": digest}
    (destination / "LUNA-Release.json").write_text(
        json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
        encoding="utf-8")
    return manifest

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--portable", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--source-run", type=int, required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.portable, args.destination, args.tag,
                             args.source_run, args.commit), sort_keys=True))
