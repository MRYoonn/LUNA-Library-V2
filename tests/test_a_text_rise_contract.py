"""Synthetic PDF text-rise regression: no private PDF or production changes.

Run: python -m unittest tests.test_a_text_rise_contract -v
This checks fixture construction only, NOT native Ctrl+C accuracy.
"""
import math
import re
import unittest
from tools.smoke_windows import synthetic_pdf


class TextRiseContract(unittest.TestCase):
    def test_second_page_has_both_rise_directions(self):
        pdf = synthetic_pdf(2)
        self.assertIn(b"6 Ts (3) Tj", pdf)
        self.assertIn(b"-4 Ts (3) Tj", pdf)
        self.assertGreaterEqual(pdf.count(b"0 Ts"), 2)

    def test_each_rise_is_reset_before_normal_text(self):
        pdf = synthetic_pdf(2)
        # Validate operator order, not just the presence of isolated tokens.
        self.assertRegex(
            pdf,
            rb"6 Ts \(3\) Tj\s+0 Ts\s+/F1 16 Tf",
        )
        self.assertRegex(
            pdf,
            rb"-4 Ts \(3\) Tj\s+0 Ts\s+/F1 16 Tf",
        )

    def test_rise_operators_only_on_second_page_stream(self):
        pdf = synthetic_pdf(3)
        # The second page stream ends before the third page object begins.
        marker = b"(Third synthetic page)"
        self.assertEqual(pdf.count(marker), 1)
        before, after = pdf.split(marker, 1)
        self.assertEqual(before.count(b"6 Ts (3) Tj"), 1)
        self.assertEqual(before.count(b"-4 Ts (3) Tj"), 1)
        self.assertNotIn(b"6 Ts (3) Tj", after)
        self.assertNotIn(b"-4 Ts (3) Tj", after)

    def test_page_content_references_isolate_rise_operators(self):
        # Resolve /Contents references instead of relying on textual page markers.
        # This catches accidental relocation of rise commands to another page.
        pdf = synthetic_pdf(3)
        objects = {
            int(number): body
            for number, body in re.findall(
                rb"(\d+) 0 obj\s*(.*?)\s*endobj", pdf, re.DOTALL
            )
        }
        pages = [
            body for _, body in sorted(objects.items())
            if re.search(rb"/Type\s*/Page(?!s)\b", body)
        ]
        self.assertEqual(len(pages), 3)
        for index, page in enumerate(pages):
            match = re.search(rb"/Contents\s+(\d+)\s+0\s+R", page)
            self.assertIsNotNone(match)
            stream = objects[int(match.group(1))]
            self.assertEqual(stream.count(b"6 Ts (3) Tj"), int(index == 1))
            self.assertEqual(stream.count(b"-4 Ts (3) Tj"), int(index == 1))
            self.assertEqual(stream.count(b"0 Ts"), 2 if index == 1 else 0)


    def test_pymupdf_extraction_preserves_page_and_baseline_text(self):
        # Extraction oracle only: this does not prove native Reader drag/Ctrl+C.
        import fitz
        with fitz.open(stream=synthetic_pdf(3), filetype="pdf") as doc:
            self.assertEqual(len(doc), 3)
            texts = [page.get_text(sort=True) for page in doc]
        self.assertIn("LUNA synthetic smoke test", texts[0])
        self.assertIn("Second synthetic page", texts[1])
        self.assertIn("Third synthetic page", texts[2])
        self.assertIn("3", texts[1])
        self.assertNotIn("Second synthetic page", texts[0] + texts[2])
        self.assertNotIn("Third synthetic page", texts[0] + texts[1])

    def test_pymupdf_superscript_and_subscript_have_opposite_vertical_offsets(self):
        # Geometry oracle: verifies the generated fixture has measurable rise,
        # rather than merely containing PDF text-state operators.
        import fitz
        with fitz.open(stream=synthetic_pdf(2), filetype="pdf") as doc:
            blocks = doc[1].get_text("rawdict")["blocks"]
        lines = [
            line for block in blocks if "lines" in block
            for line in block["lines"]
        ]
        small_threes = []
        for line in lines:
            chars = [
                (char["c"], char["bbox"], span["size"])
                for span in line["spans"] for char in span["chars"]
            ]
            threes = [bbox for char, bbox, size in chars
                      if char == "3" and abs(size - 10) < 0.5]
            normals = [bbox for char, bbox, size in chars
                       if char.strip() and abs(size - 16) < 0.5]
            if threes and normals:
                self.assertEqual(len(threes), 1)
                small_y = (threes[0][1] + threes[0][3]) / 2
                normal_y = sum((b[1] + b[3]) / 2 for b in normals) / len(normals)
                small_threes.append((small_y - normal_y, threes[0][1]))
        self.assertEqual(len(small_threes), 2)
        # Top-to-bottom: the superscript is above the baseline, subscript below.
        small_threes.sort(key=lambda pair: pair[1])
        self.assertLess(small_threes[0][0], 0)
        self.assertGreater(small_threes[1][0], 0)

    def test_rise_glyphs_have_distinct_character_origins(self):
        # Check extraction coordinates, not only stream bytes. This remains
        # a synthetic PyMuPDF oracle, not the native Reader selection path.
        import fitz
        with fitz.open(stream=synthetic_pdf(3), filetype="pdf") as doc:
            page = doc[1]
            glyphs = [
                (char["c"], tuple(char["origin"]), span["size"])
                for block in page.get_text("rawdict")["blocks"]
                if "lines" in block
                for line in block["lines"]
                for span in line["spans"]
                for char in span["chars"]
            ]
            threes = [(origin, size) for char, origin, size in glyphs
                      if char == "3" and abs(size - 10) < 0.5]
            self.assertEqual(len(threes), 2)
            # Both text runs use different baselines; preserve both origins.
            self.assertNotEqual(threes[0][0][1], threes[1][0][1])
            for origin, size in threes:
                self.assertTrue(all(math.isfinite(v) for v in origin))
                self.assertAlmostEqual(size, 10, delta=0.5)
            for other in (doc[0], doc[2]):
                self.assertNotIn("3", other.get_text())

    def test_rise_character_bbox_is_finite_and_page_local(self):
        # Detect non-finite or out-of-page glyph coordinates before a future
        # selection bridge consumes them. Synthetic extraction oracle only.
        import fitz
        with fitz.open(stream=synthetic_pdf(3), filetype="pdf") as doc:
            page = doc[1]
            rect = page.rect
            small = [
                char
                for block in page.get_text("rawdict")["blocks"]
                if "lines" in block
                for line in block["lines"]
                for span in line["spans"]
                if abs(span["size"] - 10) < 0.5
                for char in span["chars"]
                if char["c"] == "3"
            ]
            self.assertEqual(len(small), 2)
            for char in small:
                bbox = fitz.Rect(char["bbox"])
                self.assertTrue(all(math.isfinite(v) for v in char["bbox"]))
                self.assertFalse(bbox.is_empty)
                self.assertTrue(rect.contains(bbox))
            self.assertNotEqual(tuple(small[0]["bbox"]), tuple(small[1]["bbox"]))

    def test_rise_page_copy_oracle_excludes_neighbor_pages(self):
        # Page-local clipboard baseline only; native drag selection is not covered.
        import fitz
        with fitz.open(stream=synthetic_pdf(3), filetype="pdf") as doc:
            copied = doc[1].get_text(sort=True)
            self.assertIn("Second synthetic page", copied)
            self.assertIn("CF", copied)
            self.assertIn("3", copied)
            self.assertNotIn("LUNA synthetic smoke test", copied)
            self.assertNotIn("Third synthetic page", copied)
            self.assertEqual(sum("Second synthetic page" in p.get_text()
                                 for p in doc), 1)

    def test_region_copy_excludes_text_outside_selected_rectangle(self):
        # A rectangular extraction oracle for region selection. This is not
        # native LUNA drag selection or clipboard validation.
        import fitz
        with fitz.open(stream=synthetic_pdf(3), filetype="pdf") as doc:
            page = doc[1]
            matches = page.search_for("Second synthetic page")
            self.assertEqual(len(matches), 1)
            selected = page.get_textbox(matches[0] + (-1, -1, 1, 1))
            self.assertIn("Second synthetic page", selected)
            self.assertNotIn("CF", selected)
            self.assertNotIn("3", selected)
            self.assertNotIn("LUNA synthetic smoke test", selected)
            self.assertNotIn("Third synthetic page", selected)

    def test_same_selection_rectangle_does_not_leak_across_pages(self):
        # Identical page-local rectangles must never return adjacent page text.
        # PyMuPDF oracle only; native LUNA drag/Ctrl+C remains unverified.
        import fitz
        with fitz.open(stream=synthetic_pdf(3), filetype="pdf") as doc:
            titles = (
                "LUNA synthetic smoke test",
                "Second synthetic page",
                "Third synthetic page",
            )
            for index, expected in enumerate(titles):
                matches = doc[index].search_for(expected)
                self.assertEqual(len(matches), 1)
                selected = doc[index].get_textbox(matches[0] + (-1, -1, 1, 1))
                self.assertIn(expected, selected)
                for other in titles:
                    if other != expected:
                        self.assertNotIn(other, selected)

    def test_two_column_region_order_oracle(self):
        # Synthetic two-column reading-order baseline, not native LUNA selection.
        # Extract each column independently to prevent row-wise interleaving.
        import fitz
        with fitz.open() as doc:
            page = doc.new_page(width=420, height=300)
            for x, y, value in (
                (30, 55, "LEFT FIRST"),
                (30, 90, "LEFT SECOND"),
                (230, 55, "RIGHT FIRST"),
                (230, 90, "RIGHT SECOND"),
            ):
                page.insert_text((x, y), value, fontsize=12)
            left = page.get_text("text", clip=fitz.Rect(20, 20, 205, 250), sort=True)
            right = page.get_text("text", clip=fitz.Rect(215, 20, 410, 250), sort=True)
        self.assertEqual(left.splitlines(), ["LEFT FIRST", "LEFT SECOND"])
        self.assertEqual(right.splitlines(), ["RIGHT FIRST", "RIGHT SECOND"])
        self.assertEqual(
            (left + right).splitlines(),
            ["LEFT FIRST", "LEFT SECOND", "RIGHT FIRST", "RIGHT SECOND"],
        )

    def test_single_page_does_not_contain_rise_fixture(self):
        pdf = synthetic_pdf(1)
        self.assertNotIn(b"6 Ts (3) Tj", pdf)
        self.assertNotIn(b"-4 Ts (3) Tj", pdf)

    def test_third_page_retains_second_page_rise(self):
        pdf = synthetic_pdf(3)
        self.assertIn(b"6 Ts (3) Tj", pdf)
        self.assertIn(b"-4 Ts (3) Tj", pdf)


if __name__ == "__main__":
    unittest.main()
