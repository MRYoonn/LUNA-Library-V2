#!/usr/bin/env python3
"""Pinned CC BY publisher fixture; no research PDF is committed or packaged."""
import argparse
import hashlib
import io
import json
import time
import zipfile
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib.request import Request, urlopen

URL = "https://journals.plos.org/plosone/article/file?id=10.1371/journal.pone.0000308&type=printable"
SHA256 = "b52ca44dfd09d240543b80315f0cd43fe9ba7946f2b5650d5580213ed5d9186c"
CREDIT = """Piwowar HA, Day RS, Fridsma DB (2007).
Sharing Detailed Research Data Is Associated with Increased Citation Rate.
PLOS ONE 2(3): e308. https://doi.org/10.1371/journal.pone.0000308
Publisher states Creative Commons Attribution License; unrestricted use,
distribution and reproduction with credit to original authors and source.
Source: https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0000308
The research screenshot shows the native LUNA/PDF4QT rendering of this public
article. Test-only highlights were added by automated validation.
The PDF itself is downloaded only into a temporary test directory and is not
included in the app portable package or repository.
"""


def probe(package: Path, output: Path):
    if sys.platform != "win32":
        raise RuntimeError("Public research integration requires native Windows")
    package = package.resolve(strict=True)
    output.mkdir(parents=True, exist_ok=True)
    (output / "PUBLIC_PAPER_CREDIT.txt").write_text(CREDIT, encoding="utf-8")
    request = Request(URL, headers={"User-Agent": "LUNA-public-fixture-validation"})
    with urlopen(request, timeout=30) as response:
        data = response.read(2_000_001)
    if len(data) > 2_000_000 or hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError("Public publisher fixture changed; expected checksum was not bypassed")
    environment = os.environ.copy()
    system = Path(os.environ["SystemRoot"])
    environment.update(PATH=os.pathsep.join(map(str, (package, system / "System32", system))),
        QT_PLUGIN_PATH=str(package), QT_QPA_PLATFORM_PLUGIN_PATH=str(package / "platforms"),
        QT_QPA_PLATFORM="windows", QT_SCALE_FACTOR="1", QT_FORCE_STDERR_LOGGING="1")
    with tempfile.TemporaryDirectory(prefix="luna-public-research-") as temporary:
        root = Path(temporary)
        pdf = root / "public-research.pdf"
        report = root / "research.txt"
        pdf.write_bytes(data)
        try:
            subprocess.run([str(package / "LUNA.exe"), "--luna-research-report", str(report), str(pdf)],
                cwd=root, env=environment, check=True, timeout=90)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            failure = Path(str(report) + ".failure.png")
            if failure.is_file(): shutil.copyfile(failure, output / "research-failure.png")
            detail = report.read_text(encoding="utf-8")[:512] if report.is_file() else "No native research diagnostic"
            raise RuntimeError(detail) from error
        if report.read_bytes() != b"LUNA_RESEARCH_ROUNDTRIP_OK\n" or pdf.read_bytes() != data:
            raise RuntimeError("Research marker or original-byte preservation failed")
        screenshot = Path(str(report) + ".png")
        annotated = Path(str(report) + ".annotated.pdf")
        if not screenshot.is_file() or not annotated.read_bytes().startswith(b"%PDF-"):
            raise RuntimeError("Research capture or annotated copy missing")
        shutil.copyfile(screenshot, output / "research-native.png")
    print("[PASS] Pinned public CC BY five-page paper: native two-column and figure-caption selection/copy, highlights, Save As and reopen; original unchanged")
    print("[NOTICE] This public-paper check is separate from synthetic PDF regressions; other papers and long-session use are not implied.")


