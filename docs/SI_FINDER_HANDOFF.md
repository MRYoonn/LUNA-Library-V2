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
  **SUCCESS** on exact test/code HEAD `075676e3`: Python/privacy,
  Linux Qt6 and Windows Qt6 native jobs all passed. The final documentation
  commit does not change tested code. Earlier exact-code
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


## Follow-up — stop ACS/Figshare detail requests on HTTP 429 (2026-11-02)

- Reviewed main `76e1fd7`, Draft PR #13/#15/#16, Issue #9 and PR #17's
  previously green exact-code Actions #41414871935. PR #16's DOI and SI
  classification remain owned by their branches; no reader/renderer change.
- **Bug:** the detail loop previously swallowed HTTP 429 into a generic
  `detail_failures` counter and continued issuing GET requests. This could
  worsen provider throttling and report an empty set as `needs_review`
  without clearly recording rate limiting.
- `tools/si_finder.py`: a detail-level HTTP 429 now **stops further requests**.
  If earlier SI metadata was collected, the result remains display-only
  `partial`; if none was collected, it returns `rate_limited`, never a
  definitive `not_found`. A sanitized `detail_rate_limited=1` diagnostic
  records the stop. Non-429 detail failures still use the existing
  partial/review handling. The DOI publisher fallback remains available.
- `tests/test_si_finder.py`: two new synthetic offline regressions cover
  the 429 stop after a valid SI and 429 on the first detail, including
  assertion that **no later GET** occurs. No live API calls or file downloads.
- Source/test commits: `923ed5fb`, `57abadd6`. New-head Python/privacy
  and Windows/Linux Qt6 CI must be checked before calling this revision PASS.
  The previous #41414871935 SUCCESS covers the older implementation only.
- Not verified: real ACS/Figshare HTTP 429 behavior, actual SI-file
  accessibility, full Windows LUNA GUI, physical move/rename/restart, and
  PR #17 integration divergence. Keep Draft; no main/other PR changes,
  auto-merge or Release.

Next: confirm the isolated push-triggered CI on the exact new code SHA.
Then run the opt-in live public ACS DOI metadata/landing probe on a network
with access to official endpoints, without downloading any SI files.


## 2026-11-02 — landing-level HTTP 429 back-pressure (pending CI)

- Continued Draft PR #17 on `agent/paper-si-finder-linker`. Reviewed latest main `76e1fd76997c97c90a53b31fa0b0f47db847a3f7`, PR #13/#15/#16 (PR #16 `03ebb8ba`), and Issue #9. The PR #16 move helper blob `fe2a7b1` still matches the pinned native test source; no production branch was edited.
- `tools/si_landing_verify.py`: an HTTP 429 returned **or raised** by a landing probe now stops all *new* landing requests for that lookup. Previously the independent landing checker continued probing later URLs even though the metadata finder already stopped after 429. Earlier confirmed HTML landing checks remain individually marked `landing_verified`; later unprobed URLs remain `landing_unchecked_rate_limited`. A 404 is **not** global throttling. The diagnostic `landings_checked` now counts actual probe calls, not cached/unprobed URLs; `landing_rate_limited` records the stop condition.
- `tests/test_si_landing_verify.py`: three synthetic/offline regressions cover partial verification followed by 429, an injected HTTPError(429) with no later requests, and 404 followed by a valid second landing. No real network request or PDF download is performed by these tests.
- **Validation boundary:** new exact-head Python/Qt CI not confirmed at documentation edit time; prior CI #41414871935 PASS belongs to earlier code. A successful HTML landing HEAD response is **not** verification of individual SI file availability. Real ACS/Figshare API/landing reachability, actual Windows GUI, physical move/rename/restart and user data remain unverified.
- Next: confirm exact-head `si-isolated.yml` Python/privacy and Linux/Windows Qt6 jobs; fix any failures without modifying main/PR #13/#15/#16. Then run the existing opt-in `tools/si_live_probe.py --doi <authorized-public-acs-doi> --live --check-landings --require-landing-http` on an official-endpoint-accessible network. Do not infer SI-file reachability or auto-associate local PDFs. Keep PR Draft; no merge/Release.


## Follow-up — HEAD-only landing false-positive guard (2026-11-02)

- Reviewed main `76e1fd7`, open Draft PR #13/#15/#16/#17, Issue #9, and the
  existing ACS/Figshare finder, landing verifier and exact-code CI history.
  Earlier SI CI #41424752313 passed on `35f9ff56` (code/test) only.
- Found a false-positive: `official_landing_status()` previously promoted a
  public Figshare landing after **HEAD 200 text/html**, without checking whether
  the page actually opens via GET. A HEAD-allowed/GET-blocked landing could
  therefore be marked `landing_verified`.
- The verifier now requires a successful bounded HTML **GET** after HEAD 200
  (or HEAD 405/501). GET uses the existing one-byte Range request and never
  reads the response body; redirects, 403, 404, 429 and non-HTML remain
  unverified. A GET-level 429 stops later landing probes. No SI file, PDF,
  user document, credential or publisher paywall is accessed.
- Four synthetic offline regressions cover HEAD-only false positives,
  successful HEAD+GET, HEAD 404 without GET, and GET 429 back-pressure.
  Files: `tools/si_landing_verify.py`, `tests/test_si_landing_verify.py`.
  Source/test commits: `a22822b0`, `ce4f1c7e`.
