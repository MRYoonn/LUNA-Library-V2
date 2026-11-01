#!/usr/bin/env python3
"""Opt-in public ACS/Figshare metadata + landing smoke probe.

No PDF or SI file is downloaded. This is a manual diagnostic, not an
end-to-end acceptance test or a claim that individual SI files open.
"""
from __future__ import annotations

import argparse
import json

try:
    from tools.si_finder import lookup_acs_figshare
    from tools.si_landing_verify import verify_landing_candidates
except ImportError:
    from si_finder import lookup_acs_figshare
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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--doi", required=True, help="One public ACS article DOI")
    parser.add_argument("--live", action="store_true", help="Opt in to public Figshare API requests")
    parser.add_argument("--check-landings", action="store_true",
                        help="Also opt in to bounded HEAD/GET landing checks (requires --live)")
    args = parser.parse_args(argv)
    if args.check_landings and not args.live:
        parser.error("--check-landings requires --live")
    report = inspect_doi(args.doi, live=args.live, check_landings=args.check_landings)
    print(json.dumps(report, indent=2, ensure_ascii=True, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
