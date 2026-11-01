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
