"""Manual-only SI attachment must not create automatic DOI matches or move PDFs.

Source contracts complement CTest's real Qt Windows disk I/O regressions and
full LUNA PDF4QT GUI/annotation tests. No user PDF is bundled here.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
HELPER = (ROOT / "src/lunamanualsi.h").read_text("utf-8")
BIB = (ROOT / "src/lunabibliography.h").read_text("utf-8")
UI = (ROOT / "src/lunalibrary.cpp").read_text("utf-8")
NATIVE = (ROOT / "tests/native_dialog/manualsi.cpp").read_text("utf-8")
CMAKE = (ROOT / "tests/native_dialog/CMakeLists.txt").read_text("utf-8")
OVERLAY = (ROOT / "tools/apply_overlay.py").read_text("utf-8")


class ManualSIContracts(unittest.TestCase):
    def test_only_right_click_selected_si_is_attached(self):
        self.assertIn('QStringLiteral("SI 추가…")',UI)
        self.assertIn('QStringLiteral("LunaAddManualSI")',UI)
        self.assertIn('QStringLiteral("SI 연결 해제 (파일 유지)")',UI)
        self.assertIn('auto* addSI=menu.addAction(',UI)
        self.assertIn('else if (chosen == addSI) addManualSI(path)',UI)
        self.assertIn('QFileDialog::getOpenFileName(this,',UI)
        self.assertIn('luna::ManualSI::placeNextToParent(selected,parent,title)',UI)
        self.assertIn('bibliography_.attachManualSI(siId,paper.fileName())',UI)
        self.assertIn('expandedSIParents_.insert(parent)',UI)
        self.assertNotIn('QNetworkAccessManager',HELPER)
        self.assertNotIn('Crossref',HELPER)
        self.assertNotIn('OpenAlex',HELPER)

    def test_copy_no_overwrite_filename_and_unlink_preserve_data(self):
        for token in (
            'QFile::copy(source,target)', 'if(QFileInfo(target).exists()',
            'QStringLiteral("SI · ")', 'QStringLiteral(" (%1)")',
            'SidecarStorage::safeManagedDirectory(target)',
            'SidecarStorage::allCompanions(target)',
            'bool attachManualSI(', 'bool detachManualSI(',
            '"manualSiParentFile"', '"user_selected_pdf"',
            'QFile::remove(placed.path)',
        ):
            self.assertIn(token,HELPER+BIB+UI,token)
        self.assertNotIn('QFile::rename',HELPER)
        self.assertIn('bibliography_.detachManualSI(to)',UI)
        self.assertIn('QFileInfo(child).isSymLink()',UI)

    def test_same_folder_renames_existing_pdf_without_creating_duplicate(self):
        for token in (
            'CopyResult placeNextToParent(',
            'sourceFolder.compare(parentFolder,Qt::CaseInsensitive)!=0',
            'return copyNextToParent(chosenPdf,articlePdf,articleTitle)',
            'if(alreadyNamed(input.fileName(),articleTitle))',
            'result.reusedExisting=true',
            'if(!renameWithoutReplacement(source,target))',
            'result.renamedExisting=true',
            'SidecarStorage::allCompanions(source)',
            'SidecarStorage::allCompanions(target)',
        ):
            self.assertIn(token,HELPER,token)
        for token in (
            'luna::reconcileExplorerRename(placed.previousPath,placed.path,bibliography_,&error)',
            'if(!placed.reusedExisting && !placed.renamedExisting)QFile::remove(placed.path)',
            'const bool sameFolder=',
            'const bool renameNeeded=sameFolder &&',
            'if(renameNeeded && !opened.isEmpty()',
        ):
            self.assertIn(token,UI,token)
        for token in (
            'Same-folder manual SI created a duplicate or lost original PDF',
            'Same-folder rename lost crop, manual bibliography or memo',
            'Selecting already named same-folder SI must reuse without another rename/copy',
            'Outside-folder SI must be copied into article folder without moving source',
        ):
            self.assertIn(token,NATIVE,token)

    def test_card_and_list_group_only_manual_si(self):
        for token in (
            'static constexpr int kSIRowHeight = 46',
            'static constexpr int kSIFooterHeight = 29',
            'paintSIChild(painter,option,index)',
            'paintSIFooter(painter,option,index)',
            'DocumentCard::siFooterRect(rect).contains(cursor)',
            'toggleManualSIGroup(',
            'void arrangeManualSIRows(QListWidget* list)',
            'if(!info.value("manualSI").toBool())continue;',
            'ManualSI::validParentFileName(parentName)',
            'list->insertItem(list->row(parent)+1+offset++,moved)',
            'moved->setHidden(!expanded && query_->text().isEmpty())',
            'list->setUniformItemSizes(false)',
            'matchedSIParents.insert(',
        ):
            self.assertIn(token,UI,token)
        self.assertIn('arrangeManualSIRows(list);',UI)
        self.assertIn('else if (chosen == unlinkSI) detachManualSI(path)',UI)

    def test_real_windows_qt_test_and_pdf4qt_header_overlay(self):
        self.assertIn('folder / "lunamanualsi.h": (own / "lunamanualsi.h").read_bytes()',OVERLAY)
        self.assertIn('add_test(NAME native_manual_si_attachment',CMAKE)
        for token in (
            '[PASS] manual SI:', 'SI selection failed to copy',
            'Second SI did not generate',
            'SI parent relation missing after settings reload',
            'Unlink must preserve copied SI PDF',
            'SI copy overwrote or adopted stale unrelated sidecar',
        ):
            self.assertIn(token,NATIVE,token)


if __name__ == "__main__":
    unittest.main()