def bibliography_probe(package: Path, output: Path):
    """Public PMC publisher copy, never a private user file or an uploaded PDF."""
    if sys.platform != "win32":
        raise RuntimeError("Bibliography integration requires native Windows")
    package = package.resolve(strict=True)
    output.mkdir(parents=True, exist_ok=True)
    # Use the official download API directly; the article front door can reject
    # hosted Windows runners before redirecting to this exact public resource.
    sources = (
        "https://europepmc.org/api/getPdf?pmcid=PMC12599387",
        "https://pubs.rsc.org/en/content/articlepdf/2026/sc/d5sc05518k",
        "https://europepmc.org/articles/PMC12599387?pdf=render",
    )
    expected = "c9dbf56b210daa1518408b37d1ff172136ea049ab3e1f5bc4aeca00254f85b93"
    failures = []
    for source in sources:
        try:
            with urlopen(Request(source, headers={"User-Agent": "LUNA-public-fixture-validation"}), timeout=60) as response:
                data = response.read(2_000_001)
            if hashlib.sha256(data).hexdigest() != expected or not data.startswith(b"%PDF-"):
                raise RuntimeError("Public RSC publisher copy checksum mismatch")
            break
        except Exception as error:
            failures.append(f"{source}: {error}")
    else:
        raise RuntimeError("Pinned public PDF unavailable; no verification was performed: " + "; ".join(failures))
    # The public second SI attachment is read from the official OA supplement
    # response. Extract only the fixed member, enforce bounds and pin its bytes.
    supplement_url = "https://www.ebi.ac.uk/europepmc/webservices/rest/PMC12599387/supplementaryFiles"
    with urlopen(Request(supplement_url, headers={"User-Agent": "LUNA-public-fixture-validation"}), timeout=120) as response:
        archive = response.read(40_000_001)
    if len(archive) > 40_000_000:
        raise RuntimeError("Public SI archive exceeds fixture bound")
    member = "SC-017-D5SC05518K-s002.pdf"
    with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
        if bundle.namelist().count(member) != 1 or bundle.getinfo(member).file_size > 30_000_000:
            raise RuntimeError("Public SI fixture member changed")
        si_data = bundle.read(member)
    si_hash = "12b58a233e193b0e5d2cab553e3c7b4a4da9a9824f9dd24afdab26767f3708b8"
    if hashlib.sha256(si_data).hexdigest() != si_hash or not si_data.startswith(b"%PDF-"):
        raise RuntimeError("Public SI publisher copy checksum mismatch")
    (output / "PUBLIC_PAPER_CREDIT.txt").write_text(
        "Public CC BY RSC publisher copy: PMC12599387. Source: " + source + "\n"
        "PDFs are temporary test inputs, never uploaded or packaged. Main SHA256: " + expected + "\n"
        "Public SI2 source: " + supplement_url + "\nSI2 SHA256: " + si_hash + "\n",
        encoding="utf-8")
    environment = os.environ.copy()
    system = Path(os.environ["SystemRoot"])
    environment.update(PATH=os.pathsep.join(map(str, (package, system / "System32", system))),
        QT_PLUGIN_PATH=str(package), QT_QPA_PLATFORM_PLUGIN_PATH=str(package / "platforms"),
        QT_QPA_PLATFORM="windows", QT_SCALE_FACTOR="1", LUNA_PUBLIC_BIBLIOGRAPHY_PROBE="1",
        QT_FORCE_STDERR_LOGGING="1", LUNA_PERF="1")
    # Only stage names and millisecond durations from the pinned public
    # fixtures are retained. Never save raw Qt logs or PDF text/paths.
    timings = []
    measured_stages = {
        "pdf-file-read", "pdf-parse", "reader-worker-queue",
        "worker-to-gui-delivery", "document-layout-and-install",
        "request-to-first-page-paint", "open-dispatch",
        "bibliography-worker-dispatch", "bibliography-first-page-worker",
    }
    with tempfile.TemporaryDirectory(prefix="luna-public-bibliography-") as temporary:
        root = Path(temporary)
        for label, payload, is_si in (("rsc", data, False), ("si2", si_data, True)):
            pdf = root / f"public-{label}.pdf"
            pdf.write_bytes(payload)
            child_environment = environment.copy()
            child_environment.pop("LUNA_PUBLIC_BIBLIOGRAPHY_SI_PROBE", None)
            if is_si:
                child_environment["LUNA_PUBLIC_BIBLIOGRAPHY_SI_PROBE"] = "1"
            for restart in range(3):
                report = root / f"bibliography-{label}-{restart}.txt"
                try:
                    started = time.perf_counter()
                    completed = subprocess.run(
                        [str(package / "LUNA.exe"), "--luna-research-report", str(report), str(pdf)],
                        cwd=root, env=child_environment, check=True, timeout=90,
                        capture_output=True, text=True, errors="replace")
                    elapsed_ms = round((time.perf_counter() - started) * 1000)
                except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
                    failure = Path(str(report) + ".failure.png")
                    if failure.is_file():
                        shutil.copyfile(failure, output / f"bibliography-{label}-{restart}-failure.png")
                    detail = report.read_text(encoding="utf-8")[:14000] if report.is_file() else "No native bibliography diagnostic"
                    raise RuntimeError(detail) from error
                if report.read_bytes() != b"LUNA_RESEARCH_ROUNDTRIP_OK\n" or pdf.read_bytes() != payload:
                    raise RuntimeError("Bibliography marker or original bytes changed")
                stages = {}
                for line in completed.stderr.splitlines():
                    _, separator, encoded = line.partition("LUNA_PERF ")
                    if not separator:
                        continue
                    try:
                        entry = json.loads(encoded.strip())
                        stage = entry.get("stage")
                        duration = entry.get("durationMs")
                        if stage in measured_stages and isinstance(duration, (int, float)):
                            stages.setdefault(stage, []).append(round(duration, 1))
                    except (ValueError, TypeError):
                        continue  # Other Qt messages are never retained.
                if is_si and not stages.get("request-to-first-page-paint"):
                    raise RuntimeError(
                        "SI first-page-paint metric missing; cannot claim measured performance")
                if is_si and not stages.get("pdf-parse"):
                    raise RuntimeError(
                        "SI PDF parse metric missing; cannot identify load bottleneck")
                timings.append({"fixture": label, "restart": restart,
                                "elapsedMs": elapsed_ms, "stagesMs": stages})
                shutil.copyfile(Path(str(report) + ".png"), output / f"bibliography-{label}-{restart}.png")
    (output / "PUBLIC_SI_TIMINGS.json").write_text(
        json.dumps({"scope": "pinned-public-main-and-SI-only",
                    "measurements": timings}, indent=2), encoding="utf-8")
    for item in timings:
        if item["fixture"] == "si2":
            print("[SI PERF]", "restart", item["restart"],
                  "totalMs", item["elapsedMs"],
                  "firstPaintMs", item["stagesMs"].get("request-to-first-page-paint", []),
                  "parseMs", item["stagesMs"].get("pdf-parse", []),
                  "metadataMs", item["stagesMs"].get("bibliography-first-page-worker", []))
    print("[PASS] Real RSC main/SI2 PDFs: automatic John J. Molloy card, legacy Hao Fang repair, SI metadata abstention, no dialog/jump, two fresh-process restarts each, original bytes preserved")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--screenshots-directory", type=Path, required=True)
    parser.add_argument("--bibliography", action="store_true")
    args = parser.parse_args()
    (bibliography_probe if args.bibliography else probe)(args.directory, args.screenshots_directory)
