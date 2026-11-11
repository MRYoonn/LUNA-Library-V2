#!/usr/bin/env python3
"""Instrument the pinned renderer without changing path/clip/pen behavior."""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[1]
CORE = Path('Pdf4QtLibCore/sources')
MARKER = 'LUNA_PDF_RENDER_TRACE_V1'


def once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Renderer trace anchor missing or ambiguous: ' + before[:90])
    return text.replace(before, after, 1)


def patch_painter(source):
    if MARKER in source or 'LUNA_THIN_VECTOR_STROKES_V1' not in source:
        raise ValueError('Expected untraced v304 painter')
    source = once(source, '#include "pdfpainter.h"', '#include "pdfpainter.h"\n#include "lunarendertrace.h" // '+MARKER)
    source = once(source, '    m_precompiledPage->addPath(qMove(pen), qMove(brush), path, text);', '''    if (luna::RenderTrace::enabled()) luna::RenderTrace::event("compile.path", {
        {"path", luna::RenderTrace::path(path)}, {"pen", luna::RenderTrace::pen(pen)},
        {"stroke", stroke}, {"fill", fill}, {"text", text},
        {"ctm", luna::RenderTrace::matrix(getCurrentWorldMatrix())}});
    m_precompiledPage->addPath(qMove(pen), qMove(brush), path, text);''')
    source = once(source, '    m_precompiledPage->addClip(path);', '''    luna::RenderTrace::operatorPath("compile.clip", path, getCurrentWorldMatrix());
    m_precompiledPage->addClip(path);''')
    source = once(source, '    m_clips.emplace_back(qMove(path));', '''    m_clips.emplace_back(qMove(path));
    if (luna::RenderTrace::enabled()) luna::RenderTrace::event("stored.clip", {
        {"path", luna::RenderTrace::path(m_clips.back().clipPath)}});''')
    source = once(source, '    m_painter->setClipPath(path, Qt::IntersectClip);\n}', '''    if (luna::RenderTrace::enabled()) luna::RenderTrace::event("direct.clip.before", {
        {"path", luna::RenderTrace::path(path)}, {"painter", luna::RenderTrace::painter(m_painter)}});
    m_painter->setClipPath(path, Qt::IntersectClip);
    if (luna::RenderTrace::enabled()) luna::RenderTrace::event("direct.clip.after", {
        {"painter", luna::RenderTrace::painter(m_painter)}});
}''')
    # Restrict replay edits to draw(), not the later redaction/geometry switches.
    start = source.index('void PDFPrecompiledPage::draw(')
    end = source.index('\nvoid PDFPrecompiledPage::', start+1)
    draw = source[start:end]
    draw = once(draw, '\n    painter->save();\n    painter->setWorldTransform(QTransform());', '''
    if (luna::RenderTrace::enabled()) luna::RenderTrace::event("replay.begin", {
        {"cropBox", luna::RenderTrace::rect(cropBox)},
        {"pageMatrix", luna::RenderTrace::matrix(pagePointToDevicePointMatrix)},
        {"instructions", int(m_instructions.size())}, {"paths", int(m_paths.size())},
        {"clips", int(m_clips.size())}, {"painter", luna::RenderTrace::painter(painter)}});
    int lunaTraceDepth = 0;
    painter->save();
    painter->setWorldTransform(QTransform());''')
    draw = once(draw, '        switch (instruction.type)', '''        if (luna::RenderTrace::enabled()) luna::RenderTrace::event("replay.instruction", {
            {"type", int(instruction.type)}, {"index", int(instruction.dataIndex)},
            {"depth", lunaTraceDepth}, {"painter", luna::RenderTrace::painter(painter)}});
        switch (instruction.type)''')
    draw = once(draw, '                painter->drawPath(data.path);', '''                if (luna::RenderTrace::enabled()) luna::RenderTrace::event("replay.stroke", {
                    {"path", luna::RenderTrace::path(data.path)}, {"text", data.isText},
                    {"painter", luna::RenderTrace::painter(painter)}});
                painter->drawPath(data.path);''')
    draw = once(draw, '                painter->setClipPath(m_clips[instruction.dataIndex].clipPath, Qt::IntersectClip);', '''                if (luna::RenderTrace::enabled()) luna::RenderTrace::event("replay.clip.before", {
                    {"path", luna::RenderTrace::path(m_clips[instruction.dataIndex].clipPath)},
                    {"painter", luna::RenderTrace::painter(painter)}});
                painter->setClipPath(m_clips[instruction.dataIndex].clipPath, Qt::IntersectClip);
                if (luna::RenderTrace::enabled()) luna::RenderTrace::event("replay.clip.after", {
                    {"painter", luna::RenderTrace::painter(painter)}});''')
    draw = once(draw, '            case InstructionType::SaveGraphicState:\n            {\n                painter->save();', '            case InstructionType::SaveGraphicState:\n            {\n                ++lunaTraceDepth;\n                painter->save();')
    draw = once(draw, '            case InstructionType::RestoreGraphicState:\n            {\n                painter->restore();', '            case InstructionType::RestoreGraphicState:\n            {\n                --lunaTraceDepth;\n                painter->restore();')
    return source[:start]+draw+source[end:]


def patch_processor(source):
    if MARKER in source:
        raise ValueError('Already traced processor')
    source = once(source, '#include "pdfpagecontentprocessor.h"', '#include "pdfpagecontentprocessor.h"\n#include "lunarendertrace.h" // '+MARKER)
    for function, operator in [('operatorPathStroke','S'), ('operatorPathCloseStroke','s'),
                               ('operatorPathClear','n'), ('operatorClipWinding','W'),
                               ('operatorClipEvenOdd','W*')]:
        anchor = 'void PDFPageContentProcessor::'+function+'()\n{'
        source = once(source, anchor, anchor+'\n    luna::RenderTrace::operatorPath("processor.'+operator+'", m_currentPath, getCurrentWorldMatrix());')
    return source


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('pdf4qt_root',type=Path)
    parser.add_argument('--dry-run',action='store_true')
    args=parser.parse_args()
    patched = {}
    for name, transform in [('pdfpainter.cpp',patch_painter),('pdfpagecontentprocessor.cpp',patch_processor)]:
        target=args.pdf4qt_root/CORE/name
        raw=target.read_bytes(); newline=b'\r\n' if b'\r\n' in raw else b'\n'
        result=transform(raw.replace(b'\r\n',b'\n').decode()).encode().replace(b'\n',newline)
        patched[target] = result
    if not args.dry_run:
        for target, result in patched.items(): target.write_bytes(result)
        (args.pdf4qt_root/CORE/'lunarendertrace.h').write_bytes((ROOT/'src/lunarendertrace.h').read_bytes())
    print('[PASS] Explicit opt-in PDF operator/compiler/replay trace; rendering semantics unchanged')


if __name__=='__main__': main()
