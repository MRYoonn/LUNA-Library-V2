#!/usr/bin/env python3
"""Real Windows/UI Automation panel screenshots of explicit portable builds."""
import json
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import winreg
from pywinauto import Application

NAV = '라이브러리 접기 / 펼치기'
DOCS = '논문 목록 접기 / 펼치기'


def run(package, output):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='luna-panel-capture-') as root:
        # Disposable hosted Windows runner only; no existing library or PDFs.
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r'Software\LUNA\LUNA Library\library') as key:
            winreg.SetValueEx(key, 'root', 0, winreg.REG_SZ, root)
        for scale in ('1', '1.25', '1.5'):
            env = dict(os.environ, QT_SCALE_FACTOR=scale, LUNA_METADATA_OFFLINE='1')
            env.update(QT_QPA_PLATFORM='windows', QT_PLUGIN_PATH=str(package),
                       QT_QPA_PLATFORM_PLUGIN_PATH=str(package / 'platforms'))
            child = subprocess.Popen([str(package / 'LUNA.exe')], cwd=root, env=env)
            try:
                app = Application(backend='uia').connect(process=child.pid, timeout=30)
                win = app.top_window()
                win.wait('visible ready', timeout=30)
                move = ctypes.WinDLL('user32', use_last_error=True).MoveWindow
                move.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.BOOL]
                def resize(height):
                    if not move(win.handle, 16, 16, 1400, height, True):
                        raise ctypes.WinError(ctypes.get_last_error())
                resize(900)
                time.sleep(1)
                def buttons():
                    return [b for b in win.descendants(control_type='Button')
                            if b.is_visible() and b.window_text() in (NAV, DOCS)]
                def click(title):
                    found = [b for b in buttons() if b.window_text() == title]
                    if len(found) != 1:
                        raise RuntimeError('Expected one visible accessible panel control: '+title)
                    found[0].click_input()
                    time.sleep(.35)
                def rectangle(widget):
                    r = widget.rectangle()
                    return [r.left, r.top, r.right, r.bottom]
                rows=[]
                def capture(state):
                    name = scale+'-'+state
                    win.capture_as_image().save(output / (name+'.png'))
                    rows.append(dict(state=state, window=rectangle(win), controls=[
                        dict(name=b.window_text(), rectangle=rectangle(b)) for b in buttons()]))
                capture('both-open')
                click(NAV); capture('library-folded')
                click(DOCS); capture('both-folded')
                resize(800)
                time.sleep(.35); capture('both-folded-short')
                resize(900)
                click(NAV); capture('papers-folded')
                click(DOCS); capture('restored')
                (output / (scale+'-geometry.json')).write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
                win.close()
                if child.wait(timeout=20) != 0:
                    raise RuntimeError('Native capture process did not exit successfully')
            finally:
                if child.poll() is None:
                    child.terminate(); child.wait(timeout=15)


if __name__ == '__main__':
    if sys.platform != 'win32':
        raise SystemExit('Actual Windows required')
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
