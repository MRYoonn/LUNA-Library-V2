"""Offline update release gates: no real network, no user PDF, no installation."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from tools.prepare_release import prepare
from tools.verify_portable import REQUIRED_BINARIES, REQUIRED_NOTICES

ROOT = Path(__file__).resolve().parents[1]

def fake_pe():
    data = bytearray(90)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, 0x8664)
    struct.pack_into("<H", data, 88, 0x20B)
    return bytes(data)

class ReleaseUpdateTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.portable = self.root / "portable"
        self.portable.mkdir()
        for name in REQUIRED_BINARIES:
            path = self.portable / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(fake_pe())
        for name in REQUIRED_NOTICES:
            (self.portable / name).write_text("synthetic public notice", encoding="utf-8")
        license_dir = self.portable / "third_party_licenses"
        license_dir.mkdir()
        (license_dir / "mit.txt").write_text("MIT test")
        self.commit = "a" * 40
        self.run = 41012345678
        self.metadata = {
            "product": "LUNA Library", "run": str(self.run), "commit": self.commit,
            "exeSha256": hashlib.sha256(fake_pe()).hexdigest(),
            "validation": {"windowsCpp": "passed"}
        }
        (self.portable / "build-info.json").write_text(json.dumps(self.metadata), encoding="utf-8")

    def test_verified_release_zip_manifest_and_source_identity(self):
        dst = self.root / "dist"
        manifest = prepare(self.portable, dst, "v0.9.1.134", self.run, self.commit)
        zip_file = dst / "LUNA-Windows-Portable.zip"
        self.assertEqual(manifest["sha256"], hashlib.sha256(zip_file.read_bytes()).hexdigest())
        self.assertEqual(json.loads((dst / "LUNA-Release.json").read_text()), manifest)
        with zipfile.ZipFile(zip_file) as z:
            names = z.namelist()
            self.assertIn("LUNA.exe", names)
            self.assertIn("LUNA-Update.ps1", names)
            self.assertNotIn("test.pdf", names)
            self.assertEqual(len(names), len(set(names)))

    def test_wrong_tag_run_commit_or_exe_is_rejected(self):
        with self.assertRaises(ValueError):
            prepare(self.portable, self.root / "bad1", "v0.9.1-beta", self.run, self.commit)
        with self.assertRaises(ValueError):
            prepare(self.portable, self.root / "bad2", "v0.9.1.135", self.run + 1, self.commit)
        with self.assertRaises(ValueError):
            prepare(self.portable, self.root / "bad3", "v0.9.1.135", self.run, "b"*40)
        (self.portable / "LUNA.exe").write_bytes(fake_pe() + b"tampered")
        with self.assertRaises(ValueError):
            prepare(self.portable, self.root / "bad4", "v0.9.1.135", self.run, self.commit)

    def test_update_execution_requires_explicit_user_accept_and_closed_app(self):
        header = (ROOT / "src/lunaupdater.h").read_text(encoding="utf-8")
        script = (ROOT / "tools/LUNA-Update.ps1").read_text(encoding="utf-8")
        self.assertIn("QMessageBox::question", header)
        self.assertIn("QCoreApplication::aboutToQuit", header)
        self.assertIn('QStringLiteral("updater/lastCheckedUtc")', header)
        self.assertIn("$previous | Wait-Process -Timeout", script)
        self.assertIn("Get-Process -Id $WaitPid -ErrorAction SilentlyContinue", script)
        self.assertIn("Get-FileHash", script)
        self.assertIn("Archive traversal blocked", script)
        self.assertIn("rollback", script.lower())
        self.assertIn("user PDFs", script)
        self.assertNotIn("Remove-Item -LiteralPath $install", script)

    def test_release_gate_is_manual_and_draft_only(self):
        yml = (ROOT / ".github/workflows/draft-release.yml").read_text(encoding="utf-8")
        self.assertIn("workflow_dispatch:", yml)
        self.assertNotIn("\n  push:", yml)
        self.assertIn("--draft", yml)
        self.assertNotIn("--latest", yml)
        self.assertIn("Successful Windows", yml)
        self.assertIn("GH_TOKEN:", yml)

if __name__ == "__main__":
    unittest.main()
