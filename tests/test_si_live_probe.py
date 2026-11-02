"""Synthetic, offline-only tests for the explicitly opt-in SI smoke probe."""
import contextlib
from io import StringIO
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from si_live_probe import inspect_doi, main
from si_finder import SiCandidate, SiLookupResult, safe_figshare_landing

DOI = "10.1021/acsnano.8b07631"  # Public DOI; no live requests in tests.
PUBLIC_LANDING = (
    "https://acs.figshare.com/articles/dataset/"
    "Extrinsic_and_Dynamic_Edge_States_of_Two-Dimensional_Lead_Halide_Perovskites/7688531"
)


class ProbeTests(unittest.TestCase):
    def test_public_dataset_landing_shape_is_accepted(self):
        self.assertEqual(safe_figshare_landing(PUBLIC_LANDING, 7688531), PUBLIC_LANDING)
        self.assertEqual(safe_figshare_landing(PUBLIC_LANDING, 7688532), "")

    def test_default_mode_never_uses_network(self):
        finder = Mock(return_value=SiLookupResult(DOI, "offline", (), "https://doi.org/" + DOI))
        verifier = Mock(side_effect=AssertionError("landing network"))
        report = inspect_doi(DOI, finder=finder, verifier=verifier)
        finder.assert_called_once_with(DOI, offline=True)
        verifier.assert_not_called()
        self.assertEqual(report["verification_scope"], "metadata_only")
        self.assertEqual(report["result"]["status"], "offline")

    def test_metadata_live_never_probes_landing(self):
        source = SiLookupResult(DOI, "candidates", (
            SiCandidate(7688531, DOI, "Public example", "si.pdf", PUBLIC_LANDING),
        ), "https://doi.org/" + DOI)
        finder = Mock(return_value=source)
        verifier = Mock(side_effect=AssertionError("landing network"))
        report = inspect_doi(DOI, live=True, finder=finder, verifier=verifier)
        finder.assert_called_once_with(DOI, offline=False)
        verifier.assert_not_called()
        self.assertEqual(report["result"]["candidates"][0]["status"],
                         "metadata_match_link_unchecked")

    def test_landing_check_requires_explicit_live(self):
        finder = Mock(side_effect=AssertionError("API network"))
        with self.assertRaises(ValueError):
            inspect_doi(DOI, check_landings=True, finder=finder)
        finder.assert_not_called()

    def test_landing_check_uses_existing_verifier_once(self):
        source = SiLookupResult(DOI, "candidates", (
            SiCandidate(7688531, DOI, "Public example", "si.pdf", PUBLIC_LANDING),
        ), "https://doi.org/" + DOI)
        checked = SiLookupResult(DOI, "verified", source.candidates,
                                 source.fallback_url, "landings_checked=1")
        finder = Mock(return_value=source)
        verifier = Mock(return_value=checked)
        report = inspect_doi(DOI, live=True, check_landings=True,
                             finder=finder, verifier=verifier)
        verifier.assert_called_once_with(source)
        self.assertEqual(report["verification_scope"], "landing_html_only")
        self.assertEqual(report["result"]["diagnostic"], "landings_checked=1")

    def test_404_never_invokes_landing_probe(self):
        finder = Mock(return_value=SiLookupResult(DOI, "not_found", (), "https://doi.org/" + DOI))
        verifier = Mock(side_effect=AssertionError("landing network"))
        report = inspect_doi(DOI, live=True, check_landings=True,
                             finder=finder, verifier=verifier)
        self.assertEqual(report["result"]["status"], "not_found")
        verifier.assert_not_called()

    def test_cli_default_is_offline(self):
        output = StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(main(["--doi", DOI]), 0)
        data = json.loads(output.getvalue())
        self.assertEqual(data["result"]["status"], "offline")
        self.assertEqual(data["verification_scope"], "metadata_only")


    def test_strict_landing_gate_passes_only_verified_html(self):
        source = SiLookupResult(DOI, "verified", (
            SiCandidate(7688531, DOI, "Public example", "si.pdf", PUBLIC_LANDING,
                        status="landing_verified"),
        ), "https://doi.org/" + DOI)
        fake = {"verification_scope": "landing_html_only", "result": source.to_dict()}
        with patch("si_live_probe.inspect_doi", return_value=fake):
            output = StringIO()
            with contextlib.redirect_stdout(output):
                rc = main(["--doi", DOI, "--live", "--check-landings",
                           "--require-landing-http", "--expected-article-id", "7688531"])
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads(output.getvalue())["landing_http_gate"], "pass")

    def test_strict_landing_gate_rejects_partial_and_wrong_article(self):
        source = SiLookupResult(DOI, "partial_verified", (
            SiCandidate(7688531, DOI, "Public example", "si.pdf", PUBLIC_LANDING,
                        status="landing_verified"),
        ), "https://doi.org/" + DOI)
        fake = {"verification_scope": "landing_html_only", "result": source.to_dict()}
        with patch("si_live_probe.inspect_doi", return_value=fake):
            output = StringIO()
            with contextlib.redirect_stdout(output):
                rc = main(["--doi", DOI, "--live", "--check-landings", "--require-landing-http"])
        self.assertEqual(rc, 2)
        self.assertEqual(json.loads(output.getvalue())["landing_http_gate"], "fail")
        fake["result"]["status"] = "verified"
        with patch("si_live_probe.inspect_doi", return_value=fake):
            with contextlib.redirect_stdout(StringIO()):
                rc = main(["--doi", DOI, "--live", "--check-landings",
                           "--require-landing-http", "--expected-article-id", "7688532"])
        self.assertEqual(rc, 2)

    def test_strict_landing_gate_rejects_empty_or_unchecked_candidates(self):
        fake = {"verification_scope": "landing_html_only", "result": {
            "status": "verified", "candidates": []}}
        with patch("si_live_probe.inspect_doi", return_value=fake):
            with contextlib.redirect_stdout(StringIO()):
                self.assertEqual(main(["--doi", DOI, "--live", "--check-landings",
                                       "--require-landing-http"]), 2)
        fake["result"]["candidates"] = [{"article_id": 7688531,
                                         "status": "metadata_match_link_unchecked"}]
        with patch("si_live_probe.inspect_doi", return_value=fake):
            with contextlib.redirect_stdout(StringIO()):
                self.assertEqual(main(["--doi", DOI, "--live", "--check-landings",
                                       "--require-landing-http"]), 2)

    def test_strict_landing_gate_rejects_misrouted_or_forged_identity(self):
        # A successful landing GET must never certify another paper's DOI or
        # a different Figshare article. These reports are synthetic snapshots;
        # the CLI must reject them even if the status string says verified.
        source = SiLookupResult(DOI, "verified", (
            SiCandidate(7688531, DOI, "Public example", "si.pdf", PUBLIC_LANDING,
                        status="landing_verified", file_id=123),
        ), "https://doi.org/" + DOI)
        cases = (
            ("parent_doi", "result", "doi", "10.1021/jacs.6c99999"),
            ("candidate_doi", "candidate", "resource_doi", "10.1021/jacs.6c99999"),
            ("wrong_article", "candidate", "article_id", 7688532),
            ("foreign_host", "candidate", "landing_url",
             "https://acs.figshare.com.evil.invalid/articles/dataset/7688531"),
            ("unsafe_filename", "candidate", "filename", "bad／si.pdf"),
            ("unsafe_title", "candidate", "title", "Hidden\u202eTitle"),
            ("invalid_file_id", "candidate", "file_id", True),
            ("missing_candidate", "collection", "", None),
        )
        for label, scope, field, value in cases:
            with self.subTest(label=label):
                result = source.to_dict()
                if scope == "result":
                    result[field] = value
                elif scope == "candidate":
                    result["candidates"][0][field] = value
                else:
                    result["candidates"] = [None]
                report = {"verification_scope": "landing_html_only", "result": result}
                with patch("si_live_probe.inspect_doi", return_value=report):
                    output = StringIO()
                    with contextlib.redirect_stdout(output):
                        rc = main(["--doi", DOI, "--live", "--check-landings",
                                   "--require-landing-http"])
                self.assertEqual(rc, 2)
                self.assertEqual(json.loads(output.getvalue())["landing_http_gate"], "fail")

    def test_strict_landing_gate_rechecks_duplicate_file_identity_and_fallback(self):
        # Restored reports must not bypass the finder's ambiguity checks.
        # All URLs and IDs are synthetic evidence; no HTTP request is made.
        source = SiLookupResult(DOI, "verified", (
            SiCandidate(7688531, DOI, "Public example", "SI.pdf", PUBLIC_LANDING,
                        status="landing_verified", file_id=123),
            SiCandidate(7688531, DOI, "Public example", "SI2.pdf", PUBLIC_LANDING,
                        status="landing_verified", file_id=124),
        ), "https://doi.org/" + DOI)
        args = ["--doi", DOI, "--live", "--check-landings", "--require-landing-http"]

        def gate(result):
            report = {"verification_scope": "landing_html_only", "result": result}
            with patch("si_live_probe.inspect_doi", return_value=report):
                output = StringIO()
                with contextlib.redirect_stdout(output):
                    code = main(args)
            return code, json.loads(output.getvalue())["landing_http_gate"]

        self.assertEqual(gate(source.to_dict()), (0, "pass"))

        cases = ("same_filename", "nfkc_casefold_filename", "same_file_id",
                 "cross_article_file_id", "forged_fallback")
        for case in cases:
            with self.subTest(case=case):
                report = source.to_dict()
                second = report["candidates"][1]
                if case == "same_filename":
                    second["filename"] = "SI.pdf"
                elif case == "nfkc_casefold_filename":
                    second["filename"] = "ＳＩ.pdf"
                elif case == "same_file_id":
                    second["file_id"] = 123
                elif case == "cross_article_file_id":
                    second["article_id"] = 7688532
                    second["landing_url"] = PUBLIC_LANDING.replace("7688531", "7688532")
                    second["file_id"] = 123
                else:
                    report["fallback_url"] = "https://untrusted.invalid/si"
                self.assertEqual(gate(report), (2, "fail"))

    def test_strict_landing_gate_rejects_missing_opt_in_without_network(self):
        with patch("si_live_probe.inspect_doi", side_effect=AssertionError("network")):
            with contextlib.redirect_stderr(StringIO()):
                with self.assertRaises(SystemExit) as cm:
                    main(["--doi", DOI, "--require-landing-http"])
                self.assertEqual(cm.exception.code, 2)
                with self.assertRaises(SystemExit) as cm:
                    main(["--doi", DOI, "--live", "--check-landings",
                          "--expected-article-id", "7688531"])
                self.assertEqual(cm.exception.code, 2)

if __name__ == "__main__":
    unittest.main()
