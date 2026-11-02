"""Synthetic-only CLI tests; never a measurement of LUNA PDF accuracy."""
import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "bibliography_goldset_audit.py"
FIXTURE = ROOT / "tests" / "fixtures" / "bibliography_accuracy_goldset.synthetic.json"


def run_audit(gold, pdf_files=None):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "fabricated-goldset.json"
        report = Path(folder) / "fabricated-report.json"
        path.write_text(json.dumps(gold), encoding="utf-8")
        cmd = [sys.executable, "-B", str(TOOL), "--goldset", str(path),
               "--report", str(report)]
        if pdf_files is not None:
            pdf_dir = Path(folder) / "private-pdfs"
            pdf_dir.mkdir()
            for digest, content in pdf_files.items():
                (pdf_dir / (digest + ".pdf")).write_bytes(content)
            cmd += ["--pdf-dir", str(pdf_dir)]
        process = subprocess.run(
            cmd,
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

    def test_explicit_null_requires_independent_absence_evidence(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        case = copy.deepcopy(gold["cases"][0])
        case["id"] = "synthetic-null-evidence-contract"
        case["source"] = {"kind": "public", "sha256": "c" * 64}
        case["evidence"] = {field: "synthetic review label"
                            for field in case["expected"]}
        case["expected"]["doi"] = None
        case["evidence"].pop("doi", None)
        gold["cases"] = [case]
        process, report = run_audit(gold)
        self.assertEqual(process.returncode, 1, process.stderr)
        self.assertFalse(report["ready"])
        self.assertEqual(report["evidence_audit"]["gaps"], [{
            "case_id": case["id"],
            "criterion": "missing_nonblank_field_evidence", "field": "doi",
        }])

    def test_blank_positive_evidence_cannot_pass_collection_audit(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        case = copy.deepcopy(gold["cases"][0])
        case["source"] = {"kind": "permissioned", "sha256": "d" * 64}
        case["evidence"] = {field: "synthetic review label"
                            for field in case["expected"]}
        case["evidence"]["title"] = "  "
        gold["cases"] = [case]
        process, report = run_audit(gold)
        self.assertEqual(process.returncode, 1, process.stderr)
        self.assertIn({"case_id": case["id"],
                       "criterion": "missing_nonblank_field_evidence",
                       "field": "title"}, report["evidence_audit"]["gaps"])

    def test_placeholder_hash_is_not_evidence_ready(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        case = copy.deepcopy(gold["cases"][0])
        case["source"] = {"kind": "public", "sha256": "0" * 64}
        case["evidence"] = {field: "synthetic review label"
                            for field in case["expected"]}
        gold["cases"] = [case]
        process, report = run_audit(gold)
        self.assertEqual(process.returncode, 1, process.stderr)
        self.assertEqual(report["evidence_audit"]["gaps"], [{
            "case_id": case["id"], "criterion": "placeholder_pdf_sha256",
        }])


    def test_placeholder_evidence_is_not_independent_review(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        for placeholder in ("TBD", " n/A ", "Unverified"):
            with self.subTest(placeholder=placeholder):
                case = copy.deepcopy(gold["cases"][0])
                case["source"] = {"kind": "public", "sha256": "e" * 64}
                case["evidence"] = {field: "fabricated review label"
                                    for field in case["expected"]}
                case["evidence"]["doi"] = placeholder
                gold["cases"] = [case]
                process, report = run_audit(gold)
                self.assertEqual(process.returncode, 1, process.stderr)
                self.assertFalse(report["evidence_audit"]["ready"])
                self.assertIn({
                    "case_id": case["id"], "criterion": "placeholder_field_evidence",
                    "field": "doi",
                }, report["evidence_audit"]["gaps"])

    def test_report_cannot_overwrite_input_goldset(self):
        with tempfile.TemporaryDirectory() as folder:
            gold = FIXTURE.read_text(encoding="utf-8")
            input_path = Path(folder) / "goldset.json"
            input_path.write_text(gold, encoding="utf-8")
            process = subprocess.run(
                [sys.executable, "-B", str(TOOL), "--goldset", str(input_path),
                 "--report", str(input_path)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(process.returncode, 2, process.stderr)
            self.assertIn("overwrite input goldset", process.stderr)
            self.assertEqual(input_path.read_text(encoding="utf-8"), gold)


    def test_private_local_pdf_hash_match_is_not_extraction_accuracy(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        case = copy.deepcopy(gold["cases"][0])
        payload = b"%PDF-1.7\\nsynthetic hash test only\\n%%EOF\\n"
        digest = hashlib.sha256(payload).hexdigest()
        case["source"] = {"kind": "public", "sha256": digest}
        case["evidence"] = {field: "synthetic review label"
                            for field in case["expected"]}
        gold["cases"] = [case]
        process, report = run_audit(gold, {digest: payload})
        self.assertEqual(process.returncode, 1, process.stderr)
        self.assertEqual(report["pdf_integrity_audit"], {
            "status": "checked", "ready": True, "total": 1,
            "hash_matched": 1, "issues": [],
        })
        self.assertFalse(report["ready"])  # Still below 20-PDF coverage.
        self.assertIn("not_extraction_accuracy", report["assessment"])

    def test_local_pdf_missing_mismatch_and_invalid_header_are_reported(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        case = copy.deepcopy(gold["cases"][0])
        payload = b"%PDF-1.7\\nsynthetic correct\\n%%EOF\\n"
        digest = hashlib.sha256(payload).hexdigest()
        case["source"] = {"kind": "permissioned", "sha256": digest}
        case["evidence"] = {field: "synthetic review label"
                            for field in case["expected"]}
        gold["cases"] = [case]
        for files, reason in (
            ({}, "missing"),
            ({digest: b"%PDF-1.7\\nwrong bytes"}, "sha256_mismatch"),
            ({digest: b"not a PDF"}, "invalid_pdf_header"),
        ):
            with self.subTest(reason=reason):
                process, report = run_audit(gold, files)
                self.assertEqual(process.returncode, 1, process.stderr)
                integrity = report["pdf_integrity_audit"]
                self.assertFalse(integrity["ready"])
                self.assertEqual(integrity["hash_matched"], 0)
                self.assertEqual(integrity["issues"], [{
                    "case_id": case["id"], "reason": reason,
                }])
                self.assertNotIn("private-pdfs", json.dumps(report))

    def test_local_pdf_symlink_is_rejected_without_path_leak(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        case = copy.deepcopy(gold["cases"][0])
        payload = b"%PDF-1.7\\nsynthetic symlink target\\n%%EOF\\n"
        digest = hashlib.sha256(payload).hexdigest()
        case["source"] = {"kind": "public", "sha256": digest}
        case["evidence"] = {field: "synthetic review label"
                            for field in case["expected"]}
        gold["cases"] = [case]
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            target = base / "private-original.pdf"
            target.write_bytes(payload)
            pdf_dir = base / "pdfs"
            pdf_dir.mkdir()
            try:
                (pdf_dir / (digest + ".pdf")).symlink_to(target)
            except (OSError, NotImplementedError):
                self.skipTest("local symlinks unavailable")
            path = base / "gold.json"
            path.write_text(json.dumps(gold), encoding="utf-8")
            result = subprocess.run(
                [sys.executable, "-B", str(TOOL), "--goldset", str(path),
                 "--pdf-dir", str(pdf_dir)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 1, result.stderr)
            audit = json.loads(result.stdout)["pdf_integrity_audit"]
            self.assertEqual(audit["issues"], [{
                "case_id": case["id"], "reason": "unsafe_file",
            }])
            self.assertNotIn(str(target), result.stdout)


    def test_report_cannot_overwrite_staged_pdf(self):
        gold = json.loads(FIXTURE.read_text(encoding="utf-8"))
        case = copy.deepcopy(gold["cases"][0])
        payload = b"%PDF-1.7\\nsynthetic do not overwrite\\n%%EOF\\n"
        digest = hashlib.sha256(payload).hexdigest()
        case["source"] = {"kind": "public", "sha256": digest}
        case["evidence"] = {field: "synthetic review label"
                            for field in case["expected"]}
        gold["cases"] = [case]
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            pdf_dir = base / "private-pdfs"
            pdf_dir.mkdir()
            pdf_path = pdf_dir / (digest + ".pdf")
            pdf_path.write_bytes(payload)
            gold_path = base / "gold.json"
            gold_path.write_text(json.dumps(gold), encoding="utf-8")
            process = subprocess.run(
                [sys.executable, "-B", str(TOOL), "--goldset", str(gold_path),
                 "--pdf-dir", str(pdf_dir), "--report", str(pdf_path)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(process.returncode, 2)
            self.assertIn("outside the private PDF directory", process.stderr)
            self.assertEqual(pdf_path.read_bytes(), payload)

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
