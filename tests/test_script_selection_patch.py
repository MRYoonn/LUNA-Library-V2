"""Fail-closed contract for PDF4QT superscript/subscript selection correction."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "patch_script_text_layout.py"

spec = importlib.util.spec_from_file_location("luna_script_patch", SCRIPT)
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class ScriptSelectionPatchContracts(unittest.TestCase):
    def test_only_pinned_public_text_layout_is_eligible(self):
        self.assertEqual(patch.RELATIVE.as_posix(),
                         "Pdf4QtLibCore/sources/pdftextlayout.cpp")
        self.assertEqual(patch.PINNED_BLOB_SHA1,
                         "a50aeb3ca241f41bd54c4d9d49bd5682f1bed2b8")
        with self.assertRaisesRegex(ValueError, "blob SHA mismatch"):
            patch.patch_bytes(b"not the pinned upstream source")

    def test_exact_once_anchors_reject_ambiguous_upstream_changes(self):
        self.assertEqual(patch.replace_one("foo bar", "foo", "baz"), "baz bar")
        for source in ("bar", "foo foo"):
            with self.assertRaises(ValueError):
                patch.replace_one(source, "foo", "baz")

    def test_script_attachment_guards_nearby_columns_and_paragraphs(self):
        source = SCRIPT.read_text(encoding="utf-8")
        for required in (
            "LUNA_SCRIPT_SELECTION_V1",
            "ratio < 1.20 || ratio > 2.5",
            "smallBox.left() - baseBox.right()",
            "baseBox.left() - smallBox.right()",
            "gap > qMax<qreal>(1.5, base.fontSize * 0.24)",
            "baselineOffset < base.fontSize * 0.10",
            "baselineOffset > base.fontSize * 0.72",
            "isLunaInlineScript(characters[i], characters[info.index])",
            "isLunaInlineScript(previousCharacter, currentCharacter)",
        ):
            self.assertIn(required, source, required)
        self.assertIn("PINNED_BLOB_SHA1", source)
        self.assertNotIn("if True", source)

    def test_windows_build_applies_verified_patch_not_on_main_branch(self):
        workflow = (ROOT / ".github/workflows/build-windows.yml").read_text(encoding="utf-8")
        verified = "python tools/patch_script_text_layout.py pdf4qt --dry-run"
        applied = "python tools/patch_script_text_layout.py pdf4qt\n"
        self.assertIn(verified, workflow)
        self.assertIn(applied, workflow)
        self.assertLess(workflow.index(verified),
                        workflow.index("python tools/apply_overlay.py pdf4qt\n"))
        self.assertGreater(workflow.index(applied),
                           workflow.index("python tools/apply_overlay.py pdf4qt\n"))


if __name__ == "__main__":
    unittest.main()
