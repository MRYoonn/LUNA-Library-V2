"""Offline synthetic SI display contract; no PDFs, network or QSettings."""
import unittest

from tools.si_associations import AssociationIndex
from tools.si_display_model import project_si_rows
from tools.si_finder import SiCandidate, SiLookupResult

MAIN = "11111111-1111-4111-8111-111111111111"
SI1 = "22222222-2222-4222-8222-222222222222"
SI2 = "33333333-3333-4333-8333-333333333333"
DOI = "10.1021/jacs.6c01234"  # Synthetic; never queried.
URL = "https://acs.figshare.com/articles/dataset/Example_SI/12345"


class SiDisplayModelTests(unittest.TestCase):
    def setUp(self):
        self.index = AssociationIndex()
        self.index.add_document(MAIN, "main", "Papers/Main.pdf", DOI)
        self.index.add_document(SI1, "si", "Private/Supporting1.pdf")
        self.index.add_document(SI2, "si", "Private/Supporting2.pdf")
        self.index.confirm_link(MAIN, SI1, DOI, user_confirmed=True)
        self.index.confirm_link(MAIN, SI2, DOI, user_confirmed=True)

    def candidate(self, filename="Supporting1.pdf", file_id=123):
        return SiCandidate(12345, DOI, "Example public SI", filename,
                           URL, file_id=file_id)

    def lookup(self, status="candidates", candidates=None):
        if candidates is None:
            candidates = (self.candidate(),)
        return SiLookupResult(DOI, status, tuple(candidates),
                              "https://doi.org/" + DOI)

    def test_local_links_are_separate_from_public_candidates(self):
        report = project_si_rows(self.index, MAIN, self.lookup())
        self.assertEqual(len(report["local_si"]), 2)
        self.assertEqual(len(report["public_si"]), 1)
        self.assertEqual(report["public_si"][0]["local_document_id"], None)
        self.assertEqual(report["public_si"][0]["status"], "unconfirmed")
        self.assertEqual({x["status"] for x in report["local_si"]}, {"user_confirmed"})
        self.assertNotIn("Private/", str(report))
        self.assertEqual(report["fallback_url"], "https://doi.org/" + DOI)

    def test_two_public_si_rows_never_auto_link_by_filename(self):
        source = self.lookup(candidates=(
            self.candidate("Supporting1.pdf", 123),
            self.candidate("Supporting2.pdf", 124)))
        report = project_si_rows(self.index, MAIN, source)
        self.assertEqual([r["filename"] for r in report["public_si"]],
                         ["Supporting1.pdf", "Supporting2.pdf"])
        self.assertTrue(all(r["local_document_id"] is None for r in report["public_si"]))

    def test_caller_supplied_live_flag_never_certifies_http_access(self):
        verified = SiCandidate(12345, DOI, "Example public SI", "si.pdf", URL,
                               status="landing_verified", file_id=123)
        lookup = self.lookup("verified", (verified,))
        self.assertEqual(project_si_rows(self.index, MAIN, lookup)["public_si"][0]["status"],
                         "unconfirmed")
        self.assertEqual(project_si_rows(self.index, MAIN, lookup,
                                         live_landing_check_completed=True)["public_si"][0]["status"],
                         "unconfirmed")
        # Even an actual HTTP verifier must eventually pass a trusted proof object,\n        # not a Boolean supplied by restored state or a UI caller.\n        partial = self.lookup("partial", (verified,))
        self.assertEqual(project_si_rows(self.index, MAIN, partial,
                                         live_landing_check_completed=True)["public_si"][0]["status"],
                         "unconfirmed")

    def test_offline_and_provider_errors_never_show_stale_verified_candidate(self):
        forged = SiCandidate(12345, DOI, "Example", "si.pdf", URL,
                             status="landing_verified")
        for status in ("offline", "not_found", "rate_limited", "provider_unavailable"):
            with self.subTest(status=status):
                report = project_si_rows(self.index, MAIN,
                                         self.lookup(status, (forged,)),
                                         live_landing_check_completed=True)
                self.assertEqual(report["public_si"], [])
                self.assertEqual(len(report["local_si"]), 2)

    def test_doi_mismatch_never_renders_foreign_si_or_fallback(self):
        source = SiLookupResult("10.1021/jacs.6c99999", "verified",
                                (self.candidate(),),
                                "https://untrusted.invalid/fallback")
        report = project_si_rows(self.index, MAIN, source,
                                 live_landing_check_completed=True)
        self.assertEqual(report["lookup_status"], "needs_review")
        self.assertEqual(report["public_si"], [])
        self.assertEqual(report["fallback_url"], "https://doi.org/" + DOI)

    def test_unsafe_public_identity_and_title_are_suppressed(self):
        cases = (
            SiCandidate(12346, DOI, "Example", "si.pdf", URL),
            SiCandidate(12345, "10.1021/jacs.6c99999", "Example", "si.pdf", URL),
            SiCandidate(12345, DOI, "Bad\u202eTitle", "si.pdf", URL),
            SiCandidate(12345, DOI, "Example", "bad／si.pdf", URL),
            SiCandidate(12345, DOI, "Example", "si.pdf", URL, file_id=True),
        )
        for candidate in cases:
            with self.subTest(candidate=candidate):
                report = project_si_rows(self.index, MAIN,
                                         self.lookup(candidates=(candidate,)))
                self.assertEqual(report["public_si"], [])

    def test_malformed_restored_lookup_never_crashes_or_claims_verified(self):
        malformed = self.lookup("verified", (None, {"status": "landing_verified"}))
        report = project_si_rows(self.index, MAIN, malformed,
                                 live_landing_check_completed=True)
        self.assertEqual(report["public_si"], [])
        bad_collection = SiLookupResult(DOI, "verified", None,
                                        "https://doi.org/" + DOI)
        report = project_si_rows(self.index, MAIN, bad_collection,
                                 live_landing_check_completed=True)
        self.assertEqual(report["lookup_status"], "needs_review")
        self.assertEqual(report["public_si"], [])

    def test_ambiguous_public_names_and_file_ids_fail_closed(self):
        duplicate_names = (self.candidate("SI.pdf", 123),
                           self.candidate("ＳＩ.pdf", 124))
        duplicate_ids = (self.candidate("SI1.pdf", 123),
                         self.candidate("SI2.pdf", 123))
        for candidates in (duplicate_names, duplicate_ids):
            with self.subTest(candidates=candidates):
                report = project_si_rows(self.index, MAIN,
                                         self.lookup(candidates=candidates))
                self.assertEqual(report["public_si"], [])

    def test_mixed_valid_and_invalid_restored_verified_set_is_downgraded(self):
        good = SiCandidate(12345, DOI, "Example", "SI1.pdf", URL,
                           status="landing_verified", file_id=123)
        bad = SiCandidate(12346, DOI, "Example", "SI2.pdf", URL,
                          status="landing_verified", file_id=124)
        report = project_si_rows(self.index, MAIN,
                                 self.lookup("verified", (good, bad)),
                                 live_landing_check_completed=True)
        self.assertEqual(report["lookup_status"], "needs_review")
        self.assertEqual(len(report["public_si"]), 1)
        self.assertEqual(report["public_si"][0]["status"], "unconfirmed")

    def test_restored_unknown_status_fails_closed(self):
        report = project_si_rows(self.index, MAIN, self.lookup("unexpected_state"))
        self.assertEqual(report["lookup_status"], "needs_review")
        self.assertEqual(report["public_si"], [])
        self.assertEqual(len(report["local_si"]), 2)

    def test_nonboolean_live_proof_cannot_promote_link(self):
        verified = SiCandidate(12345, DOI, "Example public SI", "si.pdf", URL,
                               status="landing_verified", file_id=123)
        for fake_proof in ("true", 1, ["success"], {"ok": True}):
            with self.subTest(fake_proof=fake_proof):
                report = project_si_rows(self.index, MAIN,
                                         self.lookup("verified", (verified,)),
                                         live_landing_check_completed=fake_proof)
                self.assertEqual(report["public_si"][0]["status"], "unconfirmed")

    def test_unconfirmed_and_invalid_graph_cannot_be_rendered_as_confirmed(self):
        self.index.links[SI1] = self.index.links[SI1].__class__(
            SI2, SI1, DOI)
        with self.assertRaises(ValueError):
            project_si_rows(self.index, MAIN)
        with self.assertRaises(ValueError):
            project_si_rows(AssociationIndex(), MAIN)


if __name__ == "__main__":
    unittest.main()
