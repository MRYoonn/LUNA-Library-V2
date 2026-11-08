"""Contracts for v300 automatic verified-title live rename and linked SI."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
POLICY = (ROOT / "src/lunaautofilename.h").read_text(encoding="utf-8")
TABS = (ROOT / "src/lunatabs.cpp").read_text(encoding="utf-8")


class LiveTitleLinkedSIContracts(unittest.TestCase):
    def test_clean_open_pdf_auto_rebind_preserves_view_and_claim(self):
        for text in (
            '#include "pdfundoredomanager.h"',
            'findChild<pdfviewer::PDFUndoRedoManager*>()',
            'undo->isCurrentSaved()',
            'controller->getIsBusy()',
            'controller->closeDocument()',
            'LunaMoveRestorePending',
            'LunaPreserveReopenView',
            'movedHandler(activeEditor.data(),original,target)',
            'controller->openDocument(target)',
            'getCurrentPages()',
            'getZoom()',
            'getPageLayout()',
            'goToPage(page)',
            'reopenReader(original,false)',
            'reopenReader(moved.documentPath,true)',
        ):
            self.assertIn(text, UI, text)
        self.assertLess(
            UI.index('activeEditor->getProgramController()->closeDocument()'),
            UI.index('luna::movePdfWithThumbnails(\n            original,source.absolutePath()'),
        )
        self.assertLess(
            UI.index('movedHandler(activeEditor.data(),original,target)'),
            UI.index('controller->openDocument(target)'),
        )
        self.assertIn('s->claim = std::move(claim); s->identity = DocumentClaim::identity(destination)', TABS)

    def test_linked_si_uses_only_its_verified_parent_filename(self):
        for text in (
            'QString verifiedFilenameFor(',
            'record.value(QStringLiteral("manualSI")).toBool()',
            'record.value(QStringLiteral("manualSiParentFile"))',
            'luna::ManualSI::validParentFileName(parentName)',
            'parent.canonicalPath().compare(source.canonicalPath()',
            'bibliography_.record(DocumentClaim::identity(parent.canonicalFilePath()))',
            'luna::AutoFileName::fromVerifiedTitle(',
            'QStringLiteral("SI · ")',
            'luna::ManualSI::safeTitle(',
            'const QString preferred=verifiedFilenameFor(identity,source);',
        ):
            self.assertIn(text, UI, text)
        self.assertIn('record.value(QStringLiteral("manualSI")).toBool()', POLICY)

    def test_linked_si_menu_is_enabled_and_manual_override_is_article_only(self):
        for text in (
            'renameFromTitle->setEnabled(linkedSI ||',
            '연결된 원논문 제목으로 SI 실제 파일명 변경…',
            'if (!siLinked) approved["titleSource"]=QStringLiteral("manual");',
            'if (!siLinked && !bibliography_.save(id,record,false))',
        ):
            self.assertIn(text, UI, text)
        self.assertIn('원논문 서지정보를 확인한 후 자동으로 SI 파일명을 변경합니다.', UI)

    def test_parent_metadata_change_queues_children_and_auto_mode_never_overwrites(self):
        for text in (
            'pendingTitleRenames_.insert(identity);',
            'pendingTitleRenames_.insert(childId);',
            'metadata.value("manualSiParentFile").toString().compare(',
            'QDirIterator siblings(parent.absolutePath()',
            'renameGuard->tryLock(0)',
            'luna::movePdfWithThumbnails(',
            'QFileInfo::exists(original) || !QFileInfo(moved.documentPath).isFile()',
            'localSync_->renamed(original, moved.documentPath)',
        ):
            self.assertIn(text, UI, text)
        self.assertIn('inline QString numberedCopy(', POLICY)
        self.assertNotIn('MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()),'
                         '\n                       reinterpret_cast<LPCWSTR>(destination.utf16()),'
                         '\n                       MOVEFILE_REPLACE_EXISTING', UI)


if __name__ == "__main__":
    unittest.main()
