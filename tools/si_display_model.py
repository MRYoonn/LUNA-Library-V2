"""Read-only SI list projection for a future LUNA card adapter.

This is not a file matcher, live verifier, or Qt UI. Public Figshare
candidates and user-confirmed local links are deliberately separate rows.
No PDF access, QSettings writes, networking, or local absolute paths.
"""
from __future__ import annotations

from collections import Counter
from pathlib import PurePosixPath
import unicodedata

try:
    from tools.si_associations import AssociationIndex, document_id
    from tools.si_finder import (SiCandidate, SiLookupResult, safe_figshare_landing,
                                 safe_si_filename, safe_si_title)
except ImportError:
    from si_associations import AssociationIndex, document_id
    from si_finder import (SiCandidate, SiLookupResult, safe_figshare_landing,
                           safe_si_filename, safe_si_title)

DISPLAY_STATUSES = frozenset(("candidates", "partial", "verified",
                              "partial_verified", "needs_review"))
VERIFIED_STATUSES = frozenset(("verified", "partial_verified"))


def project_si_rows(index: AssociationIndex, main_id: str,
                    lookup: SiLookupResult | None = None,
                    *, live_landing_check_completed: bool = False) -> dict:
    """Build privacy-minimal display data without associating any new files.

    A caller may set live_landing_check_completed only after the existing
    bounded HTTP landing verifier returned its result. A metadata-only lookup,
    restored JSON, offline/error result, or DOI mismatch must not imply a
    verified landing. This flag is an integration contract, not a proof token.
    """
    if not isinstance(index, AssociationIndex):
        raise ValueError("invalid_index")
    # Never render a mutated/invalid link graph as user-confirmed evidence.
    checked = AssociationIndex.from_json(index.to_json())
    parent_id = document_id(main_id)
    parent = checked.documents.get(parent_id)
    if parent is None or parent.kind != "main":
        raise ValueError("invalid_main_document")
    fallback = "https://doi.org/" + parent.doi
    local = [
        {"document_id": si.id, "filename": PurePosixPath(si.relative_path).name,
         "status": "user_confirmed"}
        for si in checked.linked_si(parent_id)
    ]
    result = {"main_document_id": parent_id, "parent_doi": parent.doi,
              "local_si": local, "public_si": [], "lookup_status": "not_checked",
              "fallback_url": fallback}
    if lookup is None:
        return result
    if not isinstance(lookup, SiLookupResult) or lookup.doi != parent.doi:
        result["lookup_status"] = "needs_review"
        return result
    if (not isinstance(lookup.status, str)
            or not isinstance(lookup.candidates, (tuple, list))
            or len(lookup.candidates) > 800):
        result["lookup_status"] = "needs_review"
        return result
    result["lookup_status"] = lookup.status
    if lookup.status not in DISPLAY_STATUSES:
        if lookup.status not in ("offline", "not_found", "rate_limited",
                                 "provider_unavailable", "unsupported_publisher"):
            result["lookup_status"] = "needs_review"
        return result

    # Revalidate untrusted metadata at the final display boundary. Suppress
    # *all* colliding file identities, rather than picking the first entry.
    candidates = []
    for candidate in lookup.candidates:
        if (not isinstance(candidate, SiCandidate)
                or candidate.resource_doi != parent.doi
                or type(candidate.article_id) is not int
                or not 0 < candidate.article_id < 10**15
                or (candidate.file_id is not None and
                    (type(candidate.file_id) is not int or
                     not 0 < candidate.file_id < 10**15))
                or not safe_si_filename(candidate.filename)
                or not safe_si_title(candidate.title)
                or safe_figshare_landing(candidate.landing_url,
                                         candidate.article_id) != candidate.landing_url):
            continue
        candidates.append(candidate)
    names = Counter((c.article_id,
                     unicodedata.normalize("NFKC", c.filename).casefold())
                    for c in candidates)
    ids = Counter(c.file_id for c in candidates if c.file_id is not None)
    # A partly malformed/restored "verified" collection cannot certify the
    # remaining rows as a complete, correctly attributed SI listing.
    collection_complete = (len(candidates) == len(lookup.candidates)
                           and all(count == 1 for count in names.values())
                           and all(count == 1 for count in ids.values()))
    if not collection_complete:
        result["lookup_status"] = "needs_review"
    for c in candidates:
        if (names[(c.article_id,
                   unicodedata.normalize("NFKC", c.filename).casefold())] > 1
                or (c.file_id is not None and ids[c.file_id] > 1)):
            continue
        verified = (collection_complete and live_landing_check_completed is True
                    and lookup.status in VERIFIED_STATUSES
                    and c.status == "landing_verified")
        result["public_si"].append({
            "article_id": c.article_id, "file_id": c.file_id,
            "filename": c.filename, "title": c.title,
            "landing_url": c.landing_url,
            "status": "landing_verified" if verified else "unconfirmed",
            # Never infer a local document match from title/DOI/filename.
            "local_document_id": None,
        })
    return result
