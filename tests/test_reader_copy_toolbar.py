"""Contract tests for the top-right 'copy current PDF to selected folder' control.

The native Windows LUNA smoke also copies a real synthetic PDF+thumbnail via
the same handler, and checks the original tab, bytes, and sidecars remain.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
READER = (ROOT / "src/lunareadertoolbar.cpp").read_text(encoding="utf-8")
LIBRARY = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/lunalibrary.h").read_text(encoding="utf-8")
MOVES = (ROOT / "src/lunafilemove.h").read_text(encoding="utf-8")


class ReaderCopyToolbarContracts(unittest.TestCase):
    def test_button_always_left_of_search_and_print(self):
        start = READER.index('copyButton_=new QToolButton(rightGroup_)')
        end = READER.index('rightGroupAction_=new QWidgetAction', start)
        group = READER[start:end]
        self.assertIn('LunaRightCopyCurrentPdf', group)
        self.assertIn('file-add', group)
        self.assertIn('setAccessibleName', group)
        self.assertIn('for (auto* button : {copyButton_,searchButton_,printButton_})', group)
        self.assertLess(group.index('LunaRightCopyCurrentPdf'),
                        group.index('LunaRightSearch'))
        self.assertLess(group.index('LunaRightSearch'),
                        group.index('LunaRightPrint'))
        self.assertIn('readerIcon(QStringLiteral("file-add"))', group)
        self.assertIn('copyCurrentReaderPdfToSelectedFolder(shell_, *editor_)', group)
        self.assertIn('copyButton_->setEnabled(bool(editor_->getProgramController()->getDocument()))', READER)

    def test_copy_source_is_active_tab_not_paper_list_selection(self):
        self.assertIn('copyCurrentReaderPdfToSelectedFolder(QMainWindow& shell, pdfviewer::PDFEditorMainWindow& editor)', HEADER)
        start = LIBRARY.index('QString copyOpenReaderToSelectedFolder(')
        end = LIBRARY.index('void savePanelState()', start)
        section = LIBRARY[start:end]
        for text in (
            'viewer_ != &editor',
            'editor.getProgramController()->getDocument()',
            'controller->getOriginalFileName()',
            'controller->hasUnsavedChanges()',
            'current_',
            'source.canonicalPath().compare(folder.canonicalFilePath(), Qt::CaseInsensitive) == 0',
            'copyDocumentTo(source.canonicalFilePath(), folder.canonicalFilePath())',
        ):
            self.assertIn(text, section)
        self.assertNotIn('selectedItems()', section)

    def test_reuses_safe_copy_pipeline_with_metadata(self):
        for text in (
            'const auto result = copyPdfWithThumbnails(source, destination);',
            'cloneCopiedSettings(source, result.documentPath);',
            'enqueueImportedMetadata(result.documentPath);',
            'refreshFileLists();',
            'return result.documentPath;',
        ):
            self.assertIn(text, LIBRARY)
        for text in (
            'source.isSymLink()', 'folder.isSymLink()',
            'targetRoot == originalRoot',
            'SidecarStorage::companions(path, targetPdf, false)',
            'if (destination.exists() || destination.isSymLink())',
            'QFile::copy(pair.first, pair.second)',
        ):
            self.assertIn(text, MOVES)

    def test_real_windows_native_smoke_copies_synthetic_pdf(self):
        start = LIBRARY.index('checkpoint("reader-copy");')
        end = LIBRARY.index('checkpoint("ui-done");', start)
        case = LIBRARY[start:end]
        self.assertIn('QTemporaryDir readerDestination', case)
        self.assertIn('library->selectFolder(readerDestination.path())', case)
        self.assertIn('luna::copyCurrentReaderPdfToSelectedFolder(window, window)', case)
        self.assertIn('sourcePdf.readAll() != targetPdf.readAll()', case)
        self.assertIn('sourceThumb.readAll() != targetThumb.readAll()', case)
        self.assertIn('library->selectFolder(pdf.absolutePath())', case)


if __name__ == "__main__":
    unittest.main()
