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


## Follow-up: opt-in QtCore document UUID sidecar adapter (2026-11-01)

Added `src/lunasiidentity.h` and standalone `tests/native_si_identity/`
with `tests/test_si_identity_source.py`. This is a small isolated
**source implementation**, not a claim of successful Windows runtime integration.

- `SiIdentity::read(libraryRoot, pdf)` is strictly read-only. Missing identity
  remains `identity_missing`; no path-derived UUID or automatic link.
- `SiIdentity::ensure(libraryRoot, pdf)` must be invoked only after a user
  explicitly opts in to SI linking. It adds `documentId` (RFC4122 UUIDv4)
  to the existing local `<paper>.luna-meta.json`, preserving all other fields.
  A sidecar lock prevents competing opt-in assignments, and `QSaveFile` commits
  atomically without direct-write fallback. Corrupt, future-version, oversized,
  symlinked or malformed-ID sidecars are rejected without replacement.
- PDFs must be regular files inside a canonical library root. Neither
  original PDF bytes nor QSettings, annotations, memo or thumbnails are changed.
  No network requests or remote data uploads occur.
- Existing Issue #9 `movePdfWithThumbnails` already moves `.luna-meta.json`
  with a PDF; `copyPdfWithThumbnails` deliberately does not copy it. The
  standalone native fixture simulates move + sidecar rename and checks stable
  UUID after restart-style re-read; copying only the PDF requires a new UUID.
- The native test calls PR #16 `luna::Metadata::normalizeDoi`; there is no
  new DOI extractor. No SI associations are auto-created by this adapter.

Source-only test command:

    python -B -m unittest discover -s tests -p 'test_si_identity_source.py' -v

Standalone Qt6.5+ test commands (requires Qt6 Core SDK and CMake):

    cmake -S tests/native_si_identity -B build/si-identity
    cmake --build build/si-identity --config Release
    ctest --test-dir build/si-identity -C Release --output-on-failure

**Validation boundary:** This run has not executed a Qt/MSVC or Windows
native compile (Qt6 SDK is unavailable in the runner). The new Python
source-only test has not been executed against a full repository checkout.
All native assertions are test code, not PASS claims. Exact-HEAD Windows
CI is also not run. No real ACS/Figshare metadata or landing links were
verified. Keep PR #17 Draft.

**Next action:** Compile and run the isolated QtCore target in a Qt6.5+
Windows environment, fix actual compiler/runtime errors, and verify the
existing `.luna-meta.json` schema on real *permissioned* disposable
fixtures. Then implement a separate atomic local association-state store
and reconcile successful/rolled-back Issue #9 moves without editing the
move engine; do not integrate cards until native and privacy gates pass.
PR #16 has advanced independently; review its new HEAD before any
integration or branch update, avoiding changes to its own files.


## Follow-up: post-move identity reconciliation gate (2026-11-01)

New isolated Python reference files: tools/si_relocation_gate.py and
tests/test_si_relocation_gate.py. No PDF/sidecar move implementation was
changed. This is not yet wired into LUNA or the Windows reader.

**Important integration blocker found by inspecting the current branch:**
src/lunafilemove.h::movePdfWithThumbnails currently enumerates
.luna-thumb.png/.jpg/.jpeg and the PDF, but **does not enumerate
.luna-meta.json**. Consequently, the prior handoff's claim that the
existing Issue #9 move helper already moves the identity metadata sidecar
was incorrect. Do NOT enable SI-linked moves or assume UUID continuity
through that helper. Issue #9's owner must implement and test safe paired
metadata moves/rollback before the UI may claim move-safe SI linking.

The new read-only gate is designed to fail closed **after** a caller reports
a physical move as successful. It verifies old PDF/sidecar absence, regular
destination PDF, same UUIDv4 in the destination sidecar, v1 schema, a 64 KiB
sidecar cap, and rejects symlinks, invalid paths, stale index entries and
identity conflicts. Only a verified pair updates the **in-memory**
AssociationIndex path; it never moves/deletes files, repairs identities,
persists links, accesses the network or writes QSettings. It does not fix
the underlying move-helper bug. File-system race resistance, actual Windows
case-only moves, and a production Qt implementation remain unverified.

Test command from a full repository checkout:

    python -B -m unittest discover -s tests -p 'test_si_relocation_gate.py' -v

