"""Verified-title PDF filename migration must preserve user documents and tab display."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
TABS = (ROOT / "src/lunatabs.cpp").read_text(encoding="utf-8")
LIBRARY = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
POLICY = (ROOT / "src/lunaautofilename.h").read_text(encoding="utf-8")
MOVE = (ROOT / "src/lunafilemove.h").read_text(encoding="utf-8")
OVERLAY = (ROOT / "tools/apply_overlay.py").read_text(encoding="utf-8")
NATIVE = (ROOT / "tests/native_dialog/filemoves.cpp").read_text(encoding="utf-8")


class VerifiedFileNameContracts(unittest.TestCase):
    def test_tab_shares_bibliography_title_not_only_disk_basename(self):
        for snippet in (
            '#include "lunabibliography.h"',
            'tabTitleCache_.insert(id, titles.record(id).value(QStringLiteral("title"))',
            'articleTitle.isEmpty() ? QFileInfo(display).fileName() : articleTitle',
            'property("LunaBibliographyRevision")',
            'tabTitleCache_.clear()',
        ):
            self.assertIn(snippet, TABS, snippet)
        self.assertIn('host->setProperty("LunaBibliographyRevision"', LIBRARY)
        self.assertIn('setIconSize(QSize(19,19))', TABS)

    def test_filename_policy_excludes_bad_provenance_and_si(self):
        for snippet in (
            'publisher_cover_verified', 'first_page_verified', 'manual',
            'record.value(QStringLiteral("titleSource"))',
            'record.value(QStringLiteral("manualSI")).toBool()',
            'supporting_information', 'if (!sources.contains(',
            '235 - QFileInfo(folder).absoluteFilePath().size()',
            'QRegularExpression',
        ):
            self.assertIn(snippet, POLICY, snippet)
        self.assertNotIn('QStringLiteral("pdf_metadata")', POLICY)

    def test_user_can_explicitly_apply_a_displayed_title_to_real_disk_filename(self):
        for token in (
            'LunaApplyVerifiedPdfFilename',
            '서지정보 제목으로 실제 PDF 파일명 변경…',
            'PDF 실제 파일명 변경',
            'record["titleSource"]=QStringLiteral("manual")',
            'renameExtractedTitleFile(id,&reason,&renamedPath)',
            'QMessageBox::Yes|QMessageBox::Cancel',
            'pendingTitleRenames_.remove(id)',
            'flushLibraryTitleRenames(*this)',
        ):
            self.assertIn(token, LIBRARY + TABS, token)

    def test_stale_lock_is_checked_by_owner_not_file_existence(self):
        # A crash can leave a .lock file even when the PDF is not open.
        # Merely testing QFileInfo::exists(lockPath) blocks real renames forever.
        self.assertIn('#include <QLockFile>', LIBRARY)
        self.assertIn('QLockFile renameGuard(lockPath);', LIBRARY)
        self.assertIn('renameGuard.setStaleLockTime(0);', LIBRARY)
        self.assertIn('if (!renameGuard.tryLock(0))', LIBRARY)
        self.assertIn('QDir().mkpath(QFileInfo(lockPath).absolutePath())', LIBRARY)
        self.assertNotIn('if (QFileInfo::exists(lockPath)) return false;', LIBRARY)
        self.assertLess(LIBRARY.index('renameGuard.tryLock(0)'), LIBRARY.index('luna::movePdfWithThumbnails(\n            original,source.absolutePath()'))

    def test_manual_rename_reports_real_failure_instead_of_generic_message(self):
        self.assertIn('renameExtractedTitleFile(id,&reason,&renamedPath)', LIBRARY)
        self.assertIn('QStringLiteral("PDF 파일명 변경 완료")', LIBRARY)
        self.assertIn('QStringLiteral("PDF 파일명 변경 상태")', LIBRARY)
        self.assertIn('파일명이 다른 작업과 충돌했습니다', LIBRARY)
        self.assertIn('Windows에서 실제 PDF/연결 파일 이동을 거부했습니다', LIBRARY)
        self.assertIn('다른 LUNA 프로세스가 이 PDF를 사용 중', LIBRARY)
        self.assertIn('선택한 PDF가 현재 LUNA 라이브러리의 연결 폴더 밖', LIBRARY)
        self.assertNotIn('QStringLiteral("파일명 변경 대기 또는 건너뜀")', LIBRARY)

    def test_verified_collision_gets_safe_number_and_requires_real_file_move(self):
        # Never report success from a metadata title alone or a target-side file.
        for code in (
            'availableVerifiedTitleName(identity,preferred)',
            'luna::AutoFileName::numberedCopy(preferred,i,budget)',
            'SidecarStorage::companions(identity,target)',
            'QFileInfo::exists(pair.second)',
            'QString reason, renamedPath;',
            'renameExtractedTitleFile(id,&reason,&renamedPath)',
            'QFileInfo::exists(original) || !QFileInfo(moved.documentPath).isFile()',
            '!renamedPath.isEmpty() && !QFileInfo::exists(path)',
            'QFileInfo(renamedPath).isFile()',
        ):
            self.assertIn(code, LIBRARY, code)
        self.assertIn('inline QString numberedCopy(', POLICY)
        self.assertIn('numberedCopy(titledName,2)', NATIVE)

    def test_rename_is_no_overwrite_paired_and_deferred_if_open(self):
        for snippet in (
            'const QString& requestedFileName = {}',
            'SidecarStorage::companions(path, targetPdf)',
            'renameWithoutReplacement',
        ):
            self.assertIn(snippet, MOVE, snippet)
        for snippet in (
            'renameExtractedTitleFile(identity)',
            'pendingTitleRenames_.insert(identity)',
            'titleRenameTimer_.start(',
            'getDocument()',
            'DocumentClaim::endpoint(original)',
            'QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()',
            'movePdfWithThumbnails(',
            'localSync_->renamed(original, moved.documentPath)',
            'void flushLibraryTitleRenames(QMainWindow& shell)',
            'flushPendingTitleRenamesAfterReaderClose()',
        ):
            self.assertIn(snippet, LIBRARY, snippet)
        self.assertIn('folder / "lunaautofilename.h"', OVERLAY)
        self.assertIn('Paired move changed PDF, thumbnail or MYBOX metadata', NATIVE)
        self.assertIn('In-place title rename did not preserve PDF and cropped thumbnail', NATIVE)
        self.assertIn('Auto-title rename overwrote a colliding PDF', NATIVE)


if __name__ == "__main__":
    unittest.main()
