"""LUNA UI contract tests. Public synthetic source checks only; no user PDFs."""
import re
import unittest
from pathlib import Path
from tools.apply_overlay import PIN, make_main, CMAKE_ADDITION

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")

class LunaUiContractTests(unittest.TestCase):
    def test_pinned_pdf4qt_upstream(self):
        self.assertEqual(PIN, "67b6b9903cb4e318803342f2d428f02e1bb124e3")
        self.assertIn("add_executable(LUNA", CMAKE_ADDITION)
        self.assertIn("Pdf4QtLibGui", CMAKE_ADDITION)

    def test_exactly_one_luna_dock(self):
        self.assertIn('QStringLiteral("LunaLibraryDock")', CPP)
        self.assertIn("Qt::LeftDockWidgetArea", CPP)
        self.assertIn("SidebarDockWidget", CPP)
        self.assertEqual(CPP.count('QStringLiteral("LunaLibraryDock")'), 1)
        self.assertIn('window.findChild<QDockWidget*>', CPP)
        self.assertIn('QStringLiteral("SidebarDockWidget")', CPP)

    def test_sidebar_can_be_hidden_and_reopened(self):
        self.assertIn('collapse->setToolTip(QStringLiteral("왼쪽 라이브러리 접기"))', CPP)
        self.assertIn("dock->toggleViewAction()", CPP)
        self.assertIn("bar->insertAction(", CPP)

    def test_existing_pdf4qt_document_engine_reused(self):
        self.assertIn("viewer_->getProgramController()", CPP)
        self.assertIn("controller->openDocument(", CPP)
        self.assertIn("askForSaveDocumentBeforeClose()", CPP)

    def test_thumbnails_use_adjacent_sidecar_files(self):
        self.assertIn('QStringLiteral(".luna-thumb")', CPP)
        self.assertIn('QStringLiteral(".luna-thumb.png")', CPP)
        self.assertIn("info.absolutePath()", CPP)
        self.assertIn("original.absolutePath()", CPP)

    def test_hover_animated_and_bounded(self):
        self.assertIn("QEvent::MouseMove", CPP)
        self.assertIn("QGraphicsOpacityEffect", CPP)
        self.assertIn("fade_->setDuration(165)", CPP)
        self.assertIn("screen->availableGeometry()", CPP)

    def test_image_decode_guards(self):
        self.assertIn("thumbnail.size() <= 5'000'000", CPP)
        self.assertIn("original.width() > 4096", CPP)
        self.assertIn("reader.setScaledSize(scaled)", CPP)

    def test_crop_cancel_and_minimum_rectangle(self):
        self.assertIn("class ThumbnailCropOverlay", CPP)
        self.assertIn("Qt::Key_Escape", CPP)
        self.assertIn("selected.width() >= 40", CPP)
        self.assertIn("selected.height() >= 40", CPP)

    def test_crop_does_not_contaminate_image(self):
        # Crop overlay is hidden before capturing the underlying PDF viewer.
        self.assertRegex(CPP, r"hide\(\);[^\n]*\n\s*if \(selected\.width")
        self.assertIn("document->grab(area)", CPP)

    def test_user_authorized_bundle_move_rolls_back_on_error(self):
        self.assertIn("QList<QPair<QString, QString>> moves", CPP)
        self.assertIn("QMessageBox::question", CPP)
        self.assertIn("movePdfWithThumbnail", CPP)
        self.assertIn("QFileInfo::exists(newPath)", CPP)
        self.assertIn("controller->closeDocument()", CPP)
        self.assertIn("rollbackOK", CPP)
        self.assertIn("undoFile.rename", CPP)

    def test_sidecar_writes_are_local_and_atomic(self):
        self.assertIn("QSaveFile dest(output)", CPP)
        self.assertIn('image.save(&dest, "PNG")', CPP)
        self.assertIn("dest.commit()", CPP)
        self.assertIn("QMessageBox::question", CPP)
        self.assertNotIn("QNetworkAccessManager", CPP)
        self.assertNotIn("QNetworkRequest", CPP)

if __name__ == "__main__":
    unittest.main()
