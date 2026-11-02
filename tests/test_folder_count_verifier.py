"""Regression tests for the cheap folder-count semantic gate.

No real PDFs or user-specific directories are used.
"""
from pathlib import Path
import unittest

from tools.verify_folder_counts import verify_source

SOURCE = (Path(__file__).resolve().parents[1] / "src/lunalibrary.cpp").read_text(encoding="utf-8")


class FolderCountVerifierTests(unittest.TestCase):
    def test_current_direct_count_and_native_smoke_contract(self):
        self.assertEqual(verify_source(SOURCE), [])

    def test_reject_original_ancestor_accumulation_regression(self):
        changed = SOURCE.replace(
            "result->insert(folder, result->value(folder) + 1);",
            "while (folder.startsWith(normalizedRoot)) {\n"
            "    result->insert(folder, result->value(folder) + 1);\n"
            "    folder = QDir(folder).absolutePath();\n"
            "}",
            1,
        )
        self.assertIn("count_only_pdf_parent", verify_source(changed))

    def test_reject_recursive_selected_folder_view(self):
        changed = SOURCE.replace(
            "QDir::Files | QDir::Readable | QDir::NoDotAndDotDot | QDir::NoSymLinks);",
            "QDir::Files | QDir::Readable | QDir::NoDotAndDotDot | QDir::NoSymLinks,\n"
            "            QDirIterator::Subdirectories);",
            1,
        )
        self.assertIn("folder_view_nonrecursive", verify_source(changed))

    def test_reject_missing_child_deletion_assertion(self):
        changed = SOURCE.replace("pdfCount(childFolderPath) == 0", "pdfCount(countRoot) >= 0", 1)
        self.assertIn("synthetic_child_removed", verify_source(changed))

    def test_reject_stale_worker_check_removed(self):
        changed = SOURCE.replace("if (generation_ != version) return;", "(void)version;", 1)
        self.assertIn("stale_worker_result_blocked", verify_source(changed))

    def test_missing_section_fails_closed(self):
        self.assertTrue(verify_source("not a LUNA source"))


if __name__ == "__main__":
    unittest.main()