Development-run evidence: 12/12 offline synthetic gate tests PASS in an
isolated Python 3.13 environment with a **minimal compatibility shim** for
the existing association model. The shim was NOT committed. Full exact-HEAD
repository tests and Windows/Qt6 native execution are NOT RUN. The GitHub
files are the reviewed standalone source and tests, not a verified native
release.

Next: coordinate with Issue #9 owner to add .luna-meta.json to the
no-overwrite move+rollback contract, test synthetic same-volume moves and
failed rollback on Windows, run the gate against the actual association
module in full checkout, and then implement atomic local association-state
persistence with explicit opt-in. Do not silently modify another PR or
merge this Draft. Live ACS/Figshare SI landing links remain unverified.


## Follow-up: atomic local SI association state (2026-11-01)

Added `tools/si_state_store.py` and `tests/test_si_state_store.py` as an **opt-in Python persistence reference** for the already-existing `AssociationIndex` model; this is not a new DOI extractor, file mover, UI integration, or production Qt store.

- `read(library_root)` is non-mutating and returns an empty index with `missing` revision if no state exists.
- `commit(library_root, index, expected_revision, user_confirmed=True)` validates the full v1 graph through `AssociationIndex.from_json`, takes an exclusive local lock, compares SHA256 of current raw state against the expected revision, then writes to a same-directory temporary file, fsyncs and atomically replaces `.luna-si-associations.json`. Missing, corrupt, oversized, symlinked, future-schema, locked or concurrently modified state is never silently replaced.
- Link creation still requires `AssociationIndex.confirm_link(..., user_confirmed=True)`; a matching filename/DOI does not auto-create a link. Stored paths are slash-relative under a caller-selected library root, not personal absolute paths.
- No PDF bytes, QSettings, memo, user credentials or SI file downloads are sent to GitHub or a remote service. A stale lock after a crash requires deliberate user/operator recovery; it is **not** automatically deleted.
- The store does not fix the blocking `lunafilemove.h` omission of `.luna-meta.json`. Until Issue #9's owner safely moves/rolls back this sidecar, SI-linked physical moves must remain disabled. The Python relocation gate is a separate post-move check.
- Directory fsync is best effort on POSIX after replacement. Windows power-loss durability, antivirus interference, SMB/network drives, Windows Qt6/MSVC, live ACS links and real LUNA app integration are **not verified**.

Local isolated run:

    python -B -m unittest discover -s tests -p 'test_si_state_store.py' -v

**Evidence:** 11/11 synthetic offline tests passed under Python 3 in a disposable local harness with a minimal compatibility copy of the existing `AssociationIndex` API. That compatibility copy was **not committed**. Full exact-branch checkout tests against the actual association module and Windows/Qt native tests are **not run**. This is reference code only, not an accepted production feature.

**Next gates:** run the new tests against the exact GitHub branch with the real `tools/si_associations.py`; test lock/crash and atomic replacement on Windows; port the validated persistence boundary to a standalone QtCore `QSaveFile`/`QLockFile` component; reconcile paired PDF+UUID sidecar moves in Issue #9 before enabling the UI. Keep PR #17 Draft.


## Follow-up: opt-in post-move state reconciliation (2026-11-01)

Added `tools/si_move_reconcile.py` and `tests/test_si_move_reconcile.py` on the
existing dedicated Draft PR #17 branch. The new coordinator composes the
existing `si_relocation_gate` with `si_state_store`; it does **not** reimplement
the UUID, DOI parser, file mover or state serializer.

- Caller must capture the exact state revision **before** the physical move,
  explicitly confirm the operation, and only invoke reconciliation after Issue
  #9 has successfully moved **both** the PDF and `.luna-meta.json`.
- The coordinator clones the validated in-memory association index, audits
  old/destination PDF and sidecar identities, and persists the new relative
  path via optimistic compare-and-swap. Main↔SI1↔SI2 UUID links survive restart.
- Any stranded sidecar, wrong UUID, duplicate path, stale revision, lock,
  corrupt state or write failure leaves the **persisted association state**
  untouched. The physical move is **not** automatically rolled back. A failure
  means the app must block subsequent linked moves and offer explicit repair.
- This code does not read PDF contents, fetch publisher metadata, modify
  QSettings, change PDF4QT or send any local data to GitHub.

Run in a complete checkout:

    python -B -m unittest discover -s tests -p 'test_si_move_reconcile.py' -v
    python -B -m unittest discover -s tests -p 'test_si_*.py' -v
    python -B tools/preflight_public.py

