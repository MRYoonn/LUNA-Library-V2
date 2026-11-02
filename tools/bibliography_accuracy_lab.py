#!/usr/bin/env python3
"""Offline bibliography goldset scorer; no PDF parsing or network access.

This evaluates *supplied* extraction snapshots. A synthetic PASS is never a
claim about the native LUNA engine, a publisher PDF, or a Windows build.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
import unicodedata
from collections import defaultdict
from pathlib import Path

FIELDS = (
    "doi", "title", "terminal_author", "journal", "journal_abbreviation",
    "year", "volume", "issue", "page", "article_number", "document_kind",
)
ID = re.compile(r"^[A-Za-z0-9_.:-]{1,96}$")
SHA256 = re.compile(r"^[0-9a-fA-F]{64}$")
DOCUMENT_KINDS = frozenset(("main_article", "supporting_information"))
DOI = re.compile(r"^10\.\d{4,9}/\S+$", re.I)


class DatasetError(ValueError):
    pass


def load_json(path: Path) -> dict:
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise DatasetError(f"cannot read JSON {path.name}: {exc}") from exc
    if not isinstance(result, dict):
        raise DatasetError(f"{path.name}: expected JSON object")
    return result


def normalize(field: str, value: object) -> str:
    if value is None or value == "":
        return ""
    if not isinstance(value, str):
        raise DatasetError(f"{field}: value must be string or null")
    # NFKC folds chemical subscripts/superscripts (CO₂ -> CO2, Fe³⁺ -> Fe3+).
    # Titles are scientific evidence: keep these distinct while still treating
    # canonically equivalent Unicode spellings (NFC) as the same title.
    result = unicodedata.normalize("NFC" if field == "title" else "NFKC", value).strip()
    if field == "doi":
        result = re.sub(r"^(?:https?://(?:dx\.)?doi\.org/|doi\s*:\s*)", "", result, flags=re.I)
        return result.rstrip("., ").casefold()
    result = " ".join(result.split()).casefold()
    if field == "page":
        result = re.sub(r"\s*[-\u2013\u2014]\s*", "-", result)
    # Deliberately do not strip title punctuation or chemistry symbols: a
    # changed chemical formula/stereochemical descriptor is a real mismatch.
    return result


def validate_cases(doc: dict, kind: str) -> dict[str, dict]:
    if doc.get("schema_version") != 1 or doc.get("kind") != kind:
        raise DatasetError(f"{kind}: expected schema_version=1, kind={kind}")
    if not isinstance(doc.get("dataset_id"), str) or not ID.fullmatch(doc["dataset_id"]):
        raise DatasetError(f"{kind}: invalid dataset_id")
    cases = doc.get("cases")
    if not isinstance(cases, list) or not cases:
        raise DatasetError(f"{kind}: nonempty cases list required")
    indexed = {}
    for case in cases:
        if not isinstance(case, dict) or not isinstance(case.get("id"), str) or not ID.fullmatch(case["id"]):
            raise DatasetError(f"{kind}: invalid case id")
        ident = case["id"]
        if ident in indexed:
            raise DatasetError(f"{kind}: duplicate case id {ident}")
        values = case.get("expected" if kind == "goldset" else "observed")
        if not isinstance(values, dict) or set(values) - set(FIELDS):
            raise DatasetError(f"{kind}/{ident}: invalid field names or missing values object")
        for field, value in values.items():
            canonical = normalize(field, value)
            # A gold label is ground truth, not an arbitrary string. If a
            # malformed DOI is present in both datasets, it must never TP.
            # Keep invalid *predictions* scoreable as extraction failures.
            if kind == "goldset" and field == "doi" and canonical and not DOI.fullmatch(canonical):
                raise DatasetError(f"goldset/{ident}: invalid expected DOI")
            if kind == "goldset" and field == "document_kind" and canonical and canonical not in DOCUMENT_KINDS:
                raise DatasetError(f"goldset/{ident}: invalid expected document_kind")
        if kind == "goldset":
            publisher = case.get("publisher")
            if publisher not in ("ACS", "RSC", "Wiley", "Nature", "Elsevier", "Other"):
                raise DatasetError(f"goldset/{ident}: unsupported publisher")
            source = case.get("source")
            if not isinstance(source, dict) or source.get("kind") not in ("synthetic", "public", "permissioned"):
                raise DatasetError(f"goldset/{ident}: explicit source.kind required")
            if source["kind"] != "synthetic":
                if not isinstance(source.get("sha256"), str) or not SHA256.fullmatch(source["sha256"]):
                    raise DatasetError(f"goldset/{ident}: real PDF requires SHA256")
                evidence = case.get("evidence")
                if not isinstance(evidence, dict):
                    raise DatasetError(f"goldset/{ident}: real PDF requires evidence")
                for field, value in values.items():
                    if normalize(field, value) and not isinstance(evidence.get(field), str):
                        raise DatasetError(f"goldset/{ident}: missing evidence for {field}")
        indexed[ident] = case
    return indexed



def validate_real_run(gold: dict, predictions: dict) -> None:
    """Structural evidence gate; cannot independently authenticate PDF evidence.

    A --require-real report is NOT an audited extraction accuracy claim. It
    requires a native snapshot identity and evidence for known-negative labels
    as well as positive values, preventing an unrun synthetic prediction file
    from accidentally being accepted as a real Windows measurement.
    """
    commit = predictions.get("engine_commit")
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-fA-F]{40}", commit) or commit.lower() == "0" * 40:
        raise DatasetError("real run requires an exact non-placeholder 40-hex engine_commit")
    if predictions.get("capture_kind") != "qt_record_snapshot_read_only":
        raise DatasetError("real run requires qt_record_snapshot_read_only capture_kind")
    # One source PDF may not count as multiple independent goldset samples.
    # Only opaque case IDs enter errors; never expose private PDF hashes.
    seen_pdf_hashes = {}
    for case in gold["cases"]:
        ident = case["id"]
        source = case["source"]
        if source["kind"] not in ("public", "permissioned"):
            raise DatasetError(f"real run/{ident}: only public or permissioned PDFs accepted")
        if source["sha256"].lower() == "0" * 64:
            raise DatasetError(f"real run/{ident}: placeholder PDF SHA256")
        digest = source["sha256"].lower()
        first_case = seen_pdf_hashes.setdefault(digest, ident)
        if first_case != ident:
            raise DatasetError(
                f"real run/{ident}: duplicate PDF SHA256 also used by {first_case}"
            )
        evidence = case["evidence"]
        for field in case["expected"]:
            # Explicit null means independently verified ABSENCE, not unknown.
            # It needs evidence too (e.g. SI cover and no own DOI).
            value = evidence.get(field)
            if not isinstance(value, str) or not value.strip():
                raise DatasetError(f"real run/{ident}: missing nonblank evidence for {field}")
    labeled = {case["id"]: set(case["expected"]) for case in gold["cases"]}
    for case in predictions["cases"]:
        audit = case.get("audit")
        if not isinstance(audit, dict) or any(
            type(audit.get(key)) is not bool
            for key in ("manual_citation_active", "senior_author_override_active")
        ):
            raise DatasetError(f"real run/{case['id']}: native snapshot audit flags required")
        # A manually pasted citation or senior-author override can change the
        # displayed card independently of the extraction record. Measure these
        # in preservation regressions, not in automatic extraction accuracy.
        if audit["manual_citation_active"] or audit["senior_author_override_active"]:
            raise DatasetError(f"real run/{case['id']}: active manual card override; use regression-only evidence")
        sources = audit.get("field_sources", {})
        if not isinstance(sources, dict):
            raise DatasetError(f"real run/{case['id']}: invalid field_sources audit")
        for field in labeled.get(case["id"], ()):
            native_field = "authors" if field == "terminal_author" else field
            # A legacy record-wide manual=true is ambiguous. Inspect BOTH
            # native field-level provenance channels, since one can be stale.
            # Missing/unknown provenance is not evidence of auto extraction.
            candidates = (sources.get(native_field), audit.get(native_field + "Source"))
            provenance = [value.strip().casefold() for value in candidates
                          if isinstance(value, str) and value.strip()]
            if "manual" in provenance:
                raise DatasetError(
                    f"real run/{case['id']}: manually sourced {field} cannot score automatic extraction"
                )
            # The SI/main classification comes from a separate native record
            # property with no Source field. Its ground truth still requires
            # independent goldset evidence; do not invent a provenance tag.
            if field == "document_kind":
                continue
            observed = case.get("observed", {})
            if normalize(field, observed.get(field)):
                trusted = {"crossref", "openalex", "crossref + openalex",
                           "first_page_verified", "pdf_metadata", "pdf_front_matter"}
                # Both native provenance channels must be trusted. A trusted
                # field_sources entry cannot launder a stale/unknown *Source
                # value into an apparent automatic extraction success.
                if not provenance or any(value not in trusted for value in provenance):
                    raise DatasetError(
                        f"real run/{case['id']}: missing or unverified automatic provenance for {field}"
                    )
                # Crossref and OpenAlex can legitimately be combined, but
                # contradictory single-provider claims need human review.
                if len(set(provenance)) > 1:
                    # A composite provider source can coexist with either of
                    # its two providers, but NOT with PDF metadata or first-
                    # page provenance. Previously the mere presence of the
                    # composite label masked those unrelated source conflicts.
                    provider_sources = {"crossref", "openalex", "crossref + openalex"}
                    if ("crossref + openalex" not in provenance
                            or any(value not in provider_sources for value in provenance)):
                        raise DatasetError(
                            f"real run/{case['id']}: conflicting automatic provenance for {field}"
                        )


def metric(tp: int, other: int) -> float | None:
    return round(tp / (tp + other), 6) if tp + other else None


def score(gold: dict, predictions: dict, *, require_real: bool = False) -> dict:
    expected = validate_cases(gold, "goldset")
    observed = validate_cases(predictions, "predictions")
    if gold["dataset_id"] != predictions["dataset_id"]:
        raise DatasetError("dataset_id mismatch")
    if require_real and any(c["source"]["kind"] == "synthetic" for c in expected.values()):
        raise DatasetError("--require-real rejects synthetic fixtures")
    if require_real:
        validate_real_run(gold, predictions)
    unknown = sorted(set(observed) - set(expected))
    missing = sorted(set(expected) - set(observed))
    if unknown or missing:
        raise DatasetError(f"case ID mismatch: unexpected={unknown}, missing={missing}")
    stats = {field: defaultdict(int) for field in FIELDS}
    errors = []
    by_publisher = defaultdict(lambda: {"cases": 0, "errors": 0})
    by_publisher_fields = defaultdict(lambda: {field: defaultdict(int) for field in FIELDS})
    for ident, case in expected.items():
        publisher = case["publisher"]
        by_publisher[publisher]["cases"] += 1
        for field in FIELDS:
            if field not in case["expected"]:
                stats[field]["unscored"] += 1
                by_publisher_fields[publisher][field]["unscored"] += 1
                continue
            want = normalize(field, case["expected"][field])
            observed_fields = observed[ident]["observed"]
            got = normalize(field, observed_fields.get(field))
            stats[field]["scored"] += 1
            by_publisher_fields[publisher][field]["scored"] += 1
            # An omitted snapshot key is not evidence that an absent field
            # was actually checked. In particular, a missing SI DOI must
            # never inflate true negatives or produce a false PASS.
            if field not in observed_fields and not want:
                stats[field]["missing_snapshot"] += 1
                by_publisher_fields[publisher][field]["missing_snapshot"] += 1
                by_publisher[publisher]["errors"] += 1
                errors.append({"case_id": ident, "publisher": publisher, "field": field,
                               "expected": case["expected"][field], "observed": None,
                               "error": "missing_snapshot_field"})
                continue
            if want == got:
                outcome = "tp" if want else "tn"
                stats[field][outcome] += 1
                by_publisher_fields[publisher][field][outcome] += 1
            else:
                if want:
                    stats[field]["fn"] += 1
                    by_publisher_fields[publisher][field]["fn"] += 1
                if got:
                    stats[field]["fp"] += 1
                    by_publisher_fields[publisher][field]["fp"] += 1
                by_publisher[publisher]["errors"] += 1
                errors.append({"case_id": ident, "publisher": publisher, "field": field,
                               "expected": case["expected"][field],
                               "observed": observed[ident]["observed"].get(field),
                               "error": "invalid_doi" if field == "doi" and got and not DOI.fullmatch(got)
                                        else "invalid_document_kind" if field == "document_kind" and got and got not in DOCUMENT_KINDS
                                        else "mismatch" if want and got else "missing" if want else "spurious"})
    def field_row(count: dict) -> dict:
        row = {k: count[k] for k in ("tp", "fp", "fn", "tn", "scored", "unscored", "missing_snapshot")}
        row["precision"] = metric(count["tp"], count["fp"])
        row["recall"] = metric(count["tp"], count["fn"])
        return row

    field_scores = {field: field_row(count) for field, count in stats.items()}
    # Publisher-level denominators are independent: no ACS result may inflate
    # RSC precision/recall, and missing ground truth remains unscored.
    publisher_field_scores = {
        publisher: {field: field_row(count) for field, count in fields.items()}
        for publisher, fields in sorted(by_publisher_fields.items())
    }
    checks = score_regressions(predictions.get("regressions", []))
    return {
        "schema_version": 1, "dataset_id": gold["dataset_id"],
        "assessment": "synthetic_harness_only" if any(c["source"]["kind"] == "synthetic" for c in expected.values())
                      else "provided_real_goldset_not_independently_verified",
        "cases": len(expected), "field_scores": field_scores,
        "by_publisher": dict(sorted(by_publisher.items())),
        "by_publisher_fields": publisher_field_scores,
        "errors": errors, "regression_checks": checks,
        "pass": not errors and checks["failed"] == 0,
    }


def score_regressions(regressions: object) -> dict:
    if not isinstance(regressions, list):
        raise DatasetError("regressions must be a list")
    failures = []
    seen = set()
    for case in regressions:
        if not isinstance(case, dict) or not isinstance(case.get("id"), str) or not ID.fullmatch(case["id"]):
            raise DatasetError("invalid regression id")
        ident = case["id"]
        if ident in seen:
            raise DatasetError(f"duplicate regression id: {ident}")
        seen.add(ident)
        before, after = case.get("before"), case.get("after")
        preserve = case.get("must_preserve", [])
        empty = case.get("must_remain_empty", [])
        if not isinstance(before, dict) or not isinstance(after, dict):
            raise DatasetError(f"regression/{ident}: before/after objects required")
        if not isinstance(preserve, list) or not isinstance(empty, list) or not all(
            isinstance(x, str) and x.strip() for x in preserve + empty
        ):
            raise DatasetError(f"regression/{ident}: invalid field lists")
        if len(set(preserve)) != len(preserve) or len(set(empty)) != len(empty):
            raise DatasetError(f"regression/{ident}: duplicate field assertion")
        if set(preserve) & set(empty):
            raise DatasetError(f"regression/{ident}: contradictory field assertions")
        if "actual_status" in case and "expected_status" not in case:
            raise DatasetError(f"regression/{ident}: actual_status without expected_status")
        if "expected_status" in case and (
            not isinstance(case["expected_status"], str) or not case["expected_status"].strip()
        ):
            raise DatasetError(f"regression/{ident}: invalid expected_status")
        if not preserve and not empty and "expected_status" not in case:
            raise DatasetError(f"regression/{ident}: no assertions to verify")
        for field in preserve:
            # An absent snapshot field is not proof that a protected value
            # survived. In particular, before[field] == None used to match
            # after.get(field) when the after key was never captured.
            if field not in before or field not in after:
                failures.append({"case_id": ident, "field": field, "reason": "missing_asserted_field"})
            elif type(before[field]) is not type(after[field]) or before[field] != after[field]:
                # Python considers False == 0 and True == 1. A Qt/QSettings
                # snapshot changing a boolean into an integer is NOT a
                # preserved manual value, even when their values compare equal.
                failures.append({"case_id": ident, "field": field, "reason": "changed_protected_value"})
        for field in empty:
            # An omitted snapshot key is not evidence that a field was empty.
            # Require an explicit captured null/blank value to avoid a false PASS.
            if field not in after:
                failures.append({"case_id": ident, "field": field, "reason": "missing_asserted_field"})
            elif after[field] not in (None, "", []):
                failures.append({"case_id": ident, "field": field, "reason": "unexpected_value"})
        if "expected_status" in case and case["expected_status"] != case.get("actual_status"):
            failures.append({"case_id": ident, "field": "status", "reason": "wrong_status"})
    return {"total": len(regressions), "passed": len(regressions) - len({f["case_id"] for f in failures}),
            "failed": len({f["case_id"] for f in failures}), "failures": failures}


PUBLISHERS = ("ACS", "RSC", "Wiley", "Nature", "Elsevier")


def audit_goldset_coverage(gold: dict, *, minimum_total: int = 20,
                           minimum_main_per_publisher: int = 3,
                           minimum_si: int = 3, minimum_no_doi_main: int = 2) -> dict:
    """Check goldset sample breadth, not extraction accuracy or PDF authenticity."""
    from collections import Counter

    cases = validate_cases(gold, "goldset")
    main_by_publisher = Counter()
    source_counts = Counter()
    digest_to_id = {}
    duplicate_digests = []
    real_count = si_count = no_doi_main_count = 0
    for ident, case in cases.items():
        source = case["source"]
        source_counts[source["kind"]] += 1
        if source["kind"] == "synthetic":
            continue
        real_count += 1
        digest = source["sha256"].lower()
        if digest in digest_to_id:
            duplicate_digests.append({"first_case_id": digest_to_id[digest], "duplicate_case_id": ident})
        else:
            digest_to_id[digest] = ident
        expected = case["expected"]
        kind = expected.get("document_kind")
        if kind == "main_article":
            main_by_publisher[case["publisher"]] += 1
            if "doi" in expected and normalize("doi", expected["doi"]) == "":
                no_doi_main_count += 1
        elif kind == "supporting_information":
            si_count += 1
    gaps = []
    if real_count < minimum_total:
        gaps.append({"criterion": "real_pdf_total", "actual": real_count, "required": minimum_total})
    for publisher in PUBLISHERS:
        actual = main_by_publisher[publisher]
        if actual < minimum_main_per_publisher:
            gaps.append({"criterion": "main_per_publisher", "publisher": publisher,
                         "actual": actual, "required": minimum_main_per_publisher})
    if si_count < minimum_si:
        gaps.append({"criterion": "supporting_information", "actual": si_count, "required": minimum_si})
    if no_doi_main_count < minimum_no_doi_main:
        gaps.append({"criterion": "no_doi_main", "actual": no_doi_main_count,
                     "required": minimum_no_doi_main})
    if duplicate_digests:
        gaps.append({"criterion": "duplicate_pdf_sha256", "cases": duplicate_digests})
    return {
        "ready": not gaps,
        "basis": "provided_pdf_metadata_only_not_independently_verified",
        "real_pdf_total": real_count,
        "source_counts": dict(sorted(source_counts.items())),
        "main_by_publisher": {name: main_by_publisher[name] for name in PUBLISHERS},
        "supporting_information": si_count,
        "no_doi_main": no_doi_main_count,
        "gaps": gaps,
    }



def audit_field_label_coverage(gold: dict, *, minimum_positive_per_field: int = 1,
                               minimum_core_per_publisher: int = 1) -> dict:
    """Inventory independently labeled real-PDF fields; not an accuracy score.

    A 20-PDF goldset can satisfy publisher quotas yet contain no known issue,
    article-number or journal-abbreviation labels. Such absent denominators
    must be reported explicitly before claiming field-wise coverage.
    """
    from collections import Counter

    cases = validate_cases(gold, "goldset")
    positive = Counter()
    negative = Counter()
    unknown = Counter()
    core_by_publisher = {publisher: Counter() for publisher in PUBLISHERS}
    core = ("doi", "title", "terminal_author", "journal", "year", "document_kind")
    for case in cases.values():
        if case["source"]["kind"] == "synthetic":
            continue
        publisher = case["publisher"]
        labels = case["expected"]
        for field in FIELDS:
            if field not in labels:
                unknown[field] += 1
            elif normalize(field, labels[field]):
                positive[field] += 1
                if publisher in core_by_publisher and field in core:
                    core_by_publisher[publisher][field] += 1
            else:
                negative[field] += 1
    gaps = []
    for field in FIELDS:
        if positive[field] < minimum_positive_per_field:
            gaps.append({"criterion": "field_positive_labels", "field": field,
                         "actual": positive[field], "required": minimum_positive_per_field})
    for publisher in PUBLISHERS:
        for field in core:
            actual = core_by_publisher[publisher][field]
            if actual < minimum_core_per_publisher:
                gaps.append({"criterion": "publisher_core_positive_labels",
                             "publisher": publisher, "field": field,
                             "actual": actual, "required": minimum_core_per_publisher})
    return {
        "ready": not gaps,
        "basis": "provided_goldset_labels_not_independently_verified",
        "positive_labels": {field: positive[field] for field in FIELDS},
        "negative_labels": {field: negative[field] for field in FIELDS},
        "unknown_labels": {field: unknown[field] for field in FIELDS},
        "core_positive_by_publisher": {
            publisher: {field: counts[field] for field in core}
            for publisher, counts in core_by_publisher.items()
        },
        "gaps": gaps,
    }


def check_engine_identity(predictions: dict, expected_commit: str) -> None:
    """Fail closed when a capture came from a different source revision."""
    if not re.fullmatch(r"[0-9a-fA-F]{40}", expected_commit) or expected_commit.lower() == "0" * 40:
        raise DatasetError("--expected-engine-commit must be a non-placeholder 40-hex SHA")
    actual = predictions.get("engine_commit")
    if not isinstance(actual, str) or actual.lower() != expected_commit.lower():
        raise DatasetError("engine_commit mismatch: snapshot is not from the expected build")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--goldset", type=Path, required=True)
    parser.add_argument("--predictions", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--require-real", action="store_true")
    parser.add_argument("--expected-engine-commit", metavar="SHA40",
                        help="require --require-real; reject snapshots from a different engine commit")
    parser.add_argument("--coverage-gate", action="store_true",
                        help="require --require-real and >=20 diverse verified-label PDF cases")
    parser.add_argument("--field-coverage-gate", action="store_true",
                        help="require --coverage-gate and labeled positive examples for every field")
    args = parser.parse_args(argv)
    # Both inputs may contain private labels and native snapshots. Never
    # truncate either one through an accidental --report alias.
    if args.report is not None and args.report.resolve() in (
        args.goldset.resolve(), args.predictions.resolve()
    ):
        print("[BLOCK] report path would overwrite input goldset or predictions",
              file=sys.stderr)
        return 2
    try:
        if args.field_coverage_gate and not args.coverage_gate:
            raise DatasetError("--field-coverage-gate requires --coverage-gate")
        if args.coverage_gate and not args.require_real:
            raise DatasetError("--coverage-gate requires --require-real")
        if args.expected_engine_commit and not args.require_real:
            raise DatasetError("--expected-engine-commit requires --require-real")
        gold = load_json(args.goldset)
        predictions = load_json(args.predictions)
        if args.expected_engine_commit:
            check_engine_identity(predictions, args.expected_engine_commit)
        result = score(gold, predictions, require_real=args.require_real)
        if args.require_real:
            result["engine_commit"] = predictions["engine_commit"].lower()
        if args.coverage_gate:
            coverage = audit_goldset_coverage(gold)
            result["coverage_audit"] = coverage
            result["pass"] = result["pass"] and coverage["ready"]
        if args.field_coverage_gate:
            labels = audit_field_label_coverage(gold)
            result["field_label_audit"] = labels
            result["pass"] = result["pass"] and labels["ready"]
    except DatasetError as exc:
        print(f"[BLOCK] {exc}", file=sys.stderr)
        return 2
    output = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.report:
        # Use a private, same-directory temporary file and atomic replacement.
        # A symlink output must never redirect the report into unrelated data.
        temporary_report = None
        try:
            if args.report.is_symlink():
                raise OSError("refusing symlink report path")
            with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", dir=args.report.parent,
                prefix=".luna-bibliography-score-", suffix=".tmp", delete=False,
            ) as stream:
                temporary_report = Path(stream.name)
                stream.write(output)
            os.replace(temporary_report, args.report)
        except OSError as exc:
            print(f"[BLOCK] cannot write report: {exc}", file=sys.stderr)
            return 2
        finally:
            if temporary_report is not None:
                temporary_report.unlink(missing_ok=True)
    else:
        print(output, end="")
    print(f"[{'PASS' if result['pass'] else 'FAIL'}] {result['assessment']}: "
          f"{result['cases']} cases, {len(result['errors'])} field errors, "
          f"{result['regression_checks']['failed']} regression failures", file=sys.stderr)
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
