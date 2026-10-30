"""Citation-only beta contract: no DOI extraction or network metadata lookup."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CitationContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model = (ROOT / "src/lunabibliography.h").read_text(encoding="utf-8")
        cls.ui = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
        cls.native = (ROOT / "tests/native_dialog/bibliography.cpp").read_text(encoding="utf-8")

    def test_no_network_or_doi_extractor(self):
        for forbidden in (
            "QNetworkAccessManager", "QNetworkRequest", "crossref(",
            "api.crossref.org", "static QString doi(", "Bibliography::doi(", "doiText",
        ):
            self.assertNotIn(forbidden, self.model + self.ui)

    def test_full_citation_can_be_pasted_into_native_editor(self):
        self.assertIn("LunaBibliographyCitation", self.ui)
        self.assertIn("서지정보 한 줄", self.ui)
        self.assertIn('revised["citation"] = fullCitation.text()', self.ui)
        self.assertIn('return pasted;', self.model)

    def test_persist_and_moves_without_overwriting(self):
        self.assertIn('value["manual"] = manual', self.model)
        self.assertIn("if (entry.value(\"manual\").toBool()) return;", self.model)
        self.assertIn("if (!store.value(key(newIdentity)).toMap().isEmpty()) return false;", self.model)
        self.assertIn("bibliography_.relocate(sourceIdentity", self.ui)

    def test_pdf_open_never_parses_all_pages(self):
        self.assertIn("controller->getDocument()->getInfo()", self.ui)
        self.assertIn("QTimer::singleShot(0, this, [this, identity, title, authors, structured]", self.ui)
        self.assertIn("bibliography_.ingest(identity, title, authors, structured)", self.ui)
        self.assertIn('key == QStringLiteral("journal")', self.ui)
        self.assertIn('key == QStringLiteral("publicationyear")', self.ui)
        self.assertNotIn("PDFTextLayoutGenerator", self.ui)

    def test_first_page_extractor_is_opt_in_and_offline(self):
        self.assertIn("firstPageCitation(QString text)", self.model)
        self.assertIn("firstPageIdentity(const QString& text)", self.model)
        self.assertIn("void extractBibliography(const QString& path)", self.ui)
        self.assertIn("compiler->createTextLayout(0)", self.ui)
        self.assertIn("firstPageCitation(text)", self.ui)
        self.assertIn("firstPageIdentity(text)", self.ui)
        self.assertIn("bibliography_.enrichFromFirstPage(identity, evidence)", self.ui)
        self.assertNotIn("LunaExtractFirstPageCitation", self.ui)
        self.assertNotIn("서지정보 추출 결과", self.ui)

    def test_shell_does_not_persist_any_qmainwindow_toolbar_state(self):
        self.assertNotIn('window_->saveState(', self.ui)
        self.assertNotIn('window.restoreState(settings.value(', self.ui)
        self.assertIn('window_->saveGeometry()', self.ui)
        self.assertIn('window.restoreGeometry(', self.ui)

    def test_context_menu_has_first_page_extraction(self):
        self.assertIn('LunaExtractCitationContextMenu', self.ui)
        self.assertIn('extractBibliography(path)', self.ui)
        self.assertNotIn('editBibliography(path, true)', self.ui)
        self.assertNotIn('if (extractOnOpen) QTimer::singleShot(', self.ui)

    def test_no_prose_or_missing_info_is_invented(self):
        self.assertIn("credibleTitle(const QString& value)", self.model)
        self.assertIn("credibleAuthor(const QString& value)", self.model)
        self.assertIn('QStringLiteral("Supporting Information")', self.model)
        self.assertIn('QStringLiteral("No Job Name")', self.native)
        self.assertIn('QStringLiteral("J. Phys. Chem. B")', self.native)

    def test_open_papers_do_not_reorder_or_scroll_list(self):
        self.assertIn('index.data(Qt::UserRole + 7).toBool()', self.ui)
        self.assertIn('item->setData(Qt::UserRole + 7, isActive)', self.ui)
        self.assertNotIn('if (selected) list->setCurrentItem(item', self.ui)
        self.assertNotIn('if (pages_->currentIndex() == 2 && !showingFavorites_) showHistory(false)', self.ui)
        self.assertNotIn('list->setSortingEnabled(true)', self.ui)

    def test_extracts_last_listed_author_only(self):
        self.assertIn('lastListedAuthor(QString authors)', self.model)
        self.assertIn('lastListedAuthor(authors)', self.model)
        self.assertIn('John J. Molloy', self.native)

    def test_real_rsc_last_author_requires_evidence(self):
        self.assertIn("firstPageLastAuthor(const QString& firstPageText)", self.model)
        self.assertIn("first-page-terminal-and", self.model)
        self.assertIn('Q_UNUSED(authors);', self.model)
        self.assertIn('firstPageLastAuthor(text)', self.ui)
        self.assertIn('evidence.insert(QStringLiteral("authorEvidence")', self.ui)
        self.assertIn('Hannah M. Kortman,ab Hao Fang', self.native)
        self.assertIn('and John J. Molloy\\n*a', self.native)
        self.assertIn('store.record("auto-rsc")', self.native)
        self.assertIn('Manual Correct Author', self.native)

    def test_native_bibliography_store_declared_before_regressions(self):
        declaration = "luna::Bibliography store;"
        self.assertEqual(self.native.count(declaration), 1)
        self.assertLess(self.native.index(declaration), self.native.index('store.save("auto-rsc"'))
        self.assertLess(self.native.index(declaration), self.native.index('store.save("before"'))

    def test_native_fixture_exercises_citation_and_restart(self):
        for text in (
            "store.relocate", "Synthetic Paper", "J. Am. Chem. Soc.",
            "store.ingest", "Bibliography reopened",
        ):
            self.assertIn(text, self.native)


if __name__ == "__main__":
    unittest.main()