**Evidence:** the two new Python files passed local syntax compilation, and
the committed source was checked on GitHub. The 7 new integration tests have
**NOT** been executed against the full repository. No Windows/Qt6 native test,
actual ACS/Figshare landing reachability, or production file-move acceptance
has been verified. Do not mark this as production-ready.

**Next exact action:** run the above tests on the actual PR #17 checkout,
repair any failures, then execute Qt6/Windows paired-move/rollback/restart
tests once Issue #9's owner makes the metadata sidecar part of the native
no-overwrite move contract. PR #16 advanced independently; review its latest
DOI contract before any Qt integration. Keep Draft; do not merge/release.


## Follow-up: explicit Figshare landing reachability gate (2026-11-02)

Added \`tools/si_landing_verify.py\` and \`tests/test_si_landing_verify.py\` as an
**opt-in, standalone Python reference**, building on the existing DOI-matched
\`SiLookupResult\` from \`si_finder.py\`. It does not rewrite DOI extraction,
association persistence, PDF4QT, native cards, or the Issue #9 move helper.

- A matching Figshare \`resource_doi\` yields **metadata-only** candidates.
  Only a direct HTTPS response with HTML content type from the allowlisted
  Figshare \`/articles/\` landing URL promotes to \`landing_verified\`.
- Requests use HEAD, with a bounded GET Range fallback only for 405/501.
  Redirects are **never followed**, and no response body is read. A redirect,
  404, 429, timeout, non-HTML response or unknown probe result stays
  \`landing_unchecked_*\`. No PDF bytes or file download URLs are accessed.
- One probe per distinct landing URL (maximum eight); multiple SI files on
  the same article remain separate entries. A partial/paginated Figshare
  search never becomes fully verified solely because its checked landings
  open. Offline mode makes no network requests.
- Verification means **landing page reachable at check time**, not that
  individual SI file URLs or their binary content were opened. No automatic
  local Main↔SI association is created; user confirmation remains required.
- On failure, retain the existing parent DOI resolver \`fallback_url\` and
  show an **unconfirmed** state; never fabricate an SI download link.

Run from the full branch checkout:

    LUNA_SI_OFFLINE=0 python -B -m unittest discover -s tests -p 'test_si_*.py' -v
    python -B tools/preflight_public.py

Local isolated development used a **minimal noncommitted SiCandidate/
SiLookupResult/safe_figshare_landing compatibility shim**; all 15 new
synthetic verifier tests passed there. GitHub Actions run #41319527437 at
source commit `71d2f1946b8a5c0eb1dd509af3df0600f32d19b0` then passed
**84/84 actual-repository SI Python tests** and the public privacy gate
(**140 approved source/icon files**). The subsequent handoff edit is docs-only.
This is **not** a Windows/Qt test or a live ACS/Figshare reachability test.
No real public SI landing or file link is claimed reachable by this change.

**Next exact actions:** review the GitHub Actions \`si-isolated.yml\` run for
the final commit and fix any failures. Obtain a publicly usable ACS article
DOI and verify its official Figshare metadata and direct HTML landing with
the same strict probe; record evidence without copying PDFs or private data.
Then compile \`tests/native_si_identity\` on Windows Qt6 and coordinate
Issue #9's PDF + \`.luna-meta.json\` move/rollback fix. Do not enable native
linked moves or merge Draft #17 until those blockers pass.


## Follow-up: prevent false-positive landing links (2026-11-02)

A Figshare API record can have an exact matching `resource_doi` and `id`
while its `url_public_html` points at a *different* Figshare article.
Previously, a reachable HTML page could therefore be labeled
`landing_verified` for the wrong SI. The landing verifier also cached
results solely by URL, allowing a forged candidate to reuse a legitimate
candidate's successful probe.

- `safe_figshare_landing(url, article_id)` now requires a canonical public
  `/articles/.../<numeric-id>` path with a final article ID matching the
  verified Figshare API detail record. Noncanonical paths, query strings,
  encoded path components, non-ACS tenant subdomains, and other IDs fail
  closed. Only `acs.figshare.com` and `figshare.com` are accepted in
  this ACS-first prototype. A legitimate but unsupported URL remains
  **unconfirmed**; the parent DOI fallback remains available.
- `lookup_acs_figshare` rejects mismatched landing identity before
  constructing SI candidates. `verify_landing_candidates` checks each
  candidate's ID before consulting the per-URL reachability cache.
- Added synthetic regressions for wrong IDs, query/download routes,
  traversal, and the same reachable URL reused by two different IDs in
  either order. These tests do not access any PDF or remote download URL.

The official Figshare API user guide documents that the numeric final
component of an article landing URL is its article ID:
https://info.figshare.com/user-guide/how-to-use-the-figshare-api/

Validation still required: exact-HEAD GitHub Actions test result, live
ACS/Figshare metadata and landing reachability, native Windows/Qt6 tests,
and Issue #9 paired metadata-sidecar move/rollback. Keep PR #17 Draft.

## Follow-up: fail-closed DOI/landing identity (2026-11-02)

- `tools/si_landing_verify.py` now checks the canonical ACS parent DOI on the lookup result and checks each candidate's `resource_doi` against it **before** any HTTP probe or cached landing result. A mixed-paper candidate remains `landing_unchecked_doi_mismatch`; an invalid/unsupported parent DOI remains `landing_unchecked_invalid_parent_doi`. Neither becomes a verified SI link.
- HTTP 204 (No Content) is no longer accepted as a reachable HTML landing. Diagnostics now say `landing_entries_verified` instead of `files_verified`: an HTML landing response is **not** a verified downloadable SI file.
- Added three synthetic/offline regression cases in `tests/test_si_landing_verify.py`. The source is committed; exact-HEAD GitHub Actions and native Windows/Qt6 evidence must be checked independently before reporting PASS.
- Still unverified: live ACS/Figshare API and landing availability, direct SI file availability, and Issue #9 PDF+UUID sidecar move/rollback. No HTML scraping, authentication bypass or PDF downloads were introduced.


## Follow-up: opt-in public DOI smoke CLI (2026-11-02)

Added `tools/si_live_probe.py` and `tests/test_si_live_probe.py`, reusing
the existing `si_finder.lookup_acs_figshare` and
`si_landing_verify.verify_landing_candidates`. This is an isolated diagnostic
entry point, not a new DOI parser, SI downloader, app integration, or link
association implementation.

- Default invocation makes **zero network requests** and reports `offline`.
- `--live` explicitly permits the bounded official Figshare public metadata
  lookup. Candidate SI entries remain `metadata_match_link_unchecked`.
- `--check-landings` requires `--live` and separately invokes the existing
  allowlisted HEAD/limited-GET HTML landing verifier. It never verifies SI
  binary files or fetches PDF contents. A non-candidate status (including
  404/not_found) never initiates a landing request.
- JSON output declares `verification_scope` as either `metadata_only` or
  `landing_html_only`; do not interpret a reachable landing as a verified
  file. DOI fallback is preserved on failure.
- Seven offline synthetic contract tests cover opt-in behavior, no-network
  default, metadata-only mode, a 404, and a public ACS Figshare `/articles/dataset/`
  URL structure. Tests contain no personal PDFs or local paths.

Public reference to try manually: article DOI `10.1021/acsnano.8b07631`;
ACS Figshare article `7688531` has an independently browsable public landing
at `https://acs.figshare.com/articles/dataset/Extrinsic_and_Dynamic_Edge_States_of_Two-Dimensional_Lead_Halide_Perovskites/7688531`.
The landing displays supplementary DOI `10.1021/acsnano.8b07631.s002` and
a related-materials link. **The API `resource_doi` and strict HEAD result
have not been measured in this environment**. This example is not yet a
validated DOI-to-Figshare lookup fixture.

