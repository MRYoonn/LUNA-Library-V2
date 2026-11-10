"""Source contracts for screen-only PDF4QT thin-vector-line compatibility."""
import unittest

from tools.patch_thin_vector_strokes import (
    AFTER,
    BEFORE,
    INCLUDE_BEFORE,
    MARKER,
    PINNED_BLOB_SHA1,
    patch_text,
)


class ThinVectorStrokeContracts(unittest.TestCase):
    def test_only_original_line_stroke_painter_is_replaced(self):
        source = INCLUDE_BEFORE + "void demo() {\n" + BEFORE + "}\n"
        patched = patch_text(source)
        self.assertIn(MARKER, patched)
        self.assertNotIn(BEFORE, patched)
        self.assertEqual(patched.count("painter->drawPath(data.path);"), 1)
        self.assertIn("painter->setPen(screenPen);", patched)
        self.assertIn("painter->setBrush(data.brush);", patched)

    def test_screen_only_and_narrow_strokes(self):
        for snippet in (
            "!data.isText", "screenPen.style() != Qt::NoPen",
            "screenPen.widthF() > 0.0", "QInternal::Widget",
            "std::hypot(transform.m11(), transform.m12())",
            "std::hypot(transform.m21(), transform.m22())",
            "strokePixels > 0.0 && strokePixels < 1.0",
            "screenPen.setCosmetic(true)",
            "screenPen.setWidthF(1.0)",
        ):
            self.assertIn(snippet, AFTER, snippet)
        # Print/PDF devices never match QInternal::Widget.
        self.assertNotIn("QInternal::Printer", AFTER)
        self.assertNotIn("painter->setPen(Qt::black)", AFTER)

    def test_patch_fails_closed_if_not_pinned(self):
        source = INCLUDE_BEFORE + BEFORE
        with self.assertRaisesRegex(ValueError, "already present"):
            patch_text(patch_text(source))
        with self.assertRaisesRegex(ValueError, "anchors"):
            patch_text(INCLUDE_BEFORE + "painter->drawRect(rect);\n")
        with self.assertRaisesRegex(ValueError, "anchors"):
            patch_text(source + BEFORE)
        self.assertEqual(len(PINNED_BLOB_SHA1), 40)

    def test_typical_grid_line_is_subpixel_at_200_percent(self):
        # The reported thin vector grid is ~0.37 PDF pt. LUNA's
        # default 200% zoom can produce less than one screen pixel.
        self.assertLess(.3703 * 2.0, 1.0)
        self.assertGreater(.3703 * 3.0, 1.0)


if __name__ == "__main__":
    unittest.main()
