#!/usr/bin/env python3
"""Read-only ACS/Figshare SI discovery prototype; DOI in, public metadata out.

No PDF bytes, notes, paths or credentials are sent. This module is deliberately
standalone; the LUNA Qt integration and link-reachability gate are future work.
"""
from __future__ import annotations

from collections import Counter
from dataclasses import asdict, dataclass
import json
import os
import re
import time
import unicodedata
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



# Public metadata filenames are display labels, never trusted local paths.
# Reject Windows device names, invisible controls and bidirectional overrides
# before exposing a label that may later be offered for manual SI matching.
_RESERVED_DEVICE = re.compile(r"^(?:con|prn|aux|nul|com[1-9¹²³]|lpt[1-9¹²³])$", re.I)


def safe_si_filename(value: object) -> bool:
    """Accept only labels safe both as supplied and after Unicode NFKC.

    NFKC is used for duplicate detection and may be applied by a later
    local-file matcher. Fullwidth slashes, colons or device names must not
    become unsafe paths after that normalization.
    """
    if not isinstance(value, str):
        return False
    for name in (value, unicodedata.normalize("NFKC", value)):
        if (not name or len(name) > 255 or name != name.strip()
                or name[-1] in ". "
                or any(ch in '<>:"/\\\\|?*' or not ch.isprintable() for ch in name)):
            return False
        # Windows also treats CON.txt, COM1.pdf and NUL .pdf as device names.
        if _RESERVED_DEVICE.fullmatch(name.split(".", 1)[0].rstrip(" ")):
            return False
    return True


