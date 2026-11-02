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
        self.assertEqual(manifest["schema"], 2)
        self.assertEqual(manifest["sha256"], hashlib.sha256(zip_file.read_bytes()).hexdigest())
        self.assertEqual(json.loads((dst / "LUNA-Release.json").read_text()), manifest)
        with zipfile.ZipFile(zip_file) as z:
            info = json.loads(z.read("build-info.json"))
            self.assertEqual(info["version"], "v0.9.1.134")
            self.assertEqual(len(manifest["files"]), len(z.namelist()))
            for record in manifest["files"]:
                payload = z.read(record["path"])
                self.assertEqual(len(payload), record["size"])
                self.assertEqual(hashlib.sha256(payload).hexdigest(), record["sha256"])
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
        self.assertIn("$previousProcess | Wait-Process -Timeout", script)
        self.assertIn("Get-Process -Id $WaitPid -ErrorAction SilentlyContinue", script)
        self.assertIn("Get-FileHash", script)
        self.assertIn("Unsafe package path", script)
        self.assertIn("rollback", script.lower())
        self.assertIn("User PDFs", script)
        self.assertNotIn("Remove-Item -LiteralPath $install", script)

    def test_automatic_update_check_can_be_toggled_without_unattended_install(self):
        source = (ROOT / "src/lunaupdater.h").read_text(encoding="utf-8")
        shell = (ROOT / "src/lunatabs.cpp").read_text(encoding="utf-8")
        for token in (
            'QStringLiteral("LunaAutoUpdateCheck")',
            'QStringLiteral("updater/autoCheckEnabled")',
            'automatic_->setCheckable(true)',
            'automatic_->setChecked(',
            'if (!explicitRequest && !automatic_->isChecked()) return;',
            'QAction* automaticAction() const',
            'QMessageBox::question(&shell_, QStringLiteral("LUNA 업데이트")',
        ):
            self.assertIn(token, source)
        self.assertIn('help->addAction(updater_->automaticAction())', shell)
        self.assertIn('updater_->check(false);', shell)

    def test_updater_stages_before_closing_and_requires_healthy_restart(self):
        header = (ROOT / "src/lunaupdater.h").read_text(encoding="utf-8")
        script = (ROOT / "tools/LUNA-Update.ps1").read_text(encoding="utf-8")
        self.assertIn('QStringLiteral("Stage")', header)
        self.assertIn('void stage(const QString& expectedTag)', header)
        self.assertIn('shell_.close(); // Existing PDF unsaved-change prompts still apply.', header)
        self.assertLess(header.index('void stage(const QString& expectedTag)'),
                        header.index('static QString installDir()'))
        self.assertIn('stageToken_', header)
        self.assertIn('startup.ok', header)
        self.assertIn('LUNA_UPDATE_ACK_TOKEN', header)
        for gate in ("Validate-Zip", "Verify-Payload", "Stage-Path",
                     "Get-Manifest", "Undo-Copy", "New-Stage", "Apply-Stage",
                     "backup-complete"):
            self.assertIn(gate, script)
        self.assertIn("Verify-Payload $payload $manifest", script)
        self.assertIn("Verify-Payload (Join-Path $work 'payload') $manifest", script)
        self.assertIn("LUNA still running; files untouched", script)
        self.assertIn("ZIP contains symlink", script)
        self.assertIn("Staged file digest mismatch", script)
        self.assertIn("Package attempts to replace user data", script)
        self.assertIn("Get-CimInstance Win32_Process", script)
        self.assertIn("startup.ok", script)
        self.assertIn("function Recover-Stage", script)
        self.assertIn("Assert-Safe-Target", script)
        self.assertIn("'Recover' { Recover-Stage }", script)
        self.assertIn("recovery.ps1", script)
        self.assertIn("state = 'applying'", script)
        self.assertIn("Recovery backup incomplete", script)
        self.assertIn("no rollback while it is running", script)
        self.assertNotIn("Remove-Item -LiteralPath $install", script)

    def test_no_replacement_of_exe_during_download(self):
        script = (ROOT / "tools/LUNA-Update.ps1").read_text(encoding="utf-8")
        # Check/Stage must never mutate installed program files; Apply is
        # the only mode allowed to copy into the install directory.
        stage = script.split("function New-Stage", 1)[1].split("function Check-Release", 1)[0]
        self.assertIn("Copy-Item -LiteralPath $PSCommandPath", stage)
        self.assertNotIn("Copy-Item -LiteralPath $file", stage)
        self.assertNotIn("Remove-Item -LiteralPath $install", stage)
        self.assertIn("if (!$available)", stage)
        self.assertIn("files", (ROOT / "tools/prepare_release.py").read_text(encoding="utf-8"))

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
