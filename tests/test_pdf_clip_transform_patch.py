"""Regression contracts for PDF4QT device-space clipping replay and grid QA."""
import unittest

from tools.patch_pdf_clip_transform import (
    BEFORE, AFTER, MARKER, PREREQUISITE_MARKER,
    INITIAL_CTM_BEFORE, INITIAL_CTM_AFTER, INITIAL_CTM_MARKER, patch_text,
)
from tools.probe_synthetic_grid import build, long_synthetic_gray_profile

class PDFClipRendererContracts(unittest.TestCase):
    def test_original_upstream_painter_anchor_replaced_once(self):
        before = "// " + PREREQUISITE_MARKER + "\n" + INITIAL_CTM_BEFORE + BEFORE
        changed = patch_text(before)
        self.assertEqual(changed.count(MARKER), 1)
        self.assertEqual(changed.count(INITIAL_CTM_MARKER), 1)
        self.assertIn('painter->setWorldTransform(pagePointToDevicePointMatrix)', changed)
        self.assertNotIn(BEFORE, changed)
        self.assertIn("originalWorld.map(m_clips[instruction.dataIndex].clipPath)", changed)
        self.assertIn("painter->setClipPath(clipInDeviceSpace, Qt::IntersectClip)", changed)
        self.assertIn("painter->setWorldTransform(originalWorld)", changed)

    def test_patched_source_requires_previous_pinned_stroke_patch(self):
        with self.assertRaisesRegex(ValueError, "v301"):
            patch_text(BEFORE)
        with self.assertRaisesRegex(ValueError, "already applied"):
            patch_text("// " + PREREQUISITE_MARKER + "\n" + INITIAL_CTM_AFTER + AFTER)
        with self.assertRaisesRegex(ValueError, "anchor"):
            patch_text("// " + PREREQUISITE_MARKER + "\n" + INITIAL_CTM_BEFORE + BEFORE + BEFORE)

    def test_clip_uses_same_device_space_as_crop_and_restores_ctm(self):
        for snippet in (
            "const QTransform originalWorld = painter->worldTransform()",
            "originalWorld.map(m_clips[instruction.dataIndex].clipPath)",
            "painter->setWorldTransform(QTransform())",
            "painter->setClipPath(clipInDeviceSpace, Qt::IntersectClip)",
            "painter->setWorldTransform(originalWorld)",
        ):
            self.assertIn(snippet, AFTER)
        self.assertNotIn("setPen(", AFTER)
        self.assertNotIn("drawPath(", AFTER)

    def test_initial_page_ctm_precedes_first_clip_and_not_cropbox(self):
        self.assertIn("painter->setWorldTransform(pagePointToDevicePointMatrix)", INITIAL_CTM_AFTER)
        self.assertNotIn("painter->setClipPath(", INITIAL_CTM_AFTER)
        changed = patch_text("// " + PREREQUISITE_MARKER + "\n" + INITIAL_CTM_BEFORE + BEFORE)
        self.assertLess(changed.index(INITIAL_CTM_MARKER), changed.index(MARKER))

    def test_only_synthetic_user_data_free_icc_and_clip_fixture(self):
        plain = build()
        clipped = build(clip_only=True)
        transformed = build(ctm_only=True)
        both = build(nested_clip=True, long_icc=True)
        self.assertTrue(all(x.startswith(b"%PDF-1.4") for x in (plain, clipped, transformed, both)))
        self.assertIn(b" W n", clipped)
        self.assertNotIn(b" W n", plain)
        self.assertIn(b"1.028571", transformed)
        self.assertNotIn(b"1.028571", clipped)
        self.assertIn(b"1.028571", both)
        self.assertIn(b"/Cs1 CS 0.651 SC", both)
        self.assertIn(b"0.651 G", both)
        self.assertGreater(len(long_synthetic_gray_profile()), 2300)

    def test_windows_gui_requires_real_visible_grid_pixels(self):
        import pathlib
        script=(pathlib.Path(__file__).parents[1]/"tools/probe_vector_grid_windows.py").read_text()
        self.assertIn('LUNA_REQUIRE_GRID_VISIBILITY', script)
        self.assertIn('pixels["icc_gray_pixels"] < 400', script)
        self.assertIn('pixels["device_gray_pixels"] < 800', script)
        self.assertIn('short-icc-clip-only', script)
        self.assertIn('short-icc-ctm-only', script)
        self.assertIn('long-icc-nested-clip', script)

if __name__ == "__main__":
    unittest.main()
