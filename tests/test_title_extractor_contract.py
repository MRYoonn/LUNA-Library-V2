"""Source-level contracts for the offline typography-aware title extractor.

These are not real PDF recognition accuracy measurements; Qt native synthetic
fixtures also run in the existing Windows native_metadata acceptance target.
"""
from pathlib import Path
import unittest
import re

ROOT = Path(__file__).resolve().parents[1]


class TitleExtractorContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.title = (ROOT / "src/lunatitleextractor.h").read_text(encoding="utf-8")
        cls.first = (ROOT / "src/lunafirstpage.h").read_text(encoding="utf-8") + (ROOT / "src/lunafirstpageevidence.h").read_text(encoding="utf-8")
        cls.model = (ROOT / "src/lunabibliography.h").read_text(encoding="utf-8")
        cls.native = (ROOT / "tests/native_metadata/main.cpp").read_text(encoding="utf-8")
        cls.front = (ROOT / "src/lunafrontmatter.h").read_text(encoding="utf-8")
        cls.library = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")

    def test_msvc_compatible_regex_literals(self):
        # QStringLiteral macro concatenates UTF-16 literal tokens; it cannot
        # directly wrap the C++ raw R"(pattern)" spelling (Build #217 C2059).
        self.assertNotIn('QStringLiteral(\n        R"(', self.title)
        for key in ('bad', 'affiliation', 'authorList'):
            self.assertIn('QRegularExpression ' + key + '(QString::fromUtf8(\n        R"(', self.title)
        self.assertEqual(self.title.count('QString::fromUtf8(\n        R"('), 3)
        # Windows #219/#220: both two-argument QRegularExpression ctors
        # need ONE final ')' after CaseInsensitiveOption, not '));'.
        # Python regex compilation alone cannot detect this C++ syntax error.
        self.assertEqual(self.title.count('QRegularExpression::CaseInsensitiveOption);'), 2)
        self.assertNotIn('QRegularExpression::CaseInsensitiveOption));', self.title)
        # Check syntax of all three C++ raw patterns with Python's built-in
        # engine; normalize PCRE2 Unicode classes used inside character sets.
        patterns = re.findall(r'R"\((.*?)\)"', self.title, flags=re.S)
        self.assertEqual(len(patterns), 3)
        normalized = [p.replace(r'\p{Lu}', 'A-Z').replace(r'\p{Ll}', 'a-z')
                        .replace(r'\p{M}', '') for p in patterns]
        compiled = [re.compile(p, re.I) for p in normalized]
        self.assertTrue(compiled[0].search("Introduction"))
        self.assertTrue(compiled[1].search("Department of Chemistry"))
        self.assertFalse(compiled[0].search("Photocatalytic Addition to Alkenes"))

    def test_windows_overlay_includes_new_title_header(self):
        overlay = (ROOT / "tools/apply_overlay.py").read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/build-windows.yml").read_text(encoding="utf-8")
        self.assertIn('folder / "lunatitleextractor.h": (own / "lunatitleextractor.h").read_bytes()', overlay)
        self.assertIn("python tools/apply_overlay.py pdf4qt", workflow)
        self.assertIn("cmake -S tests/native_metadata", workflow)

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

    def test_v261_first_page_candidates_remain_review_only(self):
        # A weak layout candidate must never be used to rename a real PDF.
        for field in ('titleCandidate', 'titleConfidence', 'titleStatus',
                      'needs_review', 'reviewTitle'):
            self.assertIn(field, self.first, field)
        self.assertIn('first_page_layout_review', self.model)
        self.assertIn('value["titleCandidate"] = reviewTitle', self.model)
        self.assertIn('titleNeedsReview', self.library)
        self.assertIn('LunaAcceptSuggestedBibliographyTitle', self.library)
        self.assertIn('title.setText(suggestedTitle)', self.library)
        self.assertIn('bibliography_.saveEdits', self.library)
        self.assertNotIn('QNetworkAccessManager', self.first)

    def test_v261_publisher_and_last_author_evidence(self):
        self.assertIn('natureArticleTitle', self.front)
        self.assertIn('natureArticleTitle(first)', self.first)
        self.assertIn('isSupplementCover(firstPage)', self.front)
        self.assertIn('naturePublisher', self.front)
        self.assertIn('titleLines.size() >= 1', self.front)
        self.assertIn('trailingAnd.match(line)', self.model)
        self.assertIn('authorRow +=', self.model)
        for phrase in ('v261-evidence-ranked-bibliography',
                       'Nature cover font-neutral title verified',
                       'RSC split explicit terminal-and recovers true last-listed author',
                       'Low typography confidence returns only candidate',
                       'Automatic first-page extraction never overwrites manually accepted title',
                       'Missing explicit final author cannot be guessed'):
            self.assertIn(phrase, self.native, phrase)

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
