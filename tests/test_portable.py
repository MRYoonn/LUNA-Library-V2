"""Synthetic package fixtures; no real binaries or user documents are committed."""
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import subprocess
import contextlib
import io

from tools.smoke_windows import run, synthetic_pdf

from tools.verify_portable import REQUIRED_BINARIES, REQUIRED_NOTICES, check, is_x64_pe


def pe_header(machine=0x8664, magic=0x20B):
    data = bytearray(90)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, machine)
    struct.pack_into("<H", data, 88, magic)
    return data


class PortableLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in REQUIRED_BINARIES:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(pe_header())
        for name in REQUIRED_NOTICES:
            (self.root / name).write_text("Synthetic license notice")
        (self.root / "third_party_licenses").mkdir()
        (self.root / "third_party_licenses/MIT.txt").write_text("Synthetic notice")

    def test_complete_layout(self):
        self.assertEqual(check(self.root), [])

    def test_missing_qt_platform_plugin(self):
        (self.root / "platforms/qwindows.dll").unlink()
        self.assertTrue(any("qwindows.dll" in e for e in check(self.root)))

    def test_missing_pdf_engine(self):
        (self.root / "Pdf4QtLibCore.dll").unlink()
        self.assertTrue(any("Pdf4QtLibCore.dll" in e for e in check(self.root)))

    def test_missing_runtime(self):
        (self.root / "vcruntime140_1.dll").unlink()
        self.assertTrue(any("vcruntime140_1.dll" in e for e in check(self.root)))

    def test_empty_license(self):
        (self.root / "PDF4QT_LICENSE.txt").write_bytes(b"")
        self.assertTrue(any("PDF4QT_LICENSE.txt" in e for e in check(self.root)))

    def test_empty_third_party_licenses(self):
        (self.root / "third_party_licenses/MIT.txt").unlink()
        self.assertTrue(any("third-party" in e for e in check(self.root)))

    def test_wrong_architecture(self):
        (self.root / "LUNA.exe").write_bytes(pe_header(0x14C, 0x10B))
        self.assertTrue(any("x64" in e for e in check(self.root)))

    def test_truncated_and_invalid_binaries(self):
        for data in (b"", b"MZ", b"not a binary", pe_header()[:89]):
            with self.subTest(data_length=len(data)):
                (self.root / "LUNA.exe").write_bytes(data)
                self.assertFalse(is_x64_pe(self.root / "LUNA.exe"))

    def test_invalid_pe_offset(self):
        data = pe_header()
        struct.pack_into("<I", data, 60, 0xFFFFFFFF)
        (self.root / "LUNA.exe").write_bytes(data)
        self.assertFalse(is_x64_pe(self.root / "LUNA.exe"))

    def test_private_files_blocked_recursively(self):
        nested = self.root / "nested"
        nested.mkdir()
        for name in ("test.PDF", "state.sqlite3", ".env", ".env.local", "secret.key"):
            with self.subTest(name=name):
                path = nested / name
                path.write_bytes(b"synthetic")
                self.assertTrue(any("Private-data" in e for e in check(self.root)))
                path.unlink()

    def test_extra_executable(self):
        (self.root / "Other.exe").write_bytes(pe_header())
        self.assertTrue(any("Unexpected executable" in e for e in check(self.root)))

    def test_missing_directory(self):
        self.assertTrue(check(self.root / "absent"))


class NativeSmokeHarnessTests(unittest.TestCase):
    def test_synthetic_pdf_cross_reference(self):
        data = synthetic_pdf()
        xref = int(data.split(b"startxref\n")[1].splitlines()[0])
        self.assertEqual(data[xref:xref + 4], b"xref")
        entries = data[xref:].splitlines()[3:8]
        for number, entry in enumerate(entries, 1):
            offset = int(entry[:10])
            self.assertTrue(data[offset:].startswith(f"{number} 0 obj\n".encode()))
        self.assertIn(b"/Count 1", data)

    def test_non_windows_is_rejected(self):
        with patch("tools.smoke_windows.sys.platform", "linux"):
            with self.assertRaisesRegex(RuntimeError, "native Windows"):
                run(Path("."))

    def test_zero_exit_without_marker_fails(self):
        with tempfile.TemporaryDirectory() as package:
            with patch("tools.smoke_windows.sys.platform", "win32"), \
                 patch.dict("os.environ", {"SystemRoot": package}), \
                 patch("tools.smoke_windows.subprocess.run"):
                with self.assertRaisesRegex(RuntimeError, "marker missing"):
                    run(Path(package))

    def test_valid_marker_and_isolated_runtime(self):
        calls = []
        def process(command, **kwargs):
            calls.append((command, kwargs))
            if "--luna-smoke-report" in command:
                Path(command[2]).write_bytes(b"LUNA_SMOKE_PDF_OPEN_OK\n")
        with tempfile.TemporaryDirectory() as package:
            with patch("tools.smoke_windows.sys.platform", "win32"), \
                 patch.dict("os.environ", {"SystemRoot": package}), \
                 patch("tools.smoke_windows.subprocess.run", side_effect=process):
                with contextlib.redirect_stdout(io.StringIO()):
                    run(Path(package))
        self.assertEqual(len(calls), 2)
        for command, kwargs in calls:
            self.assertTrue(kwargs["check"])
            self.assertLessEqual(kwargs["timeout"], 45)
            self.assertEqual(kwargs["env"]["QT_QPA_PLATFORM"], "windows")
            self.assertNotEqual(kwargs["cwd"], Path(package))

    def test_native_timeout_propagates(self):
        with tempfile.TemporaryDirectory() as package:
            with patch("tools.smoke_windows.sys.platform", "win32"), \
                 patch.dict("os.environ", {"SystemRoot": package}), \
                 patch("tools.smoke_windows.subprocess.run", side_effect=subprocess.TimeoutExpired("LUNA", 20)):
                with self.assertRaises(subprocess.TimeoutExpired):
                    run(Path(package))


if __name__ == "__main__":
    unittest.main()
