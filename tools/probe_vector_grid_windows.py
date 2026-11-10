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
    pdf = directory / "synthetic-grid.pdf"
    from probe_synthetic_grid import build
    pdf.write_bytes(build())
    result_file = directory / "grid-smoke.txt"
    command = [
        str(executable.resolve()), "--luna-smoke-report", str(result_file),
        "--luna-layout-only", str(pdf),
    ]
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
    completed = subprocess.run(command, cwd=directory, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace", timeout=90)
    (directory / "luna-process.log").write_text(completed.stdout, encoding="utf-8")
    image_path = Path(str(result_file) + ".png")
    if completed.returncode != 0 or result_file.read_bytes() != b"LUNA_SMOKE_LAYOUT_OK\n":
        raise RuntimeError("LUNA layout-only UI smoke failed (not a grid-render result)")
    if not image_path.exists():
        raise RuntimeError("Native Qt UI screenshot not produced")
    image = Image.open(image_path).convert("RGB")
    summary = {"source": "public synthetic Gray gamma2.2",
               "screenshot": image_path.name, "dimensions": image.size,
               "luna_exit": completed.returncode,
               "pixel_metrics": classify(image)}
    (directory / "grid-result.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2), flush=True)
    # This baseline diagnostic intentionally does not require either color
    # path to be visible; the captured data determines the repair.
    # A later acceptance test MUST assert both path groups are visible.

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("dir", type=Path)
    args = parser.parse_args()
    main(args.exe, args.dir)
