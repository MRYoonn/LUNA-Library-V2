"""Offline synthetic harness tests; NOT LUNA extraction accuracy measurements."""
import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "bibliography_accuracy_lab.py"
FIXTURES = ROOT / "tests" / "fixtures"

spec = importlib.util.spec_from_file_location("bibliography_accuracy_lab", TOOL)
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


def sample():
    gold = json.loads((FIXTURES / "bibliography_accuracy_goldset.synthetic.json").read_text(encoding="utf-8"))
    predictions = json.loads((FIXTURES / "bibliography_accuracy_predictions.synthetic.json").read_text(encoding="utf-8"))
    return gold, predictions


class BibliographyAccuracyLabTests(unittest.TestCase):
    def test_synthetic_all_publishers_and_negative_doi(self):
        gold, predictions = sample()
        result = lab.score(gold, predictions)
        self.assertTrue(result["pass"])
        self.assertEqual(result["cases"], 7)
        self.assertEqual(result["assessment"], "synthetic_harness_only")
        self.assertEqual(result["regression_checks"]["total"], 9)
        self.assertEqual(result["field_scores"]["doi"]["tn"], 2)
        self.assertEqual(set(result["by_publisher"]), {"ACS", "RSC", "Wiley", "Nature", "Elsevier", "Other"})

    def test_mismatch_counts_fp_and_fn_and_no_doi_false_positive(self):
        gold, predictions = sample()
        predictions["cases"][0]["observed"]["terminal_author"] = "Wrong Author"
        predictions["cases"][2]["observed"]["doi"] = "10.1000/wrong-si-doi"
        result = lab.score(gold, predictions)
        author = result["field_scores"]["terminal_author"]
        self.assertEqual((author["fp"], author["fn"]), (1, 1))
        self.assertEqual(result["field_scores"]["doi"]["fp"], 1)
        self.assertFalse(result["pass"])
        self.assertEqual({e["error"] for e in result["errors"]}, {"mismatch", "spurious"})

    def test_abstention_is_false_negative_for_known_positive(self):
        gold, predictions = sample()
        predictions["cases"][0]["observed"].pop("title")
        result = lab.score(gold, predictions)
        self.assertEqual(result["field_scores"]["title"]["fn"], 1)
        self.assertEqual(result["field_scores"]["title"]["fp"], 0)
        self.assertFalse(result["pass"])

    def test_unlabeled_fields_are_not_assumed_negative(self):
        gold, predictions = sample()
        predictions["cases"][1]["observed"]["issue"] = "3"
        result = lab.score(gold, predictions)
        self.assertTrue(result["pass"])
        self.assertGreater(result["field_scores"]["issue"]["unscored"], 0)

    def test_preserved_manual_field_failure_is_detected(self):
        gold, predictions = sample()
        predictions["regressions"][5]["after"]["authors"] = "Auto Replaced"
        result = lab.score(gold, predictions)
        self.assertEqual(result["regression_checks"]["failed"], 1)
        self.assertFalse(result["pass"])

    def test_http_status_failure_is_detected(self):
        gold, predictions = sample()
        predictions["regressions"][1]["actual_status"] = "complete"
        self.assertEqual(lab.score(gold, predictions)["regression_checks"]["failed"], 1)

    def test_duplicate_missing_and_unknown_ids_block(self):
        gold, predictions = sample()
        predictions["cases"].append(copy.deepcopy(predictions["cases"][0]))
        with self.assertRaises(lab.DatasetError):
            lab.score(gold, predictions)
        gold, predictions = sample()
        predictions["cases"].pop()
        with self.assertRaises(lab.DatasetError):
            lab.score(gold, predictions)

    def test_real_data_require_hash_and_evidence(self):
        gold, predictions = sample()
        gold["cases"][0]["source"] = {"kind": "public"}
        with self.assertRaises(lab.DatasetError):
            lab.score(gold, predictions)
        gold["cases"][0]["source"]["sha256"] = "0" * 64
        with self.assertRaises(lab.DatasetError):
            lab.score(gold, predictions)
        gold["cases"][0]["evidence"] = {k: "page 1" for k, v in gold["cases"][0]["expected"].items() if v}
        self.assertTrue(lab.score(gold, predictions)["pass"])
        with self.assertRaises(lab.DatasetError):
            lab.score(gold, predictions, require_real=True)

    def test_title_chemistry_symbols_not_discarded(self):
        self.assertNotEqual(lab.normalize("title", "C5–C6 coupling"), lab.normalize("title", "C5 C6 coupling"))
        self.assertEqual(lab.normalize("page", "100–108"), lab.normalize("page", "100-108"))

    def test_publisher_field_metrics_isolate_wrong_author(self):
        gold, predictions = sample()
        predictions["cases"][0]["observed"]["terminal_author"] = "Wrong Author"
        result = lab.score(gold, predictions)
        acs = result["by_publisher_fields"]["ACS"]["terminal_author"]
        rsc = result["by_publisher_fields"]["RSC"]["terminal_author"]
        self.assertEqual((acs["tp"], acs["fp"], acs["fn"]), (0, 1, 1))
        self.assertEqual((acs["precision"], acs["recall"]), (0.0, 0.0))
        self.assertEqual((rsc["tp"], rsc["tn"], rsc["fp"], rsc["fn"]), (1, 1, 0, 0))
        self.assertEqual((rsc["precision"], rsc["recall"]), (1.0, 1.0))
        self.assertEqual(result["by_publisher"]["ACS"]["errors"], 1)
        self.assertEqual(result["by_publisher"]["RSC"]["errors"], 0)

    def test_publisher_field_metrics_unknown_and_negative_labels(self):
        gold, predictions = sample()
        metrics = lab.score(gold, predictions)["by_publisher_fields"]
        self.assertEqual(metrics["RSC"]["doi"]["tn"], 1)
        self.assertEqual(metrics["RSC"]["issue"]["unscored"], 2)
        self.assertIsNone(metrics["RSC"]["issue"]["precision"])
        self.assertIsNone(metrics["RSC"]["issue"]["recall"])
        self.assertEqual(metrics["ACS"]["article_number"]["tn"], 1)
        self.assertEqual(metrics["ACS"]["article_number"]["scored"], 1)

    def test_require_real_rejects_unverified_snapshot_identity(self):
        # Fabricated contract fixtures: NEVER a real-publisher measurement.
        gold, predictions = sample()
        for case in gold["cases"]:
            case["source"] = {"kind": "public", "sha256": "a" * 64}
            case["evidence"] = {field: "fabricated contract test only"
                                for field in case["expected"]}
        with self.assertRaisesRegex(lab.DatasetError, "engine_commit"):
            lab.score(gold, predictions, require_real=True)
        predictions["engine_commit"] = "b" * 40
        with self.assertRaisesRegex(lab.DatasetError, "capture_kind"):
            lab.score(gold, predictions, require_real=True)
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        with self.assertRaisesRegex(lab.DatasetError, "audit flags"):
            lab.score(gold, predictions, require_real=True)
        for case in predictions["cases"]:
            case["audit"] = {"manual_citation_active": False,
                             "senior_author_override_active": False}
        result = lab.score(gold, predictions, require_real=True)
        self.assertTrue(result["pass"])
        self.assertEqual(result["assessment"], "provided_real_goldset_not_independently_verified")

    def test_require_real_negative_label_evidence_and_pdf_hash(self):
        # Real-mode structural validation is testable without claiming the
        # fabricated labels or digest are authentic PDF evidence.
        gold, predictions = sample()
        for case in gold["cases"]:
            case["source"] = {"kind": "public", "sha256": "a" * 64}
            case["evidence"] = {field: "fabricated contract test only"
                                for field in case["expected"]}
        predictions["engine_commit"] = "b" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        for case in predictions["cases"]:
            case["audit"] = {"manual_citation_active": False,
                             "senior_author_override_active": False}
        si = next(case for case in gold["cases"] if case["id"] == "syn-rsc-si")
        si["evidence"].pop("doi")  # Known-negative null DOI needs evidence.
        with self.assertRaisesRegex(lab.DatasetError, "evidence for doi"):
            lab.score(gold, predictions, require_real=True)
        si["evidence"]["doi"] = "  "
        with self.assertRaisesRegex(lab.DatasetError, "evidence for doi"):
            lab.score(gold, predictions, require_real=True)
        si["evidence"]["doi"] = "fabricated absence evidence"
        si["source"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(lab.DatasetError, "placeholder PDF SHA256"):
            lab.score(gold, predictions, require_real=True)
        si["source"]["sha256"] = "a" * 64
        self.assertTrue(lab.score(gold, predictions, require_real=True)["pass"])

    def test_real_mode_rejects_manual_card_override_contamination(self):
        # Fabricated public-kind metadata: tests only the scorer contract.
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            case["evidence"] = {field: "fabricated contract evidence"
                                for field in case["expected"]}
        predictions["engine_commit"] = "b" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        for case in predictions["cases"]:
            case["audit"] = {"manual_citation_active": False,
                             "senior_author_override_active": False}
        for flag in ("manual_citation_active", "senior_author_override_active"):
            predictions["cases"][0]["audit"][flag] = True
            with self.assertRaisesRegex(lab.DatasetError, "manual card override"):
                lab.score(gold, predictions, require_real=True)
            predictions["cases"][0]["audit"][flag] = False
        self.assertTrue(lab.score(gold, predictions, require_real=True)["pass"])

    def test_real_mode_rejects_manual_field_source_contamination(self):
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            case["evidence"] = {field: "fabricated contract evidence"
                                for field in case["expected"]}
        predictions["engine_commit"] = "b" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        for case in predictions["cases"]:
            case["audit"] = {"manual_citation_active": False,
                             "senior_author_override_active": False}
        audit = predictions["cases"][0]["audit"]
        audit["field_sources"] = {"title": "manual"}
        with self.assertRaisesRegex(lab.DatasetError, "manually sourced title"):
            lab.score(gold, predictions, require_real=True)
        audit["field_sources"] = {}
        audit["authorsSource"] = "manual"
        with self.assertRaisesRegex(lab.DatasetError, "manually sourced terminal_author"):
            lab.score(gold, predictions, require_real=True)
        audit.pop("authorsSource")
        audit["field_sources"] = []  # malformed native provenance is not ignored
        with self.assertRaisesRegex(lab.DatasetError, "invalid field_sources"):
            lab.score(gold, predictions, require_real=True)
        audit.pop("field_sources")
        self.assertTrue(lab.score(gold, predictions, require_real=True)["pass"])

    def test_coverage_gate_synthetic_is_never_real(self):
        gold, _ = sample()
        audit = lab.audit_goldset_coverage(gold)
        self.assertFalse(audit["ready"])
        self.assertEqual(audit["real_pdf_total"], 0)
        self.assertEqual(audit["basis"], "provided_pdf_metadata_only_not_independently_verified")
        self.assertIn("real_pdf_total", {gap["criterion"] for gap in audit["gaps"]})

    def test_coverage_gate_fabricated_breadth_and_duplicate_detection(self):
        # This is a structural contract test, NOT actual publisher PDFs.
        gold, _ = sample()
        templates = {case["publisher"]: case for case in gold["cases"]
                     if case["expected"].get("document_kind") == "main_article"}
        si = next(case for case in gold["cases"]
                  if case["expected"].get("document_kind") == "supporting_information")
        no_doi = next(case for case in gold["cases"] if case["id"] == "syn-no-doi")
        cases = []
        for publisher in lab.PUBLISHERS:
            for index in range(3):
                item = copy.deepcopy(templates[publisher])
                item["id"] = f"fake-{publisher}-{index}"
                cases.append(item)
        for index in range(3):
            item = copy.deepcopy(si)
            item["id"] = f"fake-si-{index}"
            cases.append(item)
        for index in range(2):
            item = copy.deepcopy(no_doi)
            item["id"] = f"fake-no-doi-{index}"
            item["expected"]["doi"] = None
            cases.append(item)
        for index, item in enumerate(cases):
            item["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            item["evidence"] = {field: "fabricated evidence, contract test only"
                                for field in item["expected"]}
        gold["cases"] = cases
        audit = lab.audit_goldset_coverage(gold)
        self.assertTrue(audit["ready"])
        self.assertEqual(audit["real_pdf_total"], 20)
        self.assertEqual(audit["supporting_information"], 3)
        self.assertEqual(audit["no_doi_main"], 2)
        gold["cases"][1]["source"]["sha256"] = gold["cases"][0]["source"]["sha256"].upper()
        audit = lab.audit_goldset_coverage(gold)
        self.assertFalse(audit["ready"])
        self.assertIn("duplicate_pdf_sha256", {gap["criterion"] for gap in audit["gaps"]})
        gold["cases"][1]["source"]["sha256"] = f"{2:064x}"
        gold["cases"][0]["source"]["kind"] = "synthetic"
        audit = lab.audit_goldset_coverage(gold)
        self.assertEqual(audit["main_by_publisher"]["ACS"], 3)
        self.assertEqual(audit["main_by_publisher"]["RSC"], 3)
        self.assertEqual(audit["real_pdf_total"], 19)

    def test_coverage_cli_requires_real_and_writes_gap_report(self):
        gold, predictions = sample()
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            goldfile, predfile, report = (base / name for name in ("gold.json", "pred.json", "report.json"))
            goldfile.write_text(json.dumps(gold), encoding="utf-8")
            predfile.write_text(json.dumps(predictions), encoding="utf-8")
            cmd = [sys.executable, str(TOOL), "--goldset", str(goldfile),
                   "--predictions", str(predfile), "--coverage-gate", "--report", str(report)]
            blocked = subprocess.run(cmd, capture_output=True, text=True, check=False)
            self.assertEqual(blocked.returncode, 2)
            self.assertIn("requires --require-real", blocked.stderr)
            for index, case in enumerate(gold["cases"]):
                case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
                case["evidence"] = {field: "fabricated test only" for field in case["expected"]}
            predictions["engine_commit"] = "b" * 40
            predictions["capture_kind"] = "qt_record_snapshot_read_only"
            for case in predictions["cases"]:
                case["audit"] = {"manual_citation_active": False,
                                 "senior_author_override_active": False}
            goldfile.write_text(json.dumps(gold), encoding="utf-8")
            predfile.write_text(json.dumps(predictions), encoding="utf-8")
            failed = subprocess.run(cmd + ["--require-real"], capture_output=True, text=True, check=False)
            self.assertEqual(failed.returncode, 1, failed.stderr)
            result = json.loads(report.read_text(encoding="utf-8"))
            self.assertFalse(result["pass"])
            self.assertFalse(result["coverage_audit"]["ready"])
            self.assertEqual(result["coverage_audit"]["real_pdf_total"], 7)

    def test_coverage_gate_requires_three_si_even_with_twenty_cases(self):
        # Structural sample counts only: fabricated hashes are not PDF evidence.
        gold, _ = sample()
        si = next(case for case in gold["cases"]
                  if case["expected"].get("document_kind") == "supporting_information")
        gold["cases"] = []
        for index in range(2):
            item = copy.deepcopy(si)
            item["id"] = f"fabricated-si-{index}"
            item["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            item["evidence"] = {field: "synthetic test only" for field in item["expected"]}
            gold["cases"].append(item)
        audit = lab.audit_goldset_coverage(
            gold, minimum_total=2, minimum_main_per_publisher=0, minimum_no_doi_main=0
        )
        self.assertFalse(audit["ready"])
        self.assertIn("supporting_information", {gap["criterion"] for gap in audit["gaps"]})
        third = copy.deepcopy(gold["cases"][0])
        third["id"] = "fabricated-si-2"
        third["source"]["sha256"] = f"{3:064x}"
        gold["cases"].append(third)
        audit = lab.audit_goldset_coverage(
            gold, minimum_total=2, minimum_main_per_publisher=0, minimum_no_doi_main=0
        )
        self.assertTrue(audit["ready"])

    def test_engine_identity_rejects_stale_or_placeholder_snapshot(self):
        predictions = {"engine_commit": "a" * 40}
        lab.check_engine_identity(predictions, "A" * 40)
        for expected in ("b" * 40, "0" * 40, "not-a-sha"):
            with self.assertRaises(lab.DatasetError):
                lab.check_engine_identity(predictions, expected)
        with self.assertRaisesRegex(lab.DatasetError, "engine_commit mismatch"):
            lab.check_engine_identity({}, "a" * 40)

    def test_cli_real_mode_engine_identity_gate(self):
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            case["evidence"] = {field: "fabricated contract only" for field in case["expected"]}
        predictions["engine_commit"] = "a" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        for case in predictions["cases"]:
            case["audit"] = {"manual_citation_active": False,
                             "senior_author_override_active": False}
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            goldfile, predfile, report = (base / name for name in
                                         ("gold.json", "pred.json", "report.json"))
            goldfile.write_text(json.dumps(gold), encoding="utf-8")
            predfile.write_text(json.dumps(predictions), encoding="utf-8")
            cmd = [sys.executable, str(TOOL), "--goldset", str(goldfile),
                   "--predictions", str(predfile), "--report", str(report),
                   "--require-real", "--expected-engine-commit"]
            stale = subprocess.run(cmd + ["b" * 40], capture_output=True, text=True)
            self.assertEqual(stale.returncode, 2)
            self.assertIn("engine_commit mismatch", stale.stderr)
            self.assertFalse(report.exists())
            matched = subprocess.run(cmd + ["A" * 40], capture_output=True, text=True)
            self.assertEqual(matched.returncode, 0, matched.stderr)
            result = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(result["engine_commit"], "a" * 40)
            self.assertEqual(result["assessment"], "provided_real_goldset_not_independently_verified")

    def test_cli_exit_codes_and_local_report(self):
        gold = FIXTURES / "bibliography_accuracy_goldset.synthetic.json"
        predictions = FIXTURES / "bibliography_accuracy_predictions.synthetic.json"
        with tempfile.TemporaryDirectory() as folder:
            report = Path(folder) / "result.json"
            cmd = [sys.executable, str(TOOL), "--goldset", str(gold),
                   "--predictions", str(predictions), "--report", str(report)]
            result = subprocess.run(cmd, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(json.loads(report.read_text(encoding="utf-8"))["pass"])
            result = subprocess.run(cmd + ["--require-real"], capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
