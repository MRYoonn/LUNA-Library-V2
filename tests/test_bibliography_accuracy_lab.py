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
