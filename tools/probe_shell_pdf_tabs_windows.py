#!/usr/bin/env python3
"""Windows-only acceptance: second Explorer-equivalent PDF joins the live full LUNA GUI.

Only disposable synthetic PDF fixtures are used. Existing user PDF/library paths
are not enumerated, modified, or uploaded. Shell command mirrors the registered
"%1" handler; a physical Explorer double-click still needs user-PC acceptance.
"""
from __future__ import annotations
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from smoke_windows import synthetic_pdf


def read_trace(path: Path):
    try:
        rows = path.read_text(encoding="utf-8-sig").splitlines()
    except OSError:
        return []
    result = []
    for row in rows:
        try:
            result.append(json.loads(row))
        except (ValueError, TypeError):
            pass
    return result


def wait_for(predicate, timeout=50):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        candidate = predicate()
        if candidate:
            return candidate
        time.sleep(.15)
    raise RuntimeError("Timed out waiting for live Windows GUI evidence")


def visible_windows(pid):
    user = ctypes.WinDLL("user32", use_last_error=True)
    cb = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    found = []

    @cb
    def enumerate_window(hwnd, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user.EnumWindows(enumerate_window, 0)
    return found


def run(package: Path, output: Path):
    if os.name != "nt":
        raise RuntimeError("Must run on a real Windows runner")
    exe = package / "LUNA.exe"
    if not exe.is_file():
        raise RuntimeError("Packaged Windows LUNA.exe missing")
    output.mkdir(parents=True, exist_ok=True)
    report = {"accepted": False, "scope": "synthetic full GUI / external command, not physical Explorer click"}
    first_process = None
    window_handles = []
    try:
        with tempfile.TemporaryDirectory(prefix="luna-v296-shell-") as temp:
            root = Path(temp)
            first = root / "First paper.pdf"
            second = root / "두 번째 연구 논문 with spaces.pdf"
            first.write_bytes(synthetic_pdf(2))
            second.write_bytes(synthetic_pdf(3))
            original = [hashlib.sha256(p.read_bytes()).hexdigest() for p in (first, second)]
            trace_path = output / "lifecycle.jsonl"
            env = os.environ.copy()
            env.update({
                "QT_QPA_PLATFORM": "windows",
                "QT_FORCE_STDERR_LOGGING": "1",
                "QT_PLUGIN_PATH": str(package),
                "QT_QPA_PLATFORM_PLUGIN_PATH": str(package / "platforms"),
                "APPDATA": str(root / "roaming"),
                "LOCALAPPDATA": str(root / "local"),
            })
            (root / "roaming").mkdir()
            (root / "local").mkdir()
            with (output / "primary.log").open("w", encoding="utf-8") as primary_log:
                first_process = subprocess.Popen(
                    [str(exe), "--luna-lifecycle-trace", str(trace_path), str(first)],
                    cwd=package, env=env, stdout=primary_log, stderr=subprocess.STDOUT)
                def primary_ready():
                    if first_process.poll() is not None:
                        raise RuntimeError("Primary LUNA exited before showing a PDF")
                    events = read_trace(trace_path)
                    startup = [e for e in events if e.get("event") == "document-open-dispatched"
                               and e.get("openOrigin") == "api-or-startup"]
                    return startup and visible_windows(first_process.pid)
                window_handles = wait_for(primary_ready)
                def shell_launch(pdf):
                    result = subprocess.run([str(exe), str(pdf)], cwd=package, env=env,
                                            capture_output=True, timeout=45)
                    if result.returncode != 0:
                        raise RuntimeError("Second LUNA.exe failed to hand off its PDF; exit=" + str(result.returncode))
                    if first_process.poll() is not None:
                        raise RuntimeError("First LUNA.exe exited after second PDF launch")

                shell_launch(second)
                def distinct_open():
                    rows = read_trace(trace_path)
                    return next((e for e in rows if e.get("event") == "document-open-dispatched"
                                 and e.get("openOrigin") == "explorer-shell-ipc"
                                 and e.get("documents") == 2), None)
                added = wait_for(distinct_open)
                shell_launch(second)
                def duplicate_selected():
                    rows = read_trace(trace_path)
                    return next((e for e in rows if e.get("event") == "document-open-existing-tab"
                                 and e.get("openOrigin") == "explorer-shell-ipc"
                                 and e.get("documents") == 2), None)
                duplicated = wait_for(duplicate_selected)
                initial = next(e for e in read_trace(trace_path)
                               if e.get("event") == "document-open-request"
                               and e.get("openOrigin") == "api-or-startup")
                assert initial["requestedDocumentId"] != added["requestedDocumentId"], "Different PDF identity collapsed"
                assert duplicated["requestedDocumentId"] == added["requestedDocumentId"], "Duplicate path identity changed"
                assert hashlib.sha256(first.read_bytes()).hexdigest() == original[0], "Original A was altered"
                assert hashlib.sha256(second.read_bytes()).hexdigest() == original[1], "Original B was altered"
                assert first_process.poll() is None and visible_windows(first_process.pid), "Original native GUI disappeared"
                report.update(accepted=True, primaryPid=first_process.pid, finalDocuments=2,
                              distinctPdfJoinedExistingGui=True, repeatedPdfSelectedExistingTab=True,
                              sourcePdfBytesUnchanged=True, firstWindowStillVisible=True)
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        if first_process is not None and first_process.poll() is None:
            user = ctypes.WinDLL("user32", use_last_error=True)
            for hwnd in visible_windows(first_process.pid):
                user.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE, synthetic process only
            try:
                first_process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                first_process.terminate()
                try:
                    first_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    first_process.kill()
        output.mkdir(parents=True, exist_ok=True)
        (output / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        if not report["accepted"]:
            (output / "failure-analysis.md").write_text(
                "# LUNA v296 shell integration failure\n\nFirst failing stage: " +
                str(report.get("error", "unknown")) +
                "\nReview synthetic lifecycle.jsonl and primary.log; no real user PDFs were accessed.\n",
                encoding="utf-8")


if __name__ == "__main__":
    run(Path(sys.argv[1]), Path(sys.argv[2]))
