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



def stroke_area_pixels(axis, center, span, width, clip, zoom):
    """Pixel cells with positive geometric overlap (not anti-aliased color).

    Unlike pixel-centre sampling, this detects subpixel strokes and is a
    conservative visibility oracle for the native Blend2D screenshot gate.
    """
    import math
    if width <= 0 or zoom <= 0:
        return set()
    cx0, cy0, cx1, cy1 = (v * zoom for v in clip)
    lo, hi = (v * zoom for v in span)
    mid, half = center * zoom, width * zoom / 2
    if axis == "vertical":
        sx0, sy0, sx1, sy1 = mid - half, lo, mid + half, hi
    elif axis == "horizontal":
        sx0, sy0, sx1, sy1 = lo, mid - half, hi, mid + half
    else:
        raise ValueError("Unknown stroke axis")
    x0, y0 = max(cx0, sx0), max(cy0, sy0)
    x1, y1 = min(cx1, sx1), min(cy1, sy1)
    if x1 <= x0 or y1 <= y0:
        return set()
    return {(x, y) for y in range(math.floor(y0), math.ceil(y1))
            for x in range(math.floor(x0), math.ceil(x1))
            if min(x + 1, x1) > max(x, x0)
            and min(y + 1, y1) > max(y, y0)}


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


    def test_subpixel_stroke_coverage_survives_at_all_zooms(self):
        # At 100% a 0.12pt stroke can miss every pixel centre despite
        # positive area coverage; pixel-centre-only gates give false negatives.
        clip = (10, 10, 30, 30)
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    area = stroke_area_pixels(axis, 15.2, (12, 28), 0.12, clip, zoom)
                    self.assertTrue(area)
                    self.assertTrue(all(10 * zoom < x + 1 and x < 30 * zoom
                                        and 10 * zoom < y + 1 and y < 30 * zoom
                                        for x, y in area))

    def test_subpixel_stroke_clip_boundary_and_outside(self):
        clip = (10, 10, 30, 30)
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    self.assertTrue(stroke_area_pixels(axis, 9.99, (12, 28),
                                                       0.04, clip, zoom))
                    self.assertFalse(stroke_area_pixels(axis, 9.9, (12, 28),
                                                        0.04, clip, zoom))
                    self.assertFalse(stroke_area_pixels(axis, 10, (12, 28),
                                                        0, clip, zoom))

    def test_fractional_clip_only_counts_positive_area(self):
        # A clip touching the stroke boundary at one line has zero area.
        # Fractional pixel cells count only when intersection area is positive.
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    if axis == "vertical":
                        touching = (10.02, 10, 20, 20)
                        overlapping = (10.019, 10, 20, 20)
                    else:
                        touching = (10, 10.02, 20, 20)
                        overlapping = (10, 10.019, 20, 20)
                    self.assertFalse(stroke_area_pixels(
                        axis, 10, (12, 18), 0.04, touching, zoom))
                    self.assertTrue(stroke_area_pixels(
                        axis, 10, (12, 18), 0.04, overlapping, zoom))

    def test_clip_shrinking_never_adds_coverage_pixels(self):
        # A narrower clip cannot reveal a new painted pixel.
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    outer = stroke_area_pixels(
                        axis, 15.2, (8, 32), 0.12, (10, 10, 30, 30), zoom)
                    inner = stroke_area_pixels(
                        axis, 15.2, (8, 32), 0.12, (12, 12, 28, 28), zoom)
                    self.assertTrue(inner)
                    self.assertTrue(inner.issubset(outer))

    def test_subpixel_coverage_not_lost_to_pixel_centre_sampling(self):
        # Notability-like thin grid: a stroke can occupy a fractional pixel
        # without covering any pixel centre. This is an oracle, not Blend2D.
        clip = (10, 10, 30, 30)
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    # Choose a device-space position at an integer boundary,
                    # halfway between neighbouring pixel centres.
                    center = round(15 * zoom) / zoom
                    width = 0.10 / zoom
                    area = stroke_area_pixels(axis, center, (12, 18), width,
                                              clip, zoom)
                    sampled = stroke_pixels(axis, center, (12, 18), width,
                                            clip, zoom)
                    self.assertTrue(area, "Positive subpixel coverage expected")
                    self.assertFalse(sampled, "Pixel-centre gate must miss it")

    def test_integer_device_translation_preserves_subpixel_coverage(self):
        # Moving the same geometry by whole device pixels must translate
        # its coverage mask, not change the count or lose thin grid lines.
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    original = stroke_area_pixels(
                        axis, 15.2, (12, 28), 0.08,
                        (10, 10, 30, 30), zoom)
                    dx, dy = 3, 5
                    translated = stroke_area_pixels(
                        axis, 15.2 + (dx if axis == "vertical" else dy) / zoom,
                        tuple(v + (dy if axis == "vertical" else dx) / zoom
                              for v in (12, 28)),
                        0.08,
                        (10 + dx / zoom, 10 + dy / zoom,
                         30 + dx / zoom, 30 + dy / zoom),
                        zoom)
                    self.assertTrue(original)
                    self.assertEqual({(x + dx, y + dy) for x, y in original},
                                     translated)

    def test_fractional_clip_inside_one_device_pixel(self):
        # A fractional clip smaller than one device pixel must preserve the
        # one genuinely covered pixel, without bleeding into neighbours.
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    clip = tuple(v / zoom for v in (10, 10, 10.2, 10.2))
                    pixels = stroke_area_pixels(
                        axis, 10.1 / zoom, (9 / zoom, 11 / zoom),
                        0.1 / zoom, clip, zoom)
                    self.assertEqual(pixels, {(10, 10)})
                    # Zero-area clip must never emit a pixel.
                    empty = (10 / zoom, 10 / zoom,
                             10 / zoom, 10.2 / zoom)
                    self.assertEqual(stroke_area_pixels(
                        axis, 10.1 / zoom, (9 / zoom, 11 / zoom),
                        0.1 / zoom, empty, zoom), set())

    def test_subpixel_clip_partition_conserves_covered_pixels(self):
        # Partition a clip at a device-pixel boundary. Each partition must
        # contribute only pixels also covered by the original clip; their
        # union must equal the unsplit result (no seam loss or bleed).
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    clip = (10.03, 10.07, 30.11, 30.19)
                    split = round(20 * zoom) / zoom
                    if axis == "vertical":
                        first = (clip[0], clip[1], clip[2], split)
                        second = (clip[0], split, clip[2], clip[3])
                    else:
                        first = (clip[0], clip[1], split, clip[3])
                        second = (split, clip[1], clip[2], clip[3])
                    args = (axis, 15.2, (8, 32), 0.08)
                    full = stroke_area_pixels(*args, clip, zoom)
                    left = stroke_area_pixels(*args, first, zoom)
                    right = stroke_area_pixels(*args, second, zoom)
                    self.assertTrue(full)
                    self.assertEqual(left | right, full)
                    self.assertTrue(left <= full and right <= full)

    def test_fractional_partition_inside_pixel_conserves_coverage(self):
        # A split inside one device pixel must not drop a subpixel grid line.
        # Both pieces can cover the same pixel; union, not sum, is invariant.
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    clip = tuple(v / zoom for v in (10, 10, 12, 12))
                    split = 10.37 / zoom
                    if axis == "vertical":
                        a = (clip[0], clip[1], split, clip[3])
                        b = (split, clip[1], clip[2], clip[3])
                    else:
                        a = (clip[0], clip[1], clip[2], split)
                        b = (clip[0], split, clip[2], clip[3])
                    args = (axis, 10.37 / zoom,
                            (9 / zoom, 13 / zoom), 0.08 / zoom)
                    whole = stroke_area_pixels(*args, clip, zoom)
                    first = stroke_area_pixels(*args, a, zoom)
                    second = stroke_area_pixels(*args, b, zoom)
                    self.assertTrue(whole)
                    self.assertEqual(first | second, whole)
                    self.assertTrue(first <= whole and second <= whole)

    def test_disjoint_fractional_clip_has_no_false_positive(self):
        # Fractional clips can overlap a pixel cell without overlapping
        # the actual painted stroke. Their pixel sets must stay empty.
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    # Both rectangles sit inside device pixel (10, 10).
                    # The stroke occupies [10.10, 10.20] on its thin axis,
                    # while the clip occupies [10.30, 10.40].
                    center = 10.15 / zoom
                    width = 0.10 / zoom
                    if axis == "vertical":
                        clip = tuple(v / zoom for v in
                                     (10.30, 10.00, 10.40, 11.00))
                    else:
                        clip = tuple(v / zoom for v in
                                     (10.00, 10.30, 11.00, 10.40))
                    self.assertEqual(
                        stroke_area_pixels(axis, center,
                                           (10 / zoom, 11 / zoom),
                                           width, clip, zoom), set())

    def test_pinned_patch_uses_coverage_without_clip_bypass(self):
        from pathlib import Path
        source = (Path(__file__).resolve().parents[1] / "tools" /
                  "patch_blend_clip_strokes.py").read_text(encoding="utf-8")
        self.assertIn("visibilityStroker.createStroke(path)", source)
        self.assertIn("resolveClipping(strokeCoverage)", source)
        self.assertIn("clipMode == ClipMode::NotVisible", source)
        self.assertNotIn("clipMode = ClipMode::Visible;", source)


    def test_subpixel_stroke_width_monotonic_at_all_zooms(self):
        # Enlarging a stroke around the same centre cannot erase a pixel
        # with positive area coverage. This checks geometry only, not Blend2D.
        clip = (10, 10, 30, 30)
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    thin = stroke_area_pixels(axis, 15.2, (12, 28), 0.04, clip, zoom)
                    thick = stroke_area_pixels(axis, 15.2, (12, 28), 0.12, clip, zoom)
                    self.assertTrue(thin)
                    self.assertTrue(thin.issubset(thick))

    def test_subpixel_clip_monotonic_near_edge_at_all_zooms(self):
        # Expanding the clip cannot remove a positive-coverage pixel,
        # including when the stroke centreline lies outside the clip.
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    if axis == "vertical":
                        narrow = (10.005, 10, 20, 20)
                        wide = (9.995, 10, 20, 20)
                    else:
                        narrow = (10, 10.005, 20, 20)
                        wide = (10, 9.995, 20, 20)
                    a = stroke_area_pixels(axis, 10, (12, 18), 0.04, narrow, zoom)
                    b = stroke_area_pixels(axis, 10, (12, 18), 0.04, wide, zoom)
                    self.assertTrue(a)
                    self.assertTrue(a.issubset(b))


    def test_fractional_positive_overlap_is_one_pixel_not_neighbor(self):
        # The painted stroke and fractional clip intersect inside one device
        # pixel only. Adjacent pixels must not leak across the clip boundary.
        for zoom in (1.0, 1.5, 2.0):
            for axis in ("vertical", "horizontal"):
                with self.subTest(zoom=zoom, axis=axis):
                    clip = tuple(v / zoom for v in
                                 (10.25, 10.25, 10.75, 10.75))
                    pixels = stroke_area_pixels(
                        axis, 10.5 / zoom,
                        (10.0 / zoom, 11.0 / zoom),
                        0.08 / zoom, clip, zoom)
                    self.assertEqual(pixels, {(10, 10)})


if __name__ == "__main__":
    unittest.main()
