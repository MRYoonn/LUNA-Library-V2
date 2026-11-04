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
        ):
            self.assertIn(snippet, LIBRARY, snippet)
        self.assertIn('folder / "lunaautofilename.h"', OVERLAY)
        self.assertIn('Paired move changed PDF, thumbnail or MYBOX metadata', NATIVE)
        self.assertIn('In-place title rename did not preserve PDF and cropped thumbnail', NATIVE)
        self.assertIn('Auto-title rename overwrote a colliding PDF', NATIVE)


if __name__ == "__main__":
    unittest.main()
