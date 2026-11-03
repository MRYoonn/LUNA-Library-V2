#!/usr/bin/env python3
"""Minimal always-on CI evidence for public synthetic LUNA builds.

No credentials, environment dumps, real PDF files, personal QSettings or
external network requests. GitHub Actions owns detailed per-step build logs.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
import re
from pathlib import Path
import sys


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def identity() -> dict:
    # Explicit allowlist; never serialize os.environ.
    return {
        "source_sha": os.environ.get("GITHUB_SHA", "local"),
        "run_id": os.environ.get("GITHUB_RUN_ID", "local"),
        "attempt": os.environ.get("GITHUB_RUN_ATTEMPT", "local"),
        "job": os.environ.get("GITHUB_JOB", "local"),
        "runner_os": os.environ.get("RUNNER_OS", sys.platform),
    }



def classify_native_failure(evidence: Path) -> dict:
    """Only fixed categorical fields are admitted; never copy paths, PDF data or stderr."""
    failure_file = evidence / "luna-native-failure.json"
    probe_file = evidence / "luna-print-probe.txt"
    result = {"evidence_found": False, "checkpoint": "unavailable",
              "exit_code": None, "print_probe": {}, "classification": "not_established"}
    if failure_file.is_file() and failure_file.stat().st_size <= 4096:
        try:
            item = json.loads(failure_file.read_text(encoding="utf-8"))
            if isinstance(item, dict):
                result["evidence_found"] = True
                status = str(item.get("status", ""))
                match = re.fullmatch(r"exit ([0-9]{1,10})", status)
                if match:
                    result["exit_code"] = int(match.group(1))
                stage = str(item.get("last_native_stage", ""))
                if "print" in stage.lower() or stage == "ui-done":
                    result["checkpoint"] = "native-print-or-end-of-smoke"
                elif "count" in stage.lower():
                    result["checkpoint"] = "native-folder-count"
                elif stage:
                    result["checkpoint"] = "other-native-smoke"
        except (OSError, UnicodeError, ValueError, TypeError):
            pass
    if probe_file.is_file() and probe_file.stat().st_size <= 4096:
        try:
            raw = probe_file.read_text(encoding="utf-8")
            for name in ("ownedDialogs", "cancelRequests", "seen", "finished",
                         "actionTriggers", "actionEnabledBefore", "printersAvailable"):
                match = re.search(r"\\b" + name + r"=([0-9]{1,5})\\b", raw)
                if match:
                    result["print_probe"][name] = int(match.group(1))
        except (OSError, UnicodeError):
            pass
    p = result["print_probe"]
    if p.get("seen") == 0:
        result["checkpoint"] = "native-print"
        if p.get("actionTriggers") == 0:
            result["classification"] = "ctrl_p_shortcut_not_dispatched"
        elif p.get("actionTriggers") == 1:
            result["classification"] = "print_action_dispatched_no_native_dialog_observed"
        else:
            result["classification"] = "native_print_dialog_not_observed_dispatch_unknown"
    elif p.get("seen", 0) > 0 and p.get("cancelRequests", 0) > 0:
        result["classification"] = "native_print_dialog_observed_and_cancel_requested"
    return result


def record(output: Path, phase: str, *, status: str = "unknown",
           gates: str = "", native_evidence: Path | None = None) -> dict:
    if phase not in {"start", "finish"}:
        raise ValueError("Invalid phase")
    if status not in {"unknown", "success", "failure", "cancelled", "skipped"}:
        raise ValueError("Invalid outcome")
    output.mkdir(parents=True, exist_ok=True)
    source = output / "build-diagnostics.json"
    previous = json.loads(source.read_text(encoding="utf-8")) if source.is_file() else {}
    if not isinstance(previous, dict):
        raise ValueError("Malformed diagnostic record")
    if phase == "start":
        if previous.get("phase") == "finish":
            raise ValueError("Finished record cannot restart")
        document = {
            "schema": 1, **identity(), "phase": "start",
            "started_utc": utc_now(), "status": "running", "gates": {},
        }
    else:
        document = {
            **previous, "schema": 1, **identity(), "phase": "finish",
            "started_utc": previous.get("started_utc", "unavailable"),
            "finished_utc": utc_now(), "status": status,
            "gates": {},
        }
        for item in gates.split(";"):
            if not item.strip():
                continue
            name, sep, outcome = item.partition("=")
            if not sep or not name.replace("-", "").replace("_", "").isalnum():
                raise ValueError("Invalid gate label")
            if outcome not in {"success", "failure", "skipped", "cancelled", ""}:
                raise ValueError("Invalid gate outcome")
            document["gates"][name] = outcome or "unknown"
        # A failed job must be recorded as failure; never infer success
        # because an evidence file exists.
        if status == "success" and "failure" in document["gates"].values():
            raise ValueError("Contradictory successful job with failed gate")
    temporary = source.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    temporary.replace(source)
    if phase == "finish":
        failed = [name for name, outcome in document["gates"].items()
                  if outcome == "failure"]
        run = str(document["run_id"])
        url = ("https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/" + run
               if run.isdecimal() else "Local synthetic run")
        lines = [
            "# LUNA Build Error Analysis",
            "",
            f"- Build run: {run}",
            f"- Source SHA: `{document['source_sha']}`",
            f"- Status: **{document['status']}**",
            f"- First failed gate: **{failed[0] if failed else 'none recorded'}**",
            f"- Workflow logs: {url}",
            "",
            "## Gate evidence",
            "",
        ]
        lines += [f"- {name}: {outcome}" for name, outcome in document["gates"].items()]
        lines += [
            "",
            "## Root cause and corrective action",
            "",
            ("The exact root cause is **not established** by gate status alone. "
             "Inspect the linked workflow job logs and native UI artifact; "
             "do not infer a cause or mark a fix verified without a new Windows run."
             if document["status"] != "success" else
             "All recorded gates completed without a failure."),
            "",
            "## Evidence retention",
            "",
            "This Markdown file is included in the build diagnostic artifact. "
            "On CI failure a copy is also posted as a GitHub issue for durable review.",
            "",
        ]
        if status == "failure" and native_evidence is not None:
            summary = classify_native_failure(native_evidence)
            lines.extend(["", "## Native failure verifier (bounded synthetic diagnostics)", ""])
            lines.append("- Classification: **" + summary["classification"] + "**")
            lines.append("- Checkpoint: " + summary["checkpoint"])
            lines.append("- Exit code: " + str(summary["exit_code"]))
            for name, value in summary["print_probe"].items():
                lines.append("- " + name + ": " + str(value))
            lines += ["", "Classification is evidence-based, not proof of the underlying "
                      "platform or Qt root cause; inspect source and rerun the failed gate.", ""]
            (output / "native-failure-verifier.json").write_text(
                json.dumps(summary, ensure_ascii=False, indent=2) + "\\n", encoding="utf-8")
        report = output / "failure-analysis.md"
        report.write_text("\n".join(lines), encoding="utf-8")
    return document


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("start", "finish"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--status", default="unknown")
    parser.add_argument("--gates", default="")
    parser.add_argument("--native-evidence", type=Path)
    args = parser.parse_args()
    result = record(args.output, args.phase, status=args.status, gates=args.gates,
                    native_evidence=args.native_evidence)
    print("[LUNA DIAG]", json.dumps(
        {"phase": result["phase"], "status": result["status"],
         "source_sha": result["source_sha"], "gates": result["gates"]},
        ensure_ascii=False))


if __name__ == "__main__":
    main()
