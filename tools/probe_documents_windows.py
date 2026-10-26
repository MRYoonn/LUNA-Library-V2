#!/usr/bin/env python3
"""Exercise independent native PDF processes using temporary synthetic files."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from smoke_windows import run, synthetic_pdf


def probe(package: Path, output: Path) -> None:
    if sys.platform != "win32":
        raise RuntimeError("Independent document integration requires native Windows")
    package = package.resolve(strict=True)
    executable = package / "LUNA.exe"
    user = ctypes.WinDLL("user32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.IsWindowEnabled.argtypes = [wintypes.HWND]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]

    class Memory(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage")]

    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Memory), wintypes.DWORD]

    def windows(pid, filename=None):
        found = []

        @callback
        def visit(hwnd, _):
            owner = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid and user.IsWindowVisible(hwnd):
                title = ctypes.create_unicode_buffer(1024)
                user.GetWindowTextW(hwnd, title, len(title))
                if filename is None or filename in title.value:
                    found.append(hwnd)
            return True

        user.EnumWindows(visit, 0)
        return found

    def memory(pid):
        handle = kernel.OpenProcess(0x410, False, pid)
        if not handle:
            raise RuntimeError("Could not inspect own synthetic document process")
        try:
            result = Memory()
            result.cb = ctypes.sizeof(result)
            if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(result), result.cb):
                raise RuntimeError("Native memory measurement failed")
            return result.WorkingSetSize
        finally:
            kernel.CloseHandle(handle)

    def close(process):
        if process.poll() is not None:
            return
        for hwnd in windows(process.pid):
            user.PostMessageW(hwnd, 0x10, 0, 0)  # WM_CLOSE; own synthetic processes only.
        process.wait(timeout=10)
        if process.returncode != 0:
            raise RuntimeError("Native document process exited abnormally")

    child_env = os.environ.copy()
    system = Path(os.environ["SystemRoot"])
    child_env["PATH"] = os.pathsep.join(map(str, (package, system / "System32", system)))
    child_env.update(QT_PLUGIN_PATH=str(package), QT_QPA_PLATFORM_PLUGIN_PATH=str(package / "platforms"),
                     QT_QPA_PLATFORM="windows", QT_SCALE_FACTOR="1", QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES="1")
    processes = []
    measurements = []
    with tempfile.TemporaryDirectory(prefix="luna-independent-") as temporary:
        root = Path(temporary)
        fixtures = []
        try:
            for index in range(10):
                path = root / f"한글 independent document {index:02}.pdf"
                path.write_bytes(synthetic_pdf(2))
                fixtures.append(path)
                process = subprocess.Popen([str(executable), str(path)], cwd=root, env=child_env)
                processes.append(process)
                deadline = time.monotonic() + 25
                while time.monotonic() < deadline:
                    matches = windows(process.pid, path.name)
                    if matches and user.IsWindowEnabled(matches[0]):
                        break
                    if process.poll() is not None:
                        raise RuntimeError("Independent PDF exited before becoming readable")
                    time.sleep(0.1)
                else:
                    raise RuntimeError("Independent PDF did not finish loading")
                if len(processes) in (2, 5, 10):
                    if any(p.poll() is not None for p in processes):
                        raise RuntimeError("Opening another PDF closed an existing document")
                    measurements.append({"documents": len(processes), "workingSetBytes": sum(memory(p.pid) for p in processes)})
            duplicate = subprocess.run([str(executable), str(fixtures[0])], cwd=root, env=child_env, timeout=25)
            if duplicate.returncode or processes[0].poll() is not None:
                raise RuntimeError("Duplicate open did not activate the existing document")
            for process in processes[1:]:
                close(process)
            if processes[0].poll() is not None:
                raise RuntimeError("Closing other windows closed the retained document")
            # Run the full native annotation/save test in A while B stays open.
            # B must retain its title, clean-close behavior and original bytes.
            run(package, output)
            if processes[0].poll() is not None or not windows(processes[0].pid, fixtures[0].name):
                raise RuntimeError("Annotation round trip affected the independent document window")
            if any(path.read_bytes() != synthetic_pdf(2) for path in fixtures):
                raise RuntimeError("Independent document bytes changed")
            close(processes[0])
            output.mkdir(parents=True, exist_ok=True)
            (output / "independent-documents.json").write_text(json.dumps({
                "mode": "independent native processes and windows; no tabs", "measurements": measurements,
                "duplicateActivation": True, "annotationIsolation": True, "cleanExit": True,
            }, indent=2), encoding="utf-8")
            print("[PASS] Native 2/5/10 independent PDF windows, duplicate activation, annotation isolation, Unicode paths and clean close")
        finally:
            for process in processes:
                if process.poll() is None:
                    try:
                        close(process)
                    except (RuntimeError, subprocess.TimeoutExpired):
                        process.kill()
                        process.wait(timeout=10)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--screenshots-directory", type=Path, required=True)
    arguments = parser.parse_args()
    probe(arguments.directory, arguments.screenshots_directory)
