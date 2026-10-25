#!/usr/bin/env python3
"""Check the Windows release layout; this is not a Windows loader or GUI test."""
from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys

REQUIRED_BINARIES = (
    "LUNA.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
    "Pdf4QtLibCore.dll", "Pdf4QtLibWidgets.dll", "Pdf4QtLibGui.dll",
    "platforms/qwindows.dll", "msvcp140.dll", "vcruntime140.dll",
    "vcruntime140_1.dll",
)
REQUIRED_NOTICES = ("PDF4QT_LICENSE.txt", "LUNA_LICENSE_NOTICE.md")
PRIVATE_SUFFIXES = {
    ".pdf", ".db", ".sqlite", ".sqlite3", ".log", ".pem", ".pfx",
    ".p12", ".key", ".token",
}


def is_x64_pe(path: Path) -> bool:
    """Reject empty files, non-PE placeholders and wrong-architecture binaries."""
    try:
        with path.open("rb") as stream:
            dos = stream.read(64)
            if len(dos) != 64 or dos[:2] != b"MZ":
                return False
            offset = struct.unpack_from("<I", dos, 60)[0]
            if offset < 64 or offset > path.stat().st_size - 26:
                return False
            stream.seek(offset)
            header = stream.read(26)
            return (header[:4] == b"PE\0\0"
                    and struct.unpack_from("<H", header, 4)[0] == 0x8664
                    and struct.unpack_from("<H", header, 24)[0] == 0x20B)
    except (OSError, ValueError, struct.error):
        return False


def check(root: Path) -> list[str]:
    errors = []
    if not root.is_dir() or root.is_symlink():
        return ["Portable directory is missing or is a symlink"]
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            errors.append("Symlink blocked: " + relative)
        elif path.is_file():
            name = path.name.lower()
            if path.suffix.lower() in PRIVATE_SUFFIXES or name == ".env" or name.startswith(".env."):
                errors.append("Private-data file blocked: " + relative)
            if path.suffix.lower() == ".exe" and relative != "LUNA.exe":
                errors.append("Unexpected executable: " + relative)
            if path.suffix.lower() in {".dll", ".exe"} and not is_x64_pe(path):
                errors.append("Not a Windows x64 PE binary: " + relative)
    for name in REQUIRED_BINARIES + REQUIRED_NOTICES:
        path = root / name
        if not path.is_file() or path.is_symlink() or not path.stat().st_size:
            errors.append("Missing or empty required file: " + name)
    notices = root / "third_party_licenses"
    if not notices.is_dir() or not any(p.is_file() and p.stat().st_size for p in notices.rglob("*")):
        errors.append("Missing or empty third-party license directory")
    return errors


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        failures = check(args.directory)
    except OSError as error:
        failures = ["Cannot inspect portable package: " + str(error)]
    for failure in failures:
        print("[BLOCK] " + failure, file=sys.stderr)
    if failures:
        raise SystemExit(1)
    print("[PASS] Windows x64 package layout, Qt platform plugin, runtime and license files")
    print("[NOTICE] DLL import resolution, PDF rendering and GUI behavior require native tests.")
