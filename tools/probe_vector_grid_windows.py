"""Reproduce the vector-grid problem on the real packaged LUNA.exe (Windows only).

The PDF fixture is purely synthetic. Detect ICC Gray vs DeviceGray strokes in
native Qt screenshots; always retain screenshot and JSON even on failure.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from PIL import Image


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


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("dir", type=Path)
    args = parser.parse_args()
    main(args.exe, args.dir)
