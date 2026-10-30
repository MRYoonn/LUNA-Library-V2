"""Same-machine before/after opens using native dialogs and compiled-page paint events.

Only generated PDFs and the checksum-pinned CC BY fixture are used. Neither
document bytes nor user documents are included in the output.
"""
import ctypes
from ctypes import wintypes as w
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time
from urllib.request import Request, urlopen
from smoke_windows import synthetic_pdf
from probe_research_windows import URL, SHA256, CREDIT


def stats(values):
    values = sorted(values)
    return {'n': len(values), 'p50Ms': statistics.median(values),
            'p95Ms': values[math.ceil(.95 * len(values)) - 1], 'maxMs': max(values)}


def run(before, after, output):
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.SetProcessDPIAware()
    callback = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.SetForegroundWindow.argtypes = [w.HWND]
    user.GetForegroundWindow.restype = w.HWND
    class Keyboard(ctypes.Structure):
        _fields_ = [('vk', w.WORD), ('scan', w.WORD), ('flags', w.DWORD), ('time', w.DWORD), ('extra', ctypes.c_size_t)]
    class Mouse(ctypes.Structure):
        _fields_ = [('x', w.LONG), ('y', w.LONG), ('data', w.DWORD), ('flags', w.DWORD), ('time', w.DWORD), ('extra', ctypes.c_size_t)]
    class Payload(ctypes.Union):
        _fields_ = [('key', Keyboard), ('mouse', Mouse)]
    class Input(ctypes.Structure):
        _fields_ = [('kind', w.DWORD), ('payload', Payload)]
    user.SendInput.argtypes = [w.UINT, ctypes.POINTER(Input), ctypes.c_int]

    def send(keys):
        inputs = (Input * len(keys))()
        for entry, (vk, scan, flags) in zip(inputs, keys):
            entry.kind = 1
            entry.payload.key = Keyboard(vk, scan, flags, 0, 0)
        if user.SendInput(len(inputs), inputs, ctypes.sizeof(Input)) != len(inputs):
            raise RuntimeError('Native keyboard input rejected')

    def key(vk, control=False):
        send(([(0x11, 0, 0)] if control else []) + [(vk, 0, 0), (vk, 0, 2)] + ([(0x11, 0, 2)] if control else []))

    def window(pid, dialog=False):
        found = []
        @callback
        def visit(hwnd, _):
            owner = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid and user.IsWindowVisible(hwnd):
                name = ctypes.create_unicode_buffer(128)
                user.GetClassNameW(hwnd, name, len(name))
                if (name.value == '#32770') == dialog:
                    found.append(hwnd)
            return True
        user.EnumWindows(visit, 0)
        return found[0] if found else None

    def title(hwnd):
        text = ctypes.create_unicode_buffer(1024)
        user.GetWindowTextW(hwnd, text, len(text))
        return text.value

    def wait(predicate, child, seconds=60):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            if child.poll() is not None:
                raise RuntimeError(f'Benchmark process exited: {child.returncode}')
            time.sleep(.025)
        raise RuntimeError('Native benchmark timed out; preserve log for diagnosis')

    def events(path):
        result = []
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            if 'LUNA_PERF {' in line:
                try:
                    result.append(json.loads(line[line.index('{'):]))
                except ValueError:
                    pass  # A line may be in flight while polling.
        return result

    def paints(path):
        return [event for event in events(path) if event['stage'] == 'request-to-first-page-paint']

    output.mkdir(parents=True, exist_ok=True)
    with urlopen(Request(URL, headers={'User-Agent': 'LUNA-public-fixture-validation'}), timeout=30) as response:
        paper = response.read(2_000_001)
    if hashlib.sha256(paper).hexdigest() != SHA256:
        raise RuntimeError('Pinned public paper checksum mismatch')
    (output / 'PUBLIC_PAPER_CREDIT.txt').write_text(CREDIT, encoding='utf-8')
    results = []
    with tempfile.TemporaryDirectory(prefix='luna-open-benchmark-') as temp:
        root = Path(temp)
        paths = []
        for i in range(5):
            path = root / f'fixture-{i}.pdf'
            path.write_bytes(paper if i % 2 else synthetic_pdf(8))
            paths.append(path)
        workloads = {'repeat-synthetic-close-reopen': [paths[0]] * 10,
                     'repeat-public-paper-close-reopen': [paths[1]] * 10,
                     'sequential-mixed-new-tabs': paths}
        hashes = [hashlib.sha256(path.read_bytes()).hexdigest() for path in paths]
        for workload, documents in workloads.items():
            for label, package in (('before', before), ('after', after)):
                log_path = output / f'{label}-{workload}.log'
                env = dict(os.environ, LUNA_PERF='1', QT_FORCE_STDERR_LOGGING='1', QT_SCALE_FACTOR='1',
                           QT_QPA_PLATFORM='windows', QT_PLUGIN_PATH=str(package),
                           QT_QPA_PLATFORM_PLUGIN_PATH=str(package / 'platforms'))
                env.pop('LUNA_UI_AUDIT', None)
                with log_path.open('w', encoding='utf-8') as log:
                    started = time.monotonic()
                    child = subprocess.Popen([str(package / 'LUNA.exe'), str(documents[0])], cwd=package, env=env, stdout=log, stderr=subprocess.STDOUT)
                    try:
                        wait(lambda: len(paints(log_path)) >= 1, child)
                        startup = round((time.monotonic() - started) * 1000)
                        for index, path in enumerate(documents[1:], 2):
                            hwnd = wait(lambda: window(child.pid), child)
                            user.SetForegroundWindow(hwnd)
                            wait(lambda: user.GetForegroundWindow() == hwnd, child)
                            if workload.startswith('repeat-'):
                                key(ord('W'), True)
                                wait(lambda: path.name not in title(hwnd), child)
                            key(ord('O'), True)
                            dialog = wait(lambda: window(child.pid, True), child)
                            wait(lambda: user.GetForegroundWindow() == dialog, child)
                            send([(0x12, 0, 0), (ord('N'), 0, 0), (ord('N'), 0, 2), (0x12, 0, 2)])
                            key(ord('A'), True)
                            encoded = str(path).encode('utf-16-le')
                            for offset in range(0, len(encoded), 2):
                                code = int.from_bytes(encoded[offset:offset+2], 'little')
                                send([(0, code, 4), (0, code, 6)])
                            key(0x0D)
                            wait(lambda: len(paints(log_path)) >= index, child)
                        hwnd = wait(lambda: window(child.pid), child)
                        user.SetForegroundWindow(hwnd)
                        wait(lambda: user.GetForegroundWindow() == hwnd, child)
                        key(ord('Q'), True)
                        if child.wait(timeout=15) != 0:
                            raise RuntimeError('Nonzero benchmark close')
                    finally:
                        if child.poll() is None:
                            child.terminate()
                            child.wait(timeout=10)
                stages = {}
                for event in events(log_path):
                    stages.setdefault(event['stage'], []).append(event['durationMs'])
                result = {'build': label, 'workload': workload, 'processStartToFirstPaintMs': startup,
                          'stages': {stage: stats(values) for stage, values in stages.items()}}
                results.append(result)
                (output / 'summary.json').write_text(json.dumps({'results': results}, indent=2), encoding='utf-8')
                print(json.dumps(result))
        if hashes != [hashlib.sha256(path.read_bytes()).hexdigest() for path in paths]:
            raise RuntimeError('Benchmark changed an original fixture')


if __name__ == '__main__':
    run(*(Path(arg).resolve() for arg in sys.argv[1:]))
