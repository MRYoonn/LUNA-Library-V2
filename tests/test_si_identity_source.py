"""Source-only guard for the opt-in native SI identity adapter.

This is NOT a substitute for building/running the QtCore C++ test.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SiIdentitySourceContract(unittest.TestCase):
    def test_sidecar_is_local_atomic_and_opt_in(self):
        code = (ROOT / "src/lunasiidentity.h").read_text(encoding="utf-8")
        for token in ("inline Result read(", "inline Result ensure(", "QSaveFile",
                      "setDirectWriteFallback(false)", "QLockFile", "tryLock(0)",
                      "validId(", "identity_missing", "invalid_sidecar",
                      "unsupported_sidecar", "unsafe_pdf_path"):
            self.assertIn(token, code, token)
        for forbidden in ("QNetworkAccessManager", "QSettings", "QFile::remove(",
                          "QFile::copy(", "QFile::rename("):
            self.assertNotIn(forbidden, code, forbidden)

    def test_native_fixture_covers_move_copy_and_corruption(self):
        code = (ROOT / "tests/native_si_identity/main.cpp").read_text(encoding="utf-8")
        for token in ("QTemporaryDir", "read(root, moved)", "QFile::copy(moved, copied)",
                      "invalid_sidecar", "unsupported_sidecar", "identity_busy",
                      "unsafe_pdf_path", "Metadata::normalizeDoi",
                      "PDF bytes never modified"):
            self.assertIn(token, code, token)

    def test_isolated_cmake(self):
        code = (ROOT / "tests/native_si_identity/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("COMPONENTS Core", code)
        self.assertIn("luna_si_identity_native", code)
        self.assertNotIn("PDF4QT", code)
        self.assertNotIn("Widgets", code)


if __name__ == "__main__":
    unittest.main()
