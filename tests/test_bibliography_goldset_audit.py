"""Synthetic-only CLI tests; never a measurement of LUNA PDF accuracy."""
import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "bibliography_goldset_audit.py"
FIXTURE = ROOT / "tests" / "fixtures" / "bibliography_accuracy_goldset.synthetic.json"


def run_audit(gold):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "fabricated-goldset.json"
        report = Path(folder) / "fabricated-report.json"
        path.write_text(json.dumps(gold), encoding="utf-8")
        process = subprocess.run(
            [sys.executable, "-B", str(TOOL), "--goldset", str(path),
             "--report", str(report)],
            capture_output=True, text=True, check=False,
        )
        result = json.loads(report.read_text(encoding="utf-8")) if report.exists() else None
        return process, result


class GoldsetCollectionAuditTests(unittest.TestCase):
    def test_synthetic_fixture_is_not_real_coverage(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        process, result = run_audit(gold)
        self.assertEqual(process.returncode, 1, process.stderr)
        self.assertFalse(result["ready"])
        self.assertEqual(result["coverage_audit"]["real_pdf_total"], 0)
        self.assertEqual(len(result["synthetic_case_ids_excluded"]), len(gold["cases"]))
        self.assertEqual(result["unknown_cases"], [])
        self.assertEqual(result["assessment"],
                         "goldset_collection_readiness_only_not_extraction_accuracy")

    def test_unknown_vs_explicit_null_is_case_specific(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        case = copy.deepcopy(gold["cases"][0])
        case["id"] = "fabricated-case"
        case["source"] = {"kind": "public", "sha256": "a" * 64}
        case["evidence"] = {field: "fabricated contract label"
                            for field in case["expected"]}
        case["expected"]["doi"] = None
        case["evidence"]["doi"] = "fabricated known-negative DOI"
        case["expected"].pop("issue", None)
        gold["cases"] = [case]
        process, result = run_audit(gold)
        self.assertEqual(process.returncode, 1, process.stderr)
        self.assertEqual(result["coverage_audit"]["real_pdf_total"], 1)
        entry = result["unknown_cases"][0]
        self.assertEqual(entry["case_id"], "fabricated-case")
        self.assertIn("issue", entry["unknown_fields"])
        self.assertNotIn("doi", entry["unknown_fields"])
        self.assertEqual(result["field_label_audit"]["negative_labels"]["doi"], 1)
        self.assertEqual(result["unknown_by_publisher"]["ACS"]["issue"], 1)

    def test_missing_required_real_evidence_blocks_report(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        case = copy.deepcopy(gold["cases"][0])
        case["source"] = {"kind": "public", "sha256": "b" * 64}
        case["evidence"] = {}
        gold["cases"] = [case]
        process, result = run_audit(gold)
        self.assertEqual(process.returncode, 2)
        self.assertIsNone(result)
        self.assertIn("missing evidence", process.stderr)

    def test_invalid_json_is_not_a_ready_report(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "invalid.json"
            path.write_text("not json", encoding="utf-8")
            process = subprocess.run(
                [sys.executable, "-B", str(TOOL), "--goldset", str(path)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(process.returncode, 2)
            self.assertIn("[BLOCK]", process.stderr)


if __name__ == "__main__":
    unittest.main()
