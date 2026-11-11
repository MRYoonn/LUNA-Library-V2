"""Contracts for safe confirmation + one-shot Ctrl+Z of a reader PDF copy.

Synthetic-only: no user PDF enters repository or Windows CI.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIBRARY = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
TOOLBAR = (ROOT / "src/lunareadertoolbar.cpp").read_text(encoding="utf-8")
SNAPSHOT = (ROOT / "src/lunareadercopyundo.h").read_text(encoding="utf-8")
BIBLIO = (ROOT / "src/lunabibliography.h").read_text(encoding="utf-8")
OVERLAY = (ROOT / "tools/apply_overlay.py").read_text(encoding="utf-8")


class ReaderCopyUndoContract(unittest.TestCase):
    def test_confirmation_defaults_cancel_and_precedes_disk_write(self):
        start = LIBRARY.index('QString copyOpenReaderToSelectedFolder(')
        end = LIBRARY.index('bool undoLastReaderPdfCopy()', start)
        body = LIBRARY[start:end]
        self.assertIn('QStringLiteral("PDF 복사 확인")', body)
        self.assertIn("QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel", body)
        self.assertLess(body.index('QMessageBox::question('), body.index('copyDocumentTo(sourceFile, destination)'))
        self.assertIn("lastReaderCopy_.remember(copied)", body)
        self.assertIn("qApp->installEventFilter(this)", body)
        self.assertIn("Ctrl+Z", body)
        self.assertIn("Ctrl+Z", TOOLBAR)

    def test_native_build_includes_copy_undo_header(self):
        self.assertIn('folder / "lunareadercopyundo.h": (own / "lunareadercopyundo.h").read_bytes()', OVERLAY)

    def test_undo_only_last_intact_disk_copy(self):
        for phrase in (
            "bool remember(const QString& pdf)", "bool unchanged(QString* reason) const",
            "source.isSymLink()", "SidecarStorage::safeManagedDirectory",
            "SidecarStorage::allCompanions(path_)", "QCryptographicHash::Sha256",
            "file.isSymLink()", "file.isFile()",
            "present.size() != fingerprints_.size()",
            "QCryptographicHash hasher(QCryptographicHash::Sha256)",
            "hasher.addData(&file)",
            "hasher.result()",
            "void clear()",
        ):
            self.assertIn(phrase, SNAPSHOT)
        start = LIBRARY.index('bool undoLastReaderPdfCopy()')
        end = LIBRARY.index('QLineEdit* searchEdit()', start)
        method = LIBRARY[start:end]
        self.assertIn("lastReaderCopy_.unchanged(&reason)", method)
        self.assertIn("recycle.trash(copied, &reason)", method)
        self.assertIn("QToolTip::showText", method)
        self.assertIn("lastReaderCopy_.clear()", method)
        self.assertIn("source", LIBRARY)  # source stays active; not passed to trash
        self.assertNotIn("QFile::remove(copied)", method)
        self.assertNotIn("recycle.trash(source", method)

    def test_native_editor_undo_and_text_fields_are_not_stolen(self):
        start = LIBRARY.index('bool eventFilter(QObject* observed, QEvent* event) override {', LIBRARY.index("void importPdfs()"))
        section = LIBRARY[start:start+1550]
        for phrase in (
            'lastReaderCopy_.available()', 'QEvent::ShortcutOverride',
            'QEvent::KeyPress', '!QApplication::activeModalWidget()',
            'QApplication::activeWindow() == window()',
            'qobject_cast<QLineEdit*>(focused)',
            'qobject_cast<QTextEdit*>(focused)',
            'qobject_cast<QPlainTextEdit*>(focused)',
            'hasUnsavedChanges()', 'Qt::Key_Z', 'Qt::ControlModifier',
            'event->accept(); return false', 'undoLastReaderPdfCopy()',
        ):
            self.assertIn(phrase, section, phrase)

    def test_cloned_settings_must_be_unmodified_before_delete(self):
        for token in (
            'readerCopySettings_', 'readerCopyBibliography_',
            'store.value(it.key()) != it.value()',
            'bibliography_.record(DocumentClaim::identity(copied)) != readerCopyBibliography_',
            'bibliography_.forgetCopiedRecordIfUnchanged(',
            'store.remove(it.key())',
            'favorites_.remove(copied)',
            'openedDates_.remove(dateKey)',
            'memos_.remove(dateKey)',
        ):
            self.assertIn(token, LIBRARY)
        self.assertIn('store.value(key(identity)).toMap() != expected', BIBLIO)
        self.assertIn('resolve(identity) != identity', BIBLIO)

    def test_real_windows_native_smoke_cancel_confirm_and_undo(self):
        start = LIBRARY.index('checkpoint("reader-copy");')
        end = LIBRARY.index('checkpoint("ui-done");', start)
        smoke = LIBRARY[start:end]
        self.assertIn('box->done(QMessageBox::Cancel)', smoke)
        self.assertIn('box->done(QMessageBox::Yes)', smoke)
        self.assertIn('cancelled.isEmpty()', smoke)
        self.assertIn('sourcePdf.readAll() != targetPdf.readAll()', smoke)
        self.assertIn('sourceThumb.readAll() != targetThumb.readAll()', smoke)
        self.assertIn('QKeyEvent override(QEvent::ShortcutOverride,Qt::Key_Z,Qt::ControlModifier)', smoke)
        self.assertIn('QKeyEvent ctrlZ(QEvent::KeyPress,Qt::Key_Z,Qt::ControlModifier)', smoke)
        self.assertIn('QApplication::sendEvent(copyControl,&ctrlZ)', smoke)
        self.assertIn('library->undoLastReaderPdfCopy()', smoke)
        self.assertIn('QFileInfo(copiedPath).exists()', smoke)
        self.assertIn('QFileInfo(path).isFile()', smoke)


if __name__ == "__main__":
    unittest.main()
