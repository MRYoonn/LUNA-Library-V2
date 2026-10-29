#!/usr/bin/env python3
"""Exercise the production native-tab/library path using disposable PDFs only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import ctypes
from ctypes import wintypes
import time
import zipfile
from smoke_windows import synthetic_pdf


def verify_diagnostic_launcher(package, output, env):
    """Run the same local collector supplied to the user, with an empty reader."""
    destination = output / 'launcher-한글'
    powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    user = ctypes.WinDLL('user32', use_last_error=True)
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    env = dict(env, QT_SCALE_FACTOR='1')
    with (output / 'diagnostic-launcher.log').open('w', encoding='utf-8') as log:
        child = subprocess.Popen([str(powershell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(package / 'Start-Luna-Diagnostics.ps1'), '-NoPause', '-OutputDirectory', str(destination)], cwd=package, env=env, stdout=log, stderr=subprocess.STDOUT)
        windows = []
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline and child.poll() is None:
                trace = destination / 'lifecycle.jsonl'
                states = destination / 'process-state.jsonl'
                try:
                    rows = trace.read_text(encoding='utf-8-sig').splitlines()
                    pid = json.loads(rows[0])['pid']
                    samples = [json.loads(s) for s in states.read_text(encoding='utf-8-sig').splitlines()]
                    if len(samples) >= 2 and any(s['processAlive'] and s['windowVisible'] for s in samples):
                        @callback
                        def visit(hwnd, _):
                            owner = wintypes.DWORD()
                            user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
                            if owner.value == pid and user.IsWindowVisible(hwnd):
                                windows.append(hwnd)
                            return True
                        user.EnumWindows(visit, 0)
                        if windows:
                            break
                except (OSError, ValueError, IndexError, KeyError):
                    pass
                time.sleep(.1)
            if not windows:
                raise RuntimeError('Diagnostic launcher did not establish a live native child/window')
            for hwnd in windows:
                user.PostMessageW(hwnd, 0x10, 0, 0)  # Own empty fixture only.
            if child.wait(timeout=30) != 0:
                raise RuntimeError('Diagnostic collector failed after native child close')
            result = json.loads((destination / 'process-exit.json').read_text(encoding='utf-8-sig'))
            if result['exitCode'] != 0 or not result['shellApprovedExit'] or result['classification'] != 'shell-approved-exit' or result['pid'] != pid:
                raise RuntimeError('Diagnostic parent did not capture the actual approved child exit')
            archive = Path(str(destination) + '.zip')
            with zipfile.ZipFile(archive) as packaged:
                if set(packaged.namelist()) != {'build-info.json', 'lifecycle.jsonl', 'process-state.jsonl', 'process-exit.json'}:
                    raise RuntimeError('Diagnostic ZIP scope differs from the safe collector contract')
            return {'accepted': True, 'normalExitCaptured': True, 'liveWindowSamples': len(samples)}
        finally:
            if child.poll() is None:
                for hwnd in windows:
                    user.PostMessageW(hwnd, 0x10, 0, 0)
                child.terminate()


def run(package, output):
    if os.name != 'nt':
        raise RuntimeError('Actual Windows required')
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for scale in ('1', '1.25', '1.5'):
        evidence = output / ('scale-' + scale)
        evidence.mkdir()
        with tempfile.TemporaryDirectory(prefix='luna-tabs-integration-') as temporary:
            root = Path(temporary) / '한글 라이브러리'
            root.mkdir()
            (root / 'UI examples' / 'Research').mkdir(parents=True)
            (root / 'UI examples' / 'References').mkdir()
            a, b = root / 'A.pdf', root / 'B.pdf'
            a.write_bytes(synthetic_pdf(2).replace(b'smoke', b'alpha'))
            b.write_bytes(synthetic_pdf(3).replace(b'smoke', b'bravo'))
            original_b = hashlib.sha256(b.read_bytes()).hexdigest()
            for index, word in enumerate((b'delta', b'gamma', b'omega')):
                title = f'UI-long-{index + 1} 논문 제목 photochemical mechanisms and independent document state verification.pdf'
                (root / title).write_bytes(synthetic_pdf(2).replace(b'smoke', word))
            env = os.environ.copy()
            windows_root = Path(os.environ['SystemRoot'])
            env['PATH'] = os.pathsep.join(map(str, (package, windows_root / 'System32', windows_root)))
            env.update(QT_SCALE_FACTOR=scale, QT_QPA_PLATFORM='windows', QT_FORCE_STDERR_LOGGING='1',
                       QT_PLUGIN_PATH=str(package), QT_QPA_PLATFORM_PLUGIN_PATH=str(package / 'platforms'))
            with (evidence / 'native.log').open('w', encoding='utf-8') as log:
                reports = []
                for phase in ('edit', 'restore'):
                    report = evidence / (phase + '.json')
                    target = a if phase == 'edit' else root / 'Moved' / 'A.pdf'
                    command = [str(package / 'LUNA.exe'), '--luna-tabs-report', str(report)]
                    if phase == 'restore':
                        command += ['--luna-tabs-restore', str(target)]
                    else:
                        command += [str(a), str(b)]
                    try:
                        code = subprocess.run(command, cwd=package, env=env, stdout=log,
                                              stderr=subprocess.STDOUT, timeout=180, check=False).returncode
                    except subprocess.TimeoutExpired:
                        code = -1
                    result = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {'accepted': False, 'error': 'No native report'}
                    trace_path = Path(str(report) + '.lifecycle.jsonl')
                    events = [json.loads(line) for line in trace_path.read_text(encoding='utf-8').splitlines()] if trace_path.exists() else []
                    observations = [e for e in events if e['event'] == 'close-key-observed']
                    lifetime_ok = bool(observations) and all(e['nativeWindowVisible'] and e['nativeProcessActive'] and not e['closing'] for e in observations)
                    approved_quit = any(e['event'] == 'shell-close-approved' for e in events)
                    unexpected_quit = any(e['event'] == 'about-to-quit' and not e['closing'] for e in events)
                    result.update(nativeCloseKeyObservations=len(observations), nativeWindowAndProcessAlive=lifetime_ok,
                                  shellApprovedQuit=approved_quit, unexpectedAboutToQuit=unexpected_quit)
                    if result.get('accepted') and (not lifetime_ok or not approved_quit or unexpected_quit):
                        result.update(accepted=False, error='Native lifetime trace did not establish safe close/exit')
                    result.update(exitCode=code, qtScale=scale, processPhase=phase)
                    report.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
                    reports.append(result)
                    if code != 0 or result.get('accepted') is not True:
                        break
                untouched_b = hashlib.sha256(b.read_bytes()).hexdigest() == original_b
                results.append({'qtScale': scale, 'runs': reports, 'originalBUnchanged': untouched_b})
                if len(reports) != 2 or not untouched_b or any(r['exitCode'] != 0 or not r.get('accepted') for r in reports):
                    break
    accepted = len(results) == 3 and all(len(s['runs']) == 2 and s['originalBUnchanged'] and
        all(r['exitCode'] == 0 and r.get('accepted') is True for r in s['runs']) for s in results)
    diagnostic = {'accepted': False, 'scope': 'not run: preceding integration failed'}
    if accepted:
        try:
            diagnostic = verify_diagnostic_launcher(package, output, env)
        except Exception as error:
            accepted = False
            diagnostic = {'accepted': False, 'error': str(error)}
    (output / 'summary.json').write_text(json.dumps({'accepted': accepted, 'runs': results, 'diagnosticLauncher': diagnostic,
        'scope': 'production native tabs and one library; fresh EXE process restores moved PDF, crop and local memo; Qt scales are not physical monitor DPI'},
        ensure_ascii=False, indent=2), encoding='utf-8')
    if not accepted:
        raise RuntimeError('Production tabs/library integration failed; inspect native phase and screenshot')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('package', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    run(args.package.resolve(strict=True), args.output.resolve())
