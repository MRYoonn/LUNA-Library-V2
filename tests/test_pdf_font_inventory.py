"""Adobe-style PDF font inventory reaches PDF4QT's native document-font parser.

All test PDFs are public, synthetic fixtures. Font program extraction is not
implied or implemented: this feature is an embedded-font information viewer.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLBAR = (ROOT / "src/lunareadertoolbar.cpp").read_text(encoding="utf-8")
TABS = (ROOT / "src/lunatabs.cpp").read_text(encoding="utf-8")
PROBE = (ROOT / "tools/probe_tabs_integration_windows.py").read_text(encoding="utf-8")
FIXTURE = (ROOT / "tools/smoke_windows.py").read_text(encoding="utf-8")


class NativePdfFontInventoryTests(unittest.TestCase):
    def test_file_menu_action_opens_native_properties_in_fonts_tab(self):
        start = TOOLBAR.index('// PDF4QT already has a native document-properties Fonts tab')
        end = TOOLBAR.index('    hideAdvancedToolbars(window);', start)
        body = TOOLBAR[start:end]
        for value in (
            'nativeAction(window, "actionProperties")',
            'QStringLiteral("menuFile")',
            'new QAction(QStringLiteral("PDF 글꼴 정보…"), &window)',
            'QStringLiteral("LunaDocumentFontInventory")',
            'QStringLiteral("현재 PDF의 사용 글꼴·유형·인코딩·포함 여부 확인")',
            'fileMenu->insertAction(properties, inventory)',
            'inventory->setEnabled(properties->isEnabled())',
            'QObject::connect(properties, &QAction::changed',
            'QPointer<pdfviewer::PDFEditorMainWindow>(&window)',
            '!editor->getProgramController()->getDocument()',
            'QTimer::singleShot(0, editor',
            'QApplication::activeModalWidget()',
            'QStringLiteral("PDFDocumentPropertiesDialog")',
            'dialog->parentWidget() != editor',
            'QStringLiteral("tabWidget")',
            'QStringLiteral("fontsTab")',
            'tabs->setCurrentWidget(fonts)',
            'QStringLiteral("fontsGroupBox")',
            'QStringLiteral("이 문서에 사용된 글꼴")',
            'properties->trigger()',
        ):
            self.assertIn(value, body, value)
        self.assertLess(body.index('QTimer::singleShot(0, editor'), body.index('properties->trigger()'))
        self.assertNotIn('QFile::write(', body)
        self.assertNotIn('QFileDialog::getSaveFileName', body)

    def test_native_windows_smoke_inspects_real_pdf_font_tree(self):
        start = TABS.index('phase("PDF native font inventory");')
        end = TABS.index('phase("menu vertical alignment and library chrome")', start)
        body = TABS[start:end]
        for token in (
            'QStringLiteral("LunaDocumentFontInventory")',
            'QStringLiteral("actionProperties")',
            'fontFileMenu->actions().contains(inventoryAction)',
            'QStringLiteral("PDFDocumentPropertiesDialog")',
            'QStringLiteral("fontsTreeWidget")',
            'fontTabs->currentWidget()==fontTab',
            'fontTree->topLevelItemCount()>0',
            'fontCount=fontTree->topLevelItemCount()',
            'fontGuard.start(4500)',
            'inventoryAction->trigger()',
            'fontDialogOpened && fontTabSelected && fontCount>=1',
            'integrationRequire(ready(a)',
            'result["fontInventoryDiagnostics"]',
        ):
            self.assertIn(token, body, token)

    def test_synthetic_pdf_has_real_fonts_and_no_private_upload(self):
        self.assertIn('synthetic_pdf(2, bibliography_author=', PROBE)
        self.assertIn(b'/Font << /F1 4 0 R >>'.decode(), FIXTURE)
        self.assertIn('/BaseFont /Helvetica', FIXTURE)


if __name__ == "__main__":
    unittest.main()
