"""Explicit bounded public Figshare landing-page reachability check.

Not a PDF downloader or scraper. A matching DOI is not proof a landing URL
opens; only a direct HTTPS HTML response can promote an SI candidate.
"""
from __future__ import annotations

from dataclasses import replace
import os
from typing import Callable
from urllib.error import HTTPError, URLError
from urllib.request import HTTPRedirectHandler, Request, build_opener

try:
    from tools.si_finder import SiLookupResult, safe_figshare_landing
except ImportError:
    from si_finder import SiLookupResult, safe_figshare_landing

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
            if status not in ((200, 204) if method == "HEAD" else (200, 206)):
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
    """Probe only a strictly allowlisted HTTPS /articles/ landing URL."""
    if safe_figshare_landing(url) != url:
        return "unsafe_url"
    first = _request("HEAD", url)
    return _request("GET", url) if first == "head_unsupported" else first


def verify_landing_candidates(
    result: SiLookupResult,
    probe: Callable[[str], str] = official_landing_status,
    *, offline: bool = False,
) -> SiLookupResult:
    """Return a new result, preserving DOI identity and candidate order.

    A partial Figshare search cannot become fully verified. No local document
    bytes, filenames or paths are sent; caller explicitly opts into this check.
    """
    if result.status not in ("candidates", "partial") or not result.candidates:
        return result
    if offline or os.environ.get("LUNA_SI_OFFLINE") == "1":
        return replace(result, status="offline", diagnostic="landing_check_offline")
    cache: dict[str, str] = {}
    checked = []
    for candidate in result.candidates:
        url = candidate.landing_url
        if url not in cache:
            if len(cache) >= MAX_LANDINGS:
                cache[url] = "verification_limit"
            elif safe_figshare_landing(url) != url:
                cache[url] = "unsafe_url"
            else:
                try:
                    code = probe(url)
                except (HTTPError, URLError, TimeoutError, OSError, ValueError):
                    code = "network_unchecked"
                cache[url] = code if code in (
                    "reachable_html", "redirect_unchecked", "rate_limited",
                    "not_found", "non_html", "http_unchecked",
                    "network_unchecked", "head_unsupported", "unsafe_url"
                ) else "network_unchecked"
        code = cache[url]
        status = "landing_verified" if code == "reachable_html" else "landing_unchecked_" + code
        checked.append(replace(candidate, status=status))
    verified = sum(c.status == "landing_verified" for c in checked)
    all_verified = verified == len(checked) and result.status == "candidates"
    overall = "verified" if all_verified else "partial_verified" if verified else "needs_review"
    return replace(result, status=overall, candidates=tuple(checked),
                   diagnostic=f"landings_checked={min(len(cache), MAX_LANDINGS)};"
                              f"files_verified={verified};search_partial={int(result.status == 'partial')}")
