"""Reproduce the vector-grid problem on the real packaged LUNA.exe (Windows only).

The PDF fixture is purely synthetic. Detect ICC Gray vs DeviceGray strokes in
native Qt screenshots; always retain screenshot and JSON even on failure.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
from PIL import Image


def gray_ink(image, point, radius):
    x,y=map(round,point)
    sample=image.crop((x-radius,y-radius,x+radius+1,y+radius+1)).convert('RGB')
    return sum((255-(r+g+b)/3)/255 for r,g,b in sample.getdata()
               if max(r,g,b)-min(r,g,b)<=12 and 100<=(r+g+b)/3<254)


def measure_native_grid(image, geometry, case):
    from probe_synthetic_grid import visible
    a,b,c,d,tx,ty=geometry['pageToWindow']
    radius=math.ceil(geometry['zoom']*.75)+1
    checks=[]
    for color,start in [('icc',45),('device',165)]:
        points=[(x,y) for x in range(start,start+81,20) for y in (45,65,85,105)]
        points += [(x,y) for y in (35,55,75,95,115) for x in range(start+10,start+80,20)]
        for x,y in points:
            expected=visible(case,x,y)
            if expected is None:continue
            pixel=(a*x+c*y+tx,b*x+d*y+ty)
            if not radius<=pixel[0]<image.width-radius or not radius<=pixel[1]<image.height-radius:
                raise RuntimeError('Expected grid sample is outside the actual native screenshot')
            ink=gray_ink(image,pixel,radius)
            checks.append(dict(color=color,pdf=[x,y],pixel=pixel,expectedVisible=expected,
                               grayInk=ink,passed=(ink>.02 if expected else ink<.01)))
    if case=='saved-restore':
        x,y=280,25
        pixel=(round(a*x+c*y+tx),round(b*x+d*y+ty))
        r,g,blue=image.convert('RGB').getpixel(pixel)
        checks.append(dict(restoredBlueMarker=True,pixel=pixel,passed=blue>220 and r<30 and g<30))
    return dict(samples=checks,passed=bool(checks) and all(c['passed'] for c in checks))


def native_zoom_matrix(executable,directory,env):
    from probe_synthetic_grid import build,extended,EXTENDED_CASES
    original=[('short-icc-no-clip',{}),('long-icc-no-clip',{'long_icc':True}),
              ('short-icc-clip-only',{'clip_only':True}),('short-icc-ctm-only',{'ctm_only':True}),
              ('short-icc-nested-clip',{'nested_clip':True}),
              ('long-icc-nested-clip',{'long_icc':True,'nested_clip':True})]
    fixtures=[(name,build(**options)) for name,options in original]
    fixtures += [(name,extended(name)) for name in EXTENDED_CASES]
    results=[]
    for name,data in fixtures:
        for zoom in (1.0,1.5,2.0):
            folder=directory/'native-matrix'/name/str(zoom)
            folder.mkdir(parents=True)
            pdf=folder/'synthetic-grid.pdf';pdf.write_bytes(data)
            report=folder/'render.json';local_env=dict(env,LUNA_GRID_ZOOM=str(zoom),LUNA_METADATA_OFFLINE='1')
            # Keep the legacy six-case trace. New matrix captures real desktop
            # pixels without tracing, so diagnostics cannot mask the regression.
            local_env.pop('LUNA_PDF_RENDER_TRACE',None)
            if zoom==2.0:local_env['LUNA_GRID_COMPARE_ENGINES']='1'
            completed=subprocess.run([str(executable.resolve()),'--luna-grid-report',str(report),str(pdf)],
                cwd=folder,env=local_env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                text=True,errors='replace',timeout=90)
            (folder/'process.log').write_text(completed.stdout,encoding='utf-8')
            if completed.returncode or not report.exists():
                raise RuntimeError(f'Actual top-level LUNA grid capture failed: {name}/{zoom}, code={completed.returncode}')
            if not pdf.exists() or hashlib.sha256(pdf.read_bytes()).hexdigest()!=hashlib.sha256(data).hexdigest():
                raise RuntimeError('Read-only render probe changed its original PDF')
            geometry=json.loads(report.read_text(encoding='utf-8-sig'))
            if (geometry['zoom']!=zoom or geometry['devicePixelRatio']!=1 or
                not geometry['nativeWindowCapture'] or not geometry['compiledReady'] or not geometry['blendBackend']):
                raise RuntimeError('Wrong native zoom/window geometry')
            image=Image.open(str(report)+'.native.png')
            result=measure_native_grid(image,geometry,name)
            if zoom==2.0:
                reference_geometry={'zoom':zoom,'pageToWindow':[zoom,0,0,-zoom,0,220*zoom]}
                comparisons={}
                for mode in ('direct-qt','compiled-qt','direct-blend','compiled-blend'):
                    comparisons[mode]=measure_native_grid(Image.open(str(report)+'.'+mode+'.png'),reference_geometry,name)
                result['engineComparisons']=comparisons
                result['passed'] = result['passed'] and all(v['passed'] for v in comparisons.values())
            result.update(case=name,zoom=zoom,geometry=geometry)
            results.append(result)
            # Save every result before an assertion, including all mismatches.
            (directory/'native-grid-matrix.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
            print('native-window',name,zoom,'passed',result['passed'],flush=True)
    if not all(r['passed'] for r in results):
        raise RuntimeError('Actual native grid/clip-hole/empty-clip/restore/alpha pixel assertions failed')
    print('[PASS] 36 actual native Windows captures at 100/150/200% with positive and negative clip samples')


def classify(image: Image.Image) -> dict:
    # In the canonical 1366x768 LUNA smoke window the 300x220 pt PDF is at
    # 200% zoom. The crop starts near x=740 and y=145, giving two grids in
    # the displayed paper region: ICC (x=45..125), Device (x=165..245).
    # Count thin gray line pixels only; reject white background and black ink.
    result = {}
    for label, box in (
        ("icc", (824, 348, 1006, 546)),
        ("device", (1060, 348, 1246, 546)),
    ):
        sample = image.crop(box).convert("RGB")
        count = 0
        for red, green, blue in sample.getdata():
            average = (red + green + blue) / 3
            if 130 <= average <= 235 and max(red, green, blue) - min(red, green, blue) <= 12:
                count += 1
        result[label + "_gray_pixels"] = count
        result[label + "_crop"] = box
    return result


def main(executable: Path, directory: Path) -> None:
    if sys.platform != "win32":
        raise RuntimeError("Requires real Windows, not offscreen approximation")
    directory.mkdir(parents=True, exist_ok=True)
    from probe_synthetic_grid import build
    env = os.environ.copy()
    package = executable.parent.resolve()
    windows = Path(os.environ["SystemRoot"])
    env["PATH"] = os.pathsep.join([str(package), str(windows / "System32"), str(windows)])
    env["QT_PLUGIN_PATH"] = str(package)
    env["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(package / "platforms")
    env["QT_QPA_PLATFORM"] = "windows"
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    env["QT_SCALE_FACTOR"] = "1"
    env["QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES"] = "1"
    cases = [
        ("short-icc-no-clip", False, False, False, False),
        ("long-icc-no-clip", True, False, False, False),
        ("short-icc-clip-only", False, False, True, False),
        ("short-icc-ctm-only", False, False, False, True),
        ("short-icc-nested-clip", False, True, False, False),
        ("long-icc-nested-clip", True, True, False, False),
    ]
    results = {}
    for name, long_icc, nested_clip, clip_only, ctm_only in cases:
        folder = directory / name
        folder.mkdir()
        pdf = folder / "synthetic-grid.pdf"
        pdf.write_bytes(build(long_icc=long_icc, nested_clip=nested_clip, clip_only=clip_only, ctm_only=ctm_only))
        report = folder / "grid-smoke.txt"
        env["LUNA_PDF_RENDER_TRACE"] = str(folder / "render-trace.jsonl")
        command = [
            str(executable.resolve()), "--luna-smoke-report", str(report),
            "--luna-layout-only", str(pdf)
        ]
        completed = subprocess.run(command, cwd=folder, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, errors="replace", timeout=90)
        (folder / "luna-process.log").write_text(completed.stdout, encoding="utf-8")
        if (completed.returncode != 0 or not report.exists()
                or report.read_bytes() != b"LUNA_SMOKE_LAYOUT_OK\n"):
            raise RuntimeError(f"Native UI rendering smoke failed on {name} with code {completed.returncode}")
        image_path = Path(str(report) + ".png")
        if not image_path.is_file():
            raise RuntimeError(f"Missing real Qt screenshot for {name}")
        screenshot = Image.open(image_path).convert("RGB")
        result = classify(screenshot)
        results[name] = {"long_icc": long_icc, "nested_clip": nested_clip,
                         "pixel_metrics": result}
        print(name, json.dumps(result), flush=True)
    summary = {"source": "synthetic non-user data; gray 2.2 gamma profile",
               "luna_binary": str(executable.name), "cases": results}
    (directory / "grid-matrix.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    if os.getenv("LUNA_REQUIRE_GRID_VISIBILITY") == "1":
        missing = []
        for name, case in results.items():
            pixels = case["pixel_metrics"]
            if pixels["icc_gray_pixels"] < 400 or pixels["device_gray_pixels"] < 800:
                missing.append(name)
        if missing:
            raise RuntimeError(
                "Native packaged LUNA lost clipped gray vector strokes: " +
                ", ".join(missing) + "; see screenshot evidence"
            )
        print("[PASS] All six native Windows ICC/DeviceGray/clip/CTM grids are visibly drawn")
        native_zoom_matrix(executable,directory,env)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("dir", type=Path)
    args = parser.parse_args()
    main(args.exe, args.dir)
