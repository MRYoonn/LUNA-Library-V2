"""Source-only checks; NOT evidence of a compiled Qt/Windows native run."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "tests" / "native_accuracy" / "snapshot_adapter.h"
NATIVE = ROOT / "tests" / "native_accuracy" / "main.cpp"

class SnapshotSourceContract(unittest.TestCase):
    def test_pure_adapter_has_no_io_or_mutation(self):
        code = HEADER.read_text(encoding="utf-8")
        for forbidden in ("#include <QSettings>", "#include <QFile>", "#include <QNetwork", "setValue(", "sync(", "remove(", "open("):
            self.assertNotIn(forbidden, code)
        self.assertIn("const QVariantMap& visibleRecord", code)

    def test_whitelisted_fields_and_no_identity_export(self):
        code = HEADER.read_text(encoding="utf-8")
        for expected in ('"terminal_author"', '"document_kind"', '"metadata_status"', '"authorsSource"', '"journal_abbreviation"'):
            self.assertIn(expected, code)
        self.assertNotIn('observed.insert(QStringLiteral("identity")', code)
        self.assertNotIn('observed.insert(QStringLiteral("path")', code)
        self.assertIn('fieldSources.insert(field, provider)', code)
        self.assertNotIn('fieldSources.insert(QStringLiteral("private_note")', code)

    def test_known_negative_fields_are_explicitly_captured(self):
        header = HEADER.read_text(encoding="utf-8")
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn("QJsonValue(QJsonValue::Null)", header)
        self.assertIn('observed.insert(QStringLiteral("document_kind")', header)
        self.assertIn('observed.insert(QStringLiteral("terminal_author")', header)
        self.assertIn("SI explicitly captures absent paper fields", native)

    def test_native_regressions_seed_isolated_settings(self):
        code = NATIVE.read_text(encoding="utf-8")
        for expected in ('QTemporaryDir', 'Hao Fang', 'legacy author must not reappear',
                         'senior override disclosed separately', 'SI has no invented DOI'):
            self.assertIn(expected, code)

if __name__ == "__main__":
    unittest.main()
