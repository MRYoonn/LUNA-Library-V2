"""Explicit bounded public Figshare landing-page reachability check.

Not a PDF downloader or scraper. A matching DOI is not proof a landing URL
opens; only a direct HTTPS HTML response can promote an SI candidate.
"""
from __future__ import annotations

from collections import Counter
from dataclasses import replace
import os
import unicodedata
from typing import Callable
from urllib.error import HTTPError, URLError
from urllib.request import HTTPRedirectHandler, Request, build_opener

try:
    from tools.si_finder import SiLookupResult, normalize_doi, safe_figshare_landing, safe_si_filename
except ImportError:
    from si_finder import SiLookupResult, normalize_doi, safe_figshare_landing, safe_si_filename

MAX_LANDINGS = 8
HTML_TYPES = frozenset(("text/html", "application/xhtml+xml"))


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        return None


def _request(method: str, url: str) -> str:
    headers = {"Accept": "text/html,application/xhtml+xml", "Accept-Encoding": "identity",
               "User-Agent": "LUNA-SI-Landing-Check/0.1 (public landing only)"}
    if method == "GET":
        headers["Range"] = "bytes=0-0"
    request = Request(url, headers=headers, method=method)
    try:
        with build_opener(NoRedirect()).open(request, timeout=6.0) as response:
            if response.geturl() != url:
                return "redirect_unchecked"
            status = response.status
            media = response.headers.get("Content-Type", "").split(";", 1)[0].strip().lower()
            if status not in ((200,) if method == "HEAD" else (200, 206)):
                return "http_unchecked"
            if media not in HTML_TYPES:
                return "non_html"
            # Never read the response body, even on GET fallback.
            return "reachable_html"
    except HTTPError as exc:
        if method == "HEAD" and exc.code in (405, 501):
            return "head_unsupported"
        if exc.code in (301, 302, 303, 307, 308):
            return "redirect_unchecked"
        if exc.code == 429:
            return "rate_limited"
        if exc.code == 404:
            return "not_found"
        return "http_unchecked"
    except (URLError, TimeoutError, OSError, ValueError):
        return "network_unchecked"


def official_landing_status(url: str) -> str:
    """Require a real bounded HTML GET; HEAD alone never verifies a landing.

    A server can allow HEAD while rejecting GET, so only a successful HTML
    GET (without reading the response body) proves the public landing opens.
    HEAD 405/501 is an ordinary fallback; other HEAD failures are final.
    """
    if safe_figshare_landing(url) != url:
        return "unsafe_url"
    first = _request("HEAD", url)
    if first not in ("reachable_html", "head_unsupported"):
        return first
    return _request("GET", url)


