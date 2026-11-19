"""Synthetic pixel regression for clipped zero-area PDF stroke centerlines.

This is a pure-Python geometry oracle, NOT a PDF4QT/Blend2D screenshot test.
It guards the intended painted-coverage contract at 100/150/200% and must
be supplemented by the existing native Windows screenshot gates.
"""
import unittest


def stroke_pixels(axis, center, span, width, clip, zoom):
    """Rasterize pixel-centre coverage of a butt-cap axis-aligned stroke."""
    x0, y0, x1, y1 = (v * zoom for v in clip)
    pixels = set()
    for y in range(int(y0) - 2, int(y1) + 3):
        for x in range(int(x0) - 2, int(x1) + 3):
            px, py = x + 0.5, y + 0.5
            if not (x0 <= px < x1 and y0 <= py < y1):
                continue
            if axis == "vertical":
                hit = abs(px - center * zoom) <= width * zoom / 2 and span[0] * zoom <= py < span[1] * zoom
            else:
                hit = abs(py - center * zoom) <= width * zoom / 2 and span[0] * zoom <= px < span[1] * zoom
            if hit:
                pixels.add((x, y))
    return pixels


class ClippedStrokePixelContract(unittest.TestCase):
    def test_clipped_vertical_and_horizontal_at_three_zooms(self):
        clip = (10, 10, 30, 30)
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    pixels = stroke_pixels(axis, 15, (5, 35), 1, clip, zoom)
                    self.assertTrue(pixels, "Visible stroked outline must not be culled")
                    self.assertTrue(all(10 * zoom <= x + 0.5 < 30 * zoom and
                                        10 * zoom <= y + 0.5 < 30 * zoom
                                        for x, y in pixels), "Clip must never be bypassed")

    def test_centerline_outside_clip_but_stroke_overlaps(self):
        for zoom in (1.0, 1.5, 2.0):
            with self.subTest(zoom=zoom):
                pixels = stroke_pixels("vertical", 9.75, (12, 28), 1.5,
                                       (10, 10, 30, 30), zoom)
                self.assertTrue(pixels, "Coverage may intersect while centerline does not")

    def test_horizontal_centerline_outside_clip_but_coverage_inside(self):
        # Symmetric with the vertical case: the horizontal centreline is
        # outside the clip, while its finite stroke width still overlaps.
        clip = (10, 10, 30, 30)
        for zoom in (1.0, 1.5, 2.0):
            with self.subTest(zoom=zoom):
                pixels = stroke_pixels("horizontal", 9.75, (12, 28), 1.5,
                                       clip, zoom)
                self.assertTrue(pixels, "Horizontal stroke coverage must survive clipping")
                self.assertTrue(all(10 * zoom <= x + 0.5 < 30 * zoom and
                                    10 * zoom <= y + 0.5 < 30 * zoom
                                    for x, y in pixels))

    def test_both_axes_entirely_outside_clip(self):
        # A coverage-aware culling fix must not leak pixels from strokes
        # whose complete painted extent is beyond the clip boundary.
        clip = (10, 10, 30, 30)
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("horizontal", "vertical"):
                with self.subTest(zoom=zoom, axis=axis):
                    self.assertFalse(stroke_pixels(axis, 8, (12, 28), 1,
                                                   clip, zoom))

    def test_truly_invisible_stroke_stays_invisible(self):
        for zoom in (1.0, 1.5, 2.0):
            self.assertFalse(stroke_pixels("vertical", 5, (12, 28), 1,
                                           (10, 10, 30, 30), zoom))

    def test_pinned_patch_uses_coverage_without_clip_bypass(self):
        from pathlib import Path
        source = (Path(__file__).resolve().parents[1] / "tools" /
                  "patch_blend_clip_strokes.py").read_text(encoding="utf-8")
        self.assertIn("visibilityStroker.createStroke(path)", source)
        self.assertIn("resolveClipping(strokeCoverage)", source)
        self.assertIn("clipMode == ClipMode::NotVisible", source)
        self.assertNotIn("clipMode = ClipMode::Visible;", source)


if __name__ == "__main__":
    unittest.main()