Run from a complete checkout (one DOI per command):

    python -B tools/si_live_probe.py --doi 10.1021/acsnano.8b07631
    python -B tools/si_live_probe.py --doi 10.1021/acsnano.8b07631 --live
    python -B tools/si_live_probe.py --doi 10.1021/acsnano.8b07631 --live --check-landings
    python -B -m unittest discover -s tests -p 'test_si_*.py' -v
    python -B tools/preflight_public.py

The first command is offline. The next two are **explicit public network**
checks and must not run in the offline CI suite. The live API/HEAD checks
were blocked in this development environment by DNS resolution failure,
so no live lookup or strict landing PASS is claimed. The new seven tests
passed locally using noncommitted minimal dependency shims; exact-branch
CI must independently confirm against the actual repository files.

Next: inspect exact-HEAD GitHub Actions Python/Windows results; run the
manual live CLI from a network-enabled permitted environment, record
DOI/API detail ID/landing status without any file downloads, and keep
unconfirmed candidates unconfirmed. Issue #9 still needs the actual
`.luna-meta.json` paired move/rollback fix before native SI-linked moves
can be enabled. Keep PR #17 Draft; no merge/release.


## Follow-up: strict opt-in public DOI landing gate (2026-11-02)

The existing smoke CLI normally returns exit code 0 even for `offline`, `not_found`,
`partial`, or unverified results: this is intentional for exploratory JSON
reports, but must not be mistaken for an acceptance PASS. A separate **opt-in**
strict gate now returns exit code **2** unless all returned SI candidates have
verified HTTP HTML landing responses and the result is fully `verified`.
It never downloads SI files, opens PDFs, scrapes HTML or certifies file bytes.

