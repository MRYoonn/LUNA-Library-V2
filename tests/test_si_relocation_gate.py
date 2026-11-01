"""Offline synthetic fixtures; no real PDFs, QSettings, or network calls."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from si_associations import AssociationIndex
from si_relocation_gate import audit_paired_move, apply_verified_relocation

MAIN = "11111111-1111-4111-8111-111111111111"
SI = "22222222-2222-4222-8222-222222222222"
DOI = "10.1021/jacs.6c01234"  # synthetic


class PairedMoveGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "Old").mkdir()
        (self.root / "New").mkdir()
        self.old_pdf = self.root / "Old" / "SI.pdf"
        self.old_meta = self.root / "Old" / "SI.luna-meta.json"
        self.new_pdf = self.root / "New" / "Renamed.pdf"
        self.new_meta = self.root / "New" / "Renamed.luna-meta.json"
        self.old_pdf.write_bytes(b"%PDF-1.4\n% synthetic-only\n")
        self.old_meta.write_text(json.dumps({"schemaVersion": 1, "documentId": SI,
                                             "manualTitle": "Do not modify"}))
        self.index = AssociationIndex()
        self.index.add_document(MAIN, "main", "Old/Main.pdf", DOI)
        self.index.add_document(SI, "si", "Old/SI.pdf")
        self.index.confirm_link(MAIN, SI, DOI, user_confirmed=True)

    def move_pdf_only(self):
        self.old_pdf.rename(self.new_pdf)

    def move_both(self):
        self.move_pdf_only()
        self.old_meta.rename(self.new_meta)

    def audit(self):
        return apply_verified_relocation(self.index, self.root, SI,
                                         "Old/SI.pdf", "New/Renamed.pdf")

    def test_successful_pair_updates_only_in_memory(self):
        self.move_both()
        before = self.new_meta.read_bytes()
        result = self.audit()
        self.assertEqual(result.code, "verified_paired_move")
        self.assertTrue(result.ok)
        self.assertEqual(self.index.documents[SI].relative_path, "New/Renamed.pdf")
        self.assertEqual(self.index.links[SI].main_id, MAIN)
        self.assertEqual(self.new_meta.read_bytes(), before)

    def test_pdf_only_move_is_blocked_with_stranded_sidecar(self):
        self.move_pdf_only()
        self.assertEqual(self.audit().code, "sidecar_stranded")
        self.assertEqual(self.index.documents[SI].relative_path, "Old/SI.pdf")

    def test_missing_destination_identity_is_blocked(self):
        self.move_pdf_only()
        self.old_meta.unlink()
        self.assertEqual(self.audit().code, "destination_identity_missing")

    def test_duplicate_original_pdf_is_blocked(self):
        self.new_pdf.write_bytes(self.old_pdf.read_bytes())
        self.new_meta.write_bytes(self.old_meta.read_bytes())
        self.assertEqual(self.audit().code, "old_pdf_still_present")

    def test_mismatched_uuid_does_not_rebind(self):
        self.move_both()
        self.new_meta.write_text(json.dumps({"schemaVersion": 1, "documentId": MAIN}))
        self.assertEqual(self.audit().code, "identity_mismatch")
        self.assertIn(SI, self.index.links)

    def test_corrupt_and_future_sidecars(self):
        self.move_both()
        self.new_meta.write_bytes(b"{bad")
        self.assertEqual(self.audit().code, "invalid_sidecar")
        self.new_meta.write_text(json.dumps({"schemaVersion": 2, "documentId": SI}))
        self.assertEqual(self.audit().code, "unsupported_sidecar")

    def test_oversized_sidecar(self):
        self.move_both()
        self.new_meta.write_bytes(b"x" * 65537)
        self.assertEqual(self.audit().code, "unsafe_sidecar")

    def test_invalid_relative_paths(self):
        self.move_both()
        for path in ("../New/Renamed.pdf", "C:/New/Renamed.pdf", "New\\Renamed.pdf"):
            with self.subTest(path=path):
                self.assertFalse(audit_paired_move(self.root, "Old/SI.pdf", path, SI).ok)

    def test_stale_index_refused(self):
        self.move_both()
        result = apply_verified_relocation(self.index, self.root, SI,
                                           "Elsewhere/SI.pdf", "New/Renamed.pdf")
        self.assertEqual(result.code, "stale_move")

    def test_root_symlink_refused(self):
        self.move_both()
        alias = self.root / "Alias"
        try:
            alias.symlink_to(self.root, target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest("symlink creation unavailable")
        self.assertEqual(audit_paired_move(alias, "Old/SI.pdf", "New/Renamed.pdf", SI).code,
                         "unsafe_root")

    def test_destination_symlink_refused(self):
        self.move_both()
        self.new_pdf.unlink()
        try:
            self.new_pdf.symlink_to(self.root / "Old" / "Main.pdf")
        except (OSError, NotImplementedError):
            self.skipTest("symlink creation unavailable")
        self.assertEqual(self.audit().code, "unsafe_path")

    def test_no_file_writes(self):
        self.move_both()
        before = self.new_pdf.read_bytes(), self.new_meta.read_bytes()
        with patch("si_relocation_gate.os.replace", create=True,
                   side_effect=AssertionError("no writes allowed")):
            self.assertTrue(self.audit().ok)
        self.assertEqual((self.new_pdf.read_bytes(), self.new_meta.read_bytes()), before)


if __name__ == "__main__":
    unittest.main()
