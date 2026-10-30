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
        self.assertIn("QTimer::singleShot(0, this, [this, identity, title, authors]", self.ui)
        self.assertIn("bibliography_.ingest(identity, title, authors)", self.ui)
        self.assertNotIn("PDFTextLayoutGenerator", self.ui)

    def test_native_fixture_exercises_citation_and_restart(self):
        for text in (
            "store.relocate", "Synthetic Paper", "J. Am. Chem. Soc.",
            "store.ingest", "Bibliography reopened",
        ):
            self.assertIn(text, self.native)


if __name__ == "__main__":
    unittest.main()
