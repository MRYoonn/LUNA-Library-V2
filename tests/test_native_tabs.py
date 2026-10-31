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
            'setFreeTextAnnotationStyle(annotationRef, style)',
        ):
            self.assertIn(token, adapter, token)
        for token in (
            'Reader phase: selection-first highlight and non-overlap',
            'highlighter duplicated a pre-existing marked region',
            'Reader phase: transparent on-page FreeText note',
            'FreeText note did not participate in native undo',
            'Reader phase: FreeText save and reopen',
            'persistedContentsFound',
            'LUNA FreeText save/reopen changed native annotation counts',
        ):
            self.assertIn(token, smoke, token)
        self.assertIn('add("LunaHighlightSelected")', adapter)
        self.assertIn('add("LunaFreeTextBox")', adapter)
        # The PDF4QT native tools stay intact for backwards-compatible Insert menus.
        self.assertIn('checked("actionCreateTextHighlight")', adapter)
        self.assertIn('checked("actionStickyNoteComment")', adapter)

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
