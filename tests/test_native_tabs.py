"""Source contract checks only; none claim Windows runtime acceptance."""
from pathlib import Path
import ast
import unittest
from tools.smoke_windows import synthetic_pdf

ROOT = Path(__file__).resolve().parents[1]

class NativeTabsContract(unittest.TestCase):
    def test_synthetic_unique_words_preserve_xref(self):
        original = synthetic_pdf(2)
        for word in (b'alpha', b'bravo'):
            altered = original.replace(b'smoke', word)
            self.assertEqual(len(original), len(altered))
            self.assertIn(word, altered)
            self.assertEqual(original[original.index(b'xref\n'):], altered[altered.index(b'xref\n'):])

    def test_scripts_parse(self):
        for path in ('prepare.py', 'run_windows.py'):
            ast.parse((ROOT / 'experiments/native_tabs' / path).read_text())

    def test_workflow_remains_manual_and_proof_not_released(self):
        text = (ROOT / '.github/workflows/build-windows.yml').read_text()
        self.assertIn('native_tabs_proof:', text)
        self.assertIn("if: ${{ !inputs.native_tabs_proof && steps.engine-cache.outputs.cache-hit != 'true' }}", text)
        self.assertIn('Upload LUNA Windows portable artifact\n        if: ${{ !inputs.native_tabs_proof }}', text)

if __name__ == '__main__':
    unittest.main()