```sh
python -B tools/si_live_probe.py --doi 10.1021/acsnano.8b07631 --live --check-landings --require-landing-http --expected-article-id 7688531
```

The expected ID is taken from a publicly indexed ACS Figshare landing URL,
**not** from a completed live API test. This command must be run from an
internet-enabled environment before claiming any live DOI→SI verification.
`--expected-article-id` is optional, but when supplied it must be a positive
integer and the strict gate must be enabled. Even a strict PASS confirms only
public metadata identity plus HTTP HTML landing reachability, **not** that an
individual SI file opens or downloads. Failure remains `미확인` with the
original DOI fallback; the default offline behavior is unchanged.

## 2026-11-02 — malformed Figshare file entries and moving dependency

- `tools/si_finder.py` now counts malformed file entries in an otherwise
  DOI-matched article. A mixture of valid and malformed file entries yields
  `partial` (never `candidates`); an all-malformed list yields
  `needs_review` (never a false `not_found`). The diagnostic exposes only
  the count `invalid_file_entries`, not remote file URLs. Two synthetic,
  offline tests cover both cases. CI evidence must be checked for the exact
  resulting HEAD; source edits alone are not a PASS.
- The **current PR #16 head** (as inspected this run) has a
  `.luna-meta.json` move clause in `src/lunafilemove.h`, unlike the older
  file inspected when the original Issue #9 blocker was recorded. The
  dependent SI branch has not been rebased or integrated; the existence of
  that code in #16 does **not** establish paired-move rollback, case-only
  rename, cross-volume behavior, or real Windows app acceptance.
  Do not edit the owner branch here. Recheck Issue #9 native acceptance after
  the owning change is reviewed, then run independent restart/reconcile tests.
- A public ACS Figshare HTML landing page for article ID 7688531 was visible
  through a web reader, but no direct Figshare API response or strict HTTP
  HEAD/GET result was obtained by the opt-in SI probe. The program must keep
  its live link verification status unconfirmed; a readable third-party web
  preview is not an end-to-end probe PASS.


## Follow-up: safe public SI filename display (2026-11-02)

- `tools/si_finder.py` now rejects unsafe public Figshare filename labels:
  Windows device names (including COM/LPT superscript-digit variants),
  forbidden Windows filename characters, leading/trailing whitespace or dots,
  nonprintable Unicode including bidirectional overrides, and overlong names.
  This is a **metadata display and manual matching safety gate**, not an
  automatic local rename, file downloader, or SI file integrity check.
- A record containing both valid and invalid file labels stays `partial`;
  an entirely invalid set becomes `needs_review`. Valid Korean/Greek
  scientific filenames and non-PDF SI formats remain eligible.
- `tests/test_si_finder.py` adds synthetic offline cases for these boundaries.
- Previous exact-HEAD `355c5f21` SI CI run `41360225684` succeeded in
  both offline-contract and Windows QtCore identity jobs. That result does
  **not** certify this new patch; inspect the newest exact-HEAD run before
  claiming PASS.
- Live ACS/Figshare API and SI landing HTTP checks are still **unverified**.
  A public landing-page fetch in this environment returned HTTP 403; do not
  treat the DOI or candidate URLs as verified or download SI bytes.
- The independent Python finder is not connected to the production LUNA
  cards. Issue #9's paired `.luna-meta.json` move/rollback and a real
  Windows application restart test remain integration blockers.


## Follow-up: POSIX library-root case boundary (2026-11-02)

- Reviewed the latest main and Draft #13/#15/#16 and Issue #9. PR #16 now
  contains a `.luna-meta.json` paired-move clause, but this does **not**
  establish end-to-end move/rollback/restart acceptance. Do not modify the
  owning file-move implementation in this isolated module.
- The standalone `SiIdentity::guardedPdf` previously used
  `Qt::CaseInsensitive` on **all** operating systems. On a case-sensitive
  Linux filesystem, `CaseRoot/` and `caseroot/` can be distinct siblings:
  a PDF in the latter could pass the former's library-root check. The guard
  now uses platform-appropriate case comparison (Windows insensitive;
  POSIX sensitive), failing closed before sidecar creation.
