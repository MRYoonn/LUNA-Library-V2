#!/usr/bin/env python3
"""Opt-in public ACS/Figshare metadata + landing smoke probe.

No PDF or SI file is downloaded. This is a manual diagnostic, not an
end-to-end acceptance test or a claim that individual SI files open.
"""
from __future__ import annotations

import argparse
import json
import unicodedata

try:
    from tools.si_finder import (lookup_acs_figshare, normalize_doi, safe_figshare_landing,
                                 safe_si_filename, safe_si_title)
    from tools.si_landing_verify import verify_landing_candidates
except ImportError:
    from si_finder import (lookup_acs_figshare, normalize_doi, safe_figshare_landing,
                           safe_si_filename, safe_si_title)
    from si_landing_verify import verify_landing_candidates


def inspect_doi(doi: str, *, live: bool = False, check_landings: bool = False,
                finder=lookup_acs_figshare,
                verifier=verify_landing_candidates) -> dict:
    """Only an explicit live flag allows the existing bounded network clients."""
    if check_landings and not live:
        raise ValueError("landing checks require explicit --live")
    result = finder(doi, offline=not live)
    if check_landings and result.status in ("candidates", "partial"):
        result = verifier(result)
    return {
        "verification_scope": "landing_html_only" if check_landings else "metadata_only",
        "result": result.to_dict(),
    }


def _verified_identity(candidate: dict, doi: str) -> bool:
    """Independently check the CLI acceptance gate's public identity evidence.

    This does not verify individual SI files; the landing verifier is still
    responsible for the bounded HTTP check. Never accept a restored/misrouted
    report solely because it claims landing_verified.
    """
    if not isinstance(candidate, dict):
        return False
    article_id = candidate.get("article_id")
    file_id = candidate.get("file_id")
    landing = candidate.get("landing_url")
    return (type(article_id) is int and 0 < article_id < 10**15
            and candidate.get("resource_doi") == doi
            and isinstance(landing, str)
            and safe_figshare_landing(landing, article_id) == landing
            and safe_si_filename(candidate.get("filename"))
            and bool(safe_si_title(candidate.get("title")))
            and (file_id is None or
                 (type(file_id) is int and 0 < file_id < 10**15)))



def _verified_collection(candidates: object, doi: str) -> bool:
    """Fail closed on duplicate SI identities in restored acceptance reports.

    The finder and landing verifier already reject ambiguous file identities.
    Recheck at the CLI boundary because externally assembled reports can claim
    verified without having passed those earlier gates.
    """
    if not isinstance(candidates, list) or not candidates:
        return False
    seen_names: set[tuple[int, str]] = set()
    seen_file_ids: set[int] = set()
    for candidate in candidates:
        if (not isinstance(candidate, dict)
                or candidate.get("status") != "landing_verified"
                or not _verified_identity(candidate, doi)):
            return False
        name_key = (candidate["article_id"],
                    unicodedata.normalize("NFKC", candidate["filename"]).casefold())
        if name_key in seen_names:
            return False
        seen_names.add(name_key)
        file_id = candidate.get("file_id")
        if file_id is not None:
            if file_id in seen_file_ids:
                return False
            seen_file_ids.add(file_id)
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--doi", required=True, help="One public ACS article DOI")
    parser.add_argument("--live", action="store_true", help="Opt in to public Figshare API requests")
    parser.add_argument("--check-landings", action="store_true",
                        help="Also opt in to bounded HEAD/GET landing checks (requires --live)")
    parser.add_argument("--require-landing-http", action="store_true",
                        help="Exit 2 unless all SI HTML landings respond successfully; never certifies SI files")
    parser.add_argument("--expected-article-id", type=int, metavar="ID",
                        help="Optional public Figshare article ID to require in the verified landing set")
    args = parser.parse_args(argv)
    if args.check_landings and not args.live:
        parser.error("--check-landings requires --live")
    if args.require_landing_http and not (args.live and args.check_landings):
        parser.error("--require-landing-http requires --live --check-landings")
    if args.expected_article_id is not None and (args.expected_article_id <= 0 or
                                                  not args.require_landing_http):
        parser.error("--expected-article-id requires a positive ID and --require-landing-http")
    report = inspect_doi(args.doi, live=args.live, check_landings=args.check_landings)
    accepted = True
    if args.require_landing_http:
        result = report["result"]
        candidates = result["candidates"]
        doi = normalize_doi(args.doi)
        accepted = (report["verification_scope"] == "landing_html_only"
                    and bool(doi) and doi.startswith("10.1021/")
                    and result.get("doi") == doi
                    and result.get("fallback_url") == "https://doi.org/" + doi
                    and result["status"] == "verified"
                    and _verified_collection(candidates, doi)
                    and (args.expected_article_id is None or
                         any(c["article_id"] == args.expected_article_id for c in candidates)))
        report["landing_http_gate"] = "pass" if accepted else "fail"
    print(json.dumps(report, indent=2, ensure_ascii=True, sort_keys=True))
    return 0 if accepted else 2


if __name__ == "__main__":
    raise SystemExit(main())
