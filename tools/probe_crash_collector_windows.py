"""Verify private dump capture on a disposable native AV fixture, never a PDF."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import time
import zipfile

def run(package, output):
    if os.name != 'nt':
        raise RuntimeError('Actual Windows required')
    output.mkdir(parents=True)
    report = output / 'capture-한글'
    powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    env = dict(os.environ, LUNA_SYNTHETIC_CRASH_FIXTURE='1')
    user = ctypes.WinDLL('user32',use_last_error=True)
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    user.EnumWindows.argtypes=[callback,wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    user.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
    with (output/'capture-launcher.log').open('w',encoding='utf-8') as log:
        capture=subprocess.Popen([str(powershell),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(package/'Start-Luna-Crash-Diagnostics.ps1'),'-NoPause','-OutputDirectory',str(report)],env=env,stdout=log,stderr=subprocess.STDOUT)
        deadline=time.monotonic()+90
        while time.monotonic()<deadline and capture.poll() is None:
            monitor=Path(str(report)+'-PRIVATE-CRASH')/'attachment-confirmed.json'
            states=report/'process-state.jsonl'
            if monitor.exists() and json.loads(monitor.read_text(encoding='utf-8-sig'))['debuggerAttached'] and states.exists() and len(states.read_text(encoding='utf-8-sig').splitlines())>=2:
                break
            time.sleep(.1)
        else:
            raise RuntimeError('Fixture debugger did not attach; inspect capture-launcher.log')
        pid=json.loads((report/'lifecycle.jsonl').read_text(encoding='utf-8-sig').splitlines()[0])['pid']
        windows=[]
        @callback
        def visit(hwnd,_):
            owner=wintypes.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            name=ctypes.create_unicode_buffer(256);user.GetClassNameW(hwnd,name,len(name))
            if owner.value==pid and name.value=='LunaCollectorSyntheticFixture':windows.append(hwnd)
            return True
        user.EnumWindows(visit,0)
        if not windows:raise RuntimeError('Own disposable fixture window not found')
        user.PostMessageW(windows[0],0x8001,0,0)  # Only this fixture's deliberate AV.
        if capture.wait(timeout=90)!=0:raise RuntimeError('Private dump collector failed')
    result=json.loads((report/'process-exit.json').read_text(encoding='utf-8-sig'))
    if result['exitCodeHex']!='C0000005' or result['shellApprovedExit']:raise RuntimeError('Collector did not establish the actual AV exit')
    private=Path(str(report)+'-PRIVATE-CRASH'); dump=private/'LUNA-private.dmp'
    with dump.open('rb') as file:
        if file.read(4)!=b'MDMP':raise RuntimeError('Actual native minidump missing')
    with zipfile.ZipFile(str(report)+'.zip') as archive:
        if set(archive.namelist())!={'build-info.json','lifecycle.jsonl','process-state.jsonl','process-exit.json'}:raise RuntimeError('Private data leaked into regular diagnostic ZIP')
    with (output/'analysis-launcher.log').open('w',encoding='utf-8') as log:
        subprocess.run([str(powershell),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(package/'Analyze-Luna-Crash.ps1'),'-NoPause','-DumpFile',str(dump)],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    stack=(private/'stack-summary.txt').read_text(encoding='utf-8-sig')
    if 'fixtureCrash' not in stack or 'MODULE_NAME: LUNA' not in stack or 'IMAGE_NAME: LUNA.exe' not in stack or 'FAULT_MODULE_OFFSET: 0x' not in stack or 'MODULE_NAME: mov' in stack:
        raise RuntimeError('Actual native fault function/module/offset missing or misidentified in local stack summary')
    (output/'collector-summary.json').write_text(json.dumps({'accepted':True,'actualExitCode':result['exitCodeHex'],'nativeDumpCreated':True,'faultFunction':'fixtureCrash','safeZipExcludesDump':True,'scope':'synthetic native crash capture/analysis only; no PDF input; actual user-PC P0 remains unresolved'},indent=2),encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('package',type=Path);p.add_argument('output',type=Path);a=p.parse_args();run(a.package.resolve(strict=True),a.output.resolve())
