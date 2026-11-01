"""Offline contract tests for the read-only Figshare SI finder."""
import json
from pathlib import Path
import sys
import unittest
from urllib.error import HTTPError

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from si_finder import (API_ROOT, lookup_acs_figshare, normalize_doi,
                       safe_figshare_landing, official_json)

DOI = "10.1021/jacs.6c01234"  # Synthetic test identifier; not a real paper.


def article(id_, doi=DOI, url=None, files=None):
    return {"id": id_, "resource_doi": doi, "title": "Synthetic supporting data",
            "url_public_html": url or f"https://acs.figshare.com/articles/journal_contribution/Fixture/{id_}",
            "files": files if files is not None else [
                {"name": "si1.pdf", "download_url": "https://example.invalid/not-to-fetch"},
                {"name": "si2.pdf"}]}


class FakeApi:
    def __init__(self, search=None, details=None):
        self.search = search if search is not None else [{"id": 123}]
        self.details = details if details is not None else {123: article(123)}
        self.calls = []

    def __call__(self, method, url, body):
        self.calls.append((method, url, body))
        if method == "POST":
            return self.search
        return self.details.get(int(url.rsplit("/", 1)[-1])) or self._missing(url)

    @staticmethod
    def _missing(url):
        raise HTTPError(url, 404, "not found", {}, None)


