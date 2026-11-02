#!/usr/bin/env python3
"""Fail-closed LUNA fix for inline superscript/subscript selection in pinned PDF4QT.

Only patches the public MIT-licensed PDFTextLayout engine in the isolated
LUNA Windows build. No user PDF is read or modified. The existing PDF4QT
source must match the pinned upstream Git blob before any write.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import sys

RELATIVE = Path("Pdf4QtLibCore/sources/pdftextlayout.cpp")
PINNED_BLOB_SHA1 = "a50aeb3ca241f41bd54c4d9d49bd5682f1bed2b8"
MARKER = "LUNA_SCRIPT_SELECTION_V1"

# Font ratio / horizontal adjacency / baseline displacement are deliberately
# bounded to avoid joining nearby columns, separate lines or large footnotes.
HELPER = r'''
// LUNA_SCRIPT_SELECTION_V1: superscript/subscript characters retain the
// reading order of their adjacent body word (C(sp3), CF3, ref. 23, affiliations).
// This changes only the text layout graph; glyphs, PDF bytes and drawing stay
// unchanged. Work in the already-rotated character coordinate system.
static bool isLunaInlineScript(const TextCharacter& a, const TextCharacter& b)
{
    if (a.character.isSpace() || b.character.isSpace() ||
        a.fontSize <= 0.0 || b.fontSize <= 0.0)
        return false;
    const TextCharacter& small = a.fontSize <= b.fontSize ? a : b;
    const TextCharacter& base = a.fontSize <= b.fontSize ? b : a;
    const qreal ratio = base.fontSize / small.fontSize;
    if (ratio < 1.20 || ratio > 2.5)
        return false;

    const QRectF smallBox = small.boundingBox.boundingRect();
    const QRectF baseBox = base.boundingBox.boundingRect();
    if (!smallBox.isValid() || !baseBox.isValid())
        return false;

    // A superscript must touch the base word horizontally, regardless of
    // whether it appears above or below the typographic baseline.
    const qreal gap = qMax<qreal>(0.0, qMax(
        smallBox.left() - baseBox.right(), baseBox.left() - smallBox.right()));
    if (gap > qMax<qreal>(1.5, base.fontSize * 0.24))
        return false;
    const qreal baselineOffset = qAbs(small.position.y() - base.position.y());
    if (baselineOffset < base.fontSize * 0.10 ||
        baselineOffset > base.fontSize * 0.72)
        return false;
    if (qAbs(smallBox.center().y() - baseBox.center().y()) >
        base.fontSize * 0.85)
        return false;
    return true;
}

'''
INTRO = "/// Nearest character found by the spatial index of text characters\n"
BEFORE_LINE = r'''            if (info.squaredDistance < maximalDistance * maximalDistance && // 1)
                std::fabs(characters[i].position.y() - characters[info.index].position.y()) < fontSizeMin * m_settings.charactersOnLineSensitivity && // 2)
                fontSizeMax / fontSizeMin < m_settings.fontSensitivity) // 3)
'''
AFTER_LINE = r'''            const bool nativeSameLine =
                info.squaredDistance < maximalDistance * maximalDistance &&
                std::fabs(characters[i].position.y() - characters[info.index].position.y()) <
                    fontSizeMin * m_settings.charactersOnLineSensitivity &&
                fontSizeMax / fontSizeMin < m_settings.fontSensitivity;
            // Adjacent raised/lowered glyphs may be a separate PDF text run.
            // Join them into the same logical line before sorting by X. This
            // preserves Ctrl+C order and the selection/highlighter geometry.
            if (nativeSameLine || isLunaInlineScript(characters[i], characters[info.index]))
'''
BEFORE_SPACE = r'''                    if (!previousCharacter.character.isSpace() && QLineF(previousCharacter.position, currentCharacter.position).length() > previousCharacter.advance * 1.2)
'''
AFTER_SPACE = r'''                    if (!previousCharacter.character.isSpace() &&
                        !isLunaInlineScript(previousCharacter, currentCharacter) &&
                        QLineF(previousCharacter.position, currentCharacter.position).length() >
                            previousCharacter.advance * 1.2)
'''

def replace_one(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise ValueError("Pinned PDFTextLayout semantic anchor missing or ambiguous")
    return source.replace(before, after, 1)

def patch_bytes(original: bytes) -> bytes:
    line_ending = b"\r\n" if b"\r\n" in original else b"\n"
    normalized = original.replace(b"\r\n", b"\n")
    git_blob = b"blob " + str(len(normalized)).encode("ascii") + b"\x00" + normalized
    if hashlib.sha1(git_blob).hexdigest() != PINNED_BLOB_SHA1:
        raise ValueError("Pinned PDF4QT text layout blob SHA mismatch")
    source = normalized.decode("utf-8")
    if MARKER in source:
        raise ValueError("Script selection patch is already present")
    source = replace_one(source, INTRO, HELPER + INTRO)
    source = replace_one(source, BEFORE_LINE, AFTER_LINE)
    source = replace_one(source, BEFORE_SPACE, AFTER_SPACE)
    return source.encode("utf-8").replace(b"\n", line_ending)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pdf4qt_root", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    root = args.pdf4qt_root.resolve(strict=True)
    path = root / RELATIVE
    if not path.is_file() or path.is_symlink():
        raise ValueError("Expected ordinary pinned public PDFTextLayout file")
    output = patch_bytes(path.read_bytes())
    if not args.dry_run:
        path.write_bytes(output)
    print("[PASS] LUNA inline script selection patch",
          "(verified only)" if args.dry_run else "(applied)")

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as e:
        print("[BLOCK]", e, file=sys.stderr)
        sys.exit(1)
