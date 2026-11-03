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
    # v0.9.0.243 exposed a real compatibility bug: its updater refuses the
    # 'Luna-Crash-Tools.ps1' filename. Every future release MUST remain
    # installable by that already-published updater. Embed the helper
    # functions into two allowlisted consumer scripts in the release package,
    # while keeping the original developer Portable and its EXE unchanged.
    helper = root / "Luna-Crash-Tools.ps1"
    if not helper.is_file() or helper.is_symlink():
        raise ValueError("Expected standalone crash helper is missing or unsafe")
    helper_source = helper.read_text(encoding="utf-8-sig")
    standalone_scripts = {"Analyze-Luna-Crash.ps1", "Start-Luna-Crash-Diagnostics.ps1"}
    import_marker = ". (Join-Path $PSScriptRoot 'Luna-Crash-Tools.ps1')"
    if "function Get-LunaCdb" not in helper_source or "function Get-LunaProcDump" not in helper_source:
        raise ValueError("Crash helper does not provide expected functions")
    paths = sorted((f for f in root.rglob("*") if f.is_file() and
                   f.relative_to(root).as_posix() != "Luna-Crash-Tools.ps1"),
                   key=lambda p: p.relative_to(root).as_posix())
    if any(p.is_symlink() for p in root.rglob("*")):
        raise ValueError("Symlinks not allowed")
    destination.mkdir(parents=True, exist_ok=True)
    zip_path = destination / "LUNA-Windows-Portable.zip"
    # The tagged release owns a semantic version, even when the source CI
    # package has only an immutable Actions run number. The EXE is unchanged.
    info["version"] = tag
    manifest_files = []
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for file in paths:
            relative = file.relative_to(root).as_posix()
            if relative.startswith("/") or ".." in Path(relative).parts:
                raise ValueError("Unsafe archive entry")
            if relative == "build-info.json":
                data = (json.dumps(info, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")
            elif relative in standalone_scripts:
                script = file.read_text(encoding="utf-8-sig")
                if script.count(import_marker) != 1:
                    raise ValueError("Crash script source no longer matches the audited helper import")
                # This is ordinary source inlining, not a generated downloader.
                # Production program binaries and all other files remain byte-identical.
                data = script.replace(import_marker, helper_source.rstrip() + "\n").encode("utf-8")
            else:
                data = file.read_bytes()
            item = zipfile.ZipInfo(relative, date_time=(2026, 1, 1, 0, 0, 0))
            item.compress_type = zipfile.ZIP_DEFLATED
            item.external_attr = 0o100644 << 16
            archive.writestr(item, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
            manifest_files.append({"path": relative, "sha256": hashlib.sha256(data).hexdigest(),
                                   "size": len(data)})
    digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    manifest = {"schema": 2, "tag": tag, "source_run": source_run,
                "commit": commit, "zip": zip_path.name,
                "sha256": digest, "files": manifest_files}
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
