"""Synthetic contracts; these are NOT actual PDF4QT extraction tests."""
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("bibliography_si_risk_probe", ROOT / "tools" / "bibliography_si_risk_probe.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class DummyPage:
    def __init__(self, lines):
        self.lines = lines

    def get_text(self, fmt):
        assert fmt == "dict"
        return {"blocks": [{"lines": [
            {"bbox": (float(x), float(y), float(x + 40), float(y + 6)),
             "spans": [{"text": text}]} for y, x, text in self.lines
        ]}]}


class DummyDoc:
    def __init__(self, lines):
        self.page = DummyPage(lines)

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        pass

    def __len__(self):
        return 1

    def __getitem__(self, item):
        assert item == 0
        return self.page


class DummyFitz:
    def __init__(self, lines):
        self.lines = lines

    def open(self, _path):
        return DummyDoc(self.lines)


class SiRiskProbeSyntheticTests(unittest.TestCase):
    def test_current_rule_flags_acs_card_link_as_si(self):
        lines = ["Article", "Article title", "Cite This: J. Org. Chem. 2025",
                 "Read Online", "Article Recommendations", "Supporting Information"]
        self.assertEqual(probe.classify_lines(lines), "supporting_information")
        self.assertEqual(probe.classify_lines(["Supporting Information", "Contents"]), "supporting_information")
        self.assertEqual(probe.classify_lines(["Supplementary Information (SI) for Organic & Biomolecular Chemistry"]), "supporting_information")
        self.assertEqual(probe.classify_lines(["Article", "Research report"]), "main_article")
        self.assertEqual(probe.classify_lines(["Article"] * 16 + ["Supporting Information"]), "main_article")

    def test_position_sort_prioritizes_top_of_page_rsc_si(self):
        # Synthetic source-order block stream: SI header appears last in the
        # stream, but its page y-coordinate is first. PDF4QT sorts y then x.
        scrambled = [(40, 40, "Table of Contents"), (120, 40, "Material and Methods"),
                     (-1, 10, "Supplementary Information (SI) for Organic & Biomolecular Chemistry")]
        lines = probe.position_lines(DummyPage(scrambled))
        self.assertTrue(probe.first16_si(lines))
        self.assertTrue(lines[0].startswith("Supplementary Information"))

    def test_opaque_report_and_integrity_gate(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            filename = "synthetic-test.pdf"
            data = b"%PDF-1.7\nsynth not a real PDF\n"
            (root / filename).write_bytes(data)
            case = {"case_id": "syn-01", "file_name": filename,
                    "pdf_sha256": hashlib.sha256(data).hexdigest(),
                    "candidate_expected": {"document_kind": "main_article"}}
            inp = {"kind": "candidate_reference_not_approved_goldset", "cases": [case]}
            mock = DummyFitz([(10, 5, "Article"), (90, 12, "Supporting Information")])
            result = probe.examine(inp, [root], mock)
            self.assertEqual(result["potential_misclassifications"], 1)
            self.assertEqual(result["assessment"], "local_pdf_position_proxy_NOT_LUNA_PDF4QT_accuracy")
            serialized = json.dumps(result)
            self.assertNotIn(filename, serialized)
            self.assertNotIn(str(root), serialized)
            self.assertNotIn("Supporting Information", serialized)
            # Modified PDF contents must not be silently treated as the same source.
            (root / filename).write_bytes(b"%PDF-1.7\nmodified\n")
            with self.assertRaisesRegex(ValueError, "SHA256"):
                probe.examine(inp, [root], mock)

    def test_rejects_unsafe_paths_duplicates_and_non_pdf(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            data = b"not-pdf"
            filename = "sample.pdf"
            (root / filename).write_bytes(data)
            case = {"case_id": "abc", "file_name": filename,
                    "pdf_sha256": hashlib.sha256(data).hexdigest(),
                    "candidate_expected": {"document_kind": "main_article"}}
            with self.assertRaisesRegex(ValueError, "signature"):
                probe.verified_local_pdf(case, [root])
            case["file_name"] = "../sample.pdf"
            with self.assertRaisesRegex(ValueError, "unsafe"):
                probe.verified_local_pdf(case, [root])
            case["file_name"] = filename
            inp = {"kind": "candidate_reference_not_approved_goldset", "cases": [case, case]}
            with self.assertRaises(ValueError):
                probe.examine(inp, [root], DummyFitz([]))
            (root / filename).unlink()
            (root / filename).symlink_to(root / "unrelated-private.txt")
            with self.assertRaisesRegex(ValueError, "symbolic-link"):
                probe.verified_local_pdf(case, [root])


if __name__ == "__main__":
    unittest.main()
