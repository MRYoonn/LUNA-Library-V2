#!/usr/bin/env python3
"""Read-only ACS/Figshare SI discovery prototype; DOI in, public metadata out.

No PDF bytes, notes, paths or credentials are sent. This module is deliberately
standalone; the LUNA Qt integration and link-reachability gate are future work.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass
import json
import os
import re
import time
from typing import Callable
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

API_ROOT = "https://api.figshare.com/v2"
DOI_RE = re.compile(r"^10\.\d{4,9}/[-._;()/:a-z0-9]+$", re.I)
MAX_BYTES = 1024 * 1024
MAX_ARTICLES = 20
MAX_FILES = 40


def normalize_doi(raw: str) -> str:
    if not isinstance(raw, str):
        return ""
    value = re.sub(r"^doi\s*:\s*", "", raw.strip(), flags=re.I).strip()
    value = re.sub(r"^https?://(?:dx\.)?doi\.org/", "", value, flags=re.I)
    value = re.sub(r"\s+", "", value).rstrip(".,;:!?]}>\"'”’")
    while value.endswith(")") and value.count(")") > value.count("("):
        value = value[:-1]
    return value.lower() if len(value) <= 220 and DOI_RE.fullmatch(value) else ""


def safe_figshare_landing(url: object, article_id: int | None = None) -> str:
    """Validate a canonical public article landing, optionally bound to its API id.

    Figshare documents that the final numeric path component is the article
    id. Reject query strings, encoded path separators and noncanonical paths
    so a different reachable page cannot verify an unrelated SI record.
    """
    if not isinstance(url, str) or len(url) > 2048:
        return ""
    if any(ord(ch) < 32 for ch in url) or url != url.strip():
        return ""
    try:
        parsed = urlsplit(url)
        port = parsed.port
    except ValueError:
        return ""
    host = (parsed.hostname or "").lower()
    if (parsed.scheme != "https" or parsed.username or parsed.password
            or port not in (None, 443) or parsed.fragment or parsed.query
            or host not in ("acs.figshare.com", "figshare.com")
            or "%" in parsed.path or "\\" in parsed.path):
        return ""
    segments = parsed.path.split("/")
    if (len(segments) < 4 or segments[:2] != ["", "articles"]
            or not all(re.fullmatch(r"[A-Za-z0-9_-]+", part) for part in segments[2:])):
        return ""
    numeric_id = segments[-1]
    if not re.fullmatch(r"[1-9][0-9]*", numeric_id):
        return ""
    if article_id is not None and (type(article_id) is not int or int(numeric_id) != article_id):
        return ""
    return url


_last_request_at = 0.0


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        return None


def official_json(method: str, url: str, body: dict | None = None) -> object:
    """Bounded HTTPS-only metadata requests, with redirects disabled."""
    global _last_request_at
    parsed = urlsplit(url)
    if (parsed.scheme, parsed.hostname, parsed.port) != ("https", "api.figshare.com", None):
        raise ValueError("blocked_host")
    if not (url == API_ROOT + "/articles/search" or
            re.fullmatch(re.escape(API_ROOT) + r"/articles/[1-9][0-9]*", url)):
        raise ValueError("blocked_endpoint")
    if not ((method == "POST" and url.endswith("/articles/search") and isinstance(body, dict))
            or (method == "GET" and body is None and url != API_ROOT + "/articles/search")):
        raise ValueError("blocked_method")
    # Figshare recommends at most one request per second.
    now = time.monotonic()
    time.sleep(max(0.0, 1.0 - (now - _last_request_at))) if _last_request_at else None
    _last_request_at = time.monotonic()
    data = json.dumps(body).encode("utf-8") if body is not None else None
    request = Request(url, data=data, method=method,
                      headers={"Accept": "application/json", "Content-Type": "application/json",
                               "User-Agent": "LUNA-SI-Finder/0.1 (public metadata only)"})
    with build_opener(NoRedirect()).open(request, timeout=8.5) as response:
        if response.status != 200:
            raise ValueError("http_status")
        data = response.read(MAX_BYTES + 1)
        if len(data) > MAX_BYTES:
            raise ValueError("response_too_large")
    return json.loads(data)


@dataclass(frozen=True)
class SiCandidate:
    article_id: int
    resource_doi: str
    title: str
    filename: str
    landing_url: str
    # An exact metadata DOI match is NOT proof the landing page is reachable.
    status: str = "metadata_match_link_unchecked"


@dataclass(frozen=True)
class SiLookupResult:
    doi: str
    status: str
    candidates: tuple[SiCandidate, ...]
    fallback_url: str
    diagnostic: str = ""

    def to_dict(self) -> dict:
        return {**asdict(self), "candidates": [asdict(c) for c in self.candidates]}


def lookup_acs_figshare(
    raw_doi: str,
    request_json: Callable[[str, str, dict | None], object] = official_json,
    *,
    offline: bool = False,
) -> SiLookupResult:
    """Search public Figshare records by exact resource DOI and read details.

    Does not fetch PDF files, follow article links or persist anything. Network
    errors and uncertain identity always produce non-success status.
    """
    doi = normalize_doi(raw_doi)
    if not doi:
        return SiLookupResult("", "invalid_doi", (), "")
    fallback = "https://doi.org/" + doi
    if offline or os.environ.get("LUNA_SI_OFFLINE") == "1":
        return SiLookupResult(doi, "offline", (), fallback)
    if not doi.startswith("10.1021/"):
        return SiLookupResult(doi, "unsupported_publisher", (), fallback)
    try:
        found = request_json("POST", API_ROOT + "/articles/search",
                             {"resource_doi": doi, "page": 1, "page_size": MAX_ARTICLES})
        if not isinstance(found, list):
            raise ValueError("invalid_search_json")
        if len(found) > MAX_ARTICLES:
            raise ValueError("unbounded_search_json")
        candidates: list[SiCandidate] = []
        seen: set[int] = set()
        rejected = 0
        failed_details = 0
        truncated_files = 0
        for summary in found:
            if not isinstance(summary, dict):
                rejected += 1
                continue
            article_id = summary.get("id")
            if type(article_id) is not int or not (0 < article_id < 10**15):
                rejected += 1
                continue
            if article_id in seen:
                continue
            seen.add(article_id)
            try:
                detail = request_json("GET", f"{API_ROOT}/articles/{article_id}", None)
            except (ValueError, HTTPError, URLError, TimeoutError, OSError, json.JSONDecodeError):
                failed_details += 1
                continue
            if (not isinstance(detail, dict) or type(detail.get("id")) is not int
                    or detail["id"] != article_id):
                rejected += 1
                continue
            if normalize_doi(detail.get("resource_doi", "")) != doi:
                rejected += 1
                continue
            landing = safe_figshare_landing(detail.get("url_public_html"), article_id)
            if not landing:
                rejected += 1
                continue
            title = str(detail.get("title") or "").strip()[:240]
            files = detail.get("files")
            if not isinstance(files, list):
                rejected += 1
                continue
            # Do not silently claim a complete SI listing when the public
            # detail record contains more files than this bounded prototype
            # can display. Retain the first MAX_FILES for user review.
            if len(files) > MAX_FILES:
                truncated_files += 1
            for item in files[:MAX_FILES]:
                if not isinstance(item, dict):
                    continue
                filename = item.get("name")
                if (not isinstance(filename, str) or not filename or len(filename) > 255
                        or any(ch in filename for ch in "/\\\r\n\0")):
                    continue
                # Do not expose unvalidated remote download URLs. Every entry
                # opens the official landing page for user review in a browser.
                candidates.append(SiCandidate(article_id, doi, title, filename, landing))
        if candidates:
            status = "partial" if (failed_details or truncated_files
                                   or len(found) == MAX_ARTICLES) else "candidates"
        elif rejected or failed_details:
            status = "needs_review"
        else:
            status = "not_found"
        diagnostic = (f"rejected={rejected};detail_failures={failed_details};"
                      f"search_page_full={int(len(found) == MAX_ARTICLES)};"
                      f"file_lists_truncated={truncated_files}")
        return SiLookupResult(doi, status, tuple(candidates), fallback, diagnostic)
    except (ValueError, HTTPError, URLError, TimeoutError, OSError, json.JSONDecodeError) as exc:
        # Never include remote response bodies or local paths in diagnostics.
        if isinstance(exc, HTTPError) and exc.code == 429:
            code = "rate_limited"
        elif isinstance(exc, HTTPError) and exc.code == 404:
            code = "not_found"
        else:
            code = "provider_unavailable"
        return SiLookupResult(doi, code, (), fallback, type(exc).__name__)
