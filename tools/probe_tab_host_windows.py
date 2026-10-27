#!/usr/bin/env python3
"""Synthetic-only foreign-window lifecycle comparison; never POC acceptance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from smoke_windows import synthetic_pdf


def run(host, package, output):
    if os.name != "nt":
        raise RuntimeError("Actual Windows required")
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for method in ("qt", "win32"):
        for scale in ("1", "1.25", "1.5"):
            label = f"{method}-{scale}"
            report = output / f"{label}.json"
            with tempfile.TemporaryDirectory(prefix="luna-tab-fixtures-") as directory:
                root = Path(directory)
                files = [root / f"합성 문서 {name}.pdf" for name in ("A", "B")]
                for file in files:
                    file.write_bytes(synthetic_pdf(2))
                hashes = [hashlib.sha256(file.read_bytes()).hexdigest() for file in files]
                env = os.environ.copy()
                env.update(QT_SCALE_FACTOR=scale, QT_FORCE_STDERR_LOGGING="1",
                           QT_PLUGIN_PATH=str(package), QT_QPA_PLATFORM_PLUGIN_PATH=str(package / "platforms"))
                env["PATH"] = str(package) + os.pathsep + env["PATH"]
                with (output / f"{label}.log").open("w", encoding="utf-8") as log:
                    process = subprocess.Popen([str(host), "--method", method, "--report", str(report),
                                                str(package / "LUNA.exe"), *map(str, files)],
                                               env=env, stdout=log, stderr=subprocess.STDOUT)
                    try:
                        code = process.wait(timeout=75)
                    except subprocess.TimeoutExpired:
                        # Only this experiment's own synthetic process tree.
                        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                       stdout=log, stderr=subprocess.STDOUT, check=False)
                        code = -1
                unchanged = hashes == [hashlib.sha256(file.read_bytes()).hexdigest() for file in files]
                evidence = json.loads(report.read_text(encoding="utf-8")) if report.exists() else {"embeddingLifecycle": "failed", "detail": "No host report"}
                evidence.update(method=method, qtScale=scale, exitCode=code, sourceBytesUnchanged=unchanged,
                                pocAccepted=False)
                results.append(evidence)
                report.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
                if code != 0:
                    # Do not leave hidden native windows behind or mask failure
                    # by proceeding with a contaminated desktop session.
                    break
        if results[-1]["exitCode"] != 0:
            break
    (output / "summary.json").write_text(json.dumps({"pocAccepted": False, "runs": results,
        "remaining": ["real keyboard and pointer input", "focus and Alt+Tab", "annotation isolation",
                      "save and reopen", "modal dialogs", "unsaved close cancel", "physical OS DPI"]},
        ensure_ascii=False, indent=2), encoding="utf-8")
    if len(results) != 6 or any(r["exitCode"] != 0 or not r["sourceBytesUnchanged"] for r in results):
        raise RuntimeError("Embedding lifecycle comparison failed; full POC remains unaccepted")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, type=Path)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    run(args.host.resolve(strict=True), args.package.resolve(strict=True), args.output.resolve())
