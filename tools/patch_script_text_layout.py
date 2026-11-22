#!/usr/bin/env python3
"""LUNA fixes for inline scripts and multi-page selection in pinned PDF4QT.

Only patches public MIT-licensed engine and widget code in the isolated
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
TOOL_SOURCE = Path("Pdf4QtLibWidgets/sources/pdfwidgettool.cpp")
TOOL_BLOB_SHA1 = "9afaf99909f662918c17621540eee0f9019aba65"
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

    // Reject vertically stacked glyphs (adjacent lines, table cells or
    // equation rows) whose horizontal extents substantially coincide.
    // Inline scripts may overlap a base glyph's edge through kerning.
    const qreal overlap = qMax<qreal>(0.0,
        qMin(smallBox.right(), baseBox.right()) -
        qMax(smallBox.left(), baseBox.left()));
    if (overlap > smallBox.width() * 0.80 &&
        qAbs(small.position.x() - base.position.x()) < smallBox.width() * 0.35)
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

TOOL_ANCHOR = "void PDFSelectTextTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)\n"
PAGE_RANGE_HELPER = r'''// A drag across pages has two page-local endpoints. Build the same per-block
// ranges used by Select All, trimming only the first and last pages.
static PDFCharacterPointer findDragEndpoint(const PDFTextBlocks& blocks, PDFInteger pageIndex, QPointF target)
{
    PDFCharacterPointer best;
    qreal bestDistance = std::numeric_limits<qreal>::infinity();
    for (size_t blockIndex = 0; blockIndex < blocks.size(); ++blockIndex)
    {
        const auto& lines = blocks[blockIndex].getLines();
        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            const auto& characters = lines[lineIndex].getCharacters();
            for (size_t characterIndex = 0; characterIndex < characters.size(); ++characterIndex)
            {
                const QPointF delta = characters[characterIndex].boundingBox.boundingRect().center() - target;
                const qreal distance = QPointF::dotProduct(delta, delta);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = { pageIndex, blockIndex, lineIndex, characterIndex };
                }
            }
        }
    }
    return best;
}

static PDFTextSelection selectDragAcrossPages(PDFAsynchronousTextLayoutCompiler* compiler,
                                              PDFInteger firstPage, QPointF firstPoint,
                                              PDFInteger lastPage, QPointF lastPoint)
{
    if (firstPage > lastPage)
    {
        std::swap(firstPage, lastPage);
        std::swap(firstPoint, lastPoint);
    }

    PDFTextSelection selection;
    PDFTextSelectionItems ranges;
    for (PDFInteger page = firstPage; page <= lastPage; ++page)
    {
        const PDFTextLayout layout = compiler->getTextLayoutLazy(page);
        const auto& blocks = layout.getTextBlocks();
        const auto start = page == firstPage ? findDragEndpoint(blocks, page, firstPoint) : PDFCharacterPointer();
        const auto end = page == lastPage ? findDragEndpoint(blocks, page, lastPoint) : PDFCharacterPointer();
        const size_t firstBlock = start.isValid() ? start.blockIndex : 0;
        const size_t pastLastBlock = end.isValid() ? end.blockIndex + 1 : blocks.size();
        for (size_t block = firstBlock; block < pastLastBlock; ++block)
        {
            const auto& lines = blocks[block].getLines();
            if (lines.empty() || lines.front().getCharacters().empty() || lines.back().getCharacters().empty())
                continue;
            const PDFCharacterPointer blockStart{ page, block, 0, 0 };
            const PDFCharacterPointer blockEnd{ page, block, lines.size() - 1, lines.back().getCharacters().size() - 1 };
            ranges.emplace_back(start.isValid() && block == start.blockIndex ? start : blockStart,
                                end.isValid() && block == end.blockIndex ? end : blockEnd);
        }
    }
    selection.addItems(ranges, Qt::yellow);
    selection.build();
    return selection;
}

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

def patch_tool_bytes(original: bytes) -> bytes:
    line_ending = b"\r\n" if b"\r\n" in original else b"\n"
    normalized = original.replace(b"\r\n", b"\n")
    git_blob = b"blob " + str(len(normalized)).encode("ascii") + b"\x00" + normalized
    if hashlib.sha1(git_blob).hexdigest() != TOOL_BLOB_SHA1:
        raise ValueError("Pinned PDF4QT text selection tool blob SHA mismatch")
    source = normalized.decode("utf-8")
    source = replace_one(source, "#include <algorithm>\n", "#include <algorithm>\n#include <limits>\n")
    source = replace_one(source, TOOL_ANCHOR, PAGE_RANGE_HELPER + TOOL_ANCHOR)
    release_start = source.index(TOOL_ANCHOR)
    move_start = source.index("void PDFSelectTextTool::mouseMoveEvent", release_start)
    move_end = source.index("void PDFSelectTextTool::setActiveImpl", move_start)
    release = source[release_start:move_start]
    move = source[move_start:move_end]
    release = replace_one(release, """            else
            {
                setSelection(pdf::PDFTextSelection());
            }
""", """            else if (pageIndex >= 0)
            {
                setSelection(selectDragAcrossPages(getProxy()->getTextLayoutCompiler(),
                    m_selectionInfo.pageIndex, m_selectionInfo.selectionStartPoint, pageIndex, pagePoint));
            }
""")
    move = replace_one(move, """        else
        {
            setSelection(pdf::PDFTextSelection());
        }
""", """        else if (pageIndex >= 0)
        {
            setSelection(selectDragAcrossPages(getProxy()->getTextLayoutCompiler(),
                m_selectionInfo.pageIndex, m_selectionInfo.selectionStartPoint, pageIndex, pagePoint));
        }
""")
    source = source[:release_start] + release + move + source[move_end:]
    return source.encode("utf-8").replace(b"\n", line_ending)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pdf4qt_root", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    root = args.pdf4qt_root.resolve(strict=True)
    outputs = {}
    for relative, patch in ((RELATIVE, patch_bytes), (TOOL_SOURCE, patch_tool_bytes)):
        path = root / relative
        if not path.is_file() or path.is_symlink():
            raise ValueError("Expected ordinary pinned PDF4QT text selection file")
        outputs[path] = patch(path.read_bytes())
    if not args.dry_run:
        for path, output in outputs.items():
            path.write_bytes(output)
    print("[PASS] LUNA inline-script and multi-page selection patches",
          "(verified only)" if args.dry_run else "(applied)")

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as e:
        print("[BLOCK]", e, file=sys.stderr)
        sys.exit(1)
