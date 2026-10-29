"""Private-data blocking regression tests (synthetic data only)."""
import shutil
import tempfile
import unittest
from pathlib import Path
from tools.preflight_public import APPROVED, check
from tools.apply_overlay import PIN, BLOBS

ROOT = Path(__file__).resolve().parents[1]

class PublicGate(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for rel in APPROVED:
            target = self.root / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, target)
    def tearDown(self):
        self.temp.cleanup()
    def test_normal_sources(self):
        self.assertEqual(check(self.root), [])
    def test_modified_official_icon_blocked(self):
        with (self.root / "assets/icons/LUNA-Moon.ico").open("ab") as stream:
            stream.write(b"unexpected")
        self.assertTrue(any("Unapproved icon" in e for e in check(self.root)))
    def test_pdf_file_blocked(self):
        (self.root / "private.pdf").write_bytes(b"%PDF-1.7 synthetic test")
        self.assertTrue(any("Unexpected" in e for e in check(self.root)))
    def test_sqlite_blocked(self):
        (self.root / "metadata.sqlite").write_text("synthetic")
        self.assertTrue(check(self.root))
    def test_email_blocked(self):
        with (self.root / "README.md").open("a") as f:
            f.write("\n" + "sample" + "@" + "example" + "." + "org")
        self.assertTrue(any("email" in e for e in check(self.root)))
    def test_token_blocked(self):
        with (self.root / "README.md").open("a") as f:
            f.write("\n" + "ghp_" + "A" * 35)
        self.assertTrue(any("GitHub credential" in e for e in check(self.root)))
    def test_windows_profile_path_blocked(self):
        with (self.root / "README.md").open("a") as f:
            f.write("\n" + "C:" + "\\Users\\SomeName\\Documents")
        self.assertTrue(any("Windows user path" in e for e in check(self.root)))
    def test_unsafe_push_trigger_blocked(self):
        p = self.root / ".github/workflows/build-windows.yml"
        p.write_text(p.read_text().replace("workflow_dispatch:", "push:"))
        self.assertTrue(any("workflow" in e.lower() for e in check(self.root)))
    def test_secrets_reference_blocked(self):
        p = self.root / ".github/workflows/build-windows.yml"
        p.write_text(p.read_text() + "\n" + "$" + "{{ secrets.MY_KEY }}")
        self.assertTrue(any("secrets" in e.lower() for e in check(self.root)))
    def test_pdf4qt_commit_hash_pinned(self):
        self.assertEqual(len(PIN), 40)
        self.assertEqual(len(BLOBS), 2)
        self.assertTrue(all(len(v) == 40 for v in BLOBS.values()))

if __name__ == "__main__":
    unittest.main()
