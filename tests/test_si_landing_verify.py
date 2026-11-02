"""Offline synthetic contract: metadata match != reachable SI landing."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import MagicMock, patch
from urllib.error import HTTPError, URLError

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from si_finder import SiCandidate, SiLookupResult
from si_landing_verify import (MAX_LANDINGS, _request, official_landing_status,
                               verify_landing_candidates)

DOI = "10.1021/jacs.6c01234"  # Synthetic only.
BASE = "https://acs.figshare.com/articles/journal_contribution/Synthetic/"


def fixture(n=2, status="candidates"):
    return SiLookupResult(DOI, status, tuple(
        SiCandidate(123, DOI, "Synthetic data", f"si{i}.pdf", BASE + "123")
        for i in range(n)), "https://doi.org/" + DOI)


class LandingVerifierTests(unittest.TestCase):
    def test_two_si_one_verified_landing_one_probe(self):
        calls = []
        def probe(url):
            calls.append(url)
            return "reachable_html"
        result = verify_landing_candidates(fixture(), probe)
        self.assertEqual(result.status, "verified")
        self.assertEqual(len(calls), 1)
        self.assertEqual([x.status for x in result.candidates], ["landing_verified"] * 2)

    def test_metadata_only_does_not_verify(self):
        self.assertEqual(fixture().candidates[0].status, "metadata_match_link_unchecked")
        self.assertEqual(verify_landing_candidates(fixture(), lambda _: "not_found").status, "needs_review")

    def test_partial_search_never_full_verified(self):
        self.assertEqual(verify_landing_candidates(fixture(status="partial"),
                                                   lambda _: "reachable_html").status, "partial_verified")

    def test_no_network_offline(self):
        def forbidden(_):
            self.fail("network attempted")
        self.assertEqual(verify_landing_candidates(fixture(), forbidden, offline=True).status, "offline")
        with patch.dict(os.environ, {"LUNA_SI_OFFLINE": "1"}):
            self.assertEqual(verify_landing_candidates(fixture(), forbidden).status, "offline")

    def test_non_candidates_unchanged(self):
        source = fixture(status="rate_limited")
        self.assertIs(verify_landing_candidates(source, lambda _: self.fail("network")), source)

    def test_reject_untrusted_candidate_before_probe(self):
        source = fixture()
        bad = SiCandidate(123, DOI, "Synthetic", "si.pdf", "https://acs.figshare.com.evil.invalid/articles/a/1")
        source = SiLookupResult(DOI, "candidates", (bad,), source.fallback_url)
        result = verify_landing_candidates(source, lambda _: self.fail("network"))
        self.assertEqual(result.candidates[0].status, "landing_unchecked_unsafe_url")

    def test_same_url_with_different_article_ids_never_reuses_verification(self):
        good = fixture(1).candidates[0]
        forged = SiCandidate(456, DOI, "Wrong record", "wrong.pdf", good.landing_url)
        for ordered in ((good, forged), (forged, good)):
            with self.subTest(first_id=ordered[0].article_id):
                source = SiLookupResult(DOI, "candidates", ordered, "https://doi.org/" + DOI)
                calls = []
                result = verify_landing_candidates(
                    source, lambda url: calls.append(url) or "reachable_html")
                self.assertEqual(len(calls), 1)
                self.assertEqual(result.status, "partial_verified")
                statuses = {c.article_id: c.status for c in result.candidates}
                self.assertEqual(statuses[123], "landing_verified")
                self.assertEqual(statuses[456], "landing_unchecked_unsafe_url")

    def test_multiple_urls_partial_and_failure(self):
        first = fixture(1).candidates[0]
        second = SiCandidate(456, DOI, "Synthetic", "si2.pdf", BASE + "456")
        source = SiLookupResult(DOI, "candidates", (first, second), "https://doi.org/" + DOI)
        result = verify_landing_candidates(source, lambda u: "reachable_html" if u.endswith("123") else "rate_limited")
        self.assertEqual(result.status, "partial_verified")
        self.assertEqual([x.status for x in result.candidates], ["landing_verified", "landing_unchecked_rate_limited"])

    def test_unknown_or_raised_probe_never_verifies(self):
        for probe in (lambda _: "success", lambda _: (_ for _ in ()).throw(URLError("offline"))):
            with self.subTest(probe=probe):
                self.assertEqual(verify_landing_candidates(fixture(), probe).status, "needs_review")

    def test_bounded_unique_landing_checks(self):
        base = fixture(1).candidates[0]
        source = SiLookupResult(DOI, "candidates", tuple(
            SiCandidate(100+i, DOI, base.title, f"si{i}.pdf", BASE + str(100+i))
            for i in range(MAX_LANDINGS+3)), "https://doi.org/" + DOI)
        calls = []
        result = verify_landing_candidates(source, lambda u: calls.append(u) or "reachable_html")
        self.assertEqual(len(calls), MAX_LANDINGS)
        self.assertEqual(result.status, "partial_verified")
        self.assertEqual(result.candidates[-1].status, "landing_unchecked_verification_limit")

    def test_unsafe_landing_rejected_before_http(self):
        with patch("si_landing_verify._request", side_effect=AssertionError("HTTP")):
            self.assertEqual(official_landing_status("http://acs.figshare.com/articles/x/1"), "unsafe_url")

    def test_head_405_falls_back_to_bounded_get(self):
        with patch("si_landing_verify._request", side_effect=["head_unsupported", "reachable_html"]) as req:
            self.assertEqual(official_landing_status(BASE + "123"), "reachable_html")
            self.assertEqual([call.args[0] for call in req.call_args_list], ["HEAD", "GET"])

    def test_head_200_html_is_not_enough_without_get(self):
        # An HTML HEAD can succeed even when the actual page GET is blocked.
        with patch("si_landing_verify._request",
                   side_effect=["reachable_html", "http_unchecked"]) as req:
            self.assertEqual(official_landing_status(BASE + "123"), "http_unchecked")
            self.assertEqual([call.args[0] for call in req.call_args_list],
                             ["HEAD", "GET"])
        source = fixture(1)
        with patch("si_landing_verify._request",
                   side_effect=["reachable_html", "http_unchecked"]):
            result = verify_landing_candidates(source, official_landing_status)
        self.assertEqual(result.status, "needs_review")
        self.assertEqual(result.candidates[0].status, "landing_unchecked_http_unchecked")

    def test_head_and_bounded_get_html_both_required_for_verification(self):
        with patch("si_landing_verify._request",
                   side_effect=["reachable_html", "reachable_html"]) as req:
            self.assertEqual(official_landing_status(BASE + "123"), "reachable_html")
            self.assertEqual([call.args[0] for call in req.call_args_list],
                             ["HEAD", "GET"])

    def test_head_404_does_not_attempt_get(self):
        with patch("si_landing_verify._request", return_value="not_found") as req:
            self.assertEqual(official_landing_status(BASE + "123"), "not_found")
            req.assert_called_once_with("HEAD", BASE + "123")

    def test_get_429_after_successful_head_stops_other_landings(self):
        source = SiLookupResult(DOI, "candidates", tuple(
            SiCandidate(article_id, DOI, "Synthetic", f"si{article_id}.pdf",
                        BASE + str(article_id))
            for article_id in (123, 456)
        ), "https://doi.org/" + DOI)
        with patch("si_landing_verify._request",
                   side_effect=["reachable_html", "rate_limited"]) as req:
            result = verify_landing_candidates(source, official_landing_status)
        self.assertEqual([call.args[0] for call in req.call_args_list],
                         ["HEAD", "GET"])
        self.assertEqual(result.status, "needs_review")
        self.assertTrue(all(c.status == "landing_unchecked_rate_limited"
                            for c in result.candidates))
        self.assertIn("landings_checked=1", result.diagnostic)

    def test_redirect_never_promotes(self):
        with patch("si_landing_verify._request", return_value="redirect_unchecked"):
            self.assertEqual(official_landing_status(BASE + "123"), "redirect_unchecked")

    def test_http_probe_never_reads_body(self):
        response = MagicMock()
        response.status = 200
        response.headers = {"Content-Type": "text/html; charset=utf-8"}
        response.geturl.return_value = BASE + "123"
        opener = MagicMock()
        opener.open.return_value.__enter__.return_value = response
        with patch("si_landing_verify.build_opener", return_value=opener):
            self.assertEqual(_request("HEAD", BASE + "123"), "reachable_html")
        response.read.assert_not_called()

    def test_get_fallback_requests_one_byte_and_no_body(self):
        response = MagicMock()
        response.status = 206
        response.headers = {"Content-Type": "text/html"}
        response.geturl.return_value = BASE + "123"
        opener = MagicMock()
        opener.open.return_value.__enter__.return_value = response
        with patch("si_landing_verify.build_opener", return_value=opener):
            self.assertEqual(_request("GET", BASE + "123"), "reachable_html")
        request = opener.open.call_args.args[0]
        self.assertEqual(request.get_header("Range"), "bytes=0-0")
        response.read.assert_not_called()

    def test_mixed_parent_doi_never_probes_or_verifies(self):
        original = fixture(1)
        other = SiCandidate(123, "10.1021/jacs.6c99999", "Wrong paper",
                            "wrong-si.pdf", BASE + "123")
        source = SiLookupResult(DOI, "candidates", (other,), original.fallback_url)
        result = verify_landing_candidates(source, lambda _: self.fail("network"))
        self.assertEqual(result.status, "needs_review")
        self.assertEqual(result.candidates[0].status, "landing_unchecked_doi_mismatch")

    def test_invalid_parent_doi_never_probes_or_verifies(self):
        source = SiLookupResult("invalid-doi", "candidates", fixture(1).candidates,
                                "https://doi.org/invalid-doi")
        result = verify_landing_candidates(source, lambda _: self.fail("network"))
        self.assertEqual(result.status, "needs_review")
        self.assertEqual(result.diagnostic, "invalid_parent_doi")
        self.assertEqual(result.candidates[0].status,
                         "landing_unchecked_invalid_parent_doi")

    def test_empty_204_head_does_not_certify_landing(self):
        response = MagicMock()
        response.status = 204
        response.headers = {"Content-Type": "text/html"}
        response.geturl.return_value = BASE + "123"
        opener = MagicMock()
        opener.open.return_value.__enter__.return_value = response
        with patch("si_landing_verify.build_opener", return_value=opener):
            self.assertEqual(_request("HEAD", BASE + "123"), "http_unchecked")
        response.read.assert_not_called()

    def test_non_html_never_verified(self):
        response = MagicMock()
        response.status = 200
        response.headers = {"Content-Type": "application/pdf"}
        response.geturl.return_value = BASE + "123"
        opener = MagicMock()
        opener.open.return_value.__enter__.return_value = response
        with patch("si_landing_verify.build_opener", return_value=opener):
            self.assertEqual(_request("HEAD", BASE + "123"), "non_html")
        response.read.assert_not_called()


    def test_landing_429_stops_later_unique_urls_but_keeps_prior_verified(self):
        # HEAD/GET status 429 is global back-pressure, not a per-file miss.
        source = SiLookupResult(DOI, "candidates", tuple(
            SiCandidate(article_id, DOI, "Synthetic", f"si{article_id}.pdf",
                        BASE + str(article_id))
            for article_id in (123, 456, 789)
        ), "https://doi.org/" + DOI)
        calls = []
        def probe(url):
            calls.append(url)
            if url.endswith("/123"):
                return "reachable_html"
            if url.endswith("/456"):
                return "rate_limited"
            self.fail("a landing was probed after HTTP 429")

        result = verify_landing_candidates(source, probe)
        self.assertEqual(result.status, "partial_verified")
        self.assertEqual([c.status for c in result.candidates],
                         ["landing_verified", "landing_unchecked_rate_limited",
                          "landing_unchecked_rate_limited"])
        self.assertEqual(len(calls), 2)
        self.assertIn("landings_checked=2", result.diagnostic)
        self.assertIn("landing_rate_limited=1", result.diagnostic)

    def test_raised_http_429_stops_probes_and_never_verifies(self):
        source = SiLookupResult(DOI, "candidates", tuple(
            SiCandidate(article_id, DOI, "Synthetic", f"si{article_id}.pdf",
                        BASE + str(article_id))
            for article_id in (123, 456)
        ), "https://doi.org/" + DOI)
        calls = []
        def probe(url):
            calls.append(url)
            raise HTTPError(url, 429, "synthetic rate limit", {}, None)

        result = verify_landing_candidates(source, probe)
        self.assertEqual(result.status, "needs_review")
        self.assertEqual(len(calls), 1)
        self.assertTrue(all(c.status == "landing_unchecked_rate_limited"
                            for c in result.candidates))
        self.assertIn("landings_checked=1", result.diagnostic)

    def test_landing_404_does_not_trigger_rate_limit_backpressure(self):
        source = SiLookupResult(DOI, "candidates", tuple(
            SiCandidate(article_id, DOI, "Synthetic", f"si{article_id}.pdf",
                        BASE + str(article_id))
            for article_id in (123, 456)
        ), "https://doi.org/" + DOI)
        calls = []
        def probe(url):
            calls.append(url)
            return "not_found" if url.endswith("/123") else "reachable_html"

        result = verify_landing_candidates(source, probe)
        self.assertEqual(result.status, "partial_verified")
        self.assertEqual([c.status for c in result.candidates],
                         ["landing_unchecked_not_found", "landing_verified"])
        self.assertEqual(len(calls), 2)
        self.assertIn("landing_rate_limited=0", result.diagnostic)


if __name__ == "__main__":
    unittest.main()
