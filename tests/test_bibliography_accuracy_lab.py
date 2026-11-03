"""Offline synthetic harness tests; NOT LUNA extraction accuracy measurements."""
import copy
import hashlib
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


def seed_synthetic_audit(predictions):
    """Fabricated provider labels for contract tests; NOT engine evidence."""
    for case in predictions["cases"]:
        sources = {}
        for field, value in case["observed"].items():
            if field != "document_kind" and value not in (None, ""):
                sources["authors" if field == "terminal_author" else field] = "Crossref"
        case["audit"] = {
            "manual_citation_active": False,
            "senior_author_override_active": False,
            "field_sources": sources,
        }


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

    def test_real_mode_rejects_reused_pdf_hash_without_leaking_digest(self):
        # Fabricated hashes and provider labels only: no publisher PDFs.
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            digest = "a" * 64 if index < 2 else f"{index:064x}"
            case["source"] = {"kind": "public", "sha256": digest}
            case["evidence"] = {
                field: "synthetic contract evidence" for field in case["expected"]
            }
        predictions["engine_commit"] = "1" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        seed_synthetic_audit(predictions)
        with self.assertRaisesRegex(lab.DatasetError, "duplicate PDF SHA256") as error:
            lab.score(gold, predictions, require_real=True)
        self.assertNotIn("a" * 64, str(error.exception))

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

    def test_missing_known_negative_snapshot_is_not_a_true_negative(self):
        # Fabricated gold labels: an absent key is not an observed null.
        gold, predictions = sample()
        self.assertTrue(lab.score(gold, predictions)["pass"])
        si = next(c for c in predictions["cases"] if c["id"] == "syn-rsc-si")
        self.assertIsNone(si["observed"]["doi"])
        del si["observed"]["doi"]
        report = lab.score(gold, predictions)
        self.assertFalse(report["pass"])
        self.assertEqual(report["field_scores"]["doi"]["tn"], 1)
        self.assertEqual(report["field_scores"]["doi"]["missing_snapshot"], 1)
        self.assertEqual(report["by_publisher_fields"]["RSC"]["doi"]["missing_snapshot"], 1)
        self.assertIn({"case_id": "syn-rsc-si", "publisher": "RSC",
                       "field": "doi", "expected": None, "observed": None,
                       "error": "missing_snapshot_field"}, report["errors"])
        si["observed"]["doi"] = None
        self.assertTrue(lab.score(gold, predictions)["pass"])
        # Empty strings are explicit captured absence too, not omitted keys.
        si["observed"]["doi"] = ""
        self.assertTrue(lab.score(gold, predictions)["pass"])

    def test_missing_negative_article_number_blocks_false_acs_tn(self):
        gold, predictions = sample()
        acs = next(c for c in predictions["cases"] if c["id"] == "syn-acs-main")
        self.assertIsNone(acs["observed"]["article_number"])
        del acs["observed"]["article_number"]
        report = lab.score(gold, predictions)
        self.assertFalse(report["pass"])
        row = report["by_publisher_fields"]["ACS"]["article_number"]
        self.assertEqual((row["tn"], row["missing_snapshot"], row["scored"]), (0, 1, 1))
        self.assertEqual(report["errors"][0]["error"], "missing_snapshot_field")

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

    def test_assertion_free_regressions_cannot_claim_pass(self):
        gold, predictions = sample()
        predictions["regressions"].append({
            "id": "fake-restart", "before": {}, "after": {}
        })
        with self.assertRaisesRegex(lab.DatasetError, "no assertions"):
            lab.score(gold, predictions)
        predictions["regressions"][-1]["actual_status"] = "offline"
        with self.assertRaisesRegex(lab.DatasetError, "without expected_status"):
            lab.score(gold, predictions)

    def test_duplicate_and_contradictory_regression_assertions_block(self):
        gold, predictions = sample()
        case = predictions["regressions"][0]
        case["must_preserve"].append("title")
        with self.assertRaisesRegex(lab.DatasetError, "duplicate field assertion"):
            lab.score(gold, predictions)
        case["must_preserve"].pop()
        case["must_remain_empty"] = ["title"]
        with self.assertRaisesRegex(lab.DatasetError, "contradictory field assertions"):
            lab.score(gold, predictions)
        case["must_remain_empty"] = []
        case["expected_status"] = " "
        with self.assertRaisesRegex(lab.DatasetError, "invalid expected_status"):
            lab.score(gold, predictions)

    def test_missing_positive_snapshot_field_is_not_silent_abstention(self):
        # An omitted field is an incomplete native capture, not an explicit
        # extractor abstention. Both count FN, but only the omission is a
        # snapshot-integrity error.
        gold, predictions = sample()
        target = predictions["cases"][0]["observed"]
        self.assertTrue(target["title"])
        del target["title"]
        report = lab.score(gold, predictions)
        self.assertFalse(report["pass"])
        self.assertEqual(report["field_scores"]["title"]["fn"], 1)
        self.assertEqual(report["field_scores"]["title"]["missing_snapshot"], 1)
        self.assertTrue(any(e["field"] == "title" and
                            e["error"] == "missing_snapshot_field"
                            for e in report["errors"]))
        target["title"] = None
        report = lab.score(gold, predictions)
        self.assertFalse(report["pass"])
        self.assertEqual(report["field_scores"]["title"]["fn"], 1)
        self.assertEqual(report["field_scores"]["title"]["missing_snapshot"], 0)
        self.assertTrue(any(e["field"] == "title" and
                            e["error"] == "missing"
                            for e in report["errors"]))

    def test_missing_empty_field_snapshot_is_not_a_pass(self):
        gold, predictions = sample()
        case = next(item for item in predictions["regressions"]
                    if item["id"] == "invalid-doi")
        self.assertIsNone(case["after"]["doi"])
        self.assertTrue(lab.score(gold, predictions)["pass"])
        del case["after"]["doi"]
        report = lab.score(gold, predictions)
        self.assertFalse(report["pass"])
        self.assertIn({"case_id": "invalid-doi", "field": "doi",
                       "reason": "missing_asserted_field"},
                      report["regression_checks"]["failures"])
        case["after"]["doi"] = ""
        self.assertTrue(lab.score(gold, predictions)["pass"])
        case["after"]["doi"] = "10.1000/unexpected"
        report = lab.score(gold, predictions)
        self.assertFalse(report["pass"])
        self.assertIn({"case_id": "invalid-doi", "field": "doi",
                       "reason": "unexpected_value"},
                      report["regression_checks"]["failures"])

    def test_missing_preserved_field_is_not_a_false_pass(self):
        # Synthetic snapshots: a missing key must not compare equal to None,
        # an empty string, or an empty list from the earlier capture.
        for value in (None, "", []):
            with self.subTest(value=value):
                case = {
                    "id": "protected-field", "before": {"title": value},
                    "after": {"title": value}, "must_preserve": ["title"],
                }
                self.assertEqual(lab.score_regressions([case])["passed"], 1)
                for missing_side in ("before", "after"):
                    missing = copy.deepcopy(case)
                    del missing[missing_side]["title"]
                    report = lab.score_regressions([missing])
                    self.assertEqual(report["failed"], 1)
                    self.assertEqual(report["failures"], [{
                        "case_id": "protected-field", "field": "title",
                        "reason": "missing_asserted_field",
                    }])
        changed = {
            "id": "changed-field", "before": {"title": None},
            "after": {"title": "automatic replacement"},
            "must_preserve": ["title"],
        }
        self.assertEqual(lab.score_regressions([changed])["failures"][0]["reason"],
                         "changed_protected_value")
        gold, predictions = sample()
        predictions["regressions"].append({
            "id": "missing-null-after", "before": {"doi": None},
            "after": {}, "must_preserve": ["doi"],
        })
        self.assertFalse(lab.score(gold, predictions)["pass"])

    def test_preserved_boolean_type_drift_is_not_a_false_pass(self):
        # Python bool is an int subclass: False == 0 and True == 1.
        # Real Qt/QSettings snapshots must preserve the VALUE AND its type.
        for before, after in ((False, 0), (True, 1), (0, False), (1, True)):
            with self.subTest(before=before, after=after):
                row = {"id": "synthetic-typed-manual-flag",
                       "before": {"manual": before},
                       "after": {"manual": after},
                       "must_preserve": ["manual"]}
                report = lab.score_regressions([row])
                self.assertEqual(report["passed"], 0)
                self.assertEqual(report["failed"], 1)
                self.assertEqual(report["failures"], [{
                    "case_id": "synthetic-typed-manual-flag",
                    "field": "manual",
                    "reason": "changed_protected_value",
                }])
        for value in (False, True, 0, 1):
            with self.subTest(unchanged=value):
                row = {"id": "synthetic-unchanged-manual-flag",
                       "before": {"manual": value},
                       "after": {"manual": value},
                       "must_preserve": ["manual"]}
                self.assertEqual(lab.score_regressions([row])["passed"], 1)

    def test_empty_assertion_rejects_boolean_and_numeric_false_positives(self):
        # Python False == 0 and 0 == [] is false, but False == "" is false;
        # more importantly False == None is false and False == [] is false.
        # A numeric zero or false must never count as an explicitly blank field.
        for value in (False, 0, 0.0):
            with self.subTest(value=value):
                row = {"id": "typed-empty", "before": {},
                       "after": {"doi": value}, "must_remain_empty": ["doi"]}
                result = lab.score_regressions([row])
                self.assertEqual(result["failed"], 1)
                self.assertEqual(result["failures"][0]["reason"], "unexpected_value")
        for value in (None, "", []):
            with self.subTest(value=value):
                row = {"id": "typed-empty", "before": {},
                       "after": {"doi": value}, "must_remain_empty": ["doi"]}
                self.assertEqual(lab.score_regressions([row])["passed"], 1)

    def test_status_only_regression_is_explicit_and_scoreable(self):
        gold, predictions = sample()
        predictions["regressions"].append({
            "id": "status-only", "before": {}, "after": {},
            "expected_status": "offline", "actual_status": "offline"
        })
        report = lab.score(gold, predictions)
        self.assertTrue(report["pass"])
        self.assertEqual(report["regression_checks"]["total"], 10)
        predictions["regressions"][-1]["actual_status"] = "complete"
        report = lab.score(gold, predictions)
        self.assertFalse(report["pass"])
        self.assertEqual(report["regression_checks"]["failed"], 1)

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

    def test_terminal_doi_punctuation_is_not_silently_removed(self):
        # DOI suffix punctuation can be part of the registered identifier.
        # The scorer must not turn a wrong extracted DOI into a true positive.
        for suffix in (".", ","):
            with self.subTest(suffix=suffix):
                gold, predictions = sample()
                original = gold["cases"][0]["expected"]["doi"]
                predictions["cases"][0]["observed"]["doi"] = original + suffix
                self.assertNotEqual(lab.normalize("doi", original),
                                    lab.normalize("doi", original + suffix))
                report = lab.score(gold, predictions)
                self.assertFalse(report["pass"])
                self.assertEqual(report["field_scores"]["doi"]["fp"], 1)
                self.assertEqual(report["field_scores"]["doi"]["fn"], 1)
                self.assertEqual(report["errors"][0]["field"], "doi")

                # The same exact DOI, whether bare or in an official DOI URL,
                # must still match without loosening the suffix comparison.
                gold["cases"][0]["expected"]["doi"] = original + suffix
                predictions["cases"][0]["observed"]["doi"] = (
                    "https://doi.org/" + original + suffix
                )
                self.assertTrue(lab.score(gold, predictions)["pass"])

    def test_invalid_goldset_doi_is_rejected_even_if_prediction_matches(self):
        gold, predictions = sample()
        gold["cases"][0]["expected"]["doi"] = "not-a-doi"
        predictions["cases"][0]["observed"]["doi"] = "not-a-doi"
        with self.assertRaisesRegex(lab.DatasetError, "invalid expected DOI"):
            lab.score(gold, predictions)
        # Valid DOI URLs remain legitimate ground-truth labels.
        gold["cases"][0]["expected"]["doi"] = "https://doi.org/10.1000/synthetic-acs-main"
        self.assertTrue(lab.score(gold, sample()[1])["pass"])

    def test_invalid_extracted_doi_counts_as_error_not_dataset_failure(self):
        gold, predictions = sample()
        predictions["cases"][0]["observed"]["doi"] = "https://doi.org/not-a-doi"
        predictions["cases"][2]["observed"]["doi"] = "not-a-doi"
        report = lab.score(gold, predictions)
        self.assertFalse(report["pass"])
        self.assertEqual(report["field_scores"]["doi"]["fp"], 2)
        self.assertEqual(report["field_scores"]["doi"]["fn"], 1)
        self.assertEqual(sum(e["error"] == "invalid_doi" for e in report["errors"]), 2)
        self.assertEqual(report["by_publisher_fields"]["ACS"]["doi"]["fp"], 1)
        self.assertEqual(report["by_publisher_fields"]["RSC"]["doi"]["fp"], 1)

    def test_goldset_rejects_unrecognized_document_classification(self):
        gold, predictions = sample()
        gold["cases"][0]["expected"]["document_kind"] = "paper"
        predictions["cases"][0]["observed"]["document_kind"] = "paper"
        with self.assertRaisesRegex(lab.DatasetError, "invalid expected document_kind"):
            lab.score(gold, predictions)
        gold["cases"][0]["expected"]["document_kind"] = "main_article"
        self.assertTrue(lab.score(gold, sample()[1])["pass"])

    def test_invalid_native_document_kind_is_a_scored_error(self):
        gold, predictions = sample()
        predictions["cases"][0]["observed"]["document_kind"] = "paper"
        report = lab.score(gold, predictions)
        self.assertFalse(report["pass"])
        row = report["by_publisher_fields"]["ACS"]["document_kind"]
        self.assertEqual((row["fp"], row["fn"]), (1, 1))
        self.assertIn("invalid_document_kind", {e["error"] for e in report["errors"]})

    def test_title_chemistry_symbols_not_discarded(self):
        self.assertNotEqual(lab.normalize("title", "C5–C6 coupling"), lab.normalize("title", "C5 C6 coupling"))
        self.assertNotEqual(lab.normalize("title", "CO₂"), lab.normalize("title", "CO2"))
        self.assertNotEqual(lab.normalize("title", "Fe³⁺"), lab.normalize("title", "Fe3+"))
        self.assertEqual(lab.normalize("title", "Cafe\u0301"), lab.normalize("title", "Café"))
        self.assertEqual(lab.normalize("page", "100–108"), lab.normalize("page", "100-108"))

    def test_title_subscript_mismatch_counts_fp_and_fn(self):
        # Synthetic labels: an altered chemical formula must not score as TP.
        gold, predictions = sample()
        gold["cases"][0]["expected"]["title"] = "CO₂ photochemistry"
        predictions["cases"][0]["observed"]["title"] = "CO2 photochemistry"
        report = lab.score(gold, predictions)
        self.assertFalse(report["pass"])
        acs = report["by_publisher_fields"]["ACS"]["title"]
        self.assertEqual((acs["tp"], acs["fp"], acs["fn"]), (0, 1, 1))
        self.assertIn("title", {item["field"] for item in report["errors"]})

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
        # Each fabricated public PDF must have a distinct digest. Duplicate
        # bytes are rejected before the provenance/negative-label checks below.
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
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
        seed_synthetic_audit(predictions)
        result = lab.score(gold, predictions, require_real=True)
        self.assertTrue(result["pass"])
        self.assertEqual(result["assessment"], "provided_real_goldset_not_independently_verified")

    def test_require_real_negative_label_evidence_and_pdf_hash(self):
        # Real-mode structural validation is testable without claiming the
        # fabricated labels or digest are authentic PDF evidence.
        gold, predictions = sample()
        # Each fabricated public PDF must have a distinct digest. Duplicate
        # bytes are rejected before the provenance/negative-label checks below.
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            case["evidence"] = {field: "fabricated contract test only"
                                for field in case["expected"]}
        predictions["engine_commit"] = "b" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        seed_synthetic_audit(predictions)
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
        si_index = next(index for index, case in enumerate(gold["cases"]) if case is si)
        si["source"]["sha256"] = f"{si_index + 1:064x}"
        self.assertTrue(lab.score(gold, predictions, require_real=True)["pass"])

    def test_real_mode_rejects_placeholder_evidence_for_positive_and_null_labels(self):
        # Fabricated fixtures only: placeholder strings are not independently
        # reviewed evidence, even if the observed value happens to match.
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            case["evidence"] = {field: "fabricated contract evidence"
                                for field in case["expected"]}
        predictions["engine_commit"] = "b" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        seed_synthetic_audit(predictions)
        self.assertTrue(lab.score(gold, predictions, require_real=True)["pass"])
        main = gold["cases"][0]
        si = next(case for case in gold["cases"] if case["id"] == "syn-rsc-si")
        for case, field in ((main, "title"), (si, "doi")):
            original = case["evidence"][field]
            for placeholder in ("TBD", " n/A ", "Unverified", "pending", "unknown"):
                with self.subTest(field=field, placeholder=placeholder):
                    case["evidence"][field] = placeholder
                    with self.assertRaisesRegex(
                        lab.DatasetError, f"placeholder evidence for {field}"
                    ):
                        lab.score(gold, predictions, require_real=True)
            case["evidence"][field] = original
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
        seed_synthetic_audit(predictions)
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
        seed_synthetic_audit(predictions)
        audit = predictions["cases"][0]["audit"]
        audit["field_sources"]["title"] = "manual"
        with self.assertRaisesRegex(lab.DatasetError, "manually sourced title"):
            lab.score(gold, predictions, require_real=True)
        audit["field_sources"]["title"] = "Crossref"
        audit["authorsSource"] = "manual"
        with self.assertRaisesRegex(lab.DatasetError, "manually sourced terminal_author"):
            lab.score(gold, predictions, require_real=True)
        audit.pop("authorsSource")
        audit["field_sources"] = []  # malformed native provenance is not ignored
        with self.assertRaisesRegex(lab.DatasetError, "invalid field_sources"):
            lab.score(gold, predictions, require_real=True)
        audit.pop("field_sources")
        with self.assertRaisesRegex(lab.DatasetError, "missing or unverified automatic provenance"):
            lab.score(gold, predictions, require_real=True)
        seed_synthetic_audit(predictions)
        self.assertTrue(lab.score(gold, predictions, require_real=True)["pass"])


    def test_real_mode_rejects_unknown_and_conflicting_provenance(self):
        # All sources and hashes below are synthetic, not publisher evidence.
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            case["evidence"] = {field: "fabricated contract evidence"
                                for field in case["expected"]}
        predictions["engine_commit"] = "b" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        seed_synthetic_audit(predictions)
        audit = predictions["cases"][0]["audit"]
        self.assertTrue(lab.score(gold, predictions, require_real=True)["pass"])
        audit["field_sources"].pop("title")
        with self.assertRaisesRegex(lab.DatasetError, "missing or unverified automatic provenance for title"):
            lab.score(gold, predictions, require_real=True)
        audit["field_sources"]["title"] = "legacy_preserved"
        with self.assertRaisesRegex(lab.DatasetError, "missing or unverified automatic provenance for title"):
            lab.score(gold, predictions, require_real=True)
        audit["field_sources"]["title"] = " MANUAL "
        with self.assertRaisesRegex(lab.DatasetError, "manually sourced title"):
            lab.score(gold, predictions, require_real=True)
        audit["field_sources"]["title"] = "Crossref"
        audit["titleSource"] = "manual"  # conflicting legacy source must win
        with self.assertRaisesRegex(lab.DatasetError, "manually sourced title"):
            lab.score(gold, predictions, require_real=True)
        audit.pop("titleSource")
        self.assertTrue(lab.score(gold, predictions, require_real=True)["pass"])

    def test_real_mode_rejects_trusted_plus_unknown_provenance(self):
        # Fabricated public-kind records test only the structural guard.
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            case["evidence"] = {field: "fabricated contract evidence"
                                for field in case["expected"]}
        predictions["engine_commit"] = "b" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        seed_synthetic_audit(predictions)
        audit = predictions["cases"][0]["audit"]
        audit["titleSource"] = "legacy_preserved"
        with self.assertRaisesRegex(lab.DatasetError, "unverified automatic provenance for title"):
            lab.score(gold, predictions, require_real=True)
        audit["titleSource"] = "Crossref"
        self.assertTrue(lab.score(gold, predictions, require_real=True)["pass"])

    def test_real_mode_rejects_conflicting_trusted_providers(self):
        # Crossref and OpenAlex are each trusted, but independent contradictory
        # single-provider claims cannot be treated as one verified source.
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            case["evidence"] = {field: "fabricated contract evidence"
                                for field in case["expected"]}
        predictions["engine_commit"] = "b" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        seed_synthetic_audit(predictions)
        audit = predictions["cases"][0]["audit"]
        audit["titleSource"] = "OpenAlex"
        with self.assertRaisesRegex(lab.DatasetError, "conflicting automatic provenance for title"):
            lab.score(gold, predictions, require_real=True)
        audit["field_sources"]["title"] = "Crossref + OpenAlex"
        self.assertTrue(lab.score(gold, predictions, require_real=True)["pass"])

    def test_real_mode_composite_provider_cannot_mask_pdf_provenance(self):
        # Fabricated labels and hashes: a scorer contract, NOT real PDF evidence.
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            case["evidence"] = {field: "fabricated contract evidence"
                                for field in case["expected"]}
        predictions["engine_commit"] = "b" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        seed_synthetic_audit(predictions)
        audit = predictions["cases"][0]["audit"]
        audit["field_sources"]["title"] = "Crossref + OpenAlex"
        for unrelated in ("pdf_metadata", "first_page_verified", "pdf_front_matter"):
            audit["titleSource"] = unrelated
            with self.subTest(unrelated=unrelated):
                with self.assertRaisesRegex(
                    lab.DatasetError, "conflicting automatic provenance for title"
                ):
                    lab.score(gold, predictions, require_real=True)
        for compatible in ("Crossref", "OpenAlex", "Crossref + OpenAlex"):
            audit["titleSource"] = compatible
            with self.subTest(compatible=compatible):
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
        # A synthetic ACS sample cannot count toward the real-PDF quota.
        self.assertEqual(audit["main_by_publisher"]["ACS"], 2)
        self.assertEqual(audit["main_by_publisher"]["RSC"], 3)
        self.assertEqual(audit["real_pdf_total"], 19)
        self.assertIn("main_per_publisher", {gap["criterion"] for gap in audit["gaps"]})

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
            seed_synthetic_audit(predictions)
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


    def test_real_field_label_inventory_exposes_unmeasurable_denominators(self):
        # Entirely fabricated records: this is a schema/coverage contract.
        gold, _ = sample()
        synthetic = lab.audit_field_label_coverage(gold)
        self.assertFalse(synthetic["ready"])
        self.assertEqual(synthetic["positive_labels"]["doi"], 0)
        self.assertEqual(synthetic["unknown_labels"]["title"], 0)

        cases = []
        templates = {case["publisher"]: case for case in gold["cases"]
                     if case["publisher"] in lab.PUBLISHERS
                     and case["expected"].get("document_kind") == "main_article"}
        si = next(case for case in gold["cases"]
                  if case["expected"].get("document_kind") == "supporting_information")
        no_doi = next(case for case in gold["cases"] if case["id"] == "syn-no-doi")
        for publisher in lab.PUBLISHERS:
            for index in range(3):
                item = copy.deepcopy(templates[publisher])
                item["id"] = f"fake-label-{publisher}-{index}"
                cases.append(item)
        for index in range(3):
            item = copy.deepcopy(si)
            item["id"] = f"fake-label-si-{index}"
            cases.append(item)
        for index in range(2):
            item = copy.deepcopy(no_doi)
            item["id"] = f"fake-label-no-doi-{index}"
            cases.append(item)
        for index, item in enumerate(cases):
            item["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            item["evidence"] = {field: "fabricated, not real evidence"
                                for field in item["expected"]}
        gold["cases"] = cases
        breadth = lab.audit_goldset_coverage(gold)
        self.assertTrue(breadth["ready"], breadth["gaps"])
        self.assertEqual(breadth["main_by_publisher"]["RSC"], 3)
        labels = lab.audit_field_label_coverage(gold)
        self.assertFalse(labels["ready"])
        self.assertEqual(labels["positive_labels"]["journal_abbreviation"], 0)
        self.assertIn({"criterion": "field_positive_labels",
                       "field": "journal_abbreviation", "actual": 0, "required": 1},
                      labels["gaps"])
        self.assertEqual(labels["negative_labels"]["doi"], 5)
        self.assertEqual(labels["unknown_labels"]["issue"], 17)
        gold["cases"][0]["expected"]["journal_abbreviation"] = "J. Am. Chem. Soc."
        gold["cases"][0]["evidence"]["journal_abbreviation"] = "fabricated test"
        labels = lab.audit_field_label_coverage(gold)
        self.assertTrue(labels["ready"])
        self.assertEqual(labels["positive_labels"]["journal_abbreviation"], 1)

    def test_si_metadata_cannot_satisfy_main_publisher_core_coverage(self):
        # Fabricated labels only: SI can repeat its parent DOI/title/authors,
        # but must not prove main-article extraction for that publisher.
        gold, _ = sample()
        main = copy.deepcopy(next(case for case in gold["cases"]
                                  if case["publisher"] == "ACS"
                                  and case["expected"].get("document_kind") == "main_article"))
        si = copy.deepcopy(next(case for case in gold["cases"]
                                if case["expected"].get("document_kind") == "supporting_information"))
        main["id"] = "fabricated-acs-main-with-unknown-fields"
        main["expected"] = {"document_kind": "main_article"}
        si["id"] = "fabricated-acs-si-with-parent-metadata"
        si["publisher"] = "ACS"
        si["expected"] = {
            "document_kind": "supporting_information",
            "doi": "10.1021/fabricated-si-parent",
            "title": "Fabricated SI parent title",
            "terminal_author": "Fabricated Last Author",
            "journal": "Fabricated Journal",
            "year": "2026",
        }
        for index, case in enumerate((main, si), 1):
            case["source"] = {"kind": "public", "sha256": f"{index:064x}"}
            case["evidence"] = {field: "fabricated contract-only label"
                                for field in case["expected"]}
        gold["cases"] = [main, si]
        report = lab.audit_field_label_coverage(gold)
        self.assertEqual(report["positive_labels"]["doi"], 1)
        self.assertEqual(report["core_positive_by_publisher"]["ACS"]["doi"], 0)
        self.assertEqual(report["core_positive_by_publisher"]["ACS"]["title"], 0)
        self.assertEqual(report["core_positive_by_publisher"]["ACS"]["document_kind"], 1)
        self.assertIn({"criterion": "publisher_core_positive_labels",
                       "publisher": "ACS", "field": "doi", "actual": 0,
                       "required": 1}, report["gaps"])
        self.assertFalse(report["ready"])
        main["expected"]["doi"] = "10.1021/fabricated-main"
        main["evidence"]["doi"] = "fabricated contract-only label"
        self.assertEqual(lab.audit_field_label_coverage(gold)
                         ["core_positive_by_publisher"]["ACS"]["doi"], 1)

    def test_field_label_gate_requires_existing_real_coverage_gate(self):
        gold, predictions = sample()
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            goldfile, predfile = base / "gold.json", base / "pred.json"
            goldfile.write_text(json.dumps(gold), encoding="utf-8")
            predfile.write_text(json.dumps(predictions), encoding="utf-8")
            cmd = [sys.executable, str(TOOL), "--goldset", str(goldfile),
                   "--predictions", str(predfile), "--field-coverage-gate"]
            blocked = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(blocked.returncode, 2)
            self.assertIn("--field-coverage-gate requires --coverage-gate", blocked.stderr)


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
        seed_synthetic_audit(predictions)
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


    def test_real_mode_opt_in_pdf_bytes_gate_detects_missing_tampered_and_matched(self):
        # Fake PDF-header bytes ONLY: exercises the integrity contract, not
        # publisher provenance, a PDF parser, or native extraction accuracy.
        gold, predictions = sample()
        pdf_bytes = {}
        for index, case in enumerate(gold["cases"]):
            content = b"%PDF-1.4\nsynthetic-integrity-only-" + str(index).encode() + b"\n%%EOF\n"
            digest = hashlib.sha256(content).hexdigest()
            case["source"] = {"kind": "public", "sha256": digest}
            case["evidence"] = {field: "fabricated test label" for field in case["expected"]}
            predictions["cases"][index]["source_sha256"] = digest
            pdf_bytes[digest] = content
        predictions["engine_commit"] = "a" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        seed_synthetic_audit(predictions)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            pdf_dir = root / "local-only"
            pdf_dir.mkdir()
            goldfile, predfile, report = (root / name for name in
                                          ("gold.json", "pred.json", "score.json"))
            goldfile.write_text(json.dumps(gold), encoding="utf-8")
            predfile.write_text(json.dumps(predictions), encoding="utf-8")
            cmd = [sys.executable, "-B", str(TOOL), "--goldset", str(goldfile),
                   "--predictions", str(predfile), "--require-real",
                   "--pdf-dir", str(pdf_dir), "--report", str(report)]
            missing = subprocess.run(cmd, capture_output=True, text=True, check=False)
            self.assertEqual(missing.returncode, 1, missing.stderr)
            result = json.loads(report.read_text(encoding="utf-8"))
            self.assertFalse(result["pass"])
            self.assertEqual(result["pdf_integrity_audit"]["hash_matched"], 0)
            self.assertEqual({entry["reason"] for entry in result["pdf_integrity_audit"]["issues"]},
                             {"missing"})
            self.assertNotIn(str(pdf_dir), json.dumps(result))
            self.assertNotIn(next(iter(pdf_bytes)), json.dumps(result))
            for digest, content in pdf_bytes.items():
                (pdf_dir / (digest + ".pdf")).write_bytes(content)
            matched = subprocess.run(cmd, capture_output=True, text=True, check=False)
            self.assertEqual(matched.returncode, 0, matched.stderr)
            result = json.loads(report.read_text(encoding="utf-8"))
            self.assertTrue(result["pass"])
            self.assertTrue(result["pdf_integrity_audit"]["ready"])
            self.assertEqual(result["pdf_integrity_audit"]["hash_matched"], len(pdf_bytes))
            self.assertEqual(result["assessment"], "provided_real_goldset_not_independently_verified")
            digest = next(iter(pdf_bytes))
            (pdf_dir / (digest + ".pdf")).write_bytes(b"%PDF-1.4\ntampered\n%%EOF\n")
            mismatch = subprocess.run(cmd, capture_output=True, text=True, check=False)
            self.assertEqual(mismatch.returncode, 1, mismatch.stderr)
            result = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(result["pdf_integrity_audit"]["hash_matched"], len(pdf_bytes) - 1)
            self.assertIn("sha256_mismatch",
                          {entry["reason"] for entry in result["pdf_integrity_audit"]["issues"]})

    def test_real_pdf_binding_rejects_swapped_missing_and_invalid_snapshots(self):
        # Fabricated contract values only. An opaque ID is not sufficient to
        # establish which PDF supplied a Qt record snapshot.
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            digest = f"{index + 1:064x}"
            case["source"] = {"kind": "public", "sha256": digest}
            case["evidence"] = {field: "synthetic contract evidence"
                                for field in case["expected"]}
            predictions["cases"][index]["source_sha256"] = digest
        predictions["engine_commit"] = "a" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        seed_synthetic_audit(predictions)
        self.assertTrue(lab.score(gold, predictions, require_real=True,
                                  require_source_binding=True)["pass"])
        with self.assertRaisesRegex(lab.DatasetError, "requires --require-real"):
            lab.score(gold, predictions, require_source_binding=True)
        first, second = predictions["cases"][:2]
        first["source_sha256"], second["source_sha256"] = (
            second["source_sha256"], first["source_sha256"])
        with self.assertRaisesRegex(lab.DatasetError, "capture PDF binding mismatch") as error:
            lab.score(gold, predictions, require_real=True, require_source_binding=True)
        self.assertNotIn(first["source_sha256"], str(error.exception))
        first["source_sha256"], second["source_sha256"] = (
            second["source_sha256"], first["source_sha256"])
        del first["source_sha256"]
        with self.assertRaisesRegex(lab.DatasetError, "missing or invalid capture PDF binding"):
            lab.score(gold, predictions, require_real=True, require_source_binding=True)
        first["source_sha256"] = "not-a-sha"
        with self.assertRaisesRegex(lab.DatasetError, "missing or invalid capture PDF binding"):
            lab.score(gold, predictions, require_real=True, require_source_binding=True)

    def test_pdf_dir_cli_blocks_unbound_snapshot_before_integrity_report(self):
        gold, predictions = sample()
        for index, case in enumerate(gold["cases"]):
            case["source"] = {"kind": "public", "sha256": f"{index + 1:064x}"}
            case["evidence"] = {field: "synthetic contract evidence"
                                for field in case["expected"]}
        predictions["engine_commit"] = "a" * 40
        predictions["capture_kind"] = "qt_record_snapshot_read_only"
        seed_synthetic_audit(predictions)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            pdf_dir = root / "local-only"
            pdf_dir.mkdir()
            gold_path, pred_path, report = (root / name for name in
                                           ("gold.json", "pred.json", "score.json"))
            gold_path.write_text(json.dumps(gold), encoding="utf-8")
            pred_path.write_text(json.dumps(predictions), encoding="utf-8")
            run = subprocess.run(
                [sys.executable, "-B", str(TOOL), "--goldset", str(gold_path),
                 "--predictions", str(pred_path), "--require-real",
                 "--pdf-dir", str(pdf_dir), "--report", str(report)],
                capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 2, run.stderr)
            self.assertIn("missing or invalid capture PDF binding", run.stderr)
            self.assertFalse(report.exists())

    def test_real_mode_pdf_bytes_gate_rejects_unsafe_cli_paths(self):
        gold, predictions = sample()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            goldfile, predfile = root / "gold.json", root / "pred.json"
            goldfile.write_text(json.dumps(gold), encoding="utf-8")
            predfile.write_text(json.dumps(predictions), encoding="utf-8")
            pdf_dir = root / "local-only"
            pdf_dir.mkdir()
            cmd = [sys.executable, "-B", str(TOOL), "--goldset", str(goldfile),
                   "--predictions", str(predfile), "--pdf-dir", str(pdf_dir)]
            no_real = subprocess.run(cmd, capture_output=True, text=True, check=False)
            self.assertEqual(no_real.returncode, 2)
            self.assertIn("--pdf-dir requires --require-real", no_real.stderr)
            nested_report = pdf_dir / "private-report.json"
            unsafe = subprocess.run(cmd + ["--require-real", "--report", str(nested_report)],
                                    capture_output=True, text=True, check=False)
            self.assertEqual(unsafe.returncode, 2)
            self.assertIn("report path must be outside", unsafe.stderr)
            self.assertFalse(nested_report.exists())
            invalid = subprocess.run(cmd[:-1] + [str(goldfile), "--require-real"],
                                     capture_output=True, text=True, check=False)
            self.assertEqual(invalid.returncode, 2)
            self.assertIn("existing non-symlink directory", invalid.stderr)

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



    def test_score_report_cannot_overwrite_either_private_input(self):
        # Synthetic files only. The scorer must preserve both input byte streams.
        gold, predictions = sample()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            gold_path = root / "goldset.json"
            predictions_path = root / "predictions.json"
            gold_path.write_text(json.dumps(gold), encoding="utf-8")
            predictions_path.write_text(json.dumps(predictions), encoding="utf-8")
            originals = {p: p.read_bytes() for p in (gold_path, predictions_path)}
            for target in (gold_path, predictions_path):
                with self.subTest(target=target.name):
                    process = subprocess.run(
                        [sys.executable, "-B", str(TOOL), "--goldset", str(gold_path),
                         "--predictions", str(predictions_path), "--report", str(target)],
                        capture_output=True, text=True, check=False,
                    )
                    self.assertEqual(process.returncode, 2, process.stderr)
                    self.assertIn("overwrite input goldset or predictions", process.stderr)
                    self.assertEqual({p: p.read_bytes() for p in originals}, originals)
            self.assertEqual(list(root.glob(".luna-bibliography-score-*.tmp")), [])

    def test_score_report_symlink_does_not_follow_private_target(self):
        gold, predictions = sample()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            gold_path, predictions_path = root / "gold.json", root / "observed.json"
            gold_path.write_text(json.dumps(gold), encoding="utf-8")
            predictions_path.write_text(json.dumps(predictions), encoding="utf-8")
            unrelated = root / "unrelated-private-notes.txt"
            unrelated.write_bytes(b"keep synthetic private notes")
            report_link = root / "score.json"
            try:
                report_link.symlink_to(unrelated)
            except (OSError, NotImplementedError):
                self.skipTest("local symlinks unavailable")
            process = subprocess.run(
                [sys.executable, "-B", str(TOOL), "--goldset", str(gold_path),
                 "--predictions", str(predictions_path), "--report", str(report_link)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(process.returncode, 2, process.stderr)
            self.assertIn("refusing symlink report path", process.stderr)
            self.assertEqual(unrelated.read_bytes(), b"keep synthetic private notes")
            self.assertTrue(report_link.is_symlink())
            self.assertEqual(list(root.glob(".luna-bibliography-score-*.tmp")), [])

    def test_score_report_atomic_replacement_and_no_temp_leaks(self):
        gold, predictions = sample()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            gold_path, predictions_path = root / "gold.json", root / "observed.json"
            report_path = root / "score.json"
            gold_path.write_text(json.dumps(gold), encoding="utf-8")
            predictions_path.write_text(json.dumps(predictions), encoding="utf-8")
            for _ in range(2):
                process = subprocess.run(
                    [sys.executable, "-B", str(TOOL), "--goldset", str(gold_path),
                     "--predictions", str(predictions_path), "--report", str(report_path)],
                    capture_output=True, text=True, check=False,
                )
                self.assertEqual(process.returncode, 0, process.stderr)
                report = json.loads(report_path.read_text(encoding="utf-8"))
                self.assertTrue(report["pass"])
                self.assertEqual(report["assessment"], "synthetic_harness_only")
                self.assertEqual(list(root.glob(".luna-bibliography-score-*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
