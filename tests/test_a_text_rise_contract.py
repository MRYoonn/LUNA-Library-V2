"""Synthetic PDF text-rise regression: no private PDF or production changes.

Run: python -m unittest tests.test_a_text_rise_contract -v
This checks fixture construction only, NOT native Ctrl+C accuracy.
"""
import unittest
from tools.smoke_windows import synthetic_pdf


class TextRiseContract(unittest.TestCase):
    def test_second_page_has_both_rise_directions(self):
        pdf = synthetic_pdf(2)
        self.assertIn(b"6 Ts (3) Tj", pdf)
        self.assertIn(b"-4 Ts (3) Tj", pdf)
        self.assertGreaterEqual(pdf.count(b"0 Ts"), 2)

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