def verify_landing_candidates(
    result: SiLookupResult,
    probe: Callable[[str], str] = official_landing_status,
    *, offline: bool = False,
) -> SiLookupResult:
    """Return a new result, preserving DOI identity and candidate order.

    A partial Figshare search cannot become fully verified. No local document
    bytes, filenames or paths are sent; caller explicitly opts into this check.
    """
    # Previously checked results can be restored and checked again. Never
    # preserve an old landing_verified claim merely because the result already
    # says verified/partial_verified; an explicit offline or failed recheck
    # must revoke that evidence. Uncertain restored states may be retried, but
    # cannot be promoted to a complete listing without fresh finder evidence.
    # A restored lookup result is untrusted even when it has no candidates.
    # Canonicalize fallback for EVERY status, including not_found/429/offline:
    # otherwise a forged external URL can survive without a landing probe.
    doi = normalize_doi(result.doi)
    valid_parent = bool(doi and doi == result.doi and doi.startswith("10.1021/"))
    fallback = "https://doi.org/" + doi if valid_parent else ""
    refreshable = ("candidates", "partial", "verified", "partial_verified",
                   "needs_review", "offline")
    if result.status not in refreshable:
        if result.candidates:
            # Provider errors and unsupported/unknown lookup states cannot
            # carry independently verified SI entries. Never probe or preserve
            # stale landing_verified evidence from an inconsistent snapshot.
            return replace(result, status="needs_review", fallback_url=fallback,
                           candidates=tuple(replace(c, status="landing_unchecked_untrusted_lookup_status")
                                            for c in result.candidates),
                           diagnostic="untrusted_lookup_status")
        return replace(result, fallback_url=fallback)
    if not result.candidates:
        if result.status in ("verified", "partial_verified"):
            return replace(result, status="needs_review", fallback_url=fallback,
                           diagnostic="verified_without_candidates")
        return replace(result, fallback_url=fallback)
    if offline or os.environ.get("LUNA_SI_OFFLINE") == "1":
        # An offline check must not preserve a stale/forged landing_verified
        # status from a restored candidate. It has made no HTTP request.
        return replace(result, status="offline", fallback_url=fallback,
                       candidates=tuple(replace(c, status="landing_unchecked_offline")
                                        for c in result.candidates),
                       diagnostic="landing_check_offline")
    # A reachable page cannot certify a candidate whose parent DOI differs
    # from the lookup result. Do not trust externally assembled result objects.
    if not valid_parent:
        return replace(result, status="needs_review", fallback_url="",
                       candidates=tuple(replace(c, status="landing_unchecked_invalid_parent_doi")
                                        for c in result.candidates),
                       diagnostic="invalid_parent_doi")
    # A verifier can be called with a restored/foreign result, not only with
    # freshly sanitized finder output. Fail closed on duplicate public file
    # identities and unsafe labels before probing a shared article landing.
    # Landing reachability never proves an individual file is accessible.
    name_counts = Counter(
        (c.article_id, unicodedata.normalize("NFKC", c.filename).casefold())
        for c in result.candidates
        if type(c.article_id) is int and c.article_id > 0
        and safe_si_filename(c.filename)
    )
    id_counts = Counter(
        c.file_id for c in result.candidates
        if type(c.file_id) is int and c.file_id > 0
    )
    cache: dict[str, str] = {}
    checked = []
    probes_sent = 0
    throttled = False
    for candidate in result.candidates:
        url = candidate.landing_url
        # Validate the candidate-to-landing identity on EVERY item, including
        # cached URLs: two different article IDs must never share verification.
        if candidate.resource_doi != doi:
            code = "doi_mismatch"
        elif not safe_si_filename(candidate.filename):
            code = "unsafe_filename"
        elif candidate.file_id is not None and (
                type(candidate.file_id) is not int or not 0 < candidate.file_id < 10**15):
            code = "invalid_file_id"
        elif (name_counts[(candidate.article_id,
                           unicodedata.normalize("NFKC", candidate.filename).casefold())] > 1
              or (candidate.file_id is not None and id_counts[candidate.file_id] > 1)):
            code = "ambiguous_file_identity"
        elif safe_figshare_landing(url, candidate.article_id) != url:
            code = "unsafe_url"
        else:
            if url not in cache:
                if throttled:
                    # Respect provider back-pressure across distinct landings.
                    # Previously later URLs were still probed after HTTP 429.
                    cache[url] = "rate_limited"
                elif probes_sent >= MAX_LANDINGS:
                    cache[url] = "verification_limit"
                else:
                    probes_sent += 1
                    try:
                        response = probe(url)
                    except HTTPError as exc:
                        response = "rate_limited" if exc.code == 429 else "network_unchecked"
                    except (URLError, TimeoutError, OSError, ValueError):
                        response = "network_unchecked"
                    cache[url] = response if response in (
                        "reachable_html", "redirect_unchecked", "rate_limited",
                        "not_found", "non_html", "http_unchecked",
                        "network_unchecked", "head_unsupported", "unsafe_url"
                    ) else "network_unchecked"
                    if cache[url] == "rate_limited":
                        throttled = True
            code = cache[url]
        status = "landing_verified" if code == "reachable_html" else "landing_unchecked_" + code
        checked.append(replace(candidate, status=status))
    verified = sum(c.status == "landing_verified" for c in checked)
    all_verified = verified == len(checked) and result.status in ("candidates", "verified")
    overall = "verified" if all_verified else "partial_verified" if verified else "needs_review"
    return replace(result, status=overall, fallback_url=fallback,
                   candidates=tuple(checked), diagnostic=f"landings_checked={probes_sent};"
                              f"landing_entries_verified={verified};search_partial={int(result.status not in ('candidates', 'verified'))};"
                              f"landing_rate_limited={int(throttled)}")
