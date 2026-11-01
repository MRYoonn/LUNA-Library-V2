"""CI diagnostics must remain useful, small, and free of environment dumps."""
from pathlib import Path
import os
import tempfile
import unittest
from unittest.mock import patch

from tools.build_diagnostics import record


class BuildDiagnosticsContracts(unittest.TestCase):
    def test_started_failure_record_is_durable_and_exact_sha(self):
        with tempfile.TemporaryDirectory() as folder, patch.dict(os.environ, {
            "GITHUB_SHA": "a" * 40,
            "GITHUB_RUN_ID": "123",
            "GITHUB_JOB": "build-windows",
            "SECRET_CANARY": "never-copy",
        }):
            path = Path(folder)
            started = record(path, "start")
            self.assertEqual(started["phase"], "start")
            self.assertEqual(started["status"], "running")
            finished = record(path, "finish", status="failure",
                              gates="preflight=success;compile=success;native= failure".replace(
                                  "native= failure", "native=failure"))
            self.assertEqual(finished["source_sha"], "a" * 40)
            self.assertEqual(finished["gates"]["native"], "failure")
            data = (path / "build-diagnostics.json").read_text(encoding="utf-8")
            self.assertNotIn("never-copy", data)
            self.assertEqual(finished["started_utc"], started["started_utc"])

    def test_missing_start_can_still_record_failed_job(self):
        with tempfile.TemporaryDirectory() as folder:
            finished = record(Path(folder), "finish", status="failure",
                              gates="preflight=skipped;compile=unknown".replace(
                                  "compile=unknown", "compile="))
            self.assertEqual(finished["status"], "failure")
            self.assertEqual(finished["gates"]["compile"], "unknown")

    def test_no_false_success_for_failed_gate(self):
        with tempfile.TemporaryDirectory() as folder:
            record(Path(folder), "start")
            with self.assertRaises(ValueError):
                record(Path(folder), "finish", status="success", gates="compile=failure")
            self.assertEqual(record(Path(folder), "finish", status="failure")["status"],
                             "failure")

    def test_reject_bad_gate_and_post_finish_restart(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with self.assertRaises(ValueError):
                record(root, "finish", status="failure", gates="../credentials=success")
            record(root, "finish", status="failure")
            with self.assertRaises(ValueError):
                record(root, "start")


if __name__ == "__main__":
    unittest.main()
