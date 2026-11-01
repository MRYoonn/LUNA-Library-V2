"""Contract tests for the Windows installer; no installation is attempted on Linux."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
ISS = (ROOT / "tools/LUNA-Library-Setup.iss").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/build-windows.yml").read_text(encoding="utf-8")
UI = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
GUIDE = (ROOT / "USER_GUIDE_KO.md").read_text(encoding="utf-8")


class InstallerContracts(unittest.TestCase):
    def test_install_per_user_without_repackaging_different_executable(self):
        for expected in (
            "PrivilegesRequired=lowest", "DefaultDirName={localappdata}",
            "OutputDir=LUNA-Windows-Installer",
            "OutputBaseFilename=LUNA-Library-Setup",
            'Source: "LUNA-Windows-Portable\\*"; DestDir: "{app}"',
            "ArchitecturesAllowed=x64compatible",
            "Name: \"{autoprograms}\\LUNA Library\"",
            "Flags: unchecked",
        ):
            self.assertIn(expected, ISS, expected)
        self.assertNotIn("PrivilegesRequired=admin", ISS)
        self.assertNotIn("Source: \"src/", ISS)

    def test_installer_only_built_after_native_validation(self):
        gates = [
            "Independent document windows and native annotation round trip",
            "Production native tabs, library and fresh-process memo restoration",
            "Record validation scope in the portable package",
            "Build and smoke-test per-user LUNA Setup EXE",
            "Upload LUNA Windows installer artifact",
        ]
        offsets = [WORKFLOW.index(x) for x in gates]
        self.assertEqual(offsets, sorted(offsets))
        for token in (
            "& $iscc 'tools/LUNA-Library-Setup.iss'",
            "& $setup /VERYSILENT /SUPPRESSMSGBOXES /NORESTART",
            "Get-FileHash $installed -Algorithm SHA256",
            "if ($actual -ne $expected)",
            "& $uninstaller[0].FullName /VERYSILENT",
            "name: LUNA-Windows-Installer",
        ):
            self.assertIn(token, WORKFLOW, token)

    def test_one_library_picker_and_portable_metadata_on_any_selected_folder(self):
        self.assertNotIn('QStringLiteral("LunaMyboxConnect")', UI)
        self.assertIn('QStringLiteral("LunaLocation")', UI)
        self.assertIn("localSync_->configure(root_,true)", UI)
        self.assertNotIn('QStringLiteral("library/mybox/enabled")', UI)
        self.assertIn("별도 MYBOX 버튼은 없습니다", GUIDE)

    def test_thumbnail_renderers_share_safe_sidecar_resolution(self):
        self.assertIn("QString savedThumbnailForPdf(const QString& documentPath)", UI)
        self.assertEqual(UI.count("savedThumbnailForPdf(index.data(Qt::UserRole).toString())"), 2)
        self.assertIn('QStringLiteral(".jpeg")', UI)
        self.assertIn("sidecar.size() <= 5'000'000", UI)


if __name__ == "__main__":
    unittest.main()
