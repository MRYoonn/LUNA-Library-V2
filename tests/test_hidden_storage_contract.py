"""Fail-closed source wiring for hidden, PDF-relative LUNA sidecar storage.

Full Windows Qt filesystem behavior is separately checked by the new
native_hidden_sidecars CTest and the existing native_file_moves / UI smoke.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HiddenStorageContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.paths = (ROOT / "src/lunasidecarpaths.h").read_text(encoding="utf-8")
        cls.move = (ROOT / "src/lunafilemove.h").read_text(encoding="utf-8")
        cls.undo = (ROOT / "src/lunatrashundo.h").read_text(encoding="utf-8")
        cls.sync = (ROOT / "src/lunalocalsync.h").read_text(encoding="utf-8")
        cls.ui = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
        cls.tabs = (ROOT / "src/lunatabs.cpp").read_text(encoding="utf-8")

    def test_hidden_windows_root_and_legacy_fallback(self):
        for needle in (
            'QStringLiteral(".luna")',
            'QStringLiteral("metadata/")',
            'QStringLiteral("thumbnails/")',
            "FILE_ATTRIBUTE_HIDDEN",
            "FILE_ATTRIBUTE_REPARSE_POINT",
            'QStringLiteral("metadata"), QStringLiteral("thumbnails")',
            "bool safeManagedDirectory",
            "bool ensureArea",
            "bool migrateLegacy(",
            "if (target.exists() || target.isSymLink()) return false;",
            "QFile::rename(from, to)",
            "QString legacyMetadata",
            "QString legacyThumbnail",
            "QString metadataForRead",
            "QString thumbnailForRead",
        ):
            self.assertIn(needle, self.paths, needle)
        for forbidden in ("QNetworkAccessManager", "Invoke-WebRequest", "Remove-Item",
                          "QFile::remove(from)", "QFile::remove(to)"):
            self.assertNotIn(forbidden, self.paths)

    def test_mybox_sync_and_explorer_rename_preserve_identity(self):
        for needle in (
            "return SidecarStorage::metadata(path);",
            "return SidecarStorage::metadataForRead(path);",
            "if (!SidecarStorage::migrateLegacy(pdf))",
            "SidecarStorage::migrateLegacy(newPath);",
            "SidecarStorage::ensureArea(pdf, true)",
            "SidecarStorage::companions(oldPath, newPath)",
            "const QString previousOutput=portableSidecarForRead(pdf)",
            "const QString sidecar=portableSidecarForRead(pdf)",
            "processed<8 && elapsed.elapsed()<12",
        ):
            self.assertIn(needle, self.sync, needle)

    def test_moves_copies_and_deletion_include_both_layouts(self):
        for needle in (
            "SidecarStorage::companions(path, targetPdf)",
            "SidecarStorage::companions(path, targetPdf, false)",
            "SidecarStorage::prepareTarget(targetPdf, pair.second)",
            "if (target.exists() || target.isSymLink())",
            "if (!rename(pair.first, pair.second))",
            "if (!QFile::copy(pair.first, pair.second))",
        ):
            self.assertIn(needle, self.move, needle)
        self.assertIn("SidecarStorage::allCompanions(pdf)", self.undo)
        self.assertIn("if (!QFile::copy(item.second, item.first))", self.undo)

    def test_crop_and_cards_use_new_paths(self):
        self.assertIn("SidecarStorage::thumbnail(documentPath, QStringLiteral(\".png\"))", self.ui)
        self.assertIn("SidecarStorage::legacyThumbnail(documentPath, ext)", self.ui)
        self.assertIn("SidecarStorage::ensureArea(documentPath, false)", self.ui)
        self.assertIn("const QString thumbnail = SidecarStorage::thumbnail(path", self.ui)
        self.assertIn('const QString movedThumbnail = SidecarStorage::thumbnail(movedPath, QStringLiteral(".png"));', self.ui)
        self.assertNotIn('const QString movedThumbnail = QDir(movedFolder).filePath(QFileInfo(thumbnail).fileName());', self.ui)
        self.assertIn("folder / \"lunasidecarpaths.h\"", (ROOT / "tools/apply_overlay.py").read_text("utf-8"))
        for required in (
            'const QString thumb = luna::SidecarStorage::thumbnail(files[0], QStringLiteral(".png"));',
            'const QString thumbnail = luna::SidecarStorage::thumbnail(files[0], QStringLiteral(".png"));',
            'const QString movedThumb = luna::SidecarStorage::thumbnail(movedPdf, QStringLiteral(".png"));',
            'luna::SidecarStorage::thumbnail(files[1], QStringLiteral(".png"))',
        ):
            self.assertIn(required, self.tabs, required)
        for stale in (
            '"/A.luna-thumb.png"',
            '"/B.luna-thumb.png"',
        ):
            self.assertNotIn(stale, self.tabs, stale)

    def test_native_windows_coverage_is_real_not_only_source_assertions(self):
        cmake = (ROOT / "tests/native_dialog/CMakeLists.txt").read_text("utf-8")
        native = (ROOT / "tests/native_dialog/sidecarstorage.cpp").read_text("utf-8")
        self.assertIn("add_test(NAME native_hidden_sidecars", cmake)
        moves = (ROOT / "tests/native_dialog/filemoves.cpp").read_text("utf-8")
        self.assertIn('Mixed-version MYBOX publish overwrote a conflicting user record', moves)
        for needle in ("FILE_ATTRIBUTE_HIDDEN", "migrateLegacy(pdf)",
                       "movePdfWithThumbnails(pdf,dest)",
                       "copyPdfWithThumbnails(movedPdf,copyFolder)",
                       "undo.trash(movedPdf,&error,recycler)", "undo.undo(&error)"):
            self.assertIn(needle, native, needle)


if __name__ == "__main__":
    unittest.main()
