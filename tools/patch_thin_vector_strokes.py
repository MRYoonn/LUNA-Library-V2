#!/usr/bin/env python3
"""LUNA screen-only PDF4QT patch for subpixel-stroke visibility.

Modifies only an immutable, SHA-pinned upstream PDF4QT checkout during the
Windows build. No user PDF is read, rewritten, uploaded or committed.
The screen renderer gets a minimum one-device-pixel stroke for non-text vector
paths; PDF export/printing continue using the original PDF pen width.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

RELATIVE = Path("Pdf4QtLibCore/sources/pdfpainter.cpp")
PINNED_BLOB_SHA1 = "0cc07fc8cf5f5c60cbd9cbc075bc7600fa4d1a39"
MARKER = "LUNA_THIN_VECTOR_STROKES_V1"
BEFORE = """                painter->setPen(data.pen);
                painter->setBrush(data.brush);
                painter->drawPath(data.path);
"""
AFTER = """                // LUNA_THIN_VECTOR_STROKES_V1: PDF grids drawn with 0.36 pt
                // strokes can rasterize below one device pixel in the on-screen
                // QWidget painter. Keep their actual geometry, stroke color,
                // dashes and alpha; do not touch text or printed/PDF output.
                QPen screenPen = data.pen;
                if (!data.isText && screenPen.style() != Qt::NoPen &&
                    screenPen.widthF() > 0.0 && painter->device() &&
                    painter->device()->devType() == QInternal::Widget)
                {
                    const QTransform transform = painter->worldTransform();
                    const qreal xScale = std::hypot(transform.m11(), transform.m12());
                    const qreal yScale = std::hypot(transform.m21(), transform.m22());
                    const qreal strokePixels = screenPen.widthF() * std::min(xScale, yScale);
                    if (strokePixels > 0.0 && strokePixels < 1.0)
                    {
                        screenPen.setCosmetic(true);
                        screenPen.setWidthF(1.0);
                    }
                }
                painter->setPen(screenPen);
                painter->setBrush(data.brush);
                painter->drawPath(data.path);
"""
INCLUDE_BEFORE = '#include <QPainter>\n'
INCLUDE_AFTER = '#include <QPainter>\n#include <algorithm>\n#include <cmath>\n'


def patch_text(source: str) -> str:
    if MARKER in source:
        raise ValueError("Thin-line patch already present")
    if source.count(INCLUDE_BEFORE) != 1 or source.count(BEFORE) != 1:
        raise ValueError("Pinned PDF4QT drawing anchors missing or ambiguous")
    return source.replace(INCLUDE_BEFORE, INCLUDE_AFTER, 1).replace(BEFORE, AFTER, 1)


def patch_bytes(original: bytes) -> bytes:
    newline = b"\r\n" if b"\r\n" in original else b"\n"
    normalized = original.replace(b"\r\n", b"\n")
    blob = b"blob " + str(len(normalized)).encode("ascii") + b"\x00" + normalized
    if hashlib.sha1(blob).hexdigest() != PINNED_BLOB_SHA1:
        raise ValueError("Unrecognized PDF4QT pdfpainter.cpp blob")
    return patch_text(normalized.decode("utf-8")).encode("utf-8").replace(b"\n", newline)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pdf4qt_root", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    root = args.pdf4qt_root.resolve(strict=True)
    target = root / RELATIVE
    if not target.is_file() or target.is_symlink():
        raise ValueError("Pinned PDF4QT painter source must be a regular file")
    patched = patch_bytes(target.read_bytes())
    if not args.dry_run:
        target.write_bytes(patched)
    print("[PASS] LUNA thin-vector-stroke display compatibility",
          "(verified)" if args.dry_run else "(applied)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print("[BLOCK]", exc, file=sys.stderr)
        sys.exit(1)
