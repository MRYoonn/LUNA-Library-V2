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

    def test_unsafe_public_filenames_cannot_be_offered_as_local_si_labels(self):
        # Figshare JSON is untrusted display data. Windows reserved names,
        # control/format characters and path-special characters must not be
        # mistaken for safe local SI filenames.
        invalid = (
            "CON.pdf", "nul.txt", "COM1.pdf", "LPT9.xlsx", "COM¹.txt",
            "NUL .pdf", "si:evil.pdf", "bad|name.zip", "bad?.cif",
            "trailing.pdf.", "trailing.pdf ", " leading.pdf",
            "si\\tname.pdf", "si\\x1bname.pdf", "report\\u202e.pdf",
            "si\\u200b.pdf", ".", "..",
        )
        files = [{"name": name} for name in invalid] + [
            {"name": "한국어_SI_ΔE.pdf"}, {"name": "supporting-info.cif"},
        ]
        result = lookup_acs_figshare(
            DOI, FakeApi(details={123: article(123, files=files)}))
        self.assertEqual(result.status, "partial")
        self.assertEqual([c.filename for c in result.candidates],
                         ["한국어_SI_ΔE.pdf", "supporting-info.cif"])
        self.assertIn(f"invalid_file_entries={len(invalid)}", result.diagnostic)

    def test_only_unsafe_public_filenames_require_review(self):
        files = [{"name": name} for name in ("CON.pdf", "a\\u202eb.pdf", "si:1.pdf")]
        result = lookup_acs_figshare(
            DOI, FakeApi(details={123: article(123, files=files)}))
        self.assertEqual((result.status, result.candidates), ("needs_review", ()))
        self.assertIn("invalid_file_entries=3", result.diagnostic)

    def test_same_article_duplicate_filenames_are_ambiguous(self):
        # Different Figshare file entries can have indistinguishable labels
        # on Windows, or after Unicode compatibility normalization. Without
        # a per-file verified identity, neither entry may be offered as SI1.
        files = [
            {"name": "SI.pdf", "id": 1001},
            {"name": "si.PDF", "id": 1002},
            {"name": "Ａ-data.pdf", "id": 1003},
            {"name": "A-data.pdf", "id": 1004},
            {"name": "unique.cif", "id": 1005},
        ]
        result = lookup_acs_figshare(
            DOI, FakeApi(details={123: article(123, files=files)}))
        self.assertEqual(result.status, "partial")
        self.assertEqual([c.filename for c in result.candidates], ["unique.cif"])
        self.assertIn("ambiguous_file_entries=4", result.diagnostic)
        self.assertIn("invalid_file_entries=0", result.diagnostic)

    def test_only_ambiguous_file_labels_never_report_not_found(self):
        result = lookup_acs_figshare(
            DOI, FakeApi(details={123: article(123, files=[
                {"name": "SI.pdf"}, {"name": "si.pdf"}])}))
        self.assertEqual((result.status, result.candidates), ("needs_review", ()))
        self.assertIn("ambiguous_file_entries=2", result.diagnostic)

    def test_same_filename_in_distinct_articles_is_not_ambiguous(self):
        # The article ID + landing URL distinguishes separate public records.
        result = lookup_acs_figshare(DOI, FakeApi(
            search=[{"id": 123}, {"id": 456}],
            details={
                123: article(123, files=[{"name": "SI.pdf"}]),
                456: article(456, files=[{"name": "SI.pdf"}]),
            }))
        self.assertEqual(result.status, "candidates")
        self.assertEqual(len(result.candidates), 2)
        self.assertEqual({c.article_id for c in result.candidates}, {123, 456})
        self.assertIn("ambiguous_file_entries=0", result.diagnostic)

    def test_documented_figshare_file_ids_remain_unverified_display_metadata(self):
        # The official PublicFilePresenter carries a numeric id. The
        # untrusted download_url is never exposed or fetched.
        files = [
            {"name": "SI1.pdf", "id": 3000002,
             "download_url": "https://example.invalid/do-not-fetch"},
            {"name": "SI2.cif", "id": 3000003},
            {"name": "legacy.txt"},  # No durable per-file identity.
        ]
        result = lookup_acs_figshare(
            DOI, FakeApi(details={123: article(123, files=files)}))
        self.assertEqual(result.status, "candidates")
        self.assertEqual([c.file_id for c in result.candidates],
                         [3000002, 3000003, None])
        self.assertTrue(all(c.status == "metadata_match_link_unchecked"
                            for c in result.candidates))
        self.assertNotIn("example.invalid", json.dumps(result.to_dict()))

    def test_invalid_figshare_file_ids_fail_closed(self):
        bad_ids = (True, False, 0, -1, "3000002", 1.5, 10**15, None)
        files = [{"name": f"bad{i}.pdf", "id": value}
                 for i, value in enumerate(bad_ids)]
        files.append({"name": "good.pdf", "id": 42})
        result = lookup_acs_figshare(
            DOI, FakeApi(details={123: article(123, files=files)}))
        self.assertEqual(result.status, "partial")
        self.assertEqual([(c.filename, c.file_id) for c in result.candidates],
                         [("good.pdf", 42)])
        self.assertIn("invalid_file_entries=8", result.diagnostic)

    def test_duplicate_figshare_file_ids_block_both_entries(self):
        files = [
            {"name": "first.pdf", "id": 321},
            {"name": "second.cif", "id": 321},
            {"name": "third.pdf", "id": 322},
        ]
        result = lookup_acs_figshare(
            DOI, FakeApi(details={123: article(123, files=files)}))
        self.assertEqual(result.status, "partial")
        self.assertEqual([(c.filename, c.file_id) for c in result.candidates],
                         [("third.pdf", 322)])
        self.assertIn("ambiguous_file_ids=2", result.diagnostic)
        self.assertIn("ambiguous_file_entries=0", result.diagnostic)
        all_bad = lookup_acs_figshare(
            DOI, FakeApi(details={123: article(123, files=files[:2])}))
        self.assertEqual((all_bad.status, all_bad.candidates),
                         ("needs_review", ()))

    def test_cross_article_file_id_collision_quarantines_both_owners(self):
        # A globally identified Figshare file cannot safely belong to two
        # different public articles, even if their filenames differ.
        api = FakeApi(
            search=[{"id": 123}, {"id": 456}],
            details={
                123: article(123, files=[
                    {"name": "one.pdf", "id": 901},
                    {"name": "distinct.pdf", "id": 902},
                ]),
                456: article(456, files=[
                    {"name": "two.cif", "id": 901},
                    {"name": "third.pdf", "id": 903},
                ]),
            })
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual(result.status, "partial")
        self.assertEqual([(c.article_id, c.filename, c.file_id)
                          for c in result.candidates],
                         [(123, "distinct.pdf", 902), (456, "third.pdf", 903)])
        self.assertIn("cross_article_file_id_entries=2", result.diagnostic)
        self.assertTrue(all(c.status == "metadata_match_link_unchecked"
                            for c in result.candidates))

    def test_only_cross_article_collisions_require_review(self):
        api = FakeApi(
            search=[{"id": 123}, {"id": 456}],
            details={
                123: article(123, files=[{"name": "a.pdf", "id": 901}]),
                456: article(456, files=[{"name": "b.pdf", "id": 901}]),
            })
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual((result.status, result.candidates), ("needs_review", ()))
        self.assertIn("cross_article_file_id_entries=2", result.diagnostic)

    def test_collision_hidden_by_same_article_duplicate_label(self):
        # Both article 123 entries disappear from the display due to a
        # case-insensitive filename collision. Their IDs still count as
        # provider ownership evidence: article 456 must not inherit ID 901.
        api = FakeApi(
            search=[{"id": 123}, {"id": 456}],
            details={
                123: article(123, files=[
                    {"name": "SI.pdf", "id": 901},
                    {"name": "si.PDF", "id": 902},
                ]),
                456: article(456, files=[
                    {"name": "apparently-safe.pdf", "id": 901},
                    {"name": "independent.cif", "id": 903},
                ]),
            })
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual(result.status, "partial")
        self.assertEqual([(c.article_id, c.filename, c.file_id)
                          for c in result.candidates],
                         [(456, "independent.cif", 903)])
        self.assertIn("ambiguous_file_entries=2", result.diagnostic)
        self.assertIn("cross_article_file_id_entries=1", result.diagnostic)

    def test_collision_hidden_by_invalid_filename(self):
        # An unsafe path is never displayed, but its otherwise valid file ID
        # must still prevent an unrelated article from claiming ownership.
        api = FakeApi(
            search=[{"id": 123}, {"id": 456}],
            details={
                123: article(123, files=[
                    {"name": "../unsafe.pdf", "id": 901},
                ]),
                456: article(456, files=[
                    {"name": "apparently-safe.pdf", "id": 901},
                    {"name": "independent.cif", "id": 903},
                ]),
            })
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual(result.status, "partial")
        self.assertEqual([(c.article_id, c.filename, c.file_id)
                          for c in result.candidates],
                         [(456, "independent.cif", 903)])
        self.assertIn("invalid_file_entries=1", result.diagnostic)
        self.assertIn("cross_article_file_id_entries=1", result.diagnostic)

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

    def test_malformed_file_entries_mark_listing_partial(self):
        # A reachable article can still contain an incomplete SI listing.
        # Never certify the remaining valid entries as a complete set.
        api = FakeApi(details={123: article(123, files=[
            {"name": "valid-si.pdf"}, {"name": "../unsafe.pdf"},
            {"name": ""}, {"name": 42}, None,
        ])})
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual(result.status, "partial")
        self.assertEqual([c.filename for c in result.candidates], ["valid-si.pdf"])
        self.assertIn("invalid_file_entries=4", result.diagnostic)
        self.assertTrue(all(c.status == "metadata_match_link_unchecked"
                            for c in result.candidates))

    def test_all_malformed_file_entries_require_review_not_not_found(self):
        api = FakeApi(details={123: article(123, files=[
            {"name": "../unsafe.pdf"}, None, {"name": 9},
        ])})
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual(result.status, "needs_review")
        self.assertEqual(result.candidates, ())
        self.assertIn("invalid_file_entries=3", result.diagnostic)

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

    def test_rejected_search_records_never_mark_remaining_si_complete(self):
        # Even when a valid SI exists, discarded DOI/ID/summary records mean
        # that the search was not exhaustively verified.
        cases = (
            FakeApi(search=[{"id": 123}, {"id": 456}],
                    details={123: article(123), 456: article(456, doi="10.1021/jacs.6c99999")}),
            FakeApi(search=[{"id": 123}, {"id": "not-a-number"}],
                    details={123: article(123)}),
            FakeApi(search=[{"id": 123}, None],
                    details={123: article(123)}),
        )
        for api in cases:
            with self.subTest(search=api.search):
                result = lookup_acs_figshare(DOI, api)
                self.assertEqual(result.status, "partial")
                self.assertEqual(len(result.candidates), 2)
                self.assertIn("rejected=1", result.diagnostic)

    def test_full_search_page_with_no_files_is_not_exhaustive_not_found(self):
        # Page two is unknown when the first page fills the provider cap.
        api = FakeApi(search=[{"id": i} for i in range(1, 21)],
                      details={i: article(i, files=[]) for i in range(1, 21)})
        result = lookup_acs_figshare(DOI, api)
        self.assertEqual(result.status, "needs_review")
        self.assertEqual(result.candidates, ())
        self.assertIn("search_page_full=1", result.diagnostic)

    def test_rate_limit(self):
        def blocked(method, url, body):
            raise HTTPError(url, 429, "rate limited", {}, None)
        self.assertEqual(lookup_acs_figshare(DOI, blocked).status, "rate_limited")

    def test_detail_rate_limit_stops_later_requests(self):
        # A provider 429 is back-pressure: do not keep issuing GET requests.
        # Previously the detail loop swallowed HTTPError(429) as an ordinary
        # failed detail, which could hammer the provider and hide throttling.
        calls = []
        def limited(method, url, body):
            calls.append((method, url))
            if method == "POST":
                return [{"id": 123}, {"id": 456}, {"id": 789}]
            if url.endswith("/123"):
                return article(123, files=[{"name": "existing.pdf", "id": 101}])
            if url.endswith("/456"):
                raise HTTPError(url, 429, "rate limited", {}, None)
            self.fail("a request was issued after provider rate limiting")

        result = lookup_acs_figshare(DOI, limited)
        self.assertEqual(result.status, "partial")
        self.assertEqual([c.filename for c in result.candidates], ["existing.pdf"])
        self.assertIn("detail_rate_limited=1", result.diagnostic)
        self.assertEqual([method for method, _ in calls], ["POST", "GET", "GET"])
        self.assertEqual(result.candidates[0].status, "metadata_match_link_unchecked")

    def test_first_detail_rate_limit_never_reports_not_found(self):
        calls = []
        def limited(method, url, body):
            calls.append((method, url))
            if method == "POST":
                return [{"id": 123}, {"id": 456}]
            raise HTTPError(url, 429, "rate limited", {}, None)

        result = lookup_acs_figshare(DOI, limited)
        self.assertEqual((result.status, result.candidates), ("rate_limited", ()))
        self.assertEqual(result.fallback_url, "https://doi.org/" + DOI)
        self.assertIn("detail_rate_limited=1", result.diagnostic)
        self.assertEqual([method for method, _ in calls], ["POST", "GET"])

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
