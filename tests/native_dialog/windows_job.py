#!/usr/bin/env python3
"""Real Windows containment test using only disposable Python child processes."""
import ctypes
from ctypes import wintypes
from pathlib import Path
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from probe_tab_host_windows import SyntheticJob


def main():
    if os.name != "nt":
        raise RuntimeError("This probe requires Windows job objects")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    parent_code = "import subprocess,sys,time; p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)']); print(p.pid,flush=True); time.sleep(60)"
    for crash_parent in (False, True):
        job = SyntheticJob()
        parent = None
        child = None
        try:
            parent = job.start([sys.executable, "-c", parent_code], stdout=subprocess.PIPE, text=True)
            child_pid = int(parent.stdout.readline())
            child = kernel.OpenProcess(0x100000, False, child_pid)  # SYNCHRONIZE, original process handle.
            if not child or kernel.WaitForSingleObject(child, 0) != 258:
                raise RuntimeError("Synthetic child did not remain alive")
            if crash_parent:
                parent.kill()
                parent.wait(timeout=5)
                if kernel.WaitForSingleObject(child, 0) != 258:
                    raise RuntimeError("Could not reproduce a surviving orphan child")
            job.close()
            parent.wait(timeout=5)
            if kernel.WaitForSingleObject(child, 5000) != 0:
                raise RuntimeError("Job cleanup left an owned child alive")
        finally:
            job.close()
            if parent is not None:
                parent.wait(timeout=5)
            if child:
                kernel.CloseHandle(child)
    print("[PASS] Windows synthetic job cleans up parent/child and surviving child after parent crash")


if __name__ == "__main__":
    main()
