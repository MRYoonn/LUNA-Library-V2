"""Synthetic, offline contract for local-only SI association persistence."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from si_associations import AssociationIndex, relative_pdf_path

MAIN = "11111111-1111-4111-8111-111111111111"
SI1 = "22222222-2222-4222-8222-222222222222"
SI2 = "33333333-3333-4333-8333-333333333333"
OTHER = "44444444-4444-4444-8444-444444444444"
DOI = "10.1021/jacs.6c01234"  # synthetic, not an actual paper


def sample():
    i = AssociationIndex()
    i.add_document(MAIN, "main", "Chem/Main.pdf", DOI)
    i.add_document(SI1, "si", "Chem/SI1.pdf")
    i.add_document(SI2, "si", "Chem/SI2.pdf")
    return i


class AssociationTests(unittest.TestCase):
    def test_explicit_confirmation_and_two_si(self):
        i = sample()
        with self.assertRaisesRegex(ValueError, "confirmation_required"):
            i.confirm_link(MAIN, SI1, DOI)
        i.confirm_link(MAIN, SI1, DOI, user_confirmed=True)
        i.confirm_link(MAIN, SI2, DOI, user_confirmed=True)
        self.assertEqual({d.id for d in i.linked_si(MAIN)}, {SI1, SI2})

    def test_doi_mismatch_and_kind_rejected(self):
        i = sample()
        for args in ((MAIN, SI1, "10.1021/jacs.6c99999"), (SI1, MAIN, DOI)):
            with self.assertRaises(ValueError):
                i.confirm_link(*args, user_confirmed=True)
        self.assertEqual(i.links, {})

    def test_wrong_si_parent_cannot_be_overwritten(self):
        i = sample()
        i.add_document(OTHER, "main", "Chem/Other.pdf", DOI)
        i.confirm_link(MAIN, SI1, DOI, user_confirmed=True)
        with self.assertRaisesRegex(ValueError, "si_already_linked"):
            i.confirm_link(OTHER, SI1, DOI, user_confirmed=True)
        self.assertEqual(i.links[SI1].main_id, MAIN)

    def test_move_rename_and_restart_preserve_links(self):
        i = sample()
        i.confirm_link(MAIN, SI1, DOI, user_confirmed=True)
        i.confirm_link(MAIN, SI2, DOI, user_confirmed=True)
        i.relocate(MAIN, "Chem/Main.pdf", "Papers/Renamed.pdf")
        i.relocate(SI1, "Chem/SI1.pdf", "Papers/Supporting1.pdf")
        j = AssociationIndex.from_json(i.to_json())
        self.assertEqual(j.to_json(), i.to_json())
        self.assertEqual(j.documents[SI1].relative_path, "Papers/Supporting1.pdf")
        self.assertEqual(len(j.linked_si(MAIN)), 2)

    def test_stale_move_is_noop(self):
        i = sample()
        with self.assertRaisesRegex(ValueError, "stale_move"):
            i.relocate(SI1, "Old/Si.pdf", "New/Si.pdf")
        self.assertEqual(i.documents[SI1].relative_path, "Chem/SI1.pdf")

    def test_casefold_collision(self):
        i = sample()
        with self.assertRaisesRegex(ValueError, "path_collision"):
            i.relocate(SI1, "Chem/SI1.pdf", "CHEM/si2.PDF")
        with self.assertRaisesRegex(ValueError, "path_collision"):
            i.add_document(OTHER, "si", "CHEM/si1.PDF")

    def test_duplicate_id_never_rebinds(self):
        i = sample()
        i.add_document(SI1, "si", "Chem/SI1.pdf")
        with self.assertRaisesRegex(ValueError, "document_identity_conflict"):
            i.add_document(SI1, "si", "Chem/SI1-copy.pdf")

    def test_copy_gets_new_id_and_no_link(self):
        i = sample()
        i.confirm_link(MAIN, SI1, DOI, user_confirmed=True)
        i.add_document(OTHER, "si", "Chem/SI1-copy.pdf")
        self.assertNotIn(OTHER, i.links)

    def test_path_traversal_windows_aliases(self):
        for path in ("../secret.pdf", "/abs/a.pdf", "C:/abs/a.pdf", "Chem\\SI.pdf",
                     "Chem//SI.pdf", "Chem/./SI.pdf", "Chem/../SI.pdf", "CON.pdf",
                     "Chem/AUX.txt.pdf", "Chem/x.pdf:ads", "Chem/si.pdf.", "Chem/si.pdf ",
                     "Chem/si\n.pdf", "Chem/x.txt"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                relative_pdf_path(path)

    def test_orphan_link_rejected_on_load(self):
        i = sample()
        i.confirm_link(MAIN, SI1, DOI, user_confirmed=True)
        data = json.loads(i.to_json())
        data["documents"] = [d for d in data["documents"] if d["id"] != SI1]
        with self.assertRaises(ValueError):
            AssociationIndex.from_json(json.dumps(data))

    def test_unknown_schema_rejected(self):
        data = json.loads(sample().to_json())
        data["version"] = 2
        with self.assertRaisesRegex(ValueError, "unsupported_state"):
            AssociationIndex.from_json(json.dumps(data))

    def test_untrusted_import_cannot_forge_auto_link(self):
        i = sample()
        i.confirm_link(MAIN, SI1, DOI, user_confirmed=True)
        data = json.loads(i.to_json())
        data["links"][0]["source"] = "auto_filename_match"
        with self.assertRaisesRegex(ValueError, "invalid_link"):
            AssociationIndex.from_json(json.dumps(data))

    def test_duplicate_serialized_document_rejected(self):
        data = json.loads(sample().to_json())
        data["documents"].append(data["documents"][0])
        with self.assertRaisesRegex(ValueError, "duplicate_document"):
            AssociationIndex.from_json(json.dumps(data))

    def test_only_v4_ids_can_enter_or_be_imported(self):
        # Native SiIdentity::validId only accepts UUIDv4. A legacy/forged
        # UUIDv1/v5 must not create an unmovable linked document in the store.
        index = sample()
        before = index.to_json()
        for invalid in ("11111111-1111-1111-8111-111111111111",
                        "55555555-5555-5555-8555-555555555555",
                        "00000000-0000-0000-0000-000000000000"):
            with self.subTest(id=invalid):
                with self.assertRaisesRegex(ValueError, "invalid_document_id"):
                    index.add_document(invalid, "si", "Chem/Unverified.pdf")
                self.assertEqual(index.to_json(), before)
                forged = json.loads(before)
                forged["documents"][0]["id"] = invalid
                with self.assertRaisesRegex(ValueError, "invalid_document_id"):
                    AssociationIndex.from_json(json.dumps(forged))

    def test_windows_superscript_device_and_invisible_path_blocked(self):
        for unsafe in ("COM¹.pdf", "Chem/LPT².pdf", "Chem/COM³.txt.pdf",
                       "Chem/hidden\\u202e.pdf", "Chem/hidden\\u200b.pdf"):
            with self.subTest(path=unsafe), self.assertRaises(ValueError):
                relative_pdf_path(unsafe)
        self.assertEqual(relative_pdf_path("Chem/실험_α.pdf"), "Chem/실험_α.pdf")

    def test_boolean_schema_version_is_not_version_one(self):
        forged = json.loads(sample().to_json())
        forged["version"] = True  # Python True == 1, but JSON true is not v1.
        with self.assertRaisesRegex(ValueError, "unsupported_state"):
            AssociationIndex.from_json(json.dumps(forged))

    def test_empty_and_invalid_json(self):
        self.assertEqual(AssociationIndex.from_json(AssociationIndex().to_json()).links, {})
        with self.assertRaisesRegex(ValueError, "invalid_state"):
            AssociationIndex.from_json("{bad")


if __name__ == "__main__":
    unittest.main()
