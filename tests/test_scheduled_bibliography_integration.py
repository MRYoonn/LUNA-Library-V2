"""Fail-closed contracts for scheduled bibliography lab → actual LUNA pipeline.

These are source-level checks. Actual Qt/C++ execution is verified separately
by the Windows native_bibliography test and full program integration smoke.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FRONT = (ROOT / "src/lunafrontmatter.h").read_text(encoding="utf-8")
BIB = (ROOT / "src/lunabibliography.h").read_text(encoding="utf-8")
PAGE = (ROOT / "src/lunafirstpage.h").read_text(encoding="utf-8") + (ROOT / "src/lunafirstpageevidence.h").read_text(encoding="utf-8")
TITLE = (ROOT / "src/lunatitleextractor.h").read_text(encoding="utf-8")
OVERLAY = (ROOT / "tools/apply_overlay.py").read_text(encoding="utf-8")
NATIVE = (ROOT / "tests/native_dialog/bibliography.cpp").read_text(encoding="utf-8")


class ScheduledBibliographyIntegration(unittest.TestCase):
    def test_production_pdf4qt_overlay_includes_new_header(self):
        self.assertIn('folder / "lunafrontmatter.h": (own / "lunafrontmatter.h").read_bytes()', OVERLAY)
        self.assertIn('#include "lunafrontmatter.h"', BIB)
        self.assertIn('#include "lunafrontmatter.h"', TITLE)
        self.assertIn('FrontMatter::isSupplementCover(first)', PAGE)

    def test_si_false_positive_fixed_across_all_extractors(self):
        self.assertIn("inline bool isSupplementCover", FRONT)
        self.assertIn("Article Recommendations", FRONT)
        self.assertIn("isSupplementCover(first)", PAGE)
        self.assertIn("isSupplementCover(text)", BIB)
        self.assertIn("isSupplementCover(front.join", TITLE)
        self.assertNotIn('headings[i].contains(QStringLiteral("Supporting Information")', PAGE)
        self.assertNotIn('lines[i].text.contains(QStringLiteral("Supporting Information")', TITLE)
        self.assertIn("genuineSi", NATIVE)
        self.assertIn("acsArticle", NATIVE)

    def test_fallback_is_publisher_bounded_and_manual_safe(self):
        for token in ("pubs.acs.org/", "Cite This:", "10.1039/", "foundAuthors",
                      "isPrinterJobTitle", "publisherCitation", "Nature Communications"):
            self.assertIn(token, FRONT, token)
        self.assertIn("FrontMatter::acsTitle(first)", PAGE)
        self.assertIn("FrontMatter::rscTitle(first)", PAGE)
        self.assertIn('publisher_cover_verified', PAGE)
        self.assertIn('publisher_cover_verified', BIB)
        self.assertIn('== QStringLiteral("manual")', BIB)
        self.assertIn("publisher-manual", NATIVE)
        self.assertIn("publisher-auto", NATIVE)

    def test_citation_suffix_and_article_numbers_are_preserved(self):
        self.assertIn("article_number", FRONT)
        self.assertIn("articleNumberPattern", BIB)
        self.assertIn("const QString prefix = candidate.left(", BIB)
        self.assertIn("printCue.match(prefix)", BIB)
        self.assertIn("1234-1245", NATIVE)
        self.assertIn("12345", NATIVE)

    def test_p0_historic_author_reconciliation_and_no_manual_loss(self):
        library = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
        meta = (ROOT / "src/lunametadataengine.h").read_text(encoding="utf-8")
        native = (ROOT / "tests/native_metadata/main.cpp").read_text(encoding="utf-8")
        self.assertIn("reconcileVerifiedAuthorship", BIB)
        self.assertIn('verifiedProvider(structuredSource)', BIB)
        self.assertIn('value["legacyAuthors"]', BIB)
        self.assertIn('original.value("manual").toBool()', BIB)
        self.assertIn('original.value("documentKind").toString()', BIB)
        self.assertIn('authorsReconciledFromProvider', BIB)
        self.assertIn('verifiedAuthorConflict', BIB)
        self.assertIn('authorListReviewCandidate', BIB)
        self.assertIn('legacyStructuredAuthors', BIB)
        self.assertIn('bibliography_.reconcileVerifiedAuthorship(identity)', library)
        self.assertIn('needsAuthorRepair', library)
        self.assertIn('trustedOrdered', BIB)
        self.assertIn('needsVerifiedAuthorRefresh', BIB)
        self.assertIn('trustedSavedDoi', BIB)
        self.assertIn('freshProviderResponse', meta)
        self.assertIn('verifiedList', meta)
        self.assertIn('const bool verifiedList=sourceVerified(structuredSource)', meta)
        for token in (
            'p0-historic-automated-author-reconciliation',
            'Stored DOI-confirmed ordered list outranks old displayed Hao Fang',
            'One-time lazy reconciliation is durable and idempotent',
            'A user-edited author always overrides an older provider list',
            'Two conflicting DOI-verified ordered lists require review',
            'Unproven structured names must not silently replace printed last author',
        ):
            self.assertIn(token, native, token)

    def test_no_user_document_or_online_payload_in_new_header(self):
        for forbidden in ("QNetworkAccessManager", "QFile::open", "QFileInfo",
                          "QSettings", "https://api.", "Invoke-WebRequest"):
            self.assertNotIn(forbidden, FRONT, forbidden)
        self.assertIn("Only a verified SI COVER", TITLE)


if __name__ == "__main__":
    unittest.main()
