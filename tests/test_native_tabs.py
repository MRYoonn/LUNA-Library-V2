"""Source contract checks only; none claim Windows runtime acceptance."""
from pathlib import Path
import ast
import unittest
from tools.smoke_windows import synthetic_pdf

ROOT = Path(__file__).resolve().parents[1]

class NativeTabsContract(unittest.TestCase):
    def test_synthetic_unique_words_preserve_xref(self):
        original = synthetic_pdf(2)
        for word in (b'alpha', b'bravo'):
            altered = original.replace(b'smoke', word)
            self.assertEqual(len(original), len(altered))
            self.assertIn(word, altered)
            self.assertEqual(original[original.index(b'xref\n'):], altered[altered.index(b'xref\n'):])

    def test_scripts_parse(self):
        for path in ('prepare.py', 'run_windows.py'):
            ast.parse((ROOT / 'experiments/native_tabs' / path).read_text(encoding='utf-8'))

    def test_workflow_remains_manual_and_proof_not_released(self):
        text = (ROOT / '.github/workflows/build-windows.yml').read_text(encoding='utf-8')
        self.assertIn('native_tabs_proof:', text)
        self.assertIn("if: ${{ !inputs.native_tabs_proof && steps.engine-cache.outputs.cache-hit != 'true' }}", text)
        self.assertIn('Upload LUNA Windows portable artifact\n        if: ${{ !inputs.native_tabs_proof }}', text)

    def test_selection_first_highlight_and_freetext_contract(self):
        adapter = (ROOT / 'src/lunareadertoolbar.cpp').read_text(encoding='utf-8')
        smoke = (ROOT / 'src/lunaeditorsmoke.cpp').read_text(encoding='utf-8')
        # pdfdrawspacecontroller.h only forward-declares the compiler.
        # pdfcompiler.h owns the complete type used by getTextLayoutLazy().
        self.assertIn('#include "pdfcompiler.h"', adapter)
        self.assertIn('page->getCropBox().normalized()', adapter)
        self.assertIn('rect.moveLeft(qBound(crop.left()', adapter)
        self.assertIn('rect.moveTop(qBound(crop.top()', adapter)
        for token in (
            'LunaHighlightSelected', 'LunaFreeTextBox',
            'selected->getSelectedText().trimmed().isEmpty()',
            'PDFTextSelectionPainter painter(&selection)',
            'marked.intersects(part)', 'createAnnotationHighlight(',
            'createAnnotationFreeText(', 'QRubberBand::Rectangle',
            'LunaFreeTextFont', 'LunaFreeTextSize',
            'textBox_->setChecked(false)', 'PDFDocumentModifier modifier',
            'isModificationAllowed()', 'c->getDocument() != originalDocument',
            'LunaFreeTextEditDialog', 'setAnnotationContents(annotationRef, nextText)',
            'LunaHighlightColor', 'annotations/highlightColor',
            'setFreeTextAnnotationStyle(annotationRef, style)',
        ):
            self.assertIn(token, adapter, token)
        for token in (
            'Reader phase: selection-first highlight and non-overlap',
            'highlighter duplicated a pre-existing marked region',
            'Reader phase: transparent on-page FreeText note',
            'LUNA default FreeText escaped PDF crop bounds',
            'FreeText note did not participate in native undo',
            'Reader phase: FreeText save and reopen',
            'persistedContentsFound',
            'LUNA FreeText save/reopen changed native annotation counts',
            'Reader phase: FreeText double-click edit',
            'LUNA FreeText double-click edit did not update the annotation',
            'visiblePageRect = visiblePage->rect.intersected(QRectF(widget->rect()))',
            'getPageUnderPoint(boxStart, &localBoxStart)',
            'getPageUnderPoint(boxEnd, &localBoxEnd)',
            'LUNA FreeText edit target offscreen: actual=',
        ):
            self.assertIn(token, smoke, token)
        self.assertIn('add("LunaHighlightSelected")', adapter)
        self.assertIn('add("LunaFreeTextBox")', adapter)
        # The PDF4QT native tools stay intact for backwards-compatible Insert menus.
        self.assertIn('checked("actionCreateTextHighlight")', adapter)
        self.assertIn('checked("actionStickyNoteComment")', adapter)

    def test_highlight_palette_has_four_persistent_presets_without_extra_toolbar_action(self):
        adapter = (ROOT / 'src/lunareadertoolbar.cpp').read_text(encoding='utf-8')
        self.assertIn('LunaHighlightPalette', adapter)
        for label, hex_color in (
            ('노란색', '#FFE049'), ('초록색', '#A3E6A4'),
            ('분홍색', '#FFB3CF'), ('파란색', '#9FCAFF'),
        ):
            self.assertIn('{"' + label + '", "' + hex_color + '"}', adapter)
        self.assertIn('connect(palette, &QMenu::aboutToShow', adapter)
        self.assertIn('palette->addAction(chooseColor_)', adapter)
        smoke = (ROOT / 'src/lunaeditorsmoke.cpp').read_text(encoding='utf-8')
        self.assertIn('Reader phase: persistent four-color highlight palette', smoke)
        self.assertIn('LUNA highlight palette failed to persist chosen colors', smoke)
        self.assertIn('LUNA highlight palette color not written to PDF annotation', smoke)
        self.assertIn('std::abs(components[0] - 163.0 / 255.0)', smoke)
        self.assertIn('for (qsizetype i = 0; i < expectedColors.size(); ++i)', smoke)
        self.assertIn('annotations/highlightColor', adapter)
        self.assertIn('void reloadHighlightColor()', adapter)
        self.assertIn('reloadHighlightColor(); // Other native tabs may have changed', adapter)
        self.assertIn('reloadHighlightColor();\n        auto* proxy', adapter)
        self.assertNotIn('add("LunaHighlightColor")', adapter)

    def test_large_si_open_defers_nonessential_work_until_after_first_render(self):
        library = (ROOT / 'src/lunalibrary.cpp').read_text(encoding='utf-8')
        page_grid = (ROOT / 'src/lunapagegrid.h').read_text(encoding='utf-8')
        for token in (
            'const bool deferForLargePdf = !force && catalog && catalog->getPageCount() >= 80;',
            'delay->start(1200);',
            'if (force) if (auto* pending = delayedCitations_.value(identity, nullptr))',
            'pending->start(0);',
            'delayedCitations_.remove(identity);',
            'bibliography-first-page-worker',
            'bibliography-worker-dispatch',
            'worker->start(QThread::LowPriority)',
            'bibliography_.enrichFromFirstPage(identity, *result)',
        ):
            self.assertIn(token, library, token)
        for token in (
            'if (model() == native.model() && selectionModel() == native.selectionModel()) return;',
            'pendingModelSize_ = true;',
            'if (!isVisible()) return;',
            'if (isVisible()) {',
            'thumbnailSize_(previewHeight);',
            'pendingModelSize_ = false;',
        ):
            self.assertIn(token, page_grid, token)

    def test_public_si_performance_probe_keeps_only_numeric_timings(self):
        probe = (ROOT / 'tools/probe_research_windows.py').read_text(encoding='utf-8')
        ast.parse(probe)
        for token in (
            'LUNA_PERF="1"',
            'capture_output=True, text=True, errors="replace"',
            'PUBLIC_SI_TIMINGS.json',
            '"request-to-first-page-paint"',
            '"bibliography-first-page-worker"',
            '"elapsedMs": elapsed_ms',
            'completed.stderr.splitlines()',
        ):
            self.assertIn(token, probe, token)
        self.assertNotIn('write_text(completed.stderr', probe)
        library = (ROOT / 'src/lunalibrary.cpp').read_text(encoding='utf-8')
        self.assertIn('draw->setProperty("LunaOpenStart", performanceNow());', library)
        self.assertIn('if (qEnvironmentVariableIsSet("LUNA_PERF")) {', library)

    def test_compact_tabs_and_blue_borderless_selection_contract(self):
        tabs = (ROOT / 'src/lunatabs.cpp').read_text(encoding='utf-8')
        library = (ROOT / 'src/lunalibrary.cpp').read_text(encoding='utf-8')
        style = (ROOT / 'src/lunalibrary.qss').read_text(encoding='utf-8')
        overlay = (ROOT / 'experiments/native_tabs/prepare.py').read_text(encoding='utf-8')
        self.assertIn('setCornerWidget(corner, Qt::TopLeftCorner)', tabs)
        self.assertIn('setExpanding(false)', tabs)
        self.assertIn('Qt::ElideMiddle, 156', tabs)
        self.assertIn('row->insertWidget(0, toggle)', library)
        self.assertIn('QTabBar#LunaPdfTabBar::tab', style)
        self.assertIn('painter->fillPath(selectionPath', overlay)
        self.assertIn('PDFTextSelectionPainter selectionPainter(&m_textSelection)', overlay)
        self.assertIn('QEvent::ShortcutOverride || event->type() == QEvent::KeyPress', tabs)
        self.assertIn('ctrlWClosesOnlyActiveTabAndPreservesWindow', tabs)
        self.assertIn('ctrlWLastPdfLeavesEmptyLunaWindow', tabs)

if __name__ == '__main__':
    unittest.main()