- The QtCore fixture now checks that both read and explicit enrollment reject
  the case-distinct sibling without creating any sidecar. Added an isolated
  Ubuntu Qt6 CI job so the POSIX branch is actually compiled and exercised;
  the existing Windows QtCore job remains unchanged.
- **Evidence pending:** exact-commit GitHub Actions Linux/Windows results.
  No actual user PDF, real Windows LUNA integration, or ACS/Figshare live
  metadata/landing was tested by this patch. Keep links unconfirmed, PR #17
  Draft, and no merge/release.

## Follow-up: association identity parity gate (2026-11-02)

- Reused the existing QtCore `SiIdentity::validId` UUIDv4 contract rather than
  adding a new DOI extractor or changing the reader. The local Python
  `AssociationIndex` now rejects UUIDv1/v5/nil at insertion **and** JSON
  import, so a forged/legacy ID cannot be linked successfully in Python and
  then fail native relocation or sidecar verification.
- Reject Windows `COM¹`/`LPT²` device aliases and invisible Unicode
  control/format characters in relative PDF paths, while retaining ordinary
  Unicode paper filenames. JSON boolean `version: true` is not schema v1.
- Three synthetic regression cases cover native-identity parity, path
  validation and strict schema typing. No private PDFs, QSettings or
  credentials are used. Python CI and Windows/Linux Qt6 results must be
  checked at the exact commit before claiming PASS.
- **Integration caveat:** the current SI branch still contains the older
  `src/lunafilemove.h`; latest Draft #16 has since added a
  `.luna-meta.json` move clause. Do not overwrite #16 or merge it here.
  After its owner completes native Windows rollback/restart acceptance, test
  the stacked integration with real disposable synthetic PDFs and sidecars.
  The SI link UI remains disabled until that evidence exists.


## Follow-up: Windows-compatible local SI paths (2026-11-02)

The association index previously accepted Windows-impossible components such
as `SI?.pdf`, `Bad?Folder/SI.pdf`, `CON .pdf`, and overlong filename
components. These could be persisted in a confirmed Main↔SI link despite
being impossible to restore or move on Windows. The isolated
`relative_pdf_path` contract now rejects forbidden Windows characters,
components longer than 255 code points, and reserved device aliases with
spaces before the extension. The same validation applies to imported state
and to post-move reconciliation through the existing `relocate` API.

Two synthetic regression tests cover direct path validation, non-mutating
failed relocations, and hostile JSON import. No DOI extractor, UI, file move,
PDF bytes, QSettings or other PR branch was changed.

**Validation:** Changes committed to the dedicated PR #17 branch; full
exact-HEAD Python tests, public privacy gate and Windows/Linux Qt CI are
pending verification. Do not infer a PASS from the existence of test code.
The source PR #16 has metadata sidecar move support, but its real Windows
rollback and application integration remain separate acceptance gates.

**Next:** Check exact-HEAD CI for the new regression, investigate any failure,
then test the actual PR #16 paired move/rollback and restart behavior with
synthetic Windows fixtures before enabling linked moves. Public ACS/Figshare
landing accessibility remains unverified.


## Follow-up: native PR16 paired-move/rollback consumer contract (2026-11-02)

- Added standalone QtCore test `tests/native_si_move/` on the SI branch. It consumes
  **pinned PR #16 commit `a001ba96f1f05ae282c6168340d5bdb5964aed25`** in a
  *separate read-only checkout*, rather than copying/editing PR #16's
  `src/lunafilemove.h` or merging other branches. It also consumes this
  branch's `src/lunasiidentity.h`. The pinned commit must be reviewed and
  advanced explicitly when the owner changes the move implementation.
- Synthetic native fixtures exercise: destination metadata collision/no
  overwrite; PDF failure after sidecars move and full rollback; forced metadata
  rollback failure with stranded sidecar location reported; successful
  PDF+thumbnail+UUID sidecar move; a fresh child process reading the same
  document UUID; and a copied PDF receiving **no inherited UUID sidecar**.
  All files are disposable synthetic bytes. No private PDFs, QSettings, PDF4QT,
  API calls, or SI downloads are involved.
- CI `.github/workflows/si-isolated.yml` runs the same native consumer contract
  on Windows MSVC/Qt6 and Ubuntu Qt6. This is **not** an end-to-end production
  LUNA GUI or actual Windows Explorer rename test. PR #16 already has a native
  file-move test; this specifically covers metadata rollback and restart
  identity, which that test did not establish.
