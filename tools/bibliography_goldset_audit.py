#!/usr/bin/env python3
"""Read-only goldset collection audit. No PDF, network, or prediction input."""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

from bibliography_accuracy_lab import (
    DatasetError, FIELDS, audit_field_label_coverage,
    audit_goldset_coverage, load_json, validate_cases,
)


def inventory(gold: dict) -> dict:
    """List unknown labels per opaque case before any native snapshot exists.

    This inventories *declared* metadata only. It does not authenticate PDFs,
    hashes, publisher evidence, or engine output.
    """
    cases = validate_cases(gold, "goldset")
    coverage = audit_goldset_coverage(gold)
    labels = audit_field_label_coverage(gold)
    unknown_cases = []
    unknown_by_publisher = defaultdict(Counter)
    synthetic_ids = []
    evidence_gaps = []
    placeholder_evidence = frozenset({
        "tbd", "todo", "n/a", "n.a.", "unknown", "pending",
        "placeholder", "unverified", "-",
    })
    for ident, case in sorted(cases.items()):
        if case["source"]["kind"] == "synthetic":
            synthetic_ids.append(ident)
            continue
        # The ordinary schema permits blank positive evidence and does not
        # require evidence for explicit null (verified absence). A collection
        # audit must NOT declare readiness for either missing justification.
        if case["source"]["sha256"].lower() == "0" * 64:
            evidence_gaps.append({"case_id": ident, "criterion": "placeholder_pdf_sha256"})
        evidence = case["evidence"]
        for field in FIELDS:
            if field not in case["expected"]:
                continue  # Unknown label; no invented evidence required.
            value = evidence.get(field)
            if not isinstance(value, str) or not value.strip():
                evidence_gaps.append({
                    "case_id": ident, "criterion": "missing_nonblank_field_evidence",
                    "field": field,
                })
            elif value.strip().casefold() in placeholder_evidence:
                evidence_gaps.append({
                    "case_id": ident, "criterion": "placeholder_field_evidence",
                    "field": field,
                })
        unknown = sorted(set(FIELDS) - set(case["expected"]))
        if unknown:
            unknown_cases.append({
                "case_id": ident, "publisher": case["publisher"],
                "unknown_fields": unknown,
            })
            unknown_by_publisher[case["publisher"]].update(unknown)
    return {
        "schema_version": 1,
        "dataset_id": gold["dataset_id"],
        "assessment": "goldset_collection_readiness_only_not_extraction_accuracy",
        "ready": coverage["ready"] and labels["ready"] and not evidence_gaps,
        "coverage_audit": coverage,
        "field_label_audit": labels,
        "evidence_audit": {"ready": not evidence_gaps, "gaps": evidence_gaps},
        "unknown_cases": unknown_cases,
        "unknown_by_publisher": {
            publisher: {field: counts[field] for field in FIELDS}
            for publisher, counts in sorted(unknown_by_publisher.items())
        },
        "synthetic_case_ids_excluded": synthetic_ids,
        "warning": "Declared hashes/evidence are not independently authenticated; no PDFs or native records were read.",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--goldset", type=Path, required=True)
    parser.add_argument("--report", type=Path,
                        help="Write only to a local, access-controlled path; never commit private reports")
    args = parser.parse_args(argv)
    # An audit report must never destroy the original private goldset, even
    # when --report names it through a relative path or a symlink.
    if args.report is not None and args.report.resolve() == args.goldset.resolve():
        print("[BLOCK] report path would overwrite input goldset", file=sys.stderr)
        return 2
    try:
        result = inventory(load_json(args.goldset))
    except DatasetError as exc:
        print(f"[BLOCK] {exc}", file=sys.stderr)
        return 2
    output = json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
    if args.report:
        try:
            args.report.write_text(output, encoding="utf-8")
        except OSError as exc:
            print(f"[BLOCK] cannot write report: {exc}", file=sys.stderr)
            return 2
    else:
        print(output, end="")
    print(f"[{'READY' if result['ready'] else 'INCOMPLETE'}] goldset collection only; "
          f"{len(result['unknown_cases'])} cases with unknown labels, "
          f"{len(result['synthetic_case_ids_excluded'])} synthetic cases excluded",
          file=sys.stderr)
    return 0 if result["ready"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
