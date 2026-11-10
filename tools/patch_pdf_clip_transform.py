#!/usr/bin/env python3
"""Fix pinned PDF4QT precompiled-page clipping in logical device coordinates.

Real Windows v301 repro: both ICC Gray and DeviceGray strokes disappeared with
PDF rectangle clipping (W n), but worked without clipping and with CTM alone.
No user PDFs are copied, embedded, or modified.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import sys

RELATIVE = Path("Pdf4QtLibCore/sources/pdfpainter.cpp")
PREREQUISITE_MARKER = "LUNA_THIN_VECTOR_STROKES_V1"
MARKER = "LUNA_DEVICE_SPACE_CLIP_V1"

BEFORE = """            case InstructionType::Clip:
            {
                painter->setClipPath(m_clips[instruction.dataIndex].clipPath, Qt::IntersectClip);
                break;
            }
"""

AFTER = """            case InstructionType::Clip:
            {
                // LUNA_DEVICE_SPACE_CLIP_V1: PDF crop clipping is already in
                // logical device space; subsequent W/W* clips must intersect
                // that same coordinate system. Under Qt, intersecting a path
                // with the current nonidentity world transform erased common
                // Quartz/HWP gray vector strokes inside clipped rectangles.
                // Preserve the original CTM, path, color, alpha and print data.
                const QTransform originalWorld = painter->worldTransform();
                const QPainterPath clipInDeviceSpace =
                    originalWorld.map(m_clips[instruction.dataIndex].clipPath);
                painter->setWorldTransform(QTransform());
                painter->setClipPath(clipInDeviceSpace, Qt::IntersectClip);
                painter->setWorldTransform(originalWorld);
                break;
            }
"""


def patch_text(source: str) -> str:
    if PREREQUISITE_MARKER not in source:
        raise ValueError("Pinned v301 PDF4QT painter patch missing")
    if MARKER in source:
        raise ValueError("Device-space clipping patch already applied")
    if source.count(BEFORE) != 1:
        raise ValueError("PDF4QT clip replay anchor missing or ambiguous")
    return source.replace(BEFORE, AFTER, 1)


def patch_bytes(raw: bytes) -> bytes:
    newline = b"\r\n" if b"\r\n" in raw else b"\n"
    normalized = raw.replace(b"\r\n", b"\n")
    return patch_text(normalized.decode("utf-8")).encode("utf-8").replace(b"\n", newline)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pdf4qt_root", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    path = args.pdf4qt_root.resolve(strict=True) / RELATIVE
    if not path.is_file() or path.is_symlink():
        raise ValueError("Expected regular pinned PDF4QT painter source")
    result = patch_bytes(path.read_bytes())
    if not args.dry_run:
        path.write_bytes(result)
    print("[PASS] LUNA device-space PDF clip patch",
          "(verified)" if args.dry_run else "(applied)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print("[BLOCK]", exc, file=sys.stderr)
        sys.exit(1)
