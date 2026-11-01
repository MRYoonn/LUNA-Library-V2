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