class SiFinderTests(unittest.TestCase):
    def test_two_files_one_article(self):
        api = FakeApi()
        result = lookup_acs_figshare("https://doi.org/10.1021/JACS.6C01234", api)
        self.assertEqual(result.status, "candidates")
        self.assertEqual([c.filename for c in result.candidates], ["si1.pdf", "si2.pdf"])
        self.assertTrue(all(c.status == "metadata_match_link_unchecked" for c in result.candidates))
        self.assertEqual(api.calls[0], ("POST", API_ROOT + "/articles/search",
                                        {"resource_doi": DOI, "page": 1, "page_size": 20}))
        self.assertEqual(api.calls[1][0], "GET")
        self.assertNotIn("example.invalid", json.dumps(result.to_dict()))

    def test_multiple_articles_and_dedup(self):
        api = FakeApi(search=[{"id": 123}, {"id": 123}, {"id": 456}],
                      details={123: article(123), 456: article(456, files=[{"name": "table.xlsx"}])})
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual(len(result.candidates), 3)
        self.assertEqual(len(api.calls), 3)

    def test_wrong_resource_doi_never_linked(self):
        api = FakeApi(details={123: article(123, doi="10.1021/jacs.6c09999")})
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual((result.status, result.candidates), ("needs_review", ()))

    def test_landing_article_id_must_match_verified_metadata(self):
        # An exact parent DOI is insufficient when the public landing URL
        # points at another article or a noncanonical/download route.
        for url in (
            "https://acs.figshare.com/articles/journal_contribution/Fixture/456",
            "https://acs.figshare.com/articles/journal_contribution/Fixture/123?download=1",
            "https://acs.figshare.com/articles/journal_contribution/../123",
            "https://acs.figshare.com/articles/journal_contribution/%2e%2e/123",
            "https://acs.figshare.com/articles/journal_contribution/Fixture/not-an-id",
            "https://other.figshare.com/articles/journal_contribution/Fixture/123",
        ):
            with self.subTest(url=url):
                self.assertEqual(
                    lookup_acs_figshare(DOI, FakeApi(details={123: article(123, url=url)})).status,
                    "needs_review")
        good = "https://acs.figshare.com/articles/journal_contribution/Fixture/123"
        self.assertEqual(safe_figshare_landing(good, 123), good)
        self.assertEqual(safe_figshare_landing(good, 456), "")

    def test_summary_id_mismatch_rejected(self):
        api = FakeApi(details={123: article(456)})
        self.assertEqual(lookup_acs_figshare(DOI, api).status, "needs_review")

    def test_block_untrusted_landing_and_credential_url(self):
        for url in ("http://acs.figshare.com/articles/a/1",
                    "https://acs.figshare.com.evil.org/articles/a/1",
                    ("https://user:pass" + chr(64) + "acs.figshare.com/articles/a/1"),
                    "https://acs.figshare.com:444/articles/a/1",
                    "https://acs.figshare.com/files/1",
                    "https://acs.figshare.com/articles/a/1#anchor"):
            with self.subTest(url=url):
                self.assertEqual(safe_figshare_landing(url), "")
                self.assertEqual(lookup_acs_figshare(DOI, FakeApi(details={123: article(123, url=url)})).status,
                                 "needs_review")

    def test_filename_path_injection_is_ignored(self):
        api = FakeApi(details={123: article(123, files=[
            {"name": "../bad.pdf"}, {"name": "good-si.pdf"},
            {"name": "evil\\name.pdf"}, {"name": "line\n.pdf"}])})
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual([c.filename for c in result.candidates], ["good-si.pdf"])

    def test_missing_resource_doi_is_not_a_match(self):
        api = FakeApi(details={123: article(123, doi="")})
        self.assertEqual(lookup_acs_figshare(DOI, api).status, "needs_review")

    def test_no_files_returns_no_link(self):
        api = FakeApi(details={123: article(123, files=[])})
        self.assertEqual(lookup_acs_figshare(DOI, api).status, "not_found")

    def test_bad_port_rejected(self):
        self.assertEqual(safe_figshare_landing("https://acs.figshare.com:bad/articles/a/1"), "")

    def test_full_page_requires_review_of_pagination(self):
        api = FakeApi(search=[{"id": i} for i in range(1, 21)],
                      details={i: article(i, files=[{"name": "si.pdf"}]) for i in range(1, 21)})
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual((result.status, len(result.candidates)), ("partial", 20))

    def test_bounded_file_list_reports_partial_not_complete(self):
        # More than 40 files are never silently presented as the full SI set.
        files = [{"name": f"si{i:02}.pdf"} for i in range(42)]
        api = FakeApi(details={123: article(123, files=files)})
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual(result.status, "partial")
        self.assertEqual(len(result.candidates), 40)
        self.assertIn("file_lists_truncated=1", result.diagnostic)
        self.assertEqual(result.candidates[0].filename, "si00.pdf")
        self.assertEqual(result.candidates[-1].filename, "si39.pdf")
        self.assertTrue(all(c.status == "metadata_match_link_unchecked"
                            for c in result.candidates))
        # Exactly the cap is complete unless another error is present.
        exact = lookup_acs_figshare(
            DOI, FakeApi(details={123: article(123, files=files[:40])}))
        self.assertEqual(exact.status, "candidates")
        self.assertIn("file_lists_truncated=0", exact.diagnostic)

    def test_truncated_second_article_keeps_first_and_partial_status(self):
        api = FakeApi(
            search=[{"id": 123}, {"id": 456}],
            details={
                123: article(123, files=[{"name": "si1.pdf"}]),
                456: article(456, files=[{"name": f"extra{i}.pdf"}
                                         for i in range(41)]),
            })
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual((result.status, len(result.candidates)), ("partial", 41))
        self.assertIn("file_lists_truncated=1", result.diagnostic)

    def test_boolean_detail_id_cannot_impersonate_numeric_article(self):
        api = FakeApi(search=[{"id": 1}], details={
            1: {**article(1), "id": True}})
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual((result.status, result.candidates), ("needs_review", ()))

    def test_no_unverified_doi(self):
        api = FakeApi()
        self.assertEqual(lookup_acs_figshare("10.1021/", api).status, "invalid_doi")
        self.assertEqual(lookup_acs_figshare("10.1039/d5sc00000a", api).status, "unsupported_publisher")
        self.assertEqual(api.calls, [])

    def test_offline_no_network(self):
        api = FakeApi()
        self.assertEqual(lookup_acs_figshare(DOI, api, offline=True).status, "offline")
        self.assertEqual(api.calls, [])

    def test_empty_search(self):
        self.assertEqual(lookup_acs_figshare(DOI, FakeApi(search=[])).status, "not_found")

    def test_bad_search_shape(self):
        self.assertEqual(lookup_acs_figshare(DOI, FakeApi(search={"message": "bad"})).status,
                         "provider_unavailable")

    def test_detail_error_is_not_success(self):
        api = FakeApi(details={})
        self.assertEqual(lookup_acs_figshare(DOI, api).status, "needs_review")

    def test_partial_if_one_detail_fails(self):
        api = FakeApi(search=[{"id": 123}, {"id": 456}], details={123: article(123)})
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual((result.status, len(result.candidates)), ("partial", 2))

    def test_rate_limit(self):
        def blocked(method, url, body):
            raise HTTPError(url, 429, "rate limited", {}, None)
        self.assertEqual(lookup_acs_figshare(DOI, blocked).status, "rate_limited")

    def test_reject_large_summary(self):
        api = FakeApi(search=[{"id": i} for i in range(21)])
        self.assertEqual(lookup_acs_figshare(DOI, api).status, "provider_unavailable")

    def test_no_binary_download_endpoints(self):
        api = FakeApi()
        lookup_acs_figshare(DOI, api)
        self.assertTrue(all("/download" not in url and "/files/" not in url
                            for _, url, _ in api.calls))

    def test_doi_normalization(self):
        self.assertEqual(normalize_doi("doi: 10.1021/JACS.6C01234."), DOI)
        self.assertEqual(normalize_doi("https://doi.org/10.1021/JACS.6C01234"), DOI)

    def test_official_api_host_is_fixed(self):
        with self.assertRaisesRegex(ValueError, "blocked_host"):
            official_json("GET", "https://api.figshare.com.evil.test/v2/articles/123")
        with self.assertRaisesRegex(ValueError, "blocked_endpoint"):
            official_json("GET", API_ROOT + "/account/articles/123")


if __name__ == "__main__":
    unittest.main()
