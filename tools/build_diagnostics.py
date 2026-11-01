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


def record(output: Path, phase: str, *, status: str = "unknown",
           gates: str = "") -> dict:
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
    return document


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("start", "finish"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--status", default="unknown")
    parser.add_argument("--gates", default="")
    args = parser.parse_args()
    result = record(args.output, args.phase, status=args.status, gates=args.gates)
    print("[LUNA DIAG]", json.dumps(
        {"phase": result["phase"], "status": result["status"],
         "source_sha": result["source_sha"], "gates": result["gates"]},
        ensure_ascii=False))


if __name__ == "__main__":
    main()
