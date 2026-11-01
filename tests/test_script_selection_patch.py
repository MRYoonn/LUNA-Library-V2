"""Fail-closed contract for PDF4QT superscript/subscript selection correction."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "patch_script_text_layout.py"

spec = importlib.util.spec_from_file_location("luna_script_patch", SCRIPT)
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
from tools import patch_script_text_layout as workflow_patch
from tools.apply_performance import verify_pinned_text_layout


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

    def test_patched_source_keeps_native_fallback_and_copy_order(self):
        # Minimal upstream-format fixture; validates both LF and Windows CRLF
        # while leaving the actual pinned Git SHA check enabled in production.
        original = (
            patch.INTRO + "\n" +
            patch.BEFORE_LINE + "\n" +
            patch.BEFORE_SPACE + "\n"
        ).encode("utf-8")
        pinned = patch.PINNED_BLOB_SHA1
        try:
            blob = b"blob " + str(len(original)).encode() + b"\x00" + original
            patch.PINNED_BLOB_SHA1 = hashlib.sha1(blob).hexdigest()
            result = patch.patch_bytes(original).decode()
            self.assertIn(patch.MARKER, result)
            self.assertIn("const bool nativeSameLine", result)
            self.assertIn("nativeSameLine || isLunaInlineScript", result)
            self.assertIn("!isLunaInlineScript(previousCharacter, currentCharacter)", result)
            self.assertNotIn(patch.BEFORE_LINE, result)
            self.assertNotIn(patch.BEFORE_SPACE, result)
            with self.assertRaisesRegex(ValueError, "blob SHA mismatch"):
                patch.patch_bytes(result.encode())
            crlf = original.replace(b"\n", b"\r\n")
            self.assertIn(b"\r\n", patch.patch_bytes(crlf))
        finally:
            patch.PINNED_BLOB_SHA1 = pinned

    def test_native_pdf_fixture_really_has_raised_and_lowered_glyphs(self):
        fixture = (ROOT / "tools/smoke_windows.py").read_text(encoding="utf-8")
        native = (ROOT / "src/lunaeditorsmoke.cpp").read_text(encoding="utf-8")
        self.assertIn("/F1 10 Tf 6 Ts (3) Tj", fixture)
        self.assertIn("/F1 10 Tf -4 Ts (3) Tj", fixture)
        for required in (
            "Reader phase: inline superscript/subscript native selection and copy order",
            'glyphs.contains(QStringLiteral("C(sp3)-H"))',
            'glyphs.contains(QStringLiteral("CF3-H"))',
            'copied.contains(QStringLiteral("C(sp3)-H"))',
            'copied.contains(QStringLiteral("CF3-H"))',
        ):
            self.assertIn(required, native, required)

    def test_performance_overlay_reverses_only_known_script_patch(self):
        # Build #194 failed because the performance gate required the
        # pristine blob AFTER the independently verified script patch ran.
        original = (
            patch.INTRO + "\n" + patch.BEFORE_LINE + "\n" +
            patch.BEFORE_SPACE + "\n"
        ).encode("utf-8")
        digest = hashlib.sha1(
            b"blob " + str(len(original)).encode() + b"\x00" + original
        ).hexdigest()
        first = patch.PINNED_BLOB_SHA1
        second = workflow_patch.PINNED_BLOB_SHA1
        try:
            patch.PINNED_BLOB_SHA1 = digest
            workflow_patch.PINNED_BLOB_SHA1 = digest
            patched = patch.patch_bytes(original)
            self.assertEqual(verify_pinned_text_layout(original), original.decode("utf-8"))
            self.assertEqual(verify_pinned_text_layout(patched), patched.decode("utf-8"))
            damaged = patched.replace(b"ratio < 1.20", b"ratio < 1.01")
            with self.assertRaisesRegex(ValueError, "Unexpected script-patched|reverse to pinned upstream"):
                verify_pinned_text_layout(damaged)
            with self.assertRaisesRegex(ValueError, "Unrecognized pinned"):
                verify_pinned_text_layout(b"unapproved upstream")
        finally:
            patch.PINNED_BLOB_SHA1 = first
            workflow_patch.PINNED_BLOB_SHA1 = second

    def test_direct_script_import_mode_matches_windows_workflow(self):
        # GitHub Actions invokes 'python tools/apply_performance.py ...':
        # Python places tools/ (not the repository root) on sys.path.
        # The package-mode unit tests alone cannot catch this regression.
        probe = (
            "import sys\n"
            "sys.path.insert(0, 'tools')\n"
            "from apply_performance import verify_pinned_text_layout\n"
            "try:\n"
            "    verify_pinned_text_layout(b'not pinned')\n"
            "except ValueError as error:\n"
            "    assert 'Unrecognized pinned' in str(error), str(error)\n"
            "else:\n"
            "    raise AssertionError('Unpinned text unexpectedly accepted')\n"
        )
        result = subprocess.run(
            [sys.executable, "-c", probe], cwd=ROOT, capture_output=True,
            text=True, timeout=10, check=False
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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
