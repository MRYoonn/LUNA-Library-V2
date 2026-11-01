# SI Finder & Linker — isolated ACS/Figshare discovery prototype

## Current boundary

- Independent Python 3.10+ reference implementation in tools/si_finder.py; not wired to LUNA UI or PDF4QT.
- Input: **verified parent article DOI** from PR #16; never derive identity from SI references or filenames.
- Figshare v2 public POST /articles/search with exact resource_doi, then GET /articles/{id} for each returned record. Detail resource_doi and id must match. url_public_html must be an HTTPS Figshare /articles/ landing page. One candidate per file, so SI1/SI2 are visible separately.
- Status metadata_match_link_unchecked means metadata identity matches, **not** that a link was opened successfully. Only a later explicit reachability check can promote to verified. A DOI resolver fallback is labeled fallback, not confirmed SI.
- Only official public metadata requests; no article HTML scraping, no login, no PDF downloading, no local files/notes/paths sent, no automatic file modifications.
- Only ACS 10.1021/ DOI supported initially. Other publishers return unsupported_publisher.

## Run

python -B -m unittest discover -s tests -p 'test_si_finder.py' -v

Tests are synthetic, offline and use an injected API. No claims about live ACS API availability or link reachability. LUNA_SI_OFFLINE=1 forces no network.

Official Figshare API docs: https://docs.figshare.com/old_docs/api/articles/ and https://docs.figshare.com/v2/

## Integration order

1. Port the tested model and fixed-host request logic to standalone Qt/C++ (or provide a narrowly scoped adapter). Reuse PR #16 Metadata::normalizeDoi, do not copy the DOI extractor. Use Qt network's manual redirects, timeout, bounded response, rate-limit backoff, and offline switch.
2. Verify live ACS/Figshare API responses against authorized public DOI fixtures; separately test whether public landing pages open. Keep link statuses unverified until checked. Add safe pagination/429 backoff if necessary.
3. Add a local association store keyed by parent DOI plus stable local document identity, with explicit manual correction, relocation/rename/restart tests. Do not rewrite lunafilemove.h or persist private records in GitHub.
4. Add a small SI list entry to LUNA cards only after independent tests and manual Windows acceptance. Then consider RSC/Wiley/Nature/Elsevier adapters.

## Prior code and review gates

- Base branch: feature/citation-metadata-engine (Draft PR #16), stacked over #15 and #13. Do not merge to main or publish a Release.
- Issue #9 owns PDF+thumbnail move semantics. No file movement is performed here.
- tools/preflight_public.py has an explicit allowlist. New public source/test/doc entries must be reviewed and added there.
- Known limitations: live API not exercised, Qt/MSVC not compiled, no persistence, no local SI matching, no UI, no real link reachability. Do not call this complete.


## Follow-up: local association reference contract (2026-11-01)

New isolated files: tools/si_associations.py and tests/test_si_associations.py.
This is NOT a production LUNA association store or automatic file matcher.

- AssociationIndex models multiple SI documents under one verified parent DOI, keyed by a caller-provided stable document UUID, not PDF filename or path. It accepts a user-confirmed link only; matching a DOI, a filename, or a Figshare search result alone cannot silently link a local file.
- It holds only slash-relative PDF locations under a caller-owned root, rejects path traversal, Windows reserved device names, case-insensitive collisions, duplicate UUIDs, stale moves, orphaned links, and conflicting SI parentage. It does not open, copy, move, delete, or modify files.
- relocate(id, expected_old, new_path) updates an in-memory location only after Issue #9's existing safe file move has succeeded. The caller must persist the resulting versioned JSON atomically on the user's local PC. Export/import supports a strict v1 schema and restart round-trips. A copied PDF must receive a new UUID and never inherits an SI association without explicit confirmation.
- Existing src/lunabibliography.h uses path-derived QSettings keys and relocate() for citation records. A durable UUID mapping/sidecar compatible with the actual Qt application is NOT yet implemented. Do not assume UUID persistence is solved or bind links to paths as a workaround.
- Existing src/lunafilemove.h moves PDF, thumbnail and metadata sidecar; no change is made here. A real Qt integration must reconcile failed/rolled-back moves, path casing, disk root changes, and the QSettings/sidecar lifecycle before user-facing claims.
- This module imports the existing si_finder.normalize_doi reference normalizer; the future native port must reuse luna::Metadata::normalizeDoi from PR #16 rather than duplicate extraction logic.

Synthetic offline test command:

    python -B -m unittest discover -s tests -p 'test_si_associations.py' -v

The isolated association contract passed 14/14 Python 3.13 tests locally. It was tested with a local copy of the existing DOI normalizer, not a full GitHub checkout. Full exact-HEAD public gate, Qt/MSVC, real PDF move/restart, API reachability, and real Windows acceptance remain NOT RUN. No live ACS/Figshare links were verified.

Next actionable gate: create a Qt-only test adapter using PR #16's verified DOI and a durable per-document UUID contract; run it against temporary PDF+sidecar fixtures and a real Windows build, without changing PDF4QT or merging this Draft. Separately validate live ACS/Figshare public metadata and landing-page reachability without binary downloads.
