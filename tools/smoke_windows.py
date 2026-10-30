#!/usr/bin/env python3
"""Native packaged UI and synthetic Editor annotation save/reopen smoke test."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import shutil
import struct
import sys
import tempfile
import json


def synthetic_pdf(page_count: int = 1, *, bibliography_author: str | None = None) -> bytes:
    """Public synthetic text with a valid cross-reference table (one to three pages)."""
    if page_count not in (1, 2, 3):
        raise ValueError("Synthetic fixture supports one to three pages")
    content = b"BT /F1 18 Tf 40 160 Td (LUNA synthetic smoke test) Tj ET\n"
    if bibliography_author:
        author = bibliography_author.encode("ascii")
        content += (b"BT /F1 8 Tf 40 125 Td (Chem. Sci. 2026, 17, 247) Tj ET\n"
                    b"BT /F1 8 Tf 40 110 Td (Author One, Author Two) Tj ET\n"
                    b"BT /F1 8 Tf 40 95 Td (and " + author + b") Tj ET\n")
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 220] "
        b"/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        b"<< /Length " + str(len(content)).encode() + b" >>\nstream\n" + content + b"endstream",
    ]
    if page_count >= 2:
        objects[1] = b"<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 2 >>"
        second = b"BT /F1 18 Tf 40 160 Td (Second synthetic page) Tj ET\n"
        objects.extend([
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 220] "
            b"/Resources << /Font << /F1 4 0 R >> >> /Contents 7 0 R >>",
            b"<< /Length " + str(len(second)).encode() + b" >>\nstream\n" + second + b"endstream",
        ])
    if page_count == 3:
        objects[1] = b"<< /Type /Pages /Kids [3 0 R 6 0 R 8 0 R] /Count 3 >>"
        third = b"BT /F1 18 Tf 40 160 Td (Third synthetic page) Tj ET\n"
        objects.extend([
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 220] "
            b"/Resources << /Font << /F1 4 0 R >> >> /Contents 9 0 R >>",
            b"<< /Length " + str(len(third)).encode() + b" >>\nstream\n" + third + b"endstream",
        ])
    info_reference = 0
    if bibliography_author:
        objects.append(b"<< /Author (Hao Fang) >>")
        info_reference = len(objects)
    data = bytearray(b"%PDF-1.4\n")
    offsets = []
    for number, obj in enumerate(objects, 1):
        offsets.append(len(data))
        data.extend(f"{number} 0 obj\n".encode() + obj + b"\nendobj\n")
    xref = len(data)
    data.extend(f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode())
    for offset in offsets:
        data.extend(f"{offset:010d} 00000 n \n".encode())
    info = f" /Info {info_reference} 0 R" if info_reference else ""
    data.extend(f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R{info} >>\nstartxref\n{xref}\n%%EOF\n".encode())
    return bytes(data)


def run(package: Path, screenshots_directory: Path | None = None,
        *, layout_only: bool = False, scale: float = 1.0) -> None:
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
    child_env["QT_FORCE_STDERR_LOGGING"] = "1"
    child_env["QT_SCALE_FACTOR"] = str(scale)
    # Qt otherwise may show --version/errors in a blocking GUI message box.
    child_env["QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES"] = "1"
    with tempfile.TemporaryDirectory(prefix="luna-smoke-") as temporary:
        folder = Path(temporary)
        library = folder / "library"
        library.mkdir()
        (library / "Synthetic examples").mkdir()
        pdf = library / "synthetic.pdf"
        report = folder / "result.txt"
        pdf.write_bytes(synthetic_pdf(2))
        subprocess.run([str(executable), "--version"], cwd=folder, env=child_env,
                       check=True, timeout=20)
        try:
            subprocess.run([str(executable), "--luna-smoke-report", str(report),
                            *(["--luna-layout-only"] if layout_only else []), str(pdf)],
                           cwd=folder, env=child_env, check=True, timeout=45)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            detail = report.read_text(encoding="utf-8")[:512] if report.is_file() else "No native diagnostic"
            failure_image = Path(str(report) + ".failure.png")
            if screenshots_directory is not None and failure_image.is_file():
                screenshots_directory.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(failure_image, screenshots_directory / "luna-failure.png")
            if screenshots_directory is not None:
                # Preserve actual earlier captures when a later gate fails.
                # Their presence never changes the failing process status.
                screenshots_directory.mkdir(parents=True, exist_ok=True)
                for suffix, name in ((".memo.png", "luna-paper-memo.png"),
                                     (".pages.png", "luna-page-grid.png"),
                                     (".hover.png", "luna-thumbnail-hover.png"),
                                     (".desktop.png", "luna-windows-desktop.png")):
                    capture = Path(str(report) + suffix)
                    if capture.is_file():
                        shutil.copyfile(capture, screenshots_directory / name)
            status = f"exit {error.returncode}" if isinstance(error, subprocess.CalledProcessError) else "timed out"
            raise RuntimeError(f"Native smoke {status}: {detail}") from error
        memo_capture = Path(str(report) + ".memo.png")
        if not layout_only and screenshots_directory is not None and memo_capture.is_file():
            screenshots_directory.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(memo_capture, screenshots_directory / "luna-paper-memo.png")
        marker = b"LUNA_SMOKE_LAYOUT_OK\n" if layout_only else b"LUNA_SMOKE_EDITOR_ROUNDTRIP_OK\n"
        if not report.is_file() or report.read_bytes() != marker:
            raise RuntimeError("Native window/PDF-load success marker missing")
        if pdf.read_bytes() != synthetic_pdf(2):
            raise RuntimeError("Native annotation test modified the original PDF")
        annotated = Path(str(report) + ".annotated.pdf")
        if not layout_only and (not annotated.is_file() or not annotated.read_bytes().startswith(b"%PDF-")):
            raise RuntimeError("Native annotation test did not save a PDF copy")
        for source_name, target_name, dimensions in ((str(report) + ".png", "luna-1366.png", (1366, 768)),
                                                     (str(report) + ".wide.png", "luna-1920.png", (1920, 1080))):
            image = Path(source_name)
            if not image.is_file() or not image.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError("Native UI screenshot missing or invalid")
            with image.open("rb") as stream:
                header = stream.read(24)
            if len(header) != 24 or header[12:16] != b"IHDR":
                raise RuntimeError("Native UI PNG header invalid")
            actual = struct.unpack(">II", header[16:24])
            tolerance = 1 if layout_only else 0  # Integer logical dimensions round at fractional DPR.
            if any(abs(a - b) > tolerance for a, b in zip(actual, dimensions)):
                raise RuntimeError(f"Qt layout capture dimensions: expected {dimensions}, got {actual}")
            if screenshots_directory is not None:
                screenshots_directory.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(image, screenshots_directory / target_name)
        if not layout_only:
            pages = Path(str(report) + ".pages.png")
            if not pages.is_file() or not pages.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError("Native two-column PDF page preview capture missing")
            if screenshots_directory is not None:
                screenshots_directory.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(pages, screenshots_directory / "luna-page-grid.png")
            hover = Path(str(report) + ".hover.png")
            if not hover.is_file() or not hover.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError("Native thumbnail hover capture missing")
            if screenshots_directory is not None:
                screenshots_directory.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(hover, screenshots_directory / "luna-thumbnail-hover.png")
            desktop = Path(str(report) + ".desktop.png")
            if not desktop.is_file() or not desktop.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError("Actual Windows shell icon review capture missing")
            if screenshots_directory is not None:
                shutil.copyfile(desktop, screenshots_directory / "luna-windows-desktop.png")
        metadata = Path(str(report) + ".layout.json")
        if layout_only:
            if not metadata.is_file():
                raise RuntimeError("Scaled native layout metadata missing")
            layout = json.loads(metadata.read_text(encoding="utf-8"))
            if abs(layout.get("scale", 0) - scale) > 0.02:
                raise RuntimeError("Native Qt device pixel ratio does not match requested scale")
            if not 40 <= layout.get("toolbarHeight", 0) <= 44:
                raise RuntimeError("Scaled native reading toolbar is not a compact row")
        if metadata.is_file() and screenshots_directory is not None:
            shutil.copyfile(metadata, screenshots_directory / "layout.json")
    if layout_only:
        print(f"[PASS] Native Windows Qt scale {scale:g}: responsive toolbar, panel collapse and captures")
        print("[NOTICE] Per-process Qt scaling; physical monitor / OS per-monitor DPI changes remain separate checks.")
        return
    print("[PASS] Native library search, favorites, recent history, panel visibility and PDF reopen")
    print("[PASS] Native viewport crop/cancel, atomic PNG sidecar, asynchronous hover, 165ms fade and screen bounds")
    print("[PASS] Continuous layout at 200% and exact 1366x768 / 1920x1080 Qt layout captures")
    print("[PASS] Native text selection, highlight drag, sticky note, Save As and annotation disk round trip")
    print("[PASS] Original synthetic PDF bytes preserved; note contents and highlight color survive reopen")
    print("[PASS] Compact toolbar, page/zoom controls, Ctrl+O/F/K/S, drag-copy, native color chooser, note edit and annotation-only deletion")
    print("[PASS] Unsaved window-close confirmation, advanced view menus and default text-selection return")
    print("[NOTICE] Physical printing and OS per-monitor DPI changes remain separate checks.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--screenshots-directory", type=Path)
    parser.add_argument("--layout-only", action="store_true")
    parser.add_argument("--scale", type=float, choices=(1.0, 1.25, 1.5), default=1.0)
    args = parser.parse_args()
    try:
        run(args.directory, args.screenshots_directory, layout_only=args.layout_only, scale=args.scale)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("[BLOCK] " + str(error), file=sys.stderr)
        raise SystemExit(1)
