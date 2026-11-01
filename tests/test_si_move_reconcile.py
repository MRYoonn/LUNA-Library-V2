"""Offline synthetic integration tests using the actual SI modules in checkout."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.si_associations import AssociationIndex
from tools import si_state_store
from tools.si_move_reconcile import reconcile_completed_move

MAIN = "11111111-1111-4111-8111-111111111111"
SI1 = "22222222-2222-4222-8222-222222222222"
SI2 = "33333333-3333-4333-8333-333333333333"
DOI = "10.1021/jacs.6c01234"  # Synthetic DOI, never queried.


class MoveReconcileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for folder in ("Old", "New"):
            (self.root / folder).mkdir()
        idx = AssociationIndex()
        idx.add_document(MAIN, "main", "Old/Main.pdf", DOI)
        idx.add_document(SI1, "si", "Old/SI1.pdf")
        idx.add_document(SI2, "si", "Old/SI2.pdf")
        idx.confirm_link(MAIN, SI1, DOI, user_confirmed=True)
        idx.confirm_link(MAIN, SI2, DOI, user_confirmed=True)
        self.initial = si_state_store.commit(self.root, idx, "missing", user_confirmed=True)
        self.old = self.root / "Old" / "SI1.pdf"
        self.old_meta = self.root / "Old" / "SI1.luna-meta.json"
        self.new = self.root / "New" / "Renamed.pdf"
        self.new_meta = self.root / "New" / "Renamed.luna-meta.json"
        self.old.write_bytes(b"%PDF-1.4\n% synthetic\n")
        self.old_meta.write_text(json.dumps({"schemaVersion": 1, "documentId": SI1,
                                             "manualTitle": "preserve"}))

    def paired_move(self):
        self.old.rename(self.new)
        self.old_meta.rename(self.new_meta)

    def reconcile(self, revision=None, confirmed=True):
        return reconcile_completed_move(self.root, SI1, "Old/SI1.pdf", "New/Renamed.pdf",
                                        self.initial.revision if revision is None else revision,
                                        user_confirmed=confirmed)

    def test_success_persists_two_si_after_restart_without_changing_pdf(self):
        self.paired_move()
        pdf_bytes, meta_bytes = self.new.read_bytes(), self.new_meta.read_bytes()
        result = self.reconcile()
        self.assertTrue(result.ok)
        self.assertEqual(result.code, "reconciled")
        reopened = si_state_store.read(self.root)
        self.assertEqual(result.revision, reopened.revision)
        self.assertEqual(reopened.index.documents[SI1].relative_path, "New/Renamed.pdf")
        self.assertEqual({d.id for d in reopened.index.linked_si(MAIN)}, {SI1, SI2})
        self.assertEqual((self.new.read_bytes(), self.new_meta.read_bytes()), (pdf_bytes, meta_bytes))

    def test_pdf_only_move_cannot_change_saved_associations(self):
        before = (self.root / si_state_store.STATE_NAME).read_bytes()
        self.old.rename(self.new)
        result = self.reconcile()
        self.assertFalse(result.ok)
        self.assertEqual(result.code, "sidecar_stranded")
        self.assertEqual((self.root / si_state_store.STATE_NAME).read_bytes(), before)

    def test_no_confirmation_does_not_change_state(self):
        self.paired_move()
        before = si_state_store.read(self.root).revision
        self.assertEqual(self.reconcile(confirmed=False).code, "confirmation_required")
        self.assertEqual(si_state_store.read(self.root).revision, before)

    def test_stale_pre_move_revision_rejected(self):
        self.paired_move()
        snapshot = si_state_store.read(self.root)
        snapshot.index.relocate(SI2, "Old/SI2.pdf", "Old/SI2-renamed.pdf")
        si_state_store.commit(self.root, snapshot.index, snapshot.revision, user_confirmed=True)
        self.assertEqual(self.reconcile().code, "stale_revision")
        self.assertEqual(si_state_store.read(self.root).index.documents[SI1].relative_path, "Old/SI1.pdf")

    def test_identity_mismatch_rejected(self):
        self.paired_move()
        self.new_meta.write_text(json.dumps({"documentId": SI2, "schemaVersion": 1}))
        self.assertEqual(self.reconcile().code, "identity_mismatch")
        self.assertEqual(si_state_store.read(self.root).index.documents[SI1].relative_path, "Old/SI1.pdf")

    def test_failed_atomic_commit_preserves_saved_state(self):
        self.paired_move()
        original = (self.root / si_state_store.STATE_NAME).read_bytes()
        with patch.object(si_state_store.os, "replace", side_effect=OSError("synthetic")):
            self.assertEqual(self.reconcile().code, "state_write_failed")
        self.assertEqual((self.root / si_state_store.STATE_NAME).read_bytes(), original)
        self.assertFalse((self.root / si_state_store.LOCK_NAME).exists())

    def test_collision_is_not_committed(self):
        self.old.rename(self.root / "Old" / "SI2.pdf")
        self.old_meta.rename(self.root / "Old" / "SI2.luna-meta.json")
        result = reconcile_completed_move(self.root, SI1, "Old/SI1.pdf", "Old/SI2.pdf",
                                          self.initial.revision, user_confirmed=True)
        self.assertEqual(result.code, "index_conflict")
        self.assertEqual(si_state_store.read(self.root).index.documents[SI1].relative_path, "Old/SI1.pdf")


if __name__ == "__main__":
    unittest.main()
