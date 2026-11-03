"""Fail-closed LUNA Supporting Information parent-bibliography source contracts.

These checks do not fake a PDF parser or network request. Actual field-level
behavior is exercised by the Windows Qt tests/native_metadata/main.cpp binary.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SI = (ROOT / "src/lunasiidentity.h").read_text(encoding="utf-8")
FIRST = (ROOT / "src/lunafirstpage.h").read_text(encoding="utf-8")
BIB = (ROOT / "src/lunabibliography.h").read_text(encoding="utf-8")
UI = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
OVERLAY = (ROOT / "tools/apply_overlay.py").read_text(encoding="utf-8")
NATIVE = (ROOT / "tests/native_metadata/main.cpp").read_text(encoding="utf-8")


class SIOriginalBibliographyContract(unittest.TestCase):
    def test_si_is_proven_cover_not_navbar_or_filename(self):
        for token in (
            "FrontMatter::isSupplementCover(firstPage)",
            "Metadata::collectCandidates({firstPage.left(11000)})",
            "candidates.size()==1",
            "candidates.first().explicitMarker",
            "candidates.first().score>=105",
            'filename_hint_unverified',
            "siArticleDoiCandidate",
            "siCoverTitle",
            "SIIdentity::sameArticleTitle",
        ):
            self.assertIn(token, SI + FIRST + BIB, token)
        for unexpected in (
            "QNetworkAccessManager", "QNetworkRequest",
            "QSettings", "QFile::open", "readFromFile",
            "Crossref?", "system(", "CreateProcess(",
        ):
            self.assertNotIn(unexpected, SI, unexpected)
        self.assertIn("return SIIdentity::evidence(first,fileName);", FIRST)

    def test_original_article_link_must_match_doi_and_strict_cover_title(self):
        for token in (
            "bool linkSIToParent(",
            "SIIdentity::isArticleTitle(title)",
            "SIIdentity::sameArticleTitle(cover,title)",
            'Metadata::normalizeDoi(article.value("doi").toString())!=doi',
            'si.value("documentKind").toString()!=QStringLiteral("supporting_information")',
            'si.value("titleSource").toString()==QStringLiteral("manual")',
            "bool linkSIFromLocal(",
            "store.childKeys()",
            "foundDoi!=doi",
            "bool markSIReview(",
            '"siLinkStatus"',
            '"siParentDoi"',
        ):
            self.assertIn(token, BIB, token)

    def test_live_reader_and_import_worker_share_same_si_pipeline(self):
        self.assertIn('luna::extractFirstPage(snapshot,nullptr,QFileInfo(identity).fileName())', UI)
        self.assertIn('luna::extractFirstPage(document,nullptr,QFileInfo(path).fileName())', UI)
        self.assertEqual(UI.count("resolveSIBibliography(identity,restore);"), 2)
        self.assertIn("bibliography_.linkSIFromLocal(identity)", UI)
        self.assertIn("bibliography_.linkSIToParent(identity,parent,doi,false)", UI)
        self.assertIn('QStringLiteral("si_parent_title_or_doi_mismatch")', UI)
        self.assertNotIn("broad title search", UI)
        self.assertIn('QStringLiteral("SI · ")+displayTitle', UI)
        self.assertIn('bibliography.value("siParentDoi"', UI)

    def test_windows_build_copies_the_real_si_header(self):
        self.assertIn('folder / "lunasiidentity.h": (own / "lunasiidentity.h").read_bytes()', OVERLAY)
        for token in (
            "checkpoint(\"si-parent-bibliography\")",
            "ACS SI cover DOI/title extracted independently",
            "RSC ESI cover parsed",
            "SI parent enrichment cannot overwrite conflicting manual title",
            "changed DOI on linked SI marks review but preserves bibliography",
            "ACS main PDF navbar is not SI",
            "filename-only SI stays review-only",
            "verified Crossref article connects to matching SI cover",
        ):
            self.assertIn(token, NATIVE, token)


if __name__ == "__main__":
    unittest.main()
