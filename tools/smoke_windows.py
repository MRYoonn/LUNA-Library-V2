#!/usr/bin/env python3
"""Native packaged-window and PDF-load smoke test; not a visual/annotation test."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile


def synthetic_pdf() -> bytes:
    """A single page with public synthetic text and a valid cross-reference table."""
    content = b"BT /F1 18 Tf 40 160 Td (LUNA synthetic smoke test) Tj ET\n"
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 220] "
        b"/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        b"<< /Length " + str(len(content)).encode() + b" >>\nstream\n" + content + b"endstream",
    ]
    data = bytearray(b"%PDF-1.4\n")
    offsets = []
    for number, obj in enumerate(objects, 1):
        offsets.append(len(data))
        data.extend(f"{number} 0 obj\n".encode() + obj + b"\nendobj\n")
    xref = len(data)
    data.extend(b"xref\n0 6\n0000000000 65535 f \n")
    for offset in offsets:
        data.extend(f"{offset:010d} 00000 n \n".encode())
    data.extend(f"trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode())
    return bytes(data)


def run(package: Path, screenshots_directory: Path | None = None) -> None:
    if sys.platform != "win32":
        raise RuntimeError("Requires native Windows; Linux execution does not validate LUNA.exe")
    package = package.resolve(strict=True)
    executable = package / "LUNA.exe"
    child_env = os.environ.copy()
    # Avoid resolving DLLs or Qt plugins from the build machine's development Qt.
    windows = Path(os.environ["SystemRoot"])
    child_env["PATH"] = os.pathsep.join(map(str, (package, windows / "System32", windows)))
    child_env["QT_PLUGIN_PATH"] = str(package)
    child_env["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(package / "platforms")
    child_env["QT_QPA_PLATFORM"] = "windows"
    # Qt otherwise may show --version/errors in a blocking GUI message box.
    child_env["QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES"] = "1"
    with tempfile.TemporaryDirectory(prefix="luna-smoke-") as temporary:
        folder = Path(temporary)
        library = folder / "library"
        library.mkdir()
        (library / "Synthetic examples").mkdir()
        pdf = library / "synthetic.pdf"
        report = folder / "result.txt"
        pdf.write_bytes(synthetic_pdf())
        subprocess.run([str(executable), "--version"], cwd=folder, env=child_env,
                       check=True, timeout=20)
        subprocess.run([str(executable), "--luna-smoke-report", str(report), str(pdf)],
                       cwd=folder, env=child_env, check=True, timeout=45)
        if not report.is_file() or report.read_bytes() != b"LUNA_SMOKE_PDF_OPEN_OK\n":
            raise RuntimeError("Native window/PDF-load success marker missing")
        for source_name, target_name in ((str(report) + ".png", "luna-1366.png"),
                                         (str(report) + ".wide.png", "luna-1920.png")):
            image = Path(source_name)
            if not image.is_file() or not image.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError("Native UI screenshot missing or invalid")
            if screenshots_directory is not None:
                screenshots_directory.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(image, screenshots_directory / target_name)
    print("[PASS] Native library search, favorites, recent history, panel visibility and PDF reopen")
    print("[PASS] Continuous layout at 200% and native screenshots at two window sizes")
    print("[NOTICE] Human visual review, text selection, annotation persistence and OS DPI remain unverified.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--screenshots-directory", type=Path)
    args = parser.parse_args()
    try:
        run(args.directory, args.screenshots_directory)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("[BLOCK] " + str(error), file=sys.stderr)
        raise SystemExit(1)
