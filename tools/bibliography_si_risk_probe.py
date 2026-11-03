#!/usr/bin/env python3
"""Local, read-only position-order proxy for PR #16's first-16-line SI heuristic.

Never an LUNA/PDF4QT run. User PDF bytes, filenames, first-page text and
watermarks are never emitted in reports. PyMuPDF is optional and local-only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path

SI_PHRASE = re.compile(r"(?:Supporting|Supplementary) Information", re.IGNORECASE)
SAFE_ID = re.compile(r"^[A-Za-z0-9_.:-]{1,96}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
KINDS = {"main_article", "supporting_information"}


def first16_si(lines: list[str]) -> bool:
    """Mirror the relevant *logical condition*, not the PDF4QT text engine."""
    nonempty = [value for value in lines if value.strip()]
    return any(SI_PHRASE.search(line) for line in nonempty[:16])


def classify_lines(lines: list[str]) -> str:
    return "supporting_information" if first16_si(lines) else "main_article"


def position_lines(page: object) -> list[str]:
    """Approximate PDF4QT y-then-x ordering using PyMuPDF's text lines."""
    ordered: list[tuple[float, float, str]] = []
    for block in page.get_text("dict")["blocks"]:
        for line in block.get("lines", []):
            text = "".join(span["text"] for span in line["spans"]).strip()
            if text:
                ordered.append((line["bbox"][1], line["bbox"][0], text))
    ordered.sort(key=lambda row: (row[0], row[1]))
    return [row[2] for row in ordered]


def verified_local_pdf(case: dict, directories: list[Path]) -> Path:
    name = case.get("file_name")
    digest = case.get("pdf_sha256")
    if not isinstance(name, str) or not name or name in {".", ".."} or \
       Path(name).name != name or "/" in name or "\\" in name:
        raise ValueError("unsafe or missing local PDF basename")
    if not isinstance(digest, str) or not SHA256.fullmatch(digest):
        raise ValueError("invalid reference SHA256")
    for directory in directories:
        path = directory / name
        if path.is_symlink():
            raise ValueError("symbolic-link PDF is not permitted")
        if not path.is_file():
            continue
        h = hashlib.sha256()
        with path.open("rb") as stream:
            header = stream.read(1024)
            if b"%PDF-" not in header:
                raise ValueError("invalid PDF signature")
            h.update(header)
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                h.update(chunk)
        if h.hexdigest() != digest:
            raise ValueError("PDF SHA256 differs from candidate reference")
        return path
    raise ValueError("PDF not found in local search directories")


def examine(reference: dict, directories: list[Path], fitz_module: object) -> dict:
    if reference.get("kind") != "candidate_reference_not_approved_goldset":
        raise ValueError("only provisional reference candidates are accepted")
    cases = reference.get("cases")
    if not isinstance(cases, list) or not cases:
        raise ValueError("reference cases must be a nonempty array")
    results = []
    ids = set()
    totals = Counter()
    for case in cases:
        ident = case.get("case_id")
        if not isinstance(ident, str) or not SAFE_ID.fullmatch(ident) or ident in ids:
            raise ValueError("duplicate or unsafe opaque case ID")
        ids.add(ident)
        kind = case.get("candidate_expected", {}).get("document_kind")
        if kind not in KINDS:
            raise ValueError("missing expected document kind")
        path = verified_local_pdf(case, directories)
        with fitz_module.open(path) as pdf:
            if len(pdf) < 1:
                raise ValueError("empty PDF")
            lines = position_lines(pdf[0])
        predicted = classify_lines(lines)
        mismatch = predicted != kind
        totals[kind] += 1
        if mismatch:
            totals["proxy_mismatch"] += 1
        results.append({"case_id": ident, "expected_kind_provisional": kind,
                        "current_rule_position_proxy": predicted,
                        "proxy_mismatch": mismatch})
    return {"assessment": "local_pdf_position_proxy_NOT_LUNA_PDF4QT_accuracy",
            "goldset_approval": "provisional_unreviewed",
            "case_count": len(results), "main_count": totals["main_article"],
            "si_count": totals["supporting_information"],
            "potential_misclassifications": totals["proxy_mismatch"],
            "cases": results}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--pdf-dir", required=True, type=Path, action="append")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(argv)
    try:
        import fitz
    except ImportError:
        print("[BLOCK] local PyMuPDF required; no PDF sent online", file=sys.stderr)
        return 2
    try:
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        result = examine(reference, args.pdf_dir, fitz)
        output = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
        if args.report:
            if args.report.is_symlink():
                raise ValueError("report symlink is not permitted")
            args.report.write_text(output, encoding="utf-8")
        else:
            print(output, end="")
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
        # No filename, private path, PDF text, or other sensitive content.
        print("[BLOCK] local reference/PDF verification failed: " + type(exc).__name__, file=sys.stderr)
        return 2
    print(f"[PROXY] {result['case_count']} local PDFs; {result['potential_misclassifications']} potential misclassifications; NOT LUNA accuracy", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
