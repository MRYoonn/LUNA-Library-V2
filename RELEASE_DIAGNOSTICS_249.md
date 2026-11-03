# LUNA Library v0.9.0.249 — Public Release Diagnostics (2026-11-03)

## Identity and published artifacts

- Published stable tag: [v0.9.0.249](https://github.com/MRYoonn/LUNA-Library-V2/releases/tag/v0.9.0.249), release ID `432735397`, draft=false, prerelease=false.
- Original PDF application source: `809a76df1e12238921768cddc2c84422f950fd4b`, from Draft PR #32 `feature/hidden-sidecar-storage-20261103`. **No rebuilt/modified application EXE** was substituted during release packaging.
- Original complete Windows Build #249: [run 41633848931](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41633848931), completed/success, two required jobs succeeded.
- Portable published ZIP `LUNA-Windows-Portable.zip`: 51,775,371 bytes, SHA-256 `423591ec93c055a2841f080ff6d2e8d4cb2c5ee4ddfbe54bc21ef7b4aa02de87`.
- Release manifest `LUNA-Release.json`: 21,539 bytes, SHA-256 `889462f4b44e23c9c91174ee3973009a015a839f155e6fc9fa3cd0da61c881a4`, schema 2, original run/source SHA and every packaged file hash bound to EXE.
- Updater payload excludes `Luna-Crash-Tools.ps1` (previously incompatible with published old updater); audited diagnostic helper functions are inlined into the allowlisted diagnostic scripts.

## Pre-release tests

- Windows source preflight + Python synthetic regression: **167 tests passed**, 145-source-file public safety allowlist.
- Windows Qt C++ synthetic regression: **9/9 native dialog tests passed**, including `native_hidden_sidecars`, `native_file_moves`, `native_document_claims` and bibliography; additional metadata-native test passed.
- Full PDF4QT/MSVC native EXE compile and Windows portable packaging **passed**.
- Native GUI smoke passed: PDF crop/cancel, hidden thumbnail save, open/close/reopen, annotations round trip, physical file movement with metadata and thumbnails, 2/5/10 independent PDF windows, Unicode paths, tab isolation, fresh-process memo persistence at 100%/125%/150% scale, CDB crash diagnostics.
- [Release package and independent Windows prepublish checks, run 41644674139](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41644674139): **completed/success**. Verified source run/commit, ZIP CRC and each digest, Windows updater PowerShell parsing and nine updater contract tests, before creating an immutable DRAFT.

## Public stable and actual old-version updater test

- [Release publish + Windows live upgrade, run 41645558037](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41645558037): **completed/success**. Publish and live-update jobs both succeeded; safety rollback job correctly skipped.
- Real GitHub-hosted Windows runner downloaded the **previously released v0.9.0.246** ZIP, executed its original `LUNA-Update.ps1` (not a rewritten test updater), then exercised public stable feed `Check` → HTTPS `Stage` → checksum/hash verification → user-consent-equivalent `Apply` → native GUI healthy-restart acknowledgment.
- Verified new `v0.9.0.249` source run/commit, published ZIP manifest SHA-256 and original EXE SHA-256 derived from the immutable release manifest. A second `Check` offered no repeated update.
- Synthetic sentinel PDF, thumbnail, citation sidecar and annotation metadata retained their pre-update SHA-256; **no actual private user PDF was read or modified**.
- GitHub evidence artifacts: `LUNA-v090249-Publish-Diagnostics` and `LUNA-v090249-Live-Update-Diagnostics` (90-day retention). Source build and prepublish diagnostics are also retained in their respective workflows.

## Root causes fixed and limitations

- [Build #247 failure, Issue #33](https://github.com/MRYoonn/LUNA-Library-V2/issues/33): test asserted legacy visible location for a successfully moved cropped thumbnail rather than new `.luna/thumbnails` location. Fixed assertion; never disabled underlying PDF reopen/move checks.
- [Build #248 failure, Issue #34](https://github.com/MRYoonn/LUNA-Library-V2/issues/34): three stale multi-tab integration assertions used previous same-folder PNG paths. Fixed all to use shared `SidecarStorage::thumbnail`, pinned in Python source contract. Full Build #249 passes.
- Initial v249 release draft run `41644469959`: application/package Windows checks passed, but the **diagnostics recording step** wrote to a leftover `luna-246-evidence` directory while creating `luna-249-evidence`. Corrected the audit workflow path and successful run `41644674139` includes diagnostics.
- Migration of existing `.luna-meta.json` and thumbnail files into a hidden per-PDF-parent `.luna/metadata` and `.luna/thumbnails` was verified on **synthetic Windows filesystem fixtures**. Colliding old/new records remain untouched. PDF contents are not migrated or edited.
- **Unverified outside CI:** actual MYBOX hidden-directory synchronization across two user PCs, very long physical-PC sessions, large real-publisher-PDF bibliography precision/recall, OS-level monitor DPI, and merge to `main` of the stacked Draft PRs. `betaComplete` remains `false`. Public release success does **not** imply those acceptance tests have been performed.

## User installation notes

1. Existing v0.9.0.246 can update in-app via `업데이트 확인…`, with explicit user agreement and checksum verified before replacement.
2. New users can download the official published `LUNA-Windows-Portable.zip` asset. Unzip all files to a normal writable directory, launch `LUNA.exe`.
3. For a folder with valuable research records, make an ordinary full copy of PDFs and their currently visible `.luna-meta.json`/thumbs before first launch. Do not manually delete or edit hidden metadata. Migration is non-overwriting; conflicts require review.
4. Users of MYBOX must verify that hidden `.luna` directories are synchronized on their own devices. Windows Explorer's hidden display setting does not control the MYBOX sync service.
