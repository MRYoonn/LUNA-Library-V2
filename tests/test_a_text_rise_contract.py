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
