#!/usr/bin/env python3
"""Crash-only CDB replay of LUNA's synthetic native UI test.

Never reads user PDFs or stores dumps. It does not alter the failed CI result,
and an unreproduced access violation is NOT a pass.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from smoke_windows import synthetic_pdf


def replay(package: Path, output: Path, debugger: Path) -> None:
    if sys.platform != "win32":
        raise RuntimeError("Actual Windows required for native crash replay")
    package = package.resolve(strict=True)
    debugger = debugger.resolve(strict=True)
    output.mkdir(parents=True, exist_ok=True)
    commands = ('sxd -c2 ".echo LUNA_UNHANDLED_ACCESS_VIOLATION;'
                '.exr -1;.ecxr;kv;~*kb;lm;q" av;'
                'sxe -c ".echo LUNA_HEAP_BREAK;kv;~*kb;q" bpe;'
                'sxe -c ".echo LUNA_DEBUGGEE_EXIT;.lastevent;q" epr;g')
    with tempfile.TemporaryDirectory(prefix="luna-crash-replay-") as temp:
        root = Path(temp)
        library = root / "library"
        (library / "Synthetic examples").mkdir(parents=True)
        pdf = library / "synthetic.pdf"
        pdf.write_bytes(synthetic_pdf(2))
        report = root / "result.txt"
        system = Path(os.environ["SystemRoot"])
        env = os.environ.copy()
        env["PATH"] = os.pathsep.join(map(str, (package, system / "System32", system)))
        env.update(QT_PLUGIN_PATH=str(package),
                   QT_QPA_PLATFORM_PLUGIN_PATH=str(package / "platforms"),
                   QT_QPA_PLATFORM="windows", QT_SCALE_FACTOR="1",
                   QT_FORCE_STDERR_LOGGING="1",
                   QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES="1")
        log = output / "cdb-stack.txt"
        stdout = output / "cdb-stdout.txt"
        command = [str(debugger), "-G", "-logo", str(log), "-c", commands,
                   str(package / "LUNA.exe"), "--luna-smoke-report",
                   str(report), str(pdf)]
        started = time.monotonic()
        exit_code = None
        timed_out = False
        try:
            with stdout.open("wb") as stream:
                exit_code = subprocess.run(command, env=env, cwd=root,
                                           stdout=stream, stderr=subprocess.STDOUT,
                                           timeout=95, check=False).returncode
        except subprocess.TimeoutExpired:
            timed_out = True
        transcript = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        # The full kernel/module listing may be extensive. Retain at most 160 KiB,
        # with the exception marker and first stacks, not user process memory.
        log.write_text(transcript[:160000], encoding="utf-8")
        marker = ("LUNA_UNHANDLED_ACCESS_VIOLATION" in transcript or
                  "LUNA_HEAP_BREAK" in transcript)
        metadata = {
            "schema": 1, "scope": "synthetic crash-only diagnostic replay",
            "first_run_remains_failed": True, "stack_exception_observed": marker,
            "timed_out": timed_out, "exit_code": exit_code,
            "elapsed_seconds": round(time.monotonic() - started, 2),
            "source_sha": os.getenv("GITHUB_SHA", "local"),
        }
        (output / "replay-status.json").write_text(
            json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        print("[LUNA CRASH REPLAY] " + json.dumps(metadata))
        if marker:
            print("[LUNA CRASH REPLAY] Native exception captured in cdb-stack.txt")
        else:
            print("[LUNA CRASH REPLAY] Exception not reproduced; original failure remains")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--debugger", type=Path, required=True)
    args = parser.parse_args()
    replay(args.package, args.output, args.debugger)
