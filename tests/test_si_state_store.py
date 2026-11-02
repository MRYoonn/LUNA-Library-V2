"""Synthetic offline tests; never touch user PDFs, paths, or settings."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from uuid import uuid4

from tools.si_associations import AssociationIndex
from tools import si_state_store as store


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.main, self.si1, self.si2 = (str(uuid4()) for _ in range(3))

    def index(self):
        idx = AssociationIndex()
        idx.add_document(self.main, "main", "Papers/Main.pdf", "10.1021/jacs.4c05881")
        idx.add_document(self.si1, "si", "SI/Supporting1.pdf")
        idx.add_document(self.si2, "si", "SI/Supporting2.pdf")
        idx.confirm_link(self.main, self.si1, "10.1021/jacs.4c05881", user_confirmed=True)
        idx.confirm_link(self.main, self.si2, "10.1021/jacs.4c05881", user_confirmed=True)
        return idx

    def test_read_missing_has_no_side_effects(self):
        snap = store.read(self.root)
        self.assertEqual(snap.revision, "missing")
        self.assertEqual(list(self.root.iterdir()), [])

    def test_confirmation_required(self):
        with self.assertRaisesRegex(store.StoreError, "confirmation_required"):
            store.commit(self.root, self.index(), "missing")
        self.assertFalse((self.root / store.STATE_NAME).exists())

    def test_multiple_si_restart_and_move_roundtrip(self):
        initial = store.read(self.root)
        committed = store.commit(self.root, self.index(), initial.revision, user_confirmed=True)
        restored = store.read(self.root)
        self.assertEqual(committed.revision, restored.revision)
        self.assertEqual(len(restored.index.linked_si(self.main)), 2)
        restored.index.relocate(self.si1, "SI/Supporting1.pdf", "Moved/NewSI.pdf")
        store.commit(self.root, restored.index, restored.revision, user_confirmed=True)
        reopened = store.read(self.root)
        self.assertEqual(reopened.index.documents[self.si1].relative_path, "Moved/NewSI.pdf")
        self.assertEqual(len(reopened.index.linked_si(self.main)), 2)

    def test_stale_revision_cannot_overwrite(self):
        first = store.read(self.root)
        store.commit(self.root, self.index(), first.revision, user_confirmed=True)
        with self.assertRaisesRegex(store.StoreError, "stale_revision"):
            store.commit(self.root, AssociationIndex(), first.revision, user_confirmed=True)
        self.assertEqual(len(store.read(self.root).index.links), 2)

    def test_busy_lock_fail_closed(self):
        (self.root / store.LOCK_NAME).write_text("busy", encoding="utf-8")
        with self.assertRaisesRegex(store.StoreError, "store_busy"):
            store.commit(self.root, self.index(), "missing", user_confirmed=True)
        self.assertEqual((self.root / store.LOCK_NAME).read_text(), "busy")

    def test_corrupt_state_never_overwritten(self):
        target = self.root / store.STATE_NAME
        target.write_bytes(b"{broken")
        with self.assertRaisesRegex(store.StoreError, "invalid_state"):
            store.commit(self.root, self.index(), "missing", user_confirmed=True)
        self.assertEqual(target.read_bytes(), b"{broken")

    def test_future_schema_rejected(self):
        (self.root / store.STATE_NAME).write_text('{"version":2,"documents":[],"links":[]}')
        with self.assertRaisesRegex(store.StoreError, "invalid_state"):
            store.read(self.root)

    def test_symlink_state_rejected(self):
        external = self.root / "outside.txt"
        external.write_text("do not touch")
        try:
            (self.root / store.STATE_NAME).symlink_to(external)
        except (OSError, NotImplementedError):
            self.skipTest("symlink privilege unavailable")
        with self.assertRaisesRegex(store.StoreError, "unsafe_state_file"):
            store.read(self.root)
        self.assertEqual(external.read_text(), "do not touch")

    def test_write_failure_preserves_old_bytes(self):
        committed = store.commit(self.root, self.index(), "missing", user_confirmed=True)
        before = (self.root / store.STATE_NAME).read_bytes()
        with patch.object(store.os, "replace", side_effect=OSError("synthetic failure")):
            with self.assertRaisesRegex(store.StoreError, "state_write_failed"):
                store.commit(self.root, AssociationIndex(), committed.revision, user_confirmed=True)
        self.assertEqual((self.root / store.STATE_NAME).read_bytes(), before)
        self.assertFalse((self.root / store.LOCK_NAME).exists())
        self.assertFalse(list(self.root.glob("*.tmp")))

    def test_invalid_index_rejected_without_write(self):
        idx = self.index()
        idx.links[self.si1] = idx.links[self.si1].__class__(self.si2, self.si1, "10.1021/jacs.4c05881")
        with self.assertRaisesRegex(store.StoreError, "invalid_index"):
            store.commit(self.root, idx, "missing", user_confirmed=True)
        self.assertFalse((self.root / store.STATE_NAME).exists())

    def test_state_size_limit(self):
        target = self.root / store.STATE_NAME
        target.write_bytes(b"x" * (store.MAX_BYTES + 1))
        with self.assertRaisesRegex(store.StoreError, "unsafe_state_file"):
            store.read(self.root)


if __name__ == "__main__":
    unittest.main()
