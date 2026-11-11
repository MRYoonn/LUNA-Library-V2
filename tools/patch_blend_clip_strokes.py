#!/usr/bin/env python3
"""Use a stroked path's painted area when Blend2D culls under arbitrary clips.

The centerline of a horizontal/vertical PDF stroke has a zero-area bounding
rectangle. QRectF::intersects rejects it even inside a valid clip. Keep the
existing clip intersection and stroke painting; repair only that early reject.
"""
import argparse
import hashlib
from pathlib import Path

RELATIVE = Path('Pdf4QtLibCore/sources/pdfblpainter.cpp')
PINNED_BLOB = '4aa6f6280ac3fadf344a91d37df0a8ec9c112c85'
MARKER = 'LUNA_CLIPPED_STROKE_COVERAGE_V1'
BEFORE = '''    QPainterPath transformedPath = m_currentTransform.map(path);
    ClipMode clipMode = resolveClipping(transformedPath);
'''
AFTER = BEFORE + '''
    // LUNA_CLIPPED_STROKE_COVERAGE_V1: a zero-width/height centerline is
    // not an empty painted stroke. Before rejecting it under a general clip,
    // test the same stroked outline used by the NeedsResolve branch below.
    // This also covers strokes whose centerline is just outside the clip but
    // whose caps/joins/width overlap it. Never enlarge or bypass the clip.
    if (clipMode == ClipMode::NotVisible && isStrokeActive() && enableStroke &&
        !m_finalClipPath->isEmpty())
    {
        QPainterPathStroker visibilityStroker(m_currentPen);
        const QPainterPath strokeCoverage =
            m_currentTransform.map(visibilityStroker.createStroke(path));
        clipMode = resolveClipping(strokeCoverage);
    }
'''


def patch_bytes(raw):
    normalized = raw.replace(b'\r\n', b'\n')
    blob = b'blob ' + str(len(normalized)).encode() + b'\0' + normalized
    if hashlib.sha1(blob).hexdigest() != PINNED_BLOB:
        raise ValueError('Unexpected pinned Blend2D painter source')
    source = normalized.decode()
    if MARKER in source or source.count(BEFORE) != 1:
        raise ValueError('Missing or ambiguous stroke culling anchor')
    newline = b'\r\n' if b'\r\n' in raw else b'\n'
    return source.replace(BEFORE, AFTER, 1).encode().replace(b'\n', newline)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('pdf4qt_root',type=Path)
    parser.add_argument('--dry-run',action='store_true')
    args = parser.parse_args()
    target = args.pdf4qt_root/RELATIVE
    if target.is_symlink() or not target.is_file(): raise ValueError('Expected regular pinned backend')
    result = patch_bytes(target.read_bytes())
    if not args.dry_run: target.write_bytes(result)
    print('[PASS] Painted stroke coverage before Blend2D clip culling', '(verified)' if args.dry_run else '(applied)')


if __name__=='__main__': main()
