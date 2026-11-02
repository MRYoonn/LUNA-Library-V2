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

    def test_restored_untrusted_fallback_is_canonicalized_after_probe(self):
        source = SiLookupResult(DOI, "candidates", fixture(1).candidates,
                                "https://evil.invalid/phishing")
        checked = verify_landing_candidates(source, lambda _: "not_found")
        self.assertEqual(checked.status, "needs_review")
        self.assertEqual(checked.fallback_url, "https://doi.org/" + DOI)
        self.assertEqual(checked.candidates[0].status, "landing_unchecked_not_found")
        self.assertEqual(source.fallback_url, "https://evil.invalid/phishing")

    def test_offline_restored_verified_candidate_is_reset_without_network(self):
        stale = SiCandidate(123, DOI, "Synthetic", "si.pdf", BASE + "123",
                            status="landing_verified")
        source = SiLookupResult(DOI, "candidates", (stale,),
                                "https://evil.invalid/phishing")
        result = verify_landing_candidates(source, lambda _: self.fail("network"),
                                           offline=True)
        self.assertEqual(result.status, "offline")
        self.assertEqual(result.candidates[0].status, "landing_unchecked_offline")
        self.assertEqual(result.fallback_url, "https://doi.org/" + DOI)
        self.assertEqual(source.candidates[0].status, "landing_verified")

    def test_invalid_parent_has_no_untrusted_fallback_even_offline(self):
        source = SiLookupResult("invalid-doi", "candidates", fixture(1).candidates,
                                "https://evil.invalid/phishing")
        for offline in (False, True):
            with self.subTest(offline=offline):
                checked = verify_landing_candidates(
                    source, lambda _: self.fail("network"), offline=offline)
                self.assertEqual(checked.fallback_url, "")
                self.assertEqual(checked.status, "offline" if offline else "needs_review")
                self.assertTrue(all(c.status.startswith("landing_unchecked_")
                                    for c in checked.candidates))

    def test_restored_verified_result_offline_revokes_stale_evidence(self):
        previous = verify_landing_candidates(fixture(1), lambda _: "reachable_html")
        self.assertEqual(previous.status, "verified")
        restored = SiLookupResult(DOI, "verified", previous.candidates,
                                  "https://evil.invalid/stale")
        offline = verify_landing_candidates(
            restored, lambda _: self.fail("offline must not probe"), offline=True)
        self.assertEqual(offline.status, "offline")
        self.assertEqual(offline.candidates[0].status, "landing_unchecked_offline")
        self.assertEqual(offline.fallback_url, "https://doi.org/" + DOI)
        self.assertEqual(restored.candidates[0].status, "landing_verified")

    def test_restored_verified_result_failed_recheck_revokes_evidence(self):
        previous = verify_landing_candidates(fixture(1), lambda _: "reachable_html")
        checked = verify_landing_candidates(previous, lambda _: "not_found")
        self.assertEqual(checked.status, "needs_review")
        self.assertEqual(checked.candidates[0].status, "landing_unchecked_not_found")
        self.assertIn("landings_checked=1", checked.diagnostic)

    def test_partial_or_uncertain_restored_results_cannot_become_complete(self):
        partial = verify_landing_candidates(
            fixture(1, status="partial"), lambda _: "reachable_html")
        self.assertEqual(partial.status, "partial_verified")
        refreshed = verify_landing_candidates(partial, lambda _: "reachable_html")
        self.assertEqual(refreshed.status, "partial_verified")
        self.assertIn("search_partial=1", refreshed.diagnostic)
        uncertain = verify_landing_candidates(fixture(1), lambda _: "not_found")
        retried = verify_landing_candidates(uncertain, lambda _: "reachable_html")
        self.assertEqual(retried.status, "partial_verified")
        offline = verify_landing_candidates(retried, lambda _: self.fail("offline"),
                                            offline=True)
        self.assertEqual(offline.status, "offline")

    def test_restored_verified_without_candidates_fails_closed(self):
        forged = SiLookupResult(DOI, "verified", (),
                                "https://evil.invalid/forged")
        result = verify_landing_candidates(forged, lambda _: self.fail("network"))
        self.assertEqual(result.status, "needs_review")
        self.assertEqual(result.fallback_url, "https://doi.org/" + DOI)
        self.assertEqual(result.diagnostic, "verified_without_candidates")

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


    def test_restored_untrusted_title_cannot_be_verified_by_shared_landing(self):
        # The finder sanitizes titles, but restored result objects may bypass it.
        safe = SiCandidate(123, DOI, "Printable ΔG‡", "good.pdf", BASE + "123")
        unsafe = SiCandidate(123, DOI, "Spoof" + chr(0x202e), "bad.pdf", BASE + "123")
        source = SiLookupResult(DOI, "candidates", (unsafe, safe),
                                "https://doi.org/" + DOI)
        calls = []
        result = verify_landing_candidates(
            source, lambda url: calls.append(url) or "reachable_html")
        self.assertEqual([c.status for c in result.candidates],
                         ["landing_unchecked_unsafe_title", "landing_verified"])
        self.assertEqual(result.status, "partial_verified")
        self.assertEqual(calls, [BASE + "123"])

    def test_restored_non_string_title_never_probes(self):
        foreign = SiCandidate(123, DOI, {"spoof": "title"}, "si.pdf", BASE + "123")
        result = verify_landing_candidates(
            SiLookupResult(DOI, "candidates", (foreign,), "https://doi.org/" + DOI),
            lambda _: self.fail("unsafe title must not probe"))
        self.assertEqual(result.status, "needs_review")
        self.assertEqual(result.candidates[0].status, "landing_unchecked_unsafe_title")

    def test_foreign_unsafe_filename_never_promotes_even_with_valid_landing(self):
        # A restored/forged candidate may bypass si_finder sanitization.
        good = fixture(1).candidates[0]
        bad = SiCandidate(123, DOI, "Synthetic", "SI／other.pdf", good.landing_url,
                          file_id=9001)
        source = SiLookupResult(DOI, "candidates", (bad, good), "https://doi.org/" + DOI)
        calls = []
        result = verify_landing_candidates(
            source, lambda url: calls.append(url) or "reachable_html")
        self.assertEqual([c.status for c in result.candidates],
                         ["landing_unchecked_unsafe_filename", "landing_verified"])
        self.assertEqual(len(calls), 1)

    def test_foreign_duplicate_file_ids_never_reuse_verified_article_landings(self):
        # Even across two articles, one Figshare file ID cannot be two SI files.
        entries = (
            SiCandidate(123, DOI, "Synthetic", "first.pdf", BASE + "123", file_id=8123),
            SiCandidate(456, DOI, "Synthetic", "second.pdf", BASE + "456", file_id=8123),
            SiCandidate(789, DOI, "Synthetic", "third.pdf", BASE + "789", file_id=9000),
        )
        source = SiLookupResult(DOI, "candidates", entries, "https://doi.org/" + DOI)
        calls = []
        result = verify_landing_candidates(
            source, lambda url: calls.append(url) or "reachable_html")
        self.assertEqual([c.status for c in result.candidates],
                         ["landing_unchecked_ambiguous_file_identity",
                          "landing_unchecked_ambiguous_file_identity",
                          "landing_verified"])
        self.assertEqual(calls, [BASE + "789"])
        self.assertEqual(result.status, "partial_verified")

    def test_foreign_compatibility_aliases_and_invalid_file_id_fail_closed(self):
        entries = (
            SiCandidate(123, DOI, "Synthetic", "SI.pdf", BASE + "123", file_id=1),
            SiCandidate(123, DOI, "Synthetic", "ＳＩ.pdf", BASE + "123", file_id=2),
            SiCandidate(456, DOI, "Synthetic", "other.pdf", BASE + "456", file_id=True),
        )
        source = SiLookupResult(DOI, "candidates", entries, "https://doi.org/" + DOI)
        result = verify_landing_candidates(source, lambda _: self.fail("network"))
        self.assertEqual([c.status for c in result.candidates],
                         ["landing_unchecked_ambiguous_file_identity",
                          "landing_unchecked_ambiguous_file_identity",
                          "landing_unchecked_invalid_file_id"])
        self.assertEqual(result.status, "needs_review")


    def test_provider_error_snapshot_cannot_preserve_verified_si(self):
        # A restored/foreign result may claim both a failed lookup and a
        # previously verified SI. Neither a reachable page nor a stale flag
        # can prove a file under that contradictory provider status.
        stale = SiCandidate(123, DOI, "Synthetic", "si.pdf", BASE + "123",
                            status="landing_verified")
        for status in ("not_found", "rate_limited", "provider_unavailable",
                       "unsupported_publisher", "unexpected_status"):
            with self.subTest(status=status):
                source = SiLookupResult(DOI, status, (stale,),
                                        "https://evil.invalid/forged")
                result = verify_landing_candidates(
                    source, lambda _: self.fail("untrusted snapshot must not probe"))
                self.assertEqual(result.status, "needs_review")
                self.assertEqual(result.candidates[0].status,
                                 "landing_unchecked_untrusted_lookup_status")
                self.assertEqual(result.fallback_url, "https://doi.org/" + DOI)
                self.assertEqual(result.diagnostic, "untrusted_lookup_status")
                self.assertEqual(source.candidates[0].status, "landing_verified")

    def test_empty_provider_error_snapshot_never_exposes_foreign_fallback(self):
        for status in ("not_found", "rate_limited", "provider_unavailable",
                       "offline", "needs_review", "candidates"):
            with self.subTest(status=status):
                source = SiLookupResult(DOI, status, (),
                                        "https://evil.invalid/forged")
                result = verify_landing_candidates(
                    source, lambda _: self.fail("no candidates must not probe"))
                self.assertEqual(result.status, status)
                self.assertEqual(result.fallback_url, "https://doi.org/" + DOI)
                self.assertEqual(source.fallback_url, "https://evil.invalid/forged")

    def test_invalid_doi_error_snapshot_cannot_offer_a_fallback(self):
        source = SiLookupResult("invalid-doi", "provider_unavailable", (),
                                "https://evil.invalid/forged")
        result = verify_landing_candidates(
            source, lambda _: self.fail("no candidates must not probe"))
        self.assertEqual(result.status, "provider_unavailable")
        self.assertEqual(result.fallback_url, "")



if __name__ == "__main__":
    unittest.main()
