"""Synthetic PDF text-rise regression: no private PDF or production changes.

Run: python -m unittest tests.test_a_text_rise_contract -v
This checks fixture construction only, NOT native Ctrl+C accuracy.
"""
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
                self.assertTrue(all(__import__("math").isfinite(v) for v in origin))
                self.assertAlmostEqual(size, 10, delta=0.5)
            for other in (doc[0], doc[2]):
                self.assertNotIn("3", other.get_text())

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
