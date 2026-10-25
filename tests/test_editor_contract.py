"""Public PDF4QT Editor integration contracts. No real user documents."""
import unittest
from pathlib import Path
from tools.apply_overlay import PIN, BLOBS, CMAKE_ADDITION

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
HEAD = (ROOT / "src/lunalibrary.h").read_text(encoding="utf-8")
ADAPTER = (ROOT / "tools/apply_overlay.py").read_text(encoding="utf-8")

class NativeEditorContract(unittest.TestCase):
    def test_editor_is_exactly_pinned(self):
        self.assertEqual(len(PIN), 40)
        self.assertEqual(BLOBS["Pdf4QtEditor/main.cpp"],
                         "208c36b730ec158854b614e48c868e518aea5cde")
        self.assertEqual(BLOBS["Pdf4QtEditor/CMakeLists.txt"],
                         "9ca656581680c7af16cee3ed35a32332e44e2dab")

    def test_editor_target_is_independent(self):
        self.assertIn("add_executable(LUNA", CMAKE_ADDITION)
        self.assertIn("Pdf4QtLibGui", CMAKE_ADDITION)
        self.assertIn("pdfeditormainwindow.h", ADAPTER)
        self.assertIn("PDFEditorMainWindow mainWindow;", ADAPTER)

    def test_no_reimplementation_of_pdf_engine(self):
        self.assertIn("PDFEditorMainWindow", HEAD)
        self.assertIn("PDFEditorMainWindow", UI)
        self.assertIn("viewer_->getProgramController()", UI)
        self.assertIn("controller->openDocument(", UI)
        self.assertIn("controller->askForSaveDocumentBeforeClose()", UI)

    def test_annotations_are_editor_actions(self):
        self.assertIn("actionCreateTextHighlight", UI)
        self.assertIn("actionStickyNoteNote", UI)
        self.assertIn("actionBookmarkPage", UI)
        self.assertIn("actionFind", UI)

    def test_two_column_shell(self):
        self.assertIn('QStringLiteral("SidebarDockWidget")', UI)
        self.assertIn("Qt::LeftDockWidgetArea", UI)
        self.assertIn('QStringLiteral("LunaLibraryDock")', UI)

    def test_no_cloud_credentials_or_uploads(self):
        self.assertNotIn("QNetworkAccessManager", UI)
        self.assertNotIn("QNetworkRequest", UI)
        self.assertNotIn("getenv(", UI)

if __name__ == "__main__":
    unittest.main()
