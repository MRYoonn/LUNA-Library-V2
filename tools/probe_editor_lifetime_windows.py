"""Replay the actual-PC library-open/first-Editor destruction sequence externally.

Runs the supplied production EXE, no internal acceptance mode and no real PDFs.
The full production regression remains in its existing build workflow.
"""
import argparse
import base64
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
from smoke_windows import synthetic_pdf

def run(package, output, debugger, legacy_state=None, expect_layout_crash=False, state_reader=None):
    if os.name!='nt':raise RuntimeError('Actual Windows required')
    import winreg
    output.mkdir(parents=True,exist_ok=True)
    user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    user.EnumWindows.argtypes=[callback,wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    for name in ('IsWindowVisible','SetForegroundWindow','IsWindowEnabled'):
        getattr(user,name).argtypes=[wintypes.HWND]
    user.GetWindowRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
    user.GetForegroundWindow.restype=wintypes.HWND
    user.ClientToScreen.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.POINT)]
    user.SetCursorPos.argtypes=[ctypes.c_int,ctypes.c_int]
    user.ShowWindow.argtypes=[wintypes.HWND,ctypes.c_int]
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    user.keybd_event.argtypes=[wintypes.BYTE,wintypes.BYTE,wintypes.DWORD,ctypes.c_size_t]
    user.mouse_event.argtypes=[wintypes.DWORD,wintypes.DWORD,wintypes.DWORD,wintypes.DWORD,ctypes.c_size_t]
    def windows(pid):
        found=[]
        @callback
        def visit(hwnd,_):
            owner=wintypes.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if owner.value==pid and user.IsWindowVisible(hwnd):
                rectangle=wintypes.RECT();user.GetWindowRect(hwnd,ctypes.byref(rectangle))
                found.append(((rectangle.right-rectangle.left)*(rectangle.bottom-rectangle.top),hwnd))
            return True
        user.EnumWindows(visit,0);return sorted(found,reverse=True)
    def key(code,control=False):
        if control:user.keybd_event(0x11,0,0,0)
        user.keybd_event(code,0,0,0);user.keybd_event(code,0,2,0)
        if control:user.keybd_event(0x11,0,2,0)
    def capture(path,env):
        powershell=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
        code="Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing; $r=[Windows.Forms.SystemInformation]::VirtualScreen; $b=New-Object Drawing.Bitmap($r.Width,$r.Height); $g=[Drawing.Graphics]::FromImage($b); $g.CopyFromScreen($r.Left,$r.Top,0,0,$b.Size); $b.Save($env:LUNA_CAPTURE_PNG); $g.Dispose(); $b.Dispose()"
        subprocess.run([str(powershell),'-NoProfile','-Command',code],env=dict(env,LUNA_CAPTURE_PNG=str(path)),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15)
    # The hosted disposable runner only: preserve/restore the exact settings we
    # temporarily select. No actual user's registry or personal library is used.
    settings=[]
    for group,name in (('library','root'),('library','splitter'),('window','geometry'),('window','state')):
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER,'Software\\LUNA\\LUNA Library\\'+group) as store:
            try:previous=winreg.QueryValueEx(store,name)
            except FileNotFoundError:previous=None
            settings.append((group,name,previous))
    results=[]
    try:
        for scale in (1,1.25,1.5):
            for repeat in range(2):
                case=output/f'scale-{scale}-case-{repeat+1}';case.mkdir()
                with tempfile.TemporaryDirectory(prefix='luna-lifetime-') as temporary:
                    root=Path(temporary)/'Synthetic Library';root.mkdir()
                    for letter,word in zip('ABC',(b'alpha',b'bravo',b'delta')):
                        (root/(letter+'.pdf')).write_bytes(synthetic_pdf(2).replace(b'smoke',word))
                    for group,name,_ in settings:
                        with winreg.CreateKey(winreg.HKEY_CURRENT_USER,'Software\\LUNA\\LUNA Library\\'+group) as store:
                            try:winreg.DeleteValue(store,name)
                            except FileNotFoundError:pass
                            if name=='root':winreg.SetValueEx(store,name,0,winreg.REG_SZ,str(root))
                            if name=='state' and legacy_state:
                                state=base64.b64decode(json.loads(legacy_state.read_text(encoding='utf-8-sig'))['stateBase64'])
                                # QSettings NativeFormat stores a QByteArray as
                                # a UTF-16 @ByteArray(...) variant, not raw bytes.
                                serialized=('@ByteArray('+state.decode('latin1')+')').encode('utf-16-le')
                                winreg.SetValueEx(store,name,0,winreg.REG_BINARY,serialized)
                    if legacy_state:
                        if not state_reader:raise RuntimeError('Native QSettings verifier required for legacy-state replay')
                        subprocess.run([str(state_reader),'--verify-legacy'],check=True,timeout=15)
                    env=dict(os.environ,QT_QPA_PLATFORM='windows',QT_SCALE_FACTOR=str(scale),QT_FORCE_STDERR_LOGGING='1')
                    system=Path(os.environ['SystemRoot']);env['PATH']=os.pathsep.join(map(str,(package,system/'System32',system)))
                    env.update(QT_PLUGIN_PATH=str(package),QT_QPA_PLATFORM_PLUGIN_PATH=str(package/'platforms'))
                    trace=case/'lifecycle.jsonl';debug_log=case/'cdb.log'
                    commands='sxd -c2 ".echo LUNA_UNHANDLED_ACCESS_VIOLATION;.exr -1;.ecxr;kv;~*kb;lm;q" av;sxe -c ".echo LUNA_HEAP_BREAK;kv;~*kb;q" bpe;sxe -c ".echo LUNA_DEBUGGEE_EXIT;.lastevent;q" epr;g'
                    result={'accepted':False,'qtScale':scale,'case':repeat+1,'input':'external Win32 keys and library-card double clicks; normal product EXE, no self-test mode','p0ActualPc':'UNRESOLVED'}
                    result['legacyWindowStateSeeded']=bool(legacy_state)
                    child=None;main=None
                    def rows():
                        events=[]
                        if trace.exists():
                            for line in trace.read_text(encoding='utf-8-sig').splitlines():
                                try:events.append(json.loads(line))
                                except ValueError:pass
                        return events
                    def wait(predicate,description,timeout=25):
                        deadline=time.monotonic()+timeout
                        while time.monotonic()<deadline:
                            if predicate():return
                            if child.poll() is not None:raise RuntimeError('Debuggee exited while '+description)
                            time.sleep(.03)
                        raise RuntimeError('Timed out while '+description)
                    def event(name,editor=None,generation=None):
                        return any(r['event']==name and (editor is None or r.get('editor')==editor) and (generation is None or r.get('loadGeneration')==generation) for r in rows())
                    with (case/'native.log').open('w',encoding='utf-8') as log:
                        try:
                            child=subprocess.Popen([str(debugger),'-G','-logo',str(debug_log),'-c',commands,str(package/'LUNA.exe'),'--luna-lifecycle-trace',str(trace)],cwd=package,env=env,stdout=log,stderr=subprocess.STDOUT)
                            # Cold CDB symbols/full page heap can delay native
                            # startup beyond 25s; still require real lifecycle
                            # evidence before sending any input or accepting it.
                            wait(lambda:bool(rows()),'initial lifecycle',timeout=120)
                            pid=rows()[0]['pid'];result['pid']=pid
                            wait(lambda:bool(windows(pid)),'visible main window')
                            main=windows(pid)[0][1];user.ShowWindow(main,3);user.SetForegroundWindow(main)
                            wait(lambda:user.GetForegroundWindow()==main,'foreground ownership')
                            def open_library(letter,editor,generation):
                                result['phase']='library open '+letter
                                key(0x4b,True);time.sleep(.1);key(0x41,True);key(ord(letter));time.sleep(.2)
                                point=wintypes.POINT(round(350*scale),round(235*scale));user.ClientToScreen(main,ctypes.byref(point));user.SetCursorPos(point.x,point.y)
                                for _ in range(2):user.mouse_event(2,0,0,0,0);user.mouse_event(4,0,0,0,0);time.sleep(.07)
                                wait(lambda:event('document-load-completed',editor,generation),'library load '+letter)
                                loaded=[r for r in rows() if r['event']=='document-load-completed' and r.get('editor')==editor][-1]
                                if loaded.get('editorOpenOrigin')!='library-open':raise RuntimeError('Document did not open through the actual library route')
                                wait(lambda:any(r.get('editor')==editor and r.get('loadGeneration')==generation and r.get('document') and r.get('loading') is False for r in rows()),'post-load native document state')
                                # Load-finished is emitted before queued native
                                # progress/UI callbacks settle under page heap.
                                time.sleep(1)
                            open_library('A',1,1)
                            if repeat:
                                key(0x46,True);wait(lambda:len(windows(pid))>1,'first Editor search popup')
                            key(0x57,True);wait(lambda:event('last-document-cleared',1),'initial last-document clear')
                            open_library('A',1,2);open_library('B',2,1);open_library('C',3,1)
                            capture(case/'three-library-documents.png',env)
                            result['phase']='close third then initial Editor'
                            key(0x57,True);wait(lambda:any(r['event']=='editor-destroyed' and r.get('destroyedEditor')==3 for r in rows()),'third Editor destruction')
                            time.sleep(.12);key(0x57,True)
                            wait(lambda:any(r['event']=='editor-destroyed' and r.get('destroyedEditor')==1 for r in rows()),'initial Editor destruction')
                            time.sleep(.4)
                            if child.poll() is not None:raise RuntimeError('Debuggee exited after first Editor destruction')
                            if not any(r.get('activeEditor')==2 and r.get('documents')==1 for r in rows()[-8:]):raise RuntimeError('Wrong survivor after two real Ctrl+W keys')
                            key(0x46,True);wait(lambda:len(windows(pid))>1,'surviving Editor search popup')
                            for character in 'BRAVO':key(ord(character))
                            key(0x57,True);wait(lambda:event('last-document-cleared',2),'search-focused survivor close')
                            before=max(r.get('creations',0) for r in rows())
                            for _ in range(10):key(0x57,True);time.sleep(.07)
                            if max(r.get('creations',0) for r in rows())!=before:raise RuntimeError('Empty reader recreated Editors')
                            result['phase']='approved Ctrl+Q exit';key(0x51,True);child.wait(timeout=30)
                            transcript=debug_log.read_text(errors='replace')
                            exits=re.findall(r'Exit process[^\r\n]*?\bcode\s+([0-9a-fA-Fx]+)',transcript,re.IGNORECASE)
                            fault=bool(re.search(r'(?m)^LUNA_(?:UNHANDLED_ACCESS_VIOLATION|HEAP_BREAK)\s*$',transcript))
                            if not exits or any(int(code,16)!=0 for code in exits) or fault or not event('shell-close-approved'):raise RuntimeError('Debugger did not establish a fault-free approved exit')
                            result.update(accepted=True,phase='passed',debuggeeExitCodes=exits,firstEditorDestroyed=True,survivorSearchThenClose=True,tenEmptyKeysStable=True)
                        except Exception as error:
                            result['error']=str(error)
                            capture(case/'failure.png',env)
                        finally:
                            if child and child.poll() is None:
                                if main:user.PostMessageW(main,0x10,0,0)
                                try:child.wait(timeout=30)
                                except subprocess.TimeoutExpired:child.kill();child.wait()
                    if expect_layout_crash and debug_log.exists():
                        transcript=debug_log.read_text(errors='replace')
                        result['baselineLayoutCrashReproduced']=bool(re.search(r'(?m)^LUNA_UNHANDLED_ACCESS_VIOLATION\s*$',transcript) and 'QWidgetItem::isEmpty' in transcript and any(r['event']=='editor-destroyed' and r.get('destroyedEditor')==1 for r in rows()))
                    (case/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');results.append(result)
                    if not result['accepted']:break
            if results and not results[-1]['accepted']:break
    finally:
        for group,name,previous in settings:
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER,'Software\\LUNA\\LUNA Library\\'+group) as store:
                if previous is None:
                    try:winreg.DeleteValue(store,name)
                    except FileNotFoundError:pass
                else:winreg.SetValueEx(store,name,0,previous[1],previous[0])
    accepted=len(results)==6 and all(r['accepted'] for r in results)
    reproduced=bool(expect_layout_crash and len(results)==1 and results[0].get('baselineLayoutCrashReproduced'))
    (output/'summary.json').write_text(json.dumps({'accepted':accepted and not expect_layout_crash,'baselineLayoutCrashReproduced':reproduced,'runs':results,'scope':'focused synthetic library-open lifecycle replay under CDB/page heap; does not resolve actual-PC AV'},indent=2),encoding='utf-8')
    if expect_layout_crash:
        if not reproduced:raise RuntimeError('Exact baseline layout crash was not reproduced')
        return
    if not accepted:raise RuntimeError('Focused lifetime replay failed; inspect native CDB evidence')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('package',type=Path);p.add_argument('output',type=Path);p.add_argument('--debugger',type=Path,required=True)
    p.add_argument('--legacy-state',type=Path);p.add_argument('--expect-layout-crash',action='store_true');p.add_argument('--state-reader',type=Path)
    a=p.parse_args();run(a.package.resolve(strict=True),a.output.resolve(),a.debugger.resolve(strict=True),a.legacy_state,a.expect_layout_crash,a.state_reader.resolve(strict=True) if a.state_reader else None)
