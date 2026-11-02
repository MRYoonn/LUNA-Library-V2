"""Source-level contracts for the offline typography-aware title extractor.

These are not real PDF recognition accuracy measurements; Qt native synthetic
fixtures also run in the existing Windows native_metadata acceptance target.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TitleExtractorContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.title = (ROOT / "src/lunatitleextractor.h").read_text(encoding="utf-8")
        cls.first = (ROOT / "src/lunafirstpage.h").read_text(encoding="utf-8")
        cls.model = (ROOT / "src/lunabibliography.h").read_text(encoding="utf-8")
        cls.native = (ROOT / "tests/native_metadata/main.cpp").read_text(encoding="utf-8")

    def test_offline_title_extractor_uses_actual_pdf_character_sizes(self):
        self.assertIn('#include "lunatitleextractor.h"', self.first)
        self.assertIn('current.fontSize', self.first)
        self.assertIn('std::sort(characterSizes.begin(),characterSizes.end())', self.first)
        self.assertIn('firstPageLines.append({row.text,row.x,row.y,row.fontSize})', self.first)
        self.assertIn('TitleExtraction::infer(firstPageLines,firstPageHeight)', self.first)
        self.assertIn('std::min<int>(3, int(catalog->getPageCount()))', self.first)
        self.assertIn('if (i==0) firstPageLines.append', self.first)
        self.assertNotIn('QNetworkAccessManager', self.title)
        self.assertNotIn('QNetworkAccessManager', self.first)

    def test_title_works_without_a_doi_or_known_journal(self):
        self.assertIn('const QVariantMap titleLayout=', self.first)
        self.assertIn('if (evidence.isEmpty() && !Metadata::acceptableIdentity(candidates)) return {};', self.first)
        self.assertIn('evidence["title"]=title;', self.first)
        self.assertIn('firstPageIdentity(first)', self.first)
        self.assertIn('first_page_layout', self.first)
        self.assertIn('titleConfidence', self.title)
        self.assertIn('needs_review', self.title)

    def test_si_protection_and_no_prose_hallucination(self):
        for token in ('isSupportingInformation(', 'Supporting Information', 'Supplementary Information',
                      'abstract', 'introduction', 'department of', 'university', 'authorList',
                      'first_page_layout', 'R"((', 'best.score < 86'):
            self.assertIn(token, self.title, token)
        self.assertIn('documentKind', self.first)
        self.assertIn('supporting_information', self.first)

    def test_existing_manual_title_cannot_be_overwritten(self):
        self.assertIn('field + QStringLiteral("Source")', self.model)
        self.assertIn('== QStringLiteral("manual")', self.model)
        self.assertIn('strongerPrintedTitle', self.model)
        self.assertIn('Metadata::compatibleTitle(old,next)', self.model)
        self.assertIn('titleSource', self.first)

    def test_native_qt_fixtures_cover_real_logic(self):
        for phrase in (
            'checkpoint("layout-title-no-doi")',
            'TitleExtraction::infer(paperCover,792)',
            'DOI-less scientific cover title captured',
            'SI parent article title cannot be assigned',
            'body/Introduction cannot be promoted',
            'competing title headings must not autosave',
            'chemistry glyphs in printed title must be preserved',
            'manual title must never be overwritten',
            'strong printed title corrects a conflicting machine-owned',
        ):
            self.assertIn(phrase, self.native, phrase)


if __name__ == "__main__":
    unittest.main()
