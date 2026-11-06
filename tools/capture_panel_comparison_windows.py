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

NAV = '라이브러리 접기 / 펼치기'
DOCS = '논문 목록 접기 / 펼치기'


def panel_state_failures(row, original, scale, fixed_top=False):
    controls = {b['name']: b['rectangle'] for b in row['controls']}
    initial = {b['name']: b['rectangle'] for b in original['controls']}
    if set(controls) != {NAV, DOCS} or len(row['controls']) != 2:
        return ['Expected exactly two reachable panel controls']
    nav_folded = row['state'] in ('library-folded', 'both-folded', 'both-folded-short')
    docs_folded = row['state'] in ('papers-folded', 'both-folded', 'both-folded-short')
    failures = []
    # A visible UIA object can still be clipped outside its scroll viewport.
    # Require the actual click to move each control into its original rail.
    nav_offset = controls[NAV][0] - row['window'][0]
    if nav_folded:
        if not 0 <= nav_offset < 60 * scale:
            failures.append('Library click did not fold the navigation into its left rail')
    elif abs(controls[NAV][0] - initial[NAV][0]) > 1:
        failures.append('Library did not restore its original width')
    docs_shift = initial[DOCS][0] - controls[DOCS][0]
    if nav_folded and docs_folded:
        if not 0 < controls[DOCS][0] - row['window'][0] < 105 * scale:
            failures.append('Both folded controls did not occupy the two adjacent left rails')
    elif nav_folded or docs_folded:
        if docs_shift < 100 * scale:
            failures.append('Paper control did not move after the requested independent fold')
    elif abs(docs_shift) > 1:
        failures.append('Papers did not restore their original width')
    if fixed_top and any(controls[name][1] != initial[name][1] for name in (NAV, DOCS)):
        failures.append('A restore control moved away from its original top Y')
    return failures


def run(package, output, fixed_top=False):
    import winreg
    from pywinauto import Application
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
                    return [b for b in win.descendants()
                            if b.is_visible() and b.element_info.name in (NAV, DOCS)]
                def click(title):
                    found = [b for b in buttons() if b.element_info.name == title]
                    if len(found) != 1:
                        details = [dict(name=b.element_info.name, type=b.element_info.control_type) for b in win.descendants() if b.is_visible()]
                        (output / ('uia-'+scale+'.json')).write_text(json.dumps(details,ensure_ascii=False,indent=2),encoding='utf-8')
                        raise RuntimeError('Expected one visible accessible panel control: '+title)
                    found[0].click_input()
                    time.sleep(.35)
                def rectangle(widget):
                    r = widget.rectangle()
                    return [r.left, r.top, r.right, r.bottom]
                rows=[]
                def capture(state):
                    name = scale+'-'+state
                    row = dict(state=state, window=rectangle(win), controls=[
                        dict(name=b.element_info.name, rectangle=rectangle(b)) for b in buttons()])
                    win.capture_as_image().save(output / (name+'.png'))
                    rows.append(row)
                    # Persist failure geometry too; filenames alone do not prove
                    # that the requested state was reached by real mouse input.
                    (output / (scale+'-geometry.json')).write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
                    failures = panel_state_failures(row, rows[0], float(scale), fixed_top)
                    if failures:
                        raise RuntimeError('; '.join(failures))
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
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), '--fixed-top' in sys.argv[3:])