def safe_si_title(value: object) -> str:
    """Only use bounded printable publisher titles as matching/display evidence.

    Untrusted Figshare JSON can contain objects, invisible bidi controls or
    unbounded text. Do not stringify arbitrary objects or silently truncate
    a title into an apparently exact matching key.
    """
    if not isinstance(value, str):
        return ""
    title = value.strip()
    if not title or len(title) > 240 or any(not ch.isprintable() for ch in title):
        return ""
    return title


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
    # Figshare's documented PublicFilePresenter.id, not a verified download
    # link or permission to associate a local file. None means display-only.
    file_id: int | None = None


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
        detail_rate_limited = 0
        truncated_files = 0
        invalid_file_entries = 0
        invalid_article_titles = 0
        ambiguous_file_entries = 0
        ambiguous_file_ids = 0
        # Collect numeric file-ID ownership before rejecting unsafe filenames
        # or suppressing same-article duplicates. Otherwise a rejected entry
        # can conceal an ID collision with a different article.
        raw_file_id_owners: dict[int, set[int]] = {}
        # An ID hidden beyond the display cap (or behind an unsafe filename)
        # still conflicts with a displayed ID. The HTTP JSON byte limit
        # bounds this scan; only the first MAX_FILES are ever displayed.
        raw_file_id_counts: Counter[tuple[int, int]] = Counter()
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
            except HTTPError as exc:
                if exc.code == 429:
                    # Stop immediately on the provider's back-pressure signal.
                    # Continuing to request later article details can worsen
                    # throttling and must not turn an incomplete lookup into
                    # a definitive "not_found".
                    detail_rate_limited = 1
                    break
                failed_details += 1
                continue
            except (ValueError, URLError, TimeoutError, OSError, json.JSONDecodeError):
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
            title = safe_si_title(detail.get("title"))
            if not title:
                invalid_article_titles += 1
            files = detail.get("files")
            if not isinstance(files, list):
                rejected += 1
                continue
            # Do not silently claim a complete SI listing when the public
            # detail record contains more files than this bounded prototype
            # can display. Retain the first MAX_FILES for user review.
            if len(files) > MAX_FILES:
                truncated_files += 1
            # Scan IDs from the ENTIRE bounded JSON response before applying
            # the 40-file display cap. Otherwise a tail entry can conceal a
            # same-article or cross-article ID collision with a visible SI.
            for item in files:
                if isinstance(item, dict):
                    observed_id = item.get("id")
                    if type(observed_id) is int and 0 < observed_id < 10**15:
                        raw_file_id_owners.setdefault(observed_id, set()).add(article_id)
                        raw_file_id_counts[(article_id, observed_id)] += 1
            # Figshare documents a numeric PublicFilePresenter.id. Preserve
            # it as evidence for display/review, but never infer that the file
            # URL was opened or that a local SI is linked. Older/partial
            # metadata without an id remains display-only (file_id=None).
            valid_entries: list[tuple[str, int | None]] = []
            for item in files[:MAX_FILES]:
                if not isinstance(item, dict) or not safe_si_filename(item.get("name")):
                    invalid_file_entries += 1
                    continue
                file_id = item.get("id")
                if "id" in item and (type(file_id) is not int or
                                     not (0 < file_id < 10**15)):
                    invalid_file_entries += 1
                    continue
                valid_entries.append((item["name"], file_id))
            keys = [unicodedata.normalize("NFKC", name).casefold()
                    for name, _ in valid_entries]
            name_counts = Counter(keys)
            # Suppress displayed entries when an ID occurs anywhere in the
            # article, even in a truncated tail or an invalid filename.
            ambiguous_file_entries += sum(name_counts[key] > 1 for key in keys)
            ambiguous_file_ids += sum(
                file_id is not None and raw_file_id_counts[(article_id, file_id)] > 1
                for _, file_id in valid_entries)
            for (filename, file_id), key in zip(valid_entries, keys):
                if (name_counts[key] > 1 or
                        (file_id is not None and raw_file_id_counts[(article_id, file_id)] > 1)):
                    continue
                # Never expose download_url: only the official article landing
                # is offered, with reachability still unverified.
                candidates.append(SiCandidate(article_id, doi, title, filename, landing,
                                              file_id=file_id))
        # Figshare file IDs identify public files across articles, not merely
        # within one article. A repeated ID under two different article IDs
        # makes ownership ambiguous; suppress every conflicting occurrence.
        # Keep unrelated SI entries available for manual review.
        cross_article_ids = {
            file_id for file_id, owners in raw_file_id_owners.items() if len(owners) > 1
        }
        cross_article_file_id_entries = sum(
            candidate.file_id in cross_article_ids for candidate in candidates
        )
        if cross_article_ids:
            candidates = [candidate for candidate in candidates
                          if candidate.file_id not in cross_article_ids]
        if candidates:
            # A rejected summary/detail makes the discovered set incomplete.
            # A full first page may conceal more records, even with zero files.
            status = "partial" if (rejected or failed_details or detail_rate_limited or truncated_files
                                   or invalid_file_entries or invalid_article_titles or ambiguous_file_entries
                                   or ambiguous_file_ids or cross_article_file_id_entries
                                   or len(found) == MAX_ARTICLES) else "candidates"
        elif detail_rate_limited:
            status = "rate_limited"
        elif (rejected or failed_details or truncated_files or invalid_file_entries
              or ambiguous_file_entries or ambiguous_file_ids
              or invalid_article_titles
              or cross_article_file_id_entries or len(found) == MAX_ARTICLES):
            status = "needs_review"
        else:
            status = "not_found"
        diagnostic = (f"rejected={rejected};detail_failures={failed_details};"
                      f"detail_rate_limited={detail_rate_limited};"
                      f"search_page_full={int(len(found) == MAX_ARTICLES)};"
                      f"file_lists_truncated={truncated_files};"
                      f"invalid_file_entries={invalid_file_entries};"
                      f"invalid_article_titles={invalid_article_titles};"
                      f"ambiguous_file_entries={ambiguous_file_entries};"
                      f"ambiguous_file_ids={ambiguous_file_ids};"
                      f"cross_article_file_id_entries={cross_article_file_id_entries}")
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