- **Validation boundary:** exact-test-HEAD Python/privacy and Windows/Linux
  Qt6 CI must finish before marking this revision PASS. Real ACS/Figshare
  metadata, landing availability and individual SI-file reachability are
  **not verified**. Real Windows LUNA UI and physical move/restart acceptance
  remain pending; keep PR #17 Draft, no merge or Release.

Next: confirm exact-code CI for `ce4f1c7e`; then run an opt-in, public DOI
probe on a permitted network with official endpoints. Never infer individual
SI-file accessibility from an HTML landing. Do not modify main, PDF4QT,
PR #13/#15/#16, or the Issue #9 file mover.

CI follow-up (exact source/test commit `ce4f1c7e`):
[SI isolated Actions #41433812741](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41433812741)
completed **SUCCESS**: Python/offline/privacy, Linux Qt6 identity and Windows
Qt6 identity/move suites all passed. This validates synthetic contracts only;
it is not a live ACS/Figshare or Windows application integration result.

## Follow-up — truncated Figshare file-ID collision quarantine (2026-11-02)

- Rechecked current main `76e1fd7`, open Draft PR #13/#15/#16/#17,
  Issue #9, and prior isolated CI #41433812741 (SUCCESS on older code).
  The independent branch remains `agent/paper-si-finder-linker`.
- **Bug:** `tools/si_finder.py` inspected only the first 40 file records
  when collecting Figshare numeric file-ID ownership. An ID repeated in
  record 41+ could leave a displayed file with ambiguous identity. A
  repeated ID under an unsafe filename in the same article also escaped
  the same-article duplicate check.
- **Fix:** inspect numeric IDs across the entire already-received,
  1-MB-bounded JSON file list before the 40-entry *display* cap. Quarantine
  visible entries whose ID repeats within the article or in another
  article's metadata, including hidden tail/unsafe-label records.
  Do not expose/download tail files or change API request count.
- `tests/test_si_finder.py`: three synthetic offline regressions cover
  same-article truncated-tail collisions, cross-article hidden-tail
  collisions, and invalid-label same-article collisions. Independent,
  unambiguous SI candidates remain display-only, never auto-linked.
- Source commit `1c82b715`; test commit `6cf88093`.
  CI run [#41441474681](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41441474681)
  was **queued** at the initial check. Do not call this new revision
  PASS until the exact test HEAD's Python/privacy and Windows/Linux Qt6
  jobs succeed. Earlier CI does not validate these new assertions.
- Still unverified: real ACS/Figshare API, actual landing/SI-file
  accessibility, Windows production GUI and physical move/rename/restart
  under Issue #9. Keep PR #17 Draft; no merge, Release, or other branch edits.

Next: inspect exact-test-HEAD CI, fix any failures, then run a permitted
public ACS DOI live metadata/landing check without downloading SI files.
Coordinate Windows app-level move/restart acceptance with the Issue #9
owner before integrating UI.

CI follow-up for exact source/test commit `6cf88093`:
[SI isolated Actions #41441474681](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41441474681)
completed **SUCCESS**: Python offline/privacy, Linux Qt6 identity/move and
Windows Qt6 identity/move jobs all passed. These are synthetic regressions;
no real ACS/Figshare endpoint, SI-file accessibility or production Windows
GUI integration was tested.

## Follow-up — NFKC-safe SI display labels (2026-11-02)

- Reviewed current main `76e1fd7`, open Draft PR #13/#15/#16/#17,
  Issue #9 and the prior exact-code isolated CI
  [#41441474681](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41441474681)
  (SUCCESS for the **previous** source/test revision).
- Found a local-matching safety gap: `safe_si_filename` rejected ASCII
  Windows path separators and device names, but accepted compatibility
  Unicode forms such as `SI／appendix.pdf` or `ＣＯＮ.pdf`.
  Unicode NFKC (already used by the candidate duplicate detector) turns
  these into `SI/appendix.pdf` and `CON.pdf`. Later local matching or
  normalization must not be offered unsafe filenames.
- `tools/si_finder.py`: validate **both original and NFKC-normalized**
  public metadata labels for forbidden characters, device names, trailing
  dots/spaces, controls and length. Keep ordinary Unicode SI labels.
  Invalid entries are excluded with `invalid_file_entries`; unrelated
  candidates remain visible, metadata-only and unlinked.
- `tests/test_si_finder.py`: two synthetic offline regressions exercise
  Unicode slash/backslash/colon/question-mark/device-name/trailing-dot
  spoofing and an end-to-end candidate filtering case.
- Source commit `855b2dc9`, test commit `a3548174`. New exact-code
  Python/privacy and Linux/Windows Qt6 CI is **not yet confirmed** at
  handoff creation. Prior CI is not evidence for this revision.
- No publisher request, SI download, local PDF move, DOI extraction, QSettings,
  renderer, main or other PR was modified. Real ACS/Figshare API and landing
  availability, SI-file access, Windows app move/restart and UI integration
  remain unverified. Keep PR #17 Draft, no merge or Release.

Next: confirm isolated CI for `a3548174`; fix any failures on this branch.
Then run the opt-in public ACS DOI metadata + landing smoke test from a
permitted network, without downloading SI files. Do not claim that an
HTML landing verifies the individual SI binary. Coordinate app-level
paired PDF/metadata-sidecar move and restart with Issue #9's owner.

Exact-source/test CI follow-up: [SI isolated Actions #41448875055](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41448875055)
completed **SUCCESS** on `a3548174fa2fe48f468222ef165004de9da905ca`:
**138/138 Python synthetic tests PASS**, public-source privacy gate PASS,
Linux Qt6 identity/move PASS, Windows Qt6 identity/move PASS.
This validates the NFKC source and tests, not real ACS/Figshare availability,
individual SI-file downloads, or production Windows GUI integration.

## Follow-up: NFKC-safe local association paths (2026-11-02)

The independent `tools/si_associations.py` reference now validates both
original and Unicode NFKC-normalized relative paths. Compatibility separators,
reserved Windows device names, and dot segments fail closed; normalized
case-insensitive path identity is used for add/relocate/import collision checks.
This closes a mismatch with the existing `si_finder.safe_si_filename` guard.
No filenames are rewritten, no PDFs or QSettings are touched, and no automatic
Main/SI link is created.

Synthetic regression coverage in `tests/test_si_associations.py` includes
unsafe compatibility separators/devices, two UUIDs with visually aliased
paths, forged persisted JSON, and valid Korean/Greek names. These are local
association-model tests, **not** a claim of real Windows GUI acceptance.

**Updated dependency check:** the current independent PR #16 branch's
`src/lunafilemove.h` now includes `.luna-meta.json` in the paired move and
rollback sequence. Earlier handoff sections stating it is unconditionally
omitted are historical and no longer describe that branch's latest source.
PR #17 is stacked on an earlier PR #16 base, and this source inspection is
not proof that the installed Windows LUNA application contains the fix.
Do not enable SI-linked physical moves until the exact integrated revision,
Windows native paired-move tests, and user-PC restart are verified.

Verification gate: `python -B -m unittest discover -s tests -p 'test_si_*.py' -v`,
`python -B tools/preflight_public.py`, and exact-commit Linux/Windows Qt CI.
Live ACS/Figshare link and SI-file access remain unverified. Keep PR #17 Draft.

**Exact-code CI result:** [SI isolated #41459077944](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41459077944)
completed SUCCESS for source/test commit `89cfb80e`: 141/141 offline
synthetic Python tests and privacy gate PASS; Linux Qt6 and Windows Qt6
standalone identity/paired-move jobs PASS. The earlier
[#41458891613](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41458891613)
failed one new test because NFKC introduced a slash not blocked by the first
patch; `89cfb80e` explicitly rejects this and passed the replacement run.
These are isolated tests, not Windows LUNA GUI acceptance. Real ACS/Figshare
metadata API, landing-page availability and individual SI file access remain
unverified. No production merge/release is authorized.


### 2026-11-03 — fail-closed landing verification for externally restored candidates
- The existing finder already rejects ambiguous file identities, but a caller can supply a restored/foreign `SiLookupResult` directly to the landing verifier. Previously a reachable **article HTML page** could mark a candidate with an unsafe filename, duplicate Figshare file ID, or NFKC-equivalent filename as `landing_verified`.
- `tools/si_landing_verify.py` now independently checks filename safety, valid positive integer file IDs, cross-article file-ID collisions and same-article NFKC/casefold filename collisions **before** any HEAD/GET. Invalid candidates remain `landing_unchecked_unsafe_filename`, `landing_unchecked_invalid_file_id` or `landing_unchecked_ambiguous_file_identity`. Unrelated candidates remain independently probeable; no file is downloaded or automatically linked.
- `tests/test_si_landing_verify.py` adds three synthetic offline regression cases, including an unsafe compatibility separator, two articles sharing one file ID, and Unicode-equivalent SI names with a Boolean file ID. Source commit `f54cc622`, test commit `48c63c7a`.
- New exact-code CI: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41470938573 — **queued at writing; PASS not yet claimed**. Prior green CI #41459077944 does not cover this revision.
- No live ACS/Figshare API, HTML landing, or individual SI file has been verified reachable. Production Windows UI association, paired move/restart and PR #16 integration remain unverified. PR #17 remains Draft; no main, renderer, PDF4QT, other PR, auto-merge or Release changes.


**Exact-code CI result (2026-11-03):** [SI isolated #41470938573](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41470938573) **SUCCESS** on source/test commit `48c63c7a`: public privacy gate PASS; **144/144** offline synthetic Python tests PASS (including all three new restored-candidate guards); Linux Qt6 standalone identity/move PASS; Windows Qt6 standalone identity/move PASS. Documentation-only commits do not change the tested source. No live ACS/Figshare or installed Windows LUNA GUI verification is implied.


### 2026-11-03 — restored-result fallback and offline evidence safety
- The landing verifier is an opt-in boundary that may receive externally restored `SiLookupResult` objects. Previously it passed through an arbitrary `fallback_url` (potentially a phishing destination) even after a failed probe, and its offline return preserved a stale or forged `landing_verified` candidate status.
- `tools/si_landing_verify.py` now derives the fallback solely from a valid canonical ACS parent DOI (`https://doi.org/<doi>`). Invalid parent DOI has no fallback. In offline mode it marks every supplied candidate `landing_unchecked_offline`, never retaining a claimed successful HTTP check without a new probe. Existing read-only finder and DOI extraction are unchanged.
- `tests/test_si_landing_verify.py` adds three synthetic offline regressions for malicious fallback URL, stale verification during offline checks, and invalid DOI in online/offline modes. No network requests, PDFs, credentials, QSettings or local paths are part of these tests.
- Source commit `511cd8e1`, test commit `378931b6`. **Exact-commit Python/privacy and Qt CI are not yet confirmed** at handoff writing. Prior #41470938573 success does not cover this change.
- Live ACS/Figshare API and HTML landing availability, individual SI file access, and installed Windows GUI association/move/restart remain unverified. PR #17 must remain Draft; no main/renderer/PDF4QT/other PR edits, merge or Release.

Next: inspect exact-HEAD isolated CI for these changes and fix failures. Run the opt-in live public DOI smoke CLI in a permitted network environment, recording official API article ID and strict HTML landing result without downloading SI files. Then verify Issue #9 paired sidecar moves and PR #16 integration on Windows before UI acceptance.


### 2026-11-03 — restored verified-state recheck regression (source/test HEAD f0dc4b79)
- Root cause: `verify_landing_candidates` returned immediately for any restored result status other than `candidates`/`partial`. A prior `verified` or `partial_verified` object therefore retained `landing_verified` even when an explicit subsequent offline or failed HTTP recheck was requested. A forged `verified` result with zero candidates also escaped the verifier.
- Reproduction: verify a synthetic ACS result with `reachable_html`, pass that returned `verified` result into the verifier with `offline=True` or a `not_found` probe. Before the fix the old `landing_verified` status persisted. A `partial_verified` result similarly bypassed fresh checks.
- Fix in `tools/si_landing_verify.py`: explicit rechecks of `verified`, `partial_verified`, `needs_review`, and `offline` results now reevaluate candidate evidence; offline and failed checks revoke stale success. Previously partial/uncertain result provenance cannot be upgraded to a complete verified listing. Empty restored verified results become `needs_review`, and untrusted fallback URLs are replaced by a canonical ACS DOI resolver.
- Four synthetic tests in `tests/test_si_landing_verify.py` exercise offline revocation, HTTP 404 revocation, partial/uncertain recheck conservatism, and empty forged verified results. No actual PDF, SI binary, credentials, local paths or QSettings are used.
- Source commit `6881ac6a`; test commit `f0dc4b79`. Exact-code Python/privacy and Linux/Windows Qt6 CI **pending verification**; previous green #41476969572 covers older source. Do not claim this change PASS until the new run completes.
- Still unverified: actual ACS/Figshare API, HTML landing reachability, individual SI file access, Windows production GUI integration, local PDF/sidecar move/rename/restart. Keep Draft PR #17; no main, PDF4QT, other PR, merge or Release modifications.

Next: check the exact-source isolated CI on `f0dc4b79`; fix any failures. Run approved live ACS DOI opt-in probe with network permission and record the exact official article URL/status (not individual SI access), then Windows integrated paired-move/restart checks against PR #16 and Issue #9.

**Exact-source CI acceptance (2026-11-03):** [SI isolated #41486735136](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41486735136) completed **SUCCESS** for source/test HEAD `f0dc4b79`: Python offline synthetic tests and public/privacy preflight PASS, Linux Qt6 standalone identity/paired-move PASS, Windows Qt6 standalone identity/paired-move PASS. The subsequent documentation-only commit does not alter tested source. This is not live publisher or production Windows GUI acceptance; PR #17 remains Draft.


### 2026-11-03 — fail-closed restored provider-error snapshots
- Follow-up to the already accepted restored `verified`/offline recheck fix. A different path still returned early for `not_found`, `rate_limited`, `provider_unavailable`, `unsupported_publisher` and unknown lookup statuses. If an imported snapshot contained a candidate marked `landing_verified` alongside one of these contradictory statuses, the stale success was returned without a probe. Empty provider-error results could also expose an arbitrary imported `fallback_url`.
- `tools/si_landing_verify.py` now canonicalizes the ACS DOI fallback on **all** verifier paths, including empty/error results. A non-refreshable status with candidates is downgraded to `needs_review`, all candidates become `landing_unchecked_untrusted_lookup_status`, and no HTTP probe occurs. Original input objects are not modified.
- `tests/test_si_landing_verify.py` adds three synthetic offline regressions covering five contradictory status values, six empty/error status values and invalid DOI fallback suppression. These tests do not read private PDFs, local QSettings or credentials.
- Source commit `655706fb`; test commit `a49f777c`. Exact-code CI result must be checked separately; no new PASS is claimed by this note.
- Public browser observation on 2026-11-03: the ACS Figshare landing page for public article ID `7688531` (associated with DOI `10.1021/acsnano.8b07631`) rendered HTML. This was **not** an execution of the module's strict HEAD/GET verifier; the Figshare API detail endpoint was inaccessible from that check. No individual SI file URL or download was verified. The standalone native Qt tests do not prove production GUI integration.

Next: check exact-commit Python/privacy and Linux/Windows Qt6 isolated CI; fix any failures. Obtain a permitted live Figshare API detail response for a known ACS DOI, then run the existing opt-in `si_live_probe.py --live --check-landings --require-landing-http` without binary downloads. Only after API/landing and Windows Issue #9 paired-move/restart acceptance should UI linking be enabled. PR #17 stays Draft; no main, PDF4QT, other PR, merge or Release changes.


### 2026-11-03 — public Figshare title trust boundary (new CI pending)
- **Cause:** `lookup_acs_figshare` previously stringified arbitrary `detail.title` JSON values and truncated overlong strings. An invisible bidi/control character or object repr could become a displayed SI title and later misleading matching evidence. A restored `SiLookupResult` could bypass the finder and reach the landing verifier with an unsafe title.
- **Reproduce:** supply a synthetic Figshare detail with `title="Spoof\\u202e.pdf"`, `title={"spoof":"title"}`, or a 241-character title, plus a valid DOI/article ID and safe SI filename. Previously the candidate title was exposed. Supply a restored candidate with the same unsafe title and a probe returning `reachable_html`; previously the article landing could promote that candidate.
- **Fix:** `tools/si_finder.py` now accepts only nonempty, bounded (<=240), printable string titles. Unsafe titles become empty, increment `invalid_article_titles`, and make the lookup `partial`/reviewable rather than complete. `tools/si_landing_verify.py` independently marks unsafe restored titles `landing_unchecked_unsafe_title` without probing that entry. Other valid SI remain individually probeable. No DOI extraction, download, Qt renderer or main changes.
- **Tests:** added two offline synthetic cases in `tests/test_si_finder.py` and two in `tests/test_si_landing_verify.py`, covering bidi/newline/object/overlong titles, legitimate Unicode and mixed valid/invalid restored candidates. Source commits `548d07bd`, `8077af2a`; tests `9946d039`, `0d39cdc3`. **Exact-code CI PASS is not yet established**; inspect the new push run's Python/privacy and Linux/Windows Qt6 jobs before acceptance.
- **Outstanding:** live ACS/Figshare metadata API and strict HTML landing response; individual SI file accessibility; PR #16 current-head production Windows PDF+sidecar move/rename/restart and GUI integration. DNS from the development container returned `[Errno -3] Temporary failure in name resolution` for `api.figshare.com` and `acs.figshare.com`; this is a network block, not a provider 404. Do not claim live link validation.
- **Next run:** first check exact-test-HEAD CI for `0d39cdc3` (fix any failure); then run opt-in `python tools/si_live_probe.py --doi 10.1021/acsnano.8b07631 --live --check-landings --require-landing-http --expected-article-id 7688531` on a network permitted to access the official public endpoints. Do not download SI files. Keep PR #17 Draft; no merge/Release.


**Same-run test correction:** Review found the standalone `si_landing_verify.py` import fallback lacked `safe_si_title`, and the first synthetic test strings contained literal escaped sequences rather than actual bidi/newline controls. Fixed fallback import in `3d7d721a` and changed the fixtures to `chr(0x202e)`/`chr(10)` in `b46d7e2a` and `9e94b204`. **Exact test/code HEAD to validate is now `9e94b204`**, not `0d39cdc3`. These are source-level corrections only; do not claim CI PASS until the new push workflow completes.


## 2026-11-03 — provider-error CI regression correction

- **Root cause:** Isolated Actions [#41507030340](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41507030340) failed 1/158 offline Python tests at `test_non_candidates_unchanged`. The test expected `rate_limited` with nonempty candidates to return the *same object*. That assertion became obsolete after the intentional fail-closed change in `verify_landing_candidates`: a restored provider-error result cannot carry trustworthy SI links, especially stale `landing_verified` claims. The privacy gate and Linux/Windows Qt6 jobs passed in that earlier run.
- **Reproduction:** `python -B -m unittest discover -s tests -p 'test_si_*.py' -v` on pre-fix branch HEAD `9bcfe7c0`; the old `test_non_candidates_unchanged` fails. This is a test-contract mismatch, not a reason to weaken the provider-error quarantine.
- **Fix:** Replaced the obsolete assertion with synthetic tests covering provider-error snapshots with forged confirmed candidates (429, 404, provider unavailable, unsupported and unknown status), and zero-candidate 429/404/provider errors with forged fallback URLs. All must avoid network probes; a valid DOI fallback is canonicalized. No finder, landing verifier, DOI engine, renderer, local PDF or UI code was changed.
- **Validation boundary:** The test source was committed to the dedicated SI branch. An exact-code CI PASS is **not yet established** by this note; older CI results cannot be reused. Live ACS/Figshare API and landing access, individual SI-file reachability, and real Windows LUNA GUI/sidecar move+restart remain **unverified**. Next execution must inspect the isolated workflow for the test commit, fix any remaining failures, then separately run an opt-in permitted ACS public DOI/landing probe. Keep Draft PR #17; no merge or Release.


## 2026-11-03 — strict live-probe identity gate (pending exact-code CI)

- Checked current main `76e1fd76`, Draft PR #13/#15/#16/#17 and Issue #9. Kept the existing DOI finder, SI classifier, landing verifier and paired mover untouched. PR #17 remains Draft and non-mergeable; do not alter another PR or force a merge.
- Previous exact-test CI [#41515499693](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41515499693) completed SUCCESS on `b4e71935` (the prior HTTP-error test correction). This does **not** validate today's new code.
- **Root cause:** `tools/si_live_probe.py --require-landing-http` trusted a returned `verified` status plus `landing_verified` strings without independently checking the requested DOI, each SI resource DOI, public Figshare landing/article ID, or unsafe title/filename/file ID. A misrouted/restored report could pass the CLI acceptance gate despite referring to a different article. The existing finder/verifier normally sanitize these fields; the independent CLI acceptance boundary did not.
- **Fix:** Bind the strict CLI gate to the normalized requested ACS DOI and each candidate's exact DOI, safe public article URL/id, bounded printable title, safe filename and valid optional file ID. Reject malformed candidate collections. Only affects the opt-in acceptance report; it does not claim individual SI file access or download anything. Source commits `578b522c`, `a9eb3468`; synthetic regression commits `d6f4946c`, `5955395d`.
- **Reproduction:** Before the fix, patch `si_live_probe.inspect_doi` to return a synthetic `verified` report whose `result.doi` differs from the requested DOI or whose candidate's `article_id` disagrees with the Figshare URL; `main([... --live --check-landings --require-landing-http])` incorrectly exits 0. New `test_strict_landing_gate_rejects_misrouted_or_forged_identity` requires exit 2 for eight inconsistent/unsafe cases.
- **Validation:** GitHub source/test commits were created; exact-code Python/privacy and Linux/Windows Qt6 CI results **pending** at note time. No live ACS/Figshare API, HTML landing, individual SI file or production Windows GUI move/rename/restart has been verified.
- **Next:** inspect the exact-test-HEAD SI isolated CI for `5955395d`; fix any failure without reimplementing the finder. On permitted network, run `python tools/si_live_probe.py --doi 10.1021/acsnano.8b07631 --live --check-landings --require-landing-http --expected-article-id 7688531` and record only genuine HTTP evidence. Coordinate the real Windows paired move/restart acceptance with Issue #9. Keep Draft; no merge/Release.

**Exact-code CI follow-up:** [SI isolated #41524889086](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41524889086) completed **SUCCESS** on source/test HEAD `5955395d`: Python public-source privacy gate and **160/160** synthetic/offline SI tests PASS; Linux Qt6 standalone identity/paired-move PASS; Windows Qt6 standalone identity/paired-move PASS. Subsequent documentation-only commits do not change the tested code. This is **not** proof of a live ACS/Figshare response, accessible individual SI file, or production Windows LUNA UI/move/restart behavior. Draft/non-mergeable remains; no merge or Release.


## 2026-11-03 — strict CLI duplicate-identity and fallback acceptance gate

- **Root cause:** The opt-in `si_live_probe.py --require-landing-http` acceptance check independently validated each candidate's DOI, title, file label, article ID and URL, but did not compare candidates against *each other*. An imported `verified` report containing two identical or Unicode-NFKC/casefold-equivalent filenames under one article, or the same Figshare file ID under two candidates/articles, could exit 0. The CLI also did not validate a restored `fallback_url`, which could point outside the canonical DOI resolver despite the report claiming verification. This was a boundary gap; the existing finder and landing verifier already have ambiguity checks.
- **Reproduce (synthetic/offline):** Patch `si_live_probe.inspect_doi` to return `verification_scope=landing_html_only`, `status=verified`, and two `landing_verified` candidates with (a) identical filename, (b) `SI.pdf` vs fullwidth `ＳＩ.pdf`, (c) reused file ID within an article, or (d) reused file ID across two article IDs with individually valid URLs. Also try a verified report with `fallback_url=https://untrusted.invalid/si`. The old CLI returned success; these reports are not trustworthy evidence.
- **Fix:** `tools/si_live_probe.py` now checks the whole candidate collection for per-article NFKC/casefold filename uniqueness and globally unique non-null file IDs, and requires the exact canonical `https://doi.org/<normalized ACS DOI>` fallback. The existing DOI extractor, finder, landing verifier, PDF4QT, UI and move helper are untouched. A distinct two-SI collection remains accepted.
- **Tests:** `tests/test_si_live_probe.py` adds a positive two-SI control and five negative synthetic cases, with the network probe mocked. Code commit `eb44b0b0`; test commit `86d7e0ed`. Exact-test-HEAD workflow: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41534143022 . Check *all* job conclusions before claiming an exact-HEAD PASS.
- **Live boundary:** On 2026-11-03 the public ACS Figshare HTML landing `https://acs.figshare.com/articles/dataset/Extrinsic_and_Dynamic_Edge_States_of_Two-Dimensional_Lead_Halide_Perovskites/7688531` was readable through a public browser source and listed parent DOI `10.1021/acsnano.8b07631` as related material. The API detail endpoint `https://api.figshare.com/v2/articles/7688531` was inaccessible through that browsing tool. This is **not** an execution of the CLI's HEAD+GET verifier, and **no individual SI file link or binary was accessed**. Do not mark the module's live gate PASS.
- **Next:** Confirm the Python/privacy and Linux/Windows Qt6 conclusions for run 41534143022. Then run the existing explicit live DOI→Figshare API→HTML gate on a network allowed to access official endpoints; record actual HTTP status and preserve `unverified` on any failure. Separately obtain Issue #9 Windows paired PDF/metadata-sidecar move/rename/restart acceptance before production UI linking. PR #17 stays Draft and non-mergeable; no merge or Release.


## 2026-11-03 — reject NFKC-only pseudo-moves (pending exact-HEAD CI)

- **Baseline:** main `76e1fd76`; Draft #13/#15/#16/#17 and Issue #9 inspected. Previous isolated CI [#41534143022](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41534143022) completed SUCCESS for earlier test HEAD `86d7e0ed`; it does not validate the new change.
- **Root cause:** `audit_paired_move` rejected same-path renames using only `casefold()`, while the existing local AssociationIndex uses `NFKC + casefold` for Windows/UI path identity. Thus an invented old spelling `New/Ｒenamed.pdf` and existing destination `New/Renamed.pdf` could pass the audit if the destination carried a matching UUID sidecar, despite no physical move at that alias. A read-only audit cannot reconstruct a historical move; it must at least reject equivalent path identities.
- **Reproduction (synthetic):** create a destination `New/Renamed.pdf` with its matching `New/Renamed.luna-meta.json`; do not create `New/Ｒenamed.pdf`. Before the fix, `audit_paired_move(root, "New/Ｒenamed.pdf", "New/Renamed.pdf", si_uuid)` returns `verified_paired_move`. After the fix, both this alias and a case-only alias return `ambiguous_move` before any file audit. The index is unchanged. This is deliberately **not** an implementation of case-only Windows rename, which belongs to Issue #9.
- **Files:** `tools/si_relocation_gate.py` and `tests/test_si_relocation_gate.py`. The finder, DOI engine, PDF4QT, renderer, UI and other PR branches were not modified.
- **Validation:** Source and regression test committed on `agent/paper-si-finder-linker`; new exact-HEAD Python/privacy and Linux/Windows Qt6 CI results remain unconfirmed at writing. No real Windows paired-move/restart test and no ACS/Figshare live API or individual SI file HTTP validation was performed. The network container still reports DNS resolution failure for official Figshare endpoints.
- **Next:** check isolated CI for regression commit `a919627c`; fix any failures. Then, from an explicitly permitted network, run the existing opt-in DOI→Figshare API→HTML probe for `10.1021/acsnano.8b07631`, recording real HTTP statuses without downloading PDFs. Coordinate production Windows PDF+sidecar rename/restart acceptance with Issue #9. PR #17 stays Draft; do not merge or release.


## Follow-up: privacy-minimal SI list projection (2026-11-03)

Added `tools/si_display_model.py` and `tests/test_si_display_model.py`.
The read-only projection renders **two separate groups**: UUID-keyed, explicitly
user-confirmed local SI documents and DOI-matched public ACS/Figshare candidate
files. It deliberately does not merge rows by DOI/title/filename, guess a local
match, expose library-relative directories, open a PDF, or persist any state.
The public list is metadata-only by default. Even a restored
`landing_verified` candidate remains **unconfirmed** unless the caller
explicitly supplies `live_landing_check_completed=True` after invoking the
existing bounded landing verifier. This boolean is an integration contract,
**not cryptographic evidence** of HTTP access; untrusted restored JSON must
never be allowed to set it. Offline/provider-error results cannot carry a
previously verified candidate into the display. Invalid/mismatched DOI,
unsafe labels, wrong landing article IDs, and duplicate NFKC-equivalent
filenames or file IDs are suppressed. The fallback always derives from the
validated parent DOI, not an external result's URL.

Synthetic regression command in a complete branch checkout:

    python -B -m unittest discover -s tests -p 'test_si_display_model.py' -v

The new source/test files and public-source allowlist were committed on the
dedicated branch. **No local exact-checkout test result is claimed here**;
check the new exact-HEAD GitHub Actions Python, Linux Qt6 and Windows Qt6
jobs before marking PASS. This is a Python display contract only; the native
Qt/C++ model, real LUNA cards, persisted association UI, live Figshare API,
individual SI file accessibility and real Windows user workflow remain
unverified.

Integration note: PR #16's current `src/lunafilemove.h` now includes
`.luna-meta.json` in its move/rollback contract; the older copy in this
stacked PR does not. The isolated CI tests the pinned PR #16 version, but
**this does not mean the production LUNA binary has the fix**. Coordinate
the branch integration with the Issue #9 owner; do not edit that file here,
merge, release or enable linked physical moves.

Next: inspect exact-HEAD CI, run the opt-in public DOI→Figshare live HTTP
probe from a permitted network, then port the validated display projection
to standalone QtCore with the existing PR #16 DOI identity and the Issue #9
move/rollback contract. Require manual Windows acceptance before exposing
any verified SI link or enabling local SI move UI.


## 2026-11-03 — standalone QtCore SI list projection (native acceptance pending)

- **Baseline:** current main `76e1fd76`, Draft PR #13/#15/#16/#17 and Issue #9 reviewed. Continued only on `agent/paper-si-finder-linker`. Prior isolated run #41542499230 passed Python/privacy and Linux/Windows Qt6, but **predates** this native display code.
- **Scope:** `src/lunasirows.h` ports the *display-only boundary* of the existing `tools/si_display_model.py` to a header-only QtCore component. `tests/native_si_rows/main.cpp` and CMake target use synthetic DOI/UUID/paths only. The existing DOI normalizer from PR #16 is reused. No PDF reader, metadata finder, SI classifier, file mover, QSettings, UI card or other PR is modified.
- **Safety contract:** Only caller-supplied explicit `user_confirmed` local UUID links appear in the local group; local directory paths are never included in the output. ACS/Figshare public candidates appear separately, with exact canonical parent DOI, article-ID-bound official HTTPS landing, bounded printable title, NFKC-safe filename, and cross-row duplicate checks. Invalid/ambiguous collections are downgraded to `needs_review`. Offline/404/429 do not carry stale public rows. Fallback is constructed only from the verified parent DOI. A restored `landing_verified` string **never** promotes a public row: this component deliberately outputs `unconfirmed` until an independent real HTTP proof is integrated. Filename/title/DOI similarity never creates a local link.
- **Native test coverage:** Main↔SI1↔SI2, no private relative directory in display output, restored verified status quarantine, offline/429/404, mismatched DOI, forged landing host, mismatched article ID, Unicode NFKC path separator, bidi title, duplicate NFKC filenames/file IDs, unconfirmed local link, path traversal, local alias collision, malformed UUID/DOI and noncanonical article IDs.
- **CI:** `.github/workflows/si-isolated.yml` now builds/runs `tests/native_si_rows` on both Linux Qt6 and Windows MSVC Qt6 in addition to existing tests. `tools/preflight_public.py` allowlists the three reviewed new text files. **No exact-new-HEAD PASS is asserted before checking the new CI.** Qt SDK is not installed in the local container and direct GitHub clone DNS failed, so this note is source-level evidence only.
- **Live evidence boundary:** The official public ACS Figshare landing for parent DOI `10.1021/acsnano.8b07631` was readable in the public browser on 2026-11-03 and showed a related-material DOI link, but `api.figshare.com/v2/articles/7688531` was inaccessible from that tool. This is **not** a successful programmatic API/landing probe and does not establish individual SI-file reachability. No file was downloaded.
- **Next:** inspect the exact-new-HEAD Python/privacy, Linux Qt6 and Windows Qt6 jobs; fix any compiler/test failure in this dedicated branch and record the failing step. From an allowed network run the existing opt-in `si_live_probe.py` with DOI `10.1021/acsnano.8b07631`, record API and landing HTTP status without binary downloads, then design a trusted per-candidate proof handoff rather than accepting a caller-supplied Boolean. Before real UI adoption, coordinate PR #16's newer `.luna-meta.json` paired-move implementation with Issue #9 and run Windows move/rename/restart acceptance. Keep PR #17 Draft; no merge/Release.


**Exact-code CI acceptance (2026-11-03):** [SI isolated #41556715957](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41556715957) on code/workflow commit `abed3e2fb79730ae9dd50ed88d573b8800c4718b` completed **SUCCESS**: offline Python integration + public-source privacy gate PASS; Linux Qt6 native identity, SI rows and pinned PR16 paired-move contract PASS; Windows Qt6/MSVC native identity, SI rows and pinned PR16 paired-move contract PASS. Later commits only append this handoff documentation; they do not change tested code. These are synthetic/isolated tests, **not** production Windows UI, live API, individual SI file accessibility, or actual user-PC acceptance. The PR remains Draft/non-mergeable.


## Follow-up: restored lookup state / live-proof type guard (2026-11-03)

- Root cause: Python display projection previously passed through unknown restored
  lookup statuses without a `needs_review` downgrade, unlike the native Qt
  projection. Also a truthy non-boolean `live_landing_check_completed` value
  (e.g. `"true"` or `1`) could promote a candidate labeled
  `landing_verified` without a strictly boolean verifier result.
- Reproduction: project a synthetic `SiLookupResult` with status
  `unexpected_state`, or status `verified` and a candidate labeled
  `landing_verified` while passing `live_landing_check_completed="true"`.
- Fix: unknown statuses now become `needs_review`; promotion requires
  `live_landing_check_completed is True`. Synthetic regression tests cover
  unknown state and several truthy non-boolean values.
- Validation: GitHub source commits recorded; exact-HEAD Python/Qt CI and
  real HTTP reachability **not yet confirmed in this run**. Do not report
  verified live links based on these synthetic tests.
- Next run: inspect CI for HEAD, run `python -B -m unittest discover -s
  tests -p 'test_si_display_model.py' -v`, then confirm public ACS/Figshare
  API/landing reachability separately without downloading SI PDFs.


## 2026-11-03 — fail-closed public SI display verification (CI pending)

- **Cause:** Python projection accepted a caller-provided `live_landing_check_completed=True` and a restored candidate's `landing_verified` string as sufficient to display a verified SI link. An untrusted caller/restored state could therefore claim HTTP verification without a network request. The QtCore projection already withheld promotion.
- **Reproduce:** Construct a synthetic `SiLookupResult` with status `verified`, a candidate with status `landing_verified`, and invoke `project_si_rows(index, main_id, lookup, live_landing_check_completed=True)`. Before this patch, the row status was `landing_verified` despite no HTTP request.
- **Fix:** Retain the Boolean argument for compatibility but never promote public SI rows from it; all public rows stay `unconfirmed`. This intentionally matches the QtCore fail-closed boundary. Existing DOI extraction, Figshare metadata lookup, move engine, UI and other PR branches were not changed.
- **Test:** Updated the existing synthetic regression to require `unconfirmed` even for a true Boolean; earlier non-Boolean spoof tests remain. Execute `python -B -m unittest discover -s tests -p 'test_si_display_model.py' -v` and check exact-HEAD CI before PASS claims.
- **Limitations:** No trusted HTTP-proof handoff has been implemented, so a genuinely opened landing is still displayed as unconfirmed. Actual ACS/Figshare API, individual SI-file reachability, Windows UI, and paired PDF/sidecar move/restart remain unverified.
- **Next:** Check exact-HEAD Python/privacy and Linux/Windows Qt CI. Design an immutable per-candidate verifier result produced inside the network boundary and bound to parent DOI, article ID, canonical URL, response status and observation time; only then allow verified UI status. Run permitted live probe without PDF downloads. Coordinate Issue #9 Windows acceptance. Keep PR #17 Draft; no merge or Release.
