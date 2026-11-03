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
            analysis = (path / "failure-analysis.md").read_text(encoding="utf-8")
            self.assertNotIn("never-copy", data + analysis)
            self.assertIn("First failed gate: **native**", analysis)
            self.assertIn("https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/123", analysis)
            self.assertIn("not established", analysis)
            self.assertEqual(finished["started_utc"], started["started_utc"])

    def test_each_finished_run_produces_durable_report_and_issues_archive(self):
        with tempfile.TemporaryDirectory() as folder:
            record(Path(folder), "finish", status="success", gates="native-ui=success")
            report = (Path(folder) / "failure-analysis.md").read_text(encoding="utf-8")
            self.assertIn("Status: **success**", report)
        root = Path(__file__).resolve().parents[1]
        workflow = (root / ".github/workflows/build-windows.yml").read_text(encoding="utf-8")
        self.assertIn("Preserve failed build analysis in a GitHub issue", workflow)
        self.assertIn("if: failure()", workflow)
        self.assertIn("gh issue create --title", workflow)
        for name in ("LUNA-Build-Diagnostics", "LUNA-Preflight-Diagnostics"):
            part = workflow.split("name: " + name, 1)[1][:230]
            self.assertIn("retention-days: 90", part)

    def test_native_print_failure_classified_without_private_path(self):
        import json
        from tools.build_diagnostics import classify_native_failure
        with tempfile.TemporaryDirectory() as folder:
            evidence = Path(folder) / "evidence"
            evidence.mkdir()
            (evidence / "luna-native-failure.json").write_text(json.dumps({
                "status": "exit 9", "last_native_stage": "ui-done",
                "leak": "never-copy-private-path"
            }), encoding="utf-8")
            (evidence / "luna-print-probe.txt").write_text(
                "ownedDialogs=0 cancelRequests=0 seen=0 finished=1\n"
                "actionTriggers=1 actionEnabledBefore=1 printersAvailable=1\n"
                "leak=never-copy-private-path", encoding="utf-8")
            summary = classify_native_failure(evidence)
            self.assertEqual(summary["exit_code"], 9)
            self.assertEqual(summary["classification"],
                             "print_action_dispatched_no_native_dialog_observed")
            output = Path(folder) / "report"
            record(output, "finish", status="failure", gates="native-ui=failure",
                   native_evidence=evidence)
            analysis = (output / "failure-analysis.md").read_text(encoding="utf-8")
            self.assertIn("print_action_dispatched_no_native_dialog_observed", analysis)
            self.assertNotIn("never-copy-private-path", analysis)
            self.assertTrue((output / "native-failure-verifier.json").is_file())

    def test_native_print_smoke_waits_for_action_and_uses_real_root_hwnd(self):
        root = Path(__file__).resolve().parents[1]
        smoke = (root / "src/lunaeditorsmoke.cpp").read_text(encoding="utf-8")
        self.assertIn('if (!waitFor([&] { return printAction->isEnabled(); }))', smoke)
        self.assertIn('const HWND printOwner = GetAncestor(editorWindow, GA_ROOT);', smoke)
        self.assertIn('nativePrintTriggers.load() != 1', smoke)
        self.assertIn('if (!sawPrint) return fail(', smoke)
        self.assertIn('finished = true; cancelPrint.join();', smoke)

    def test_native_shortcut_failure_and_no_evidence_remain_distinct(self):
        from tools.build_diagnostics import classify_native_failure
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            self.assertEqual(classify_native_failure(path)["classification"], "not_established")
            (path / "luna-print-probe.txt").write_text(
                "ownedDialogs=0 cancelRequests=0 seen=0 finished=1\n"
                "actionTriggers=0 actionEnabledBefore=1 printersAvailable=1\n",
                encoding="utf-8")
            self.assertEqual(classify_native_failure(path)["classification"],
                             "ctrl_p_shortcut_not_dispatched")

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


    def test_native_access_violation_triggers_cdb_stack_without_masking_red(self):
        root = Path(__file__).resolve().parents[1]
        workflow = (root / ".github/workflows/build-windows.yml").read_text(encoding="utf-8")
        crash = (root / "tools/probe_native_crash_windows.py").read_text(encoding="utf-8")
        start = workflow.index("Capture crash-only synthetic Windows stack (CDB)")
        end = workflow.index("Production native tabs, library and fresh-process", start)
        step = workflow[start:end]
        self.assertIn("failure() && steps.native-ui.outcome == 'failure'", step)
        self.assertIn("Get-LunaCdb", step)
        self.assertIn("probe_native_crash_windows.py", step)
        self.assertIn("luna-ui/native-crash-replay", step)
        self.assertIn("if: ${{ always() }}", workflow)
        self.assertIn("if: ${{ failure() && steps.native-ui.outcome == 'failure' }}", workflow)
        self.assertIn("LUNA_UNHANDLED_ACCESS_VIOLATION", crash)
        self.assertIn("sxd -c2", crash)
        self.assertIn("synthetic_pdf(2)", crash)
        self.assertIn("first_run_remains_failed", crash)
        self.assertIn("timeout=95", crash)
        self.assertNotIn("os.environ.items()", crash)
        self.assertNotIn("MiniDump", crash)

    def test_workflow_must_capture_first_failure_and_upload_even_if_red(self):
        root = Path(__file__).resolve().parents[1]
        workflow = (root / ".github/workflows/build-windows.yml").read_text(encoding="utf-8")
        smoke = (root / "tools/smoke_windows.py").read_text(encoding="utf-8")
        for job in ("windows-packaging-preflight:", "  build-windows:"):
            position = workflow.index(job)
            bootstrap = workflow.index("Bootstrap trace before checkout", position)
            checkout = workflow.index("uses: actions/checkout@v4", position)
            self.assertLess(bootstrap, checkout)
        for artifact in ("LUNA-Preflight-Diagnostics", "LUNA-Build-Diagnostics"):
            start = workflow.index("name: " + artifact)
            self.assertIn("if: always()", workflow[max(0, start - 180):start])
        for expected in ("luna-native-stdout.log", "luna-native-stderr.log",
                         "luna-native-failure.json", "luna-smoke-start.json",
                         "stdout=out, stderr=err", "timeout=45"):
            self.assertIn(expected, smoke)
        self.assertIn("if: ${{ always() }}", workflow)
        self.assertIn("workflow_dispatch:", workflow)
        self.assertNotIn("\n  push:", workflow)



if __name__ == "__main__":
    unittest.main()