- At authoring time the new exact-HEAD CI run
  https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41386229703
  was **QUEUED**. Do not mark these new native assertions PASS until the
  run completes. If it fails, inspect its specific job logs and fix this
  isolated harness; do not edit the move owner.
- Even if green, linked move remains **not integrated**: real user-library
  migration, Explorer rename, Windows cross-volume failure, crash/power-loss,
  SMB/network filesystem, and association-store-to-native-UI binding still
  require separate acceptance. Live ACS/Figshare landing/file reachability
  is also unverified. Keep Draft; no merge/release.


### Native contract CI resolution (2026-11-02)

- Initial run [#41386229703](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41386229703)
  **FAILED** on Linux because the harness include search path selected the SI
  branch's older `lunafilemove.h` instead of the separately pinned PR #16
  version. The observed "metadata collision was accepted" was a **test
  wiring error**, not proof of a defect in the pinned PR #16 implementation.
- Corrected the include search order so the pinned PR #16 header is first;
  added `tests/native_si_move/**` to CI path triggers so future test edits
  cannot silently skip the native workflow.
- Exact code/workflow HEAD `6c9ba31f95ea9e10ceeb4f82d5f096c10274357b`:
  [Actions #41386453218](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41386453218)
  **SUCCESS**. Offline Python SI/privacy gate PASS; Linux Qt6 identity and
  paired-move consumer contract PASS; Windows MSVC Qt6 identity and
  paired-move consumer contract PASS. No real app or user PDF was exercised.
- Follow-up is to review any newer PR #16 move-helper commit and explicitly
  repin/re-run the contract; then verify real Windows app movement/rename,
  failed rollback and local association persistence with approved disposable
  files. Live ACS/Figshare API/landing remains unverified.


## 2026-11-02 — duplicate SI filenames within one Figshare article

The public Figshare detail `files` array may contain multiple entries with
the same filename (including Windows case-insensitive or Unicode NFKC-equivalent
names). The current display model identifies a candidate by article + filename;
it does **not** expose or verify a stable per-file ID. Offering both entries as
separate SI1/SI2 candidates could therefore misidentify the local attachment.

- `tools/si_finder.py` now suppresses **all** same-article ambiguous filename
  entries, not merely the second one. Comparison is per article using Unicode
  NFKC + casefold, so unrelated articles may still display the same filename.
- Remaining unambiguous files are retained but the lookup becomes `partial`;
  an all-ambiguous result becomes `needs_review`, never `not_found`.
  The diagnostic includes `ambiguous_file_entries`. No remote download URLs
  are surfaced and no local associations are created.
- `tests/test_si_finder.py` adds three offline synthetic regressions: duplicate
  case/Unicode labels, all-ambiguous listing, and distinct-article same-name
  labels. **Do not claim a pass until exact-code-head CI completes.**
- PR #16 advanced from pinned move-helper commit `a001ba96` to
  `ce5e64b4` (10 commits); compare shows only library UI/tabs/tests and
  `src/lunalocalsync.h` changed, not `src/lunafilemove.h`. The pinned
  native move contract remains a historical check, not latest-HEAD app
  acceptance. Draft PR #17 remains diverged/non-mergeable against its base.

Next: check offline/Qt CI for the new code commit; then add an optional
**verified Figshare file ID** to the display model only after validating the
official metadata contract with a real permitted ACS DOI. Until then,
ambiguous names must remain unconfirmed. Live ACS/Figshare HTTP access,
production UI wiring and actual Windows user-library moves remain unverified.


## Follow-up — documented Figshare public file ID (2026-11-02)

- Official Figshare PublicFilePresenter specifies a numeric `id` alongside `name` and `download_url`: https://docs.figshare.com/old_docs/api/presenters/file/ . The independent finder now carries an optional `file_id` in each display candidate. This is **public metadata only**, not a verified reachable file or authorization to download it. No binary URLs are exposed or requested.
- A missing file ID remains `null` for display-only compatibility; malformed/boolean/negative IDs are rejected, and repeated IDs within one article suppress all colliding entries. Casefold/NFKC filename ambiguity protection remains active even when IDs differ. Any omission/collision yields `partial` or `needs_review`, not a false complete SI listing.
- Added three **synthetic offline** tests for valid/missing IDs, invalid types, and repeated IDs. Live ACS/Figshare metadata and landing-page reachability remain **unverified**; this change does not activate local SI auto-linking, UI integration, or file moves.
- Next: verify exact-HEAD Python/privacy and Windows/Linux QtCore CI; then run an explicitly authorized public ACS DOI live metadata + strict landing check (no PDF download). Review PR #16's latest move helper before any actual Windows integration. Keep PR #17 Draft; no main/other PR edits, merge or Release.


## Follow-up — cross-article Figshare file-ID ownership guard (2026-11-02)

- The prior per-article ID check was insufficient when two different public
  article records claim the **same numeric file ID**. This is conflicting
  provider metadata, not evidence that either SI belongs to the main paper.
- `tools/si_finder.py` now collects file-ID owners across all DOI-matched
  article details, suppresses **every** candidate for IDs claimed by more
  than one article, and retains unrelated candidates for display only.
  A partly suppressed set reports `partial`; an entirely suppressed set
  reports `needs_review`. The diagnostic records
  `cross_article_file_id_entries`. No file URLs or automatic associations
  are introduced.
- `tests/test_si_finder.py` adds two fabricated, offline regression cases:
  conflicting IDs with unrelated files retained, and all candidates
  conflicting. These tests must pass at the exact new code HEAD before
  claiming a regression PASS.
- Previous exact-HEAD [isolated CI #41397157377](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41397157377)
  finished **SUCCESS** in Python/privacy, Linux Qt6 and Windows Qt6 jobs.
  That run validates the **previous** file-ID change only.
- PR #16 is currently `03ebb8ba`; compared with pinned move-helper
  `a001ba96`, the 12 subsequent commits do not modify
  `src/lunafilemove.h`. The pinned native consumer result is historical
  and is **not** full Windows app acceptance.
- Public ACS/Figshare API and landing-page checks could not be completed
  from this environment (API retrieval unavailable; landing returned HTTP
  403). No link or individual SI file is marked reachable on that basis.
  Do not interpret the existing HTML-only landing verifier as a file test.

Next: confirm the exact new SI branch HEAD's offline and Windows/Linux CI.
Then perform an explicitly opted-in, rate-limited ACS DOI metadata + landing
probe on a network where official public endpoints respond; log HTTP status
and article ID without downloading files. Separately verify actual Windows
Explorer move/rename/restart and the PR #17 branch integration conflict.
Keep Draft; no main/renderer/other PR edits, merge or Release.

## Follow-up — hidden Figshare file-ID ownership conflict (2026-11-02)

- Reviewed latest main `76e1fd7`, Draft PR #13/#15/#16 and Issue #9. PR #16
  is at `03ebb8ba`; compared with pinned `a001ba96`, the existing
  `src/lunafilemove.h` is unchanged. No production code was modified.
- Fixed a false ownership inference in `tools/si_finder.py`: the previous
  cross-article ID guard collected owners only from display candidates **after**
  rejecting unsafe filenames and suppressing same-article duplicate names.
  An ID belonging to a hidden/rejected entry could therefore appear unique
  in another article and remain visible as an apparently unambiguous SI.
- Numeric file-ID ownership is now recorded from each bounded public detail
  entry **before** filename filtering or local deduplication. Every candidate
  carrying an ID claimed by more than one article is suppressed. Unrelated
  SI metadata remains visible; status stays `partial` or `needs_review`.
  Invalid IDs still do not count as ownership evidence. No file download,
  automatic local association, or link-reachability promotion is introduced.
- Two synthetic offline regressions cover an ID hidden behind duplicate
  same-article filenames and an ID hidden behind an unsafe filename. The
  previous code would expose the other article's reused ID in both cases.
- Code commit `c5c3a88e`; tests `075676e3`. Exact new-code CI
  [#41414871935](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41414871935)
  **in progress at handoff**; do not mark PASS until Python/privacy and both
  native Qt6 jobs complete. Earlier exact-code
  [#41404544291](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41404544291)
  succeeded on the previous revision only.
- Remaining: actual ACS/Figshare API and HTML landing reachability (prior
  public probe encountered unavailable API/HTTP 403); individual SI file
  accessibility is never inferred from an HTML landing; real Windows app
  move/rename/restart, PR #17 branch divergence, and production UI integration
  remain unverified.

Next: inspect exact-head CI and repair any failure; use an explicitly
opted-in public DOI metadata/landing probe on a permitted network, without
downloading SI files. Then coordinate a disposable-file Windows
move/rename/restart acceptance with the Issue #9 owner. Keep Draft, do not
merge or release.
