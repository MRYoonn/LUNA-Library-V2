#!/usr/bin/env python3
"""Synthetic-only foreign-window lifecycle comparison; never POC acceptance."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from smoke_windows import synthetic_pdf


class SyntheticJob:
    """Kill only this disposable test's process tree, including orphaned children."""
    def __init__(self):
        class Basic(ctypes.Structure):
            _fields_ = [("processTime", ctypes.c_int64), ("jobTime", ctypes.c_int64),
                        ("flags", wintypes.DWORD), ("minimum", ctypes.c_size_t),
                        ("maximum", ctypes.c_size_t), ("active", wintypes.DWORD),
                        ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
                        ("scheduling", wintypes.DWORD)]
        class Extended(ctypes.Structure):
            _fields_ = [("basic", Basic), ("io", ctypes.c_uint64 * 6),
                        ("processMemory", ctypes.c_size_t), ("jobMemory", ctypes.c_size_t),
                        ("peakProcess", ctypes.c_size_t), ("peakJob", ctypes.c_size_t)]
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.kernel.CreateJobObjectW.restype = wintypes.HANDLE
        self.kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        self.kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.kernel.CreateJobObjectW(None, None)
        limits = Extended()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.handle or not self.kernel.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            if self.handle:
                self.kernel.CloseHandle(self.handle)
            raise ctypes.WinError(ctypes.get_last_error())

    def start(self, command, **kwargs):
        process = subprocess.Popen(command, creationflags=0x4, **kwargs)  # CREATE_SUSPENDED
        try:
            if not self.kernel.AssignProcessToJobObject(self.handle, int(process._handle)):
                raise ctypes.WinError(ctypes.get_last_error())
            resume = ctypes.WinDLL("ntdll").NtResumeProcess
            resume.argtypes = [wintypes.HANDLE]
            resume.restype = ctypes.c_long
            if resume(int(process._handle)) != 0:
                raise RuntimeError("Could not resume the job-owned synthetic host")
            return process
        except BaseException:
            process.kill()
            process.wait()
            raise

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def run(host, package, output):
    if os.name != "nt":
        raise RuntimeError("Actual Windows required")
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for method in ("qt", "win32"):
        for scale, editor in (("1", False), ("1.25", False), ("1.5", False), ("1", True)):
            label = f"{method}-{scale}" + ("-editor" if editor else "")
            report = output / f"{label}.json"
            with tempfile.TemporaryDirectory(prefix="luna-tab-fixtures-") as directory:
                root = Path(directory)
                files = [root / name / f"합성 문서 {name}.pdf" for name in ("A", "B")]
                for file in files:
                    file.parent.mkdir()
                    (file.parent / "Synthetic examples").mkdir()
                    file.write_bytes(synthetic_pdf(2))
                hashes = [hashlib.sha256(file.read_bytes()).hexdigest() for file in files]
                env = os.environ.copy()
                env.update(QT_SCALE_FACTOR=scale, QT_FORCE_STDERR_LOGGING="1",
                           QT_PLUGIN_PATH=str(package), QT_QPA_PLATFORM_PLUGIN_PATH=str(package / "platforms"))
                env["PATH"] = str(package) + os.pathsep + env["PATH"]
                with (output / f"{label}.log").open("w", encoding="utf-8") as log:
                    job = SyntheticJob()
                    process = job.start([str(host), *(["--editor-probe"] if editor else []), "--method", method, "--report", str(report),
                                                str(package / "LUNA.exe"), *map(str, files)],
                                               env=env, stdout=log, stderr=subprocess.STDOUT)
                    try:
                        code = process.wait(timeout=165 if editor else 75)
                    except subprocess.TimeoutExpired:
                        # Only this experiment's own synthetic process tree.
                        code = -1
                    finally:
                        job.close()  # Also cleans up children if the host itself crashed.
                        process.wait(timeout=10)
                unchanged = hashes == [hashlib.sha256(file.read_bytes()).hexdigest() for file in files]
                evidence = json.loads(report.read_text(encoding="utf-8")) if report.exists() else {"embeddingLifecycle": "failed", "detail": "No host report"}
                evidence.update(method=method, qtScale=scale, editorProbe=editor, exitCode=code, sourceBytesUnchanged=unchanged,
                                pocAccepted=False)
                results.append(evidence)
                report.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
                if code != 0:
                    # The job has already removed every synthetic child. Skip
                    # later scales for this method, then compare the other one.
                    break
    (output / "summary.json").write_text(json.dumps({"pocAccepted": False, "runs": results,
        "remaining": ["real keyboard and pointer input", "focus and Alt+Tab", "annotation isolation",
                      "save and reopen", "modal dialogs", "unsaved close cancel", "physical OS DPI"]},
        ensure_ascii=False, indent=2), encoding="utf-8")
    if len(results) != 8 or any(r["exitCode"] != 0 or not r["sourceBytesUnchanged"] for r in results):
        raise RuntimeError("Embedding lifecycle comparison failed; full POC remains unaccepted")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, type=Path)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    run(args.host.resolve(strict=True), args.package.resolve(strict=True), args.output.resolve())
