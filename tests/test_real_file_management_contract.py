"""Regression contracts for native Windows file-backed LUNA operations."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
FILES = (ROOT / "src/lunafilemove.h").read_text(encoding="utf-8")
BIB = (ROOT / "src/lunabibliography.h").read_text(encoding="utf-8")


class NativeFileManagementContracts(unittest.TestCase):
    def test_real_pdf_and_sidecar_copy_with_no_overwrite(self):
        for token in (
            "FileCopyResult copyPdfWithThumbnails(",
            "copies.append(qMakePair(source.canonicalFilePath()",
            "QStringLiteral(\".luna-thumb\")",
            "if (destination.exists() || destination.isSymLink())",
            "QFile::copy(pair.first, pair.second)",
            "QFile::remove(item)",
        ):
            self.assertIn(token, FILES)

    def test_drag_move_or_control_copy_into_real_folders(self):
        for token in (
            "class PaperFileList final : public QListWidget",
            "QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override",
            "class FolderFileDropTree final : public QTreeView",
            "QStringLiteral(\"LunaFolderDropTree\")",
            "sourceList->objectName() == QStringLiteral(\"LunaDocumentList\")",
            "const bool copy = !internal || bool(event->modifiers() & Qt::ControlModifier);",
            "dropped(files, folder, copy)",
            "if (copy) copyDocumentTo(path, destination);",
            "else moveDocumentTo(path, destination);",
            "copyPdfWithThumbnails(source, destination)",
            "cloneCopiedSettings(source, result.documentPath)",
            "QGuiApplication::clipboard()->setMimeData(data)",
            "pastePdfs(current_)",
        ):
            self.assertIn(token, UI)
        self.assertIn("Qt::MoveAction", UI)
        self.assertIn("Qt::CopyAction", UI)

    def test_safe_windows_recycle_bin_and_folder_rename(self):
        for token in (
            "QFile::moveToTrash(source.canonicalFilePath())",
            "QFile::moveToTrash(name)",
            "renameWithoutReplacement(original, destination)",
            "renameFolder(model_->filePath(index))",
            "QKeySequence(Qt::Key_F2)",
            "bibliography_.relocate(file.identity, DocumentClaim::identity(newPath))",
            "QStringLiteral(\"library/memos/\")",
            "QStringLiteral(\"library/openedDates/\")",
            "QStringLiteral(\"library/favorites\")",
            "QStringLiteral(\"relativePath\")",
        ):
            self.assertIn(token, UI)

    def test_reset_requires_verified_provider_or_local_page_evidence(self):
        self.assertIn("restoreVerifiedOnline(", BIB)
        self.assertIn("restoreFromFirstPage(", BIB)
        self.assertIn('entry.remove("citation")', BIB)
        self.assertIn("Metadata::normalizeDoi(incoming.value(\"doi\").toString()) != expected", BIB)
        self.assertIn("refreshOriginalBibliography(path)", UI)
        self.assertIn("enqueueImportedMetadata(path, restore)", UI)
        self.assertIn("beginMetadataLookup(identity, doi, restore)", UI)

    def test_folder_counts_and_drag_badge_remain_filesystem_backed(self):
        for token in (
            "void refreshPdfCounts(const QString& root)",
            "QThread::create([root, result]",
            "result->insert(folder, result->value(folder) + 1)",
            "model_->refreshPdfCounts(root_);",
            "countsReady_",
            "const QRect thumb(r.left() + 11, r.top() + 12, thumbWidth, thumbHeight)",
            "void startDrag(Qt::DropActions) override",
            "const int thumbWidth = compactThumbWidth(r)",
            "QPixmap badge(132, 38)",
            "drag->setHotSpot(QPoint(13, 18))",
            "drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction)",
            "tree_->viewport()->setAcceptDrops(true)",
        ):
            self.assertIn(token, UI, token)
        self.assertIn('return QStringLiteral("%1  ·  %2").arg(label).arg(pdfCount(filePath(index)));', UI)
        self.assertIn('if (parent->objectName() == QStringLiteral("LunaFolderDropTree")', UI)

    def test_folder_selection_matches_recursive_count_badges(self):
        self.assertIn("The selected folder represents its complete subtree", UI)
        self.assertIn("QDirIterator::Subdirectories", UI)
        self.assertIn("folderCountPoll->setInterval(60000)", UI)
        self.assertIn("model_->refreshPdfCounts(root_)", UI)

    def test_favorite_smoke_reselects_after_async_folder_reload(self):
        # selectFolder() clears all QListWidgetItem objects. Its previous
        # pointers are invalid, so the smoke must reacquire and select a row.
        nav = UI.index('favoritesButton->click();')
        reload = UI.index('Folder navigation did not restore the actual PDF row', nav)
        reselect = UI.index('item = list->item(0);', reload)
        trigger = UI.index('favorite->trigger();', reselect)
        self.assertLess(nav, reload)
        self.assertLess(reload, reselect)
        self.assertLess(reselect, trigger)
        self.assertIn('settings.value(QStringLiteral("library/favorites")).toStringList().contains(path)', UI[trigger:trigger + 360])
        recent_reload = UI.index('Recent-to-folder navigation did not restore the PDF row', trigger)
        recent_reselect = UI.index('item = list->item(0);', recent_reload)
        next_probe = UI.index('const QList<QPair<QString, QWidget*>> toggles', recent_reselect)
        self.assertLess(recent_reload, recent_reselect)
        self.assertLess(recent_reselect, next_probe)

    def test_one_item_per_real_path_and_no_secondary_hover_popup(self):
        self.assertIn("QSet<QString> allSeen_", UI)
        self.assertIn("QSet<QString> folderSeen_", UI)
        self.assertIn("if (identity.isEmpty() || seen.contains(identity)) continue;", UI)
        self.assertNotIn("showThumbnailFor(path);", UI)
        self.assertIn("selectFolder(root_);", UI)


if __name__ == "__main__":
    unittest.main()
