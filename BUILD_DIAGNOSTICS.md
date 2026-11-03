# LUNA Windows build diagnostics — mandatory acceptance policy

**Scope:** Manual GitHub Actions `.github/workflows/build-windows.yml` for the public,
synthetic LUNA developer branch. This is not a release or a live-user crash collector.

Every new Windows build must, **before source checkout**, write a small
`bootstrap.json` containing the workflow run, job and exact commit SHA.
After checkout, `tools/build_diagnostics.py` must create and finalize a
`build-diagnostics.json` manifest listing stage outcomes. Both the
`windows-packaging-preflight` and `build-windows` jobs upload their evidence
with `if: always()` even when an earlier stage fails. GitHub's named job/step
logs remain the canonical complete compiler and test traces.

During the synthetic native UI smoke, `tools/smoke_windows.py` captures
LUNA.exe **stdout and stderr from process start**, preserves the last bounded
64 KiB of each, and stores a `luna-native-failure.json` with the last reported
stage and elapsed seconds on failure/timeout. Native C++ saves the current
checkpoint independently. The 45-second subprocess timeout and 15-second
1000-PDF scan acceptance threshold are **not** relaxed.

Expected evidence artifacts:

- `LUNA-Preflight-Diagnostics`: bootstrap and exact SHA, public gate result.
- `LUNA-Build-Diagnostics`: bootstrap and exact SHA, compile/native/installer
  gate outcomes, including early failures.
- `LUNA-UI-Synthetic`: safe synthetic screenshots, stdout/stderr tails,
  progress checkpoint and native failure reason (when the native step ran).

**Troubleshooting order:** Open the newest *exact-commit* workflow run; inspect
the failed job and step. Check the matching manifest before older runs.
For a native timeout inspect `luna-native-failure.json`,
`luna-native-last-stage.txt`, `luna-native-stderr.log` and GitHub step output.
If the native process never wrote a stage, report that explicitly; inspect
startup process logs instead of guessing that the 1000-PDF scan was responsible.

**Non-negotiable:** Do not delete tests, increase a timeout to label a hang
fixed, bypass an error, falsely classify a compiler-only pass as a successful
build, or release an installer without fully green native validation.

**Privacy:** This pipeline uses only generated synthetic PDFs. Do not include
personal user PDFs, notes, local settings, private paths, credentials, memory
dumps, or complete environment snapshots in public artifacts. Run IDs and
commit hashes are public diagnostic metadata.

## Mandatory permanent cause-analysis record

On every completed CI job, `tools/build_diagnostics.py` now emits a
`failure-analysis.md` alongside `build-diagnostics.json`, with the exact run,
source commit, first failed gate, per-gate outcomes, and GitHub Actions log URL.
A failed gate's specific root cause must remain **not established** until
the actual native trace, screenshot or compiler output proves otherwise.
The diagnostic artifacts use the maximum configured **90-day** retention
(instead of 14 days). On a failed Windows build the workflow also posts
the Markdown report into a durable GitHub Issue, subject to repository issue
permissions. A GitHub Issue preserves its report content after the ZIP artifact
expires. Artifact upload still runs even if issue archival is unavailable.

### Verified historical failure trail

| Build | First failing stage / verified issue |
|---|---|
| #221 | Independent library/paper collapse held a wide dock |
| #222 | Native page-number input failed the page-two check |
| #223 | PDF4QT page input committed twice on focus-out |
| #224 | Python preflight pin list grew to 3 but test expected 2 |
| #225 | The downstream pristine-upstream check rejected the earlier Editor patch |
| #226 | Viewport scrolled to page two, but continuous view still intersected page one |
| #227 | Paper-only content width 290px; outer QDockWidget retained 432px |
| #228 | Strict PDF-A paired move/same-tab check failed |
| #229 | Native UI selected a stale/wrong paper card after memo/search; A PDF was not moved |
| #230 | **SUCCESS** on exact SHA `b33c237d7a4c8ab0156f414ca61a2420aa23458f` |
| #231 | **FAIL** in preflight: outdated `test_folder_selection_matches_recursive_count_badges` expected subtree display, contradictory to direct-only selected-folder requirement. Fixed in subsequent feature branch commits. |
| #232 | **FAIL** in native UI smoke: old `Child PDF did not increment its own folder and every ancestor` check contradicted new direct-only count rule. Subsequent 1000-PDF parent scan also expected recursive parent contents; both native checks fixed, while All Documents recursion is now explicitly verified. Original native process also reported an access-violation exit; inspect next native run and CDB evidence if it recurs. |

[Successful #230 run](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41469874145).
The post-#230 feature changes (multi-PDF trash, direct folder display, icon-only
headers, size memory, zoom menu visibility, automatic update toggle) have
**NOT** been validated by a new Windows build. Do not label them released.

**Current branch limitation:** These guarantees become effective for builds
using the commit containing this workflow. Earlier historical builds do not
retroactively acquire these diagnostics. No workflow is triggered automatically:
the repository remains manual-only, and draft PRs are not merged automatically.

## Manager 8 — folder-count semantic gate (2026-11-03)

The prior Windows run #232 (41536324680, commit abec694) failed after a successful product compile: the native smoke expected an ancestor count to grow when a nested PDF was added. The current product specification is **direct child PDFs only**, so the prior assertion was inconsistent with the design. A later source change corrected the assertion but has not received a Windows full-build pass.

The public Windows preflight now runs `tools/verify_folder_counts.py` before expensive PDF4QT compilation. It fails closed if direct-only counting, nonrecursive selected-folder listing, recursive All Documents, stale-worker protection, or the native child add/delete checks disappear. Its synthetic-only JSON is retained with `LUNA-Preflight-Diagnostics` on success or failure. The native smoke additionally waits for the deleted child folder count to return to zero. This guard is **source-level**, not evidence of Windows execution or publisher-PDF accuracy. Keep all related changes in Draft until full exact-SHA Windows validation.

## Manager 8 — Build #233 native print failure (2026-11-03)

Full Windows Build #233, run 41550167904, exact SHA 36d9494b, passed the Python/public gate, native Qt tests, MSVC compilation, portable verification, ordinary GUI launch and opening benchmarks. It FAILED inside native UI smoke with exit 9: `Ctrl+P did not open native print dialog`. The Win32 print probe reported ownedDialogs=0, cancelRequests=0, seen=0, finished=1; no portable was published. This is distinct from the earlier folder-count test error.

The native smoke now keeps its Win32 cancel observer alive while Qt processes queued shortcut work and separately counts native print-action invocations. The build's permanent synthetic diagnostic report now classifies the native failure from a strict allowlist of numeric and coded fields, without copying user PDFs, private settings, or raw stderr. The failed job must remain failed; a passing preflight or compiled EXE alone is not a passing build. Actual Windows confirmation remains required on the revised SHA.

A one-shot, branch-restricted workflow dispatch bridge requests the existing manual Windows workflow on this branch. It is scoped to changes of its own bridge file and cannot run on main or independent developer branches.

## Manager 8 — exact run 41555811107 (2026-11-03)

The first print-diagnostic attempt (SHA `bb3b8ce`) again failed native UI smoke with exit 9. The now-explicit checkpoint is **Native print QAction unavailable after PDF reopen**. The source's preceding reopen check observed a document and `getIsBusy()==false`, but PDF4QT's upstream action policy requires its separate asynchronous future watcher to be idle before `Print` becomes enabled. This explains why the prior attempt did not reach Windows PrintDlgEx; window owner matching cannot fix an inactive QAction. The correction waits for `actionPrint->isEnabled()` (bounded, real Qt event processing), fails with busy/document diagnostics if not ready, and then requires one actual Ctrl+P QAction dispatch and an observed cancelable Win32 dialog. The separate top-level HWND owner fix is retained for the subsequent dialog stage. None of these checks are bypassed.

## Manager 8 — Eight-item follow-up bugfixes (2026-11-03)

1. **Shift multiselection + right-click trash:** Previous code reset a nonempty selection to the clicked path if the clicked path was outside the group. This made bulk trash operate on a single PDF. A right-click outside an existing multi-row selection now preserves the selection, and the context menu snapshots selected PDF paths before showing the menu. The fallback to the clicked item applies only for an empty selection. Native Qt viewport smoke uses a synthetic three-row selection with a fourth unselected right-click target. Existing Ctrl+Z still restores only the last file, and any open/locked file retains its safety block.
2. **Root/child folder selection:** Previous child-folder clicks force-checked the root shortcut. The actual child tree row is now selected without marking the root, while root selection clears the child tree selection. Selected-folder PDF listing remains nonrecursive; All Documents remains recursive. The native folder-count smoke tests selecting and returning from a child.
6. **Pane expansion/size memory:** Previous paper-only 290px/300px max constraints restricted expansion. All nonempty pane states now permit the existing 850px maximum. Dock resize events persist the last user width (including one-pane mode) with a 250ms debounce. The two-pane splitter ratio is stored only while both panes are visible. The saved width is restored on startup and collapse/reopen. The previous 300px upper-bound regression was changed to verify expansion.
8. **Automatic stable update checking:** The Help menu toggle remains saved in QSettings; the new implementation synchronizes the setting and restores the toggle with a visible error when saving fails. Release ZIP digest, manifest verification, explicit approval, rollback, and startup acknowledgement remain mandatory. There is **no published stable release yet**, so a real published-release-to-release update cannot be declared validated until a manually approved Release exists.

Every Windows job writes a synthetic `failure-analysis.md` and `build-diagnostics.json`, with optional `native-failure-verifier.json`. These fixes are **not Windows-validated** until a new exact-commit Windows workflow run has concluded successfully. A source test does not substitute for that run.

## Manager 8 — Windows Build #237 native crash (2026-11-03)

Failed run: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41562960971, SHA `15d5fa5c71cc86fdb1c8b1220b92da9e89ea2c5b`. Source/Python preflight, Qt metadata/dialog checks, MSVC compile, portable verification, ordinary GUI launch and benchmark all passed. The native UI smoke failed with `0xC0000005` immediately after checkpoint `count-recovered`.

The captured native CDB stack in `LUNA-UI-Synthetic/native-crash-replay/cdb-stack.txt` identifies `Qt6Widgets!QAbstractItemView::model` called by `Qt6Widgets!QListWidget::visualItemRect` in the GUI test. The new child/root folder selection test invoked `selectFolder` twice, destroying and recreating `QListWidgetItem` objects. Subsequent hover/memo tests dereferenced the earlier stale `item` pointer, causing a native test use-after-free. This stack does not implicate the actual Windows Recycle Bin operation.

Fix: after return to the root and asynchronous scan completion, reacquire `item = list->item(0)`, verify its expected source path, then continue tests. This is a direct source-cause correction, not a bypass; a new exact-SHA Windows build is required. The failed run remains preserved with the CDB evidence and its permanent `failure-analysis.md`.

## Manager 8 — Build #238 intermittent native failure and structural test isolation

[Failed Windows Build #238](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41564257499), SHA `767e5851344281ccee1967ff410017076c7f04ab`: native smoke again exited `0xC0000005` after `count-recovered`, while all source/Qt/Windows compile/portable and ordinary GUI gates passed. The synthetic CDB replay of the SAME package returned exit 0 and explicitly reported a successful native Ctrl+P cancel, so that replay does **not** prove a fresh crash stack. This is consistent with a timing-sensitive item lifetime problem; it must not be described as a CDB-confirmed identical stack in #238.

The first fix reacquired a root-list `QListWidgetItem*` immediately after switching from child to root, but the test still went on to perform asynchronous thumbnail/memo and metadata checks that could race with filesystem notifications and future list rebuilds. The stronger fix removes this early child/root selection and runs it at the **very end** of the native smoke, AFTER all test actions that hold `QListWidgetItem*` pointers (including hover, memo edit, move/restore, and 1000-document scans). No stale list item is used after the new terminal folder-selection test. This changes the test ordering, not the application's folder-selection contract. Re-run the complete Windows gate on the exact next SHA.

## Manager 8 — Windows Build #238 intermittent crash and isolation (2026-11-03)

Failed run: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41564257499, exact SHA `767e5851344281ccee1967ff410017076c7f04ab`. Full source/Python preflight, native Qt metadata/dialog tests, Windows MSVC compilation, portable staging and ordinary launch checks all passed. The synthetic GUI/native annotation step still exited `3221225477` (`0xC0000005`) after `count-recovered`. The native stderr contained `QFileSystemWatcher: FindNextChangeNotification failed` for a just-deleted synthetic `luna-count-XXXXXX` child directory.

The captured CDB **replay** was clean (`LUNA_DEBUGGEE_EXIT`, status exit 0, no `c0000005`), so it cannot establish a stack for the original #238 failure. This makes the race hypothesis plausible, **not proven**. The preceding #237 CDB replay had identified `Qt6Widgets!QListWidget::visualItemRect` after stale list resets; the revised #238 continued to crash once, so the earlier fix alone was insufficient.

For the next exact-SHA validation the count fixture's `QTemporaryDir` now lives until the entire smoke function exits. The test deletes only its nested PDF when verifying child count recovery, leaving the selected directory watchable until no more GUI operations occur. Additional fixed-category stages `preview-enter`, `crop-enter`, `folder-icons-enter`, `hover-enter`, and `memo-enter` localize the next failure without copying private documents. This change does not suppress a crash or bypass any native gates. The status remains **FAILED** until a subsequent Windows run completes cleanly.


## Manager 8 — Atomic Shift multi-delete + retained panel width follow-up (2026-11-03; verification pending)

**User reports:** Shift-selecting multiple PDFs and choosing trash on context click previously deleted only the clicked item; re-opening the paper panel did not preserve its expanded user-set size. A separate open-item highlighting concern requires validation against actual current-document state and is not considered solved by deletion work.

**Confirmed code limitations and cause:** `PaperFileList::mousePressEvent` and the context-menu selection snapshot already protected Shift multiselection in the #240 branch, but the action loop processed individual paths with `trashDocument`. Each call replaced `RecentTrashUndo`'s one-file backup, so Ctrl+Z could recover only the **last** PDF and the deletion flow could finish with a partial batch. The single-file trash handler was no longer called after the batch refactor, but retained redundant implementation. A separate source-size preflight initially detected that adding the new guarded path would exceed the explicit 248000-byte `src/lunalibrary.cpp` cap; the obsolete handler was removed rather than weakening the security size limit.

**Fix committed to the feature branch:** `RecentTrashUndo::trashBatch` stages each source PDF plus `.luna-thumb` and `.luna-meta.json` sidecars into a temporary, byte-for-byte backup **before any Recycle Bin move**; refuses symlinks, missing files, and overlapping source paths; if the recycler fails, restores already removed files or keeps a recoverable backup if rollback fails. One Ctrl+Z restores **all** files in the batch without overwriting an existing destination. Library context-menu deletion now checks for still-open/missing PDFs before deleting, performs a single confirmed batch, and saves/reconciles favorites/recent once, followed by one list refresh. The now unused legacy single-path handler was removed. No API credentials, personal PDFs or real research metadata added.

**Added regressions:** `tests/native_dialog/filemoves.cpp` now exercises two synthetic PDFs, thumbnail and metadata files through batch delete, group undo, and deterministic failure after the first move; `tests/test_library_view_modes.py` and `tests/test_real_file_management_contract.py` enforce the new batch operation. Native `lunatabs.cpp` synthetic GUI probe now sets a user-selected ~690px dock width, checks saved `QSettings`, then checks exact width restoration after ten panel collapse/re-open cycles. The long-standing `BUILD_DIAGNOSTICS.md` is retained; every subsequent failed run must append observed step/run/SHA/diagnosis. **These are new tests, not evidence of PASS until the exact source SHA successfully completes full Windows CI.**

**Remaining:** Real physical user machine multi-file Windows Recycle Bin/undo session, user-observed first-item highlight, and a published stable GitHub Release auto-update end-to-end remain unverified. Do not publish a Release or claim these are complete without new evidence.


## Manager 8 — Windows Build #241 preflight regression failure (2026-11-03)

[Failed run #241](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41576054010), exact source SHA `deec3a35e4edc716ffeee65840aa2eb6f18e5fcd`. `windows-packaging-preflight` failed at **Public source gate and regression tests** after 155 Python tests, **154 PASS / 1 FAIL**; Windows native compile and portable packaging were correctly skipped. The public security gate itself passed. The failing test was `test_ctrl_z_recycles_first_then_restores_without_overwrite` at `tests/test_real_file_management_contract.py:104`: its newly added batch integration assertion searched `FILES` (`src/lunafilemove.h`) for the native test string `undo.trashBatch(batch,&renameError,recycle)`, but that test actually lives in `tests/native_dialog/filemoves.cpp`. Root cause: **wrong Python contract fixture**, not a demonstrated product-level batch deletion failure. GitHub automatically preserved the failure record in [Issue #28](https://github.com/MRYoonn/LUNA-Library-V2/issues/28) and `LUNA-Preflight-Diagnostics` artifact (90 days). Fix: read `tests/native_dialog/filemoves.cpp` into `NATIVE_MOVES` and assert all three new native batch/undo/rollback tokens there. Do not mark full Windows build successful until next exact-SHA attempt.


## Manager 8 — Windows Build #242 native manual dock-width test setup failure (2026-11-03)

[Failed run #242](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41576315981), exact SHA `518fc9adadbf701b43fd32c4589c0e7845400ac4`. Preflight/Python 155 tests, native Qt file management 8 tests, MSVC compile, portable staging, ordinary window launch, PDF opening benchmark and native annotations all **PASS**. Failed at `Production native tabs, library and fresh-process memo restoration`, native phase `ten real independent library/paper panel toggle cycles`, error **`Library dock refused user-requested expansion`**. Source: `LUNA-UI-Synthetic` artifact `13204700516`, `integrated/summary.json` plus `integrated/scale-1/edit.json.failure.png`.

**Evidence and root cause:** failure screenshot shows the entire unmaximized shell was about **660 logical pixels wide** on a wider Windows runner desktop; newly introduced test attempted `host.resizeDocks({libraryDock}, {690}, Qt::Horizontal)` without first widening the enclosing host. A 690px left dock cannot fit a 660px shell plus PDF reader. This **test fixture precondition error**, not evidence that the app's 850px dock maximum itself was broken. The native test now expands the entire shell to available screen dimensions minus margins before exercising user-requested 690px width, and emits `manualDockWidthDiagnostics` (requested/window/dock/maximum/available widths) for future diagnostic proof. The requested persisted width must still survive ten collapse/reopen cycles; assertions remain intact, not bypassed. **Pending exact-SHA Windows validation.**


## Manager 8 — First public auto-update release; live Windows failure and successful compatibility hotfix (2026-11-03)

- First stable [`v0.9.0.243`](https://github.com/MRYoonn/LUNA-Library-V2/releases/tag/v0.9.0.243) was published from exact Windows Build #243; both assets and all 136 SHA256s matched independent reproduction. The isolated release creation workflow [#1](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41584017047) created a draft successfully, but its subsequent **draft-by-tag REST verification returned 404** (drafts have untagged URLs). Resolved by checking the existing numeric Release ID `432310440`; did not create a duplicate or alter published assets. Publication workflow [run 41584371725](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41584371725) succeeded.
- The real old Windows Build #240 update rehearsal [run 41584665613](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41584665613) returned `{"available":false,"status":"unavailable"}` at **Check**, before download. Root cause in source: the first shipped updater's `Validate-Relative` allowlist rejects packaged `Luna-Crash-Tools.ps1`; `Get-Manifest` validates every file. Therefore the manifest and existing updater were mutually incompatible, despite a healthy public GitHub feed. Synthetic failure evidence artifact `13207562059`.
- Fixed **release packaging without weakening the user's installed updater**: `tools/prepare_release.py` now excludes unsupported standalone `Luna-Crash-Tools.ps1` from stable update ZIP and inserts its original audited function source into allowlisted `Analyze-Luna-Crash.ps1` and `Start-Luna-Crash-Diagnostics.ps1`. The original Build #243 EXE and Qt binaries remain unchanged. `tests/test_updater.py` enforces the legacy allowlist, injected local diagnostic functions, and fail-closed scripts. Packaging source was reviewed and CI Python gate passed in [run 41585362495](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41585362495).
- New stable [`v0.9.0.244`](https://github.com/MRYoonn/LUNA-Library-V2/releases/tag/v0.9.0.244), GitHub Release ID `432318667`, published via [run 41585476157](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41585476157); latest public feed points to this tag. Release ZIP SHA256 `5cf267ffbaa76a4eb32f7fc39d873dd9b8e7437655fcf9a3d199210ae74ecba6` (51,762,522 bytes); manifest SHA256 `a1d2fe133d85c4f286d28cd28c1a0cd4f6c5d3949188de98ce7c2a931391d879` (21,539 bytes); 135 validated file paths.
- **Confirmed real Windows end-to-end success**: [run 41585574888](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41585574888), artifact `13206934531` `update-test-summary.json`, exact `status=passed`; all `originalBuild`, `liveCheck`, `liveDownloadAndStage`, `applyAndHealthyRestart`, `userOwnedFilesUnchanged`, `noRepeatOffer` marked `passed`. An isolated Windows GitHub runner installed the original, unchanged Build #240 Portable, queried the live public `releases/latest`, verified downloaded release, replaced only managed files, confirmed healthy new GUI startup, verified EXE hash/semantic version, preserved three synthetic user-owned file hashes and confirmed the next Check does not reoffer the same version.
- **Limitations**: CI runner is not the user's physical PC, real third-party antivirus/network conditions and extended usage remain unverified. The first `v0.9.0.243` remains on GitHub only for historical traceability; recommend installing `v0.9.0.244`. Release packaging corrections are now integrated into this manager8 feature branch for future builds, with original already-published assets unchanged.


## Scheduled Bibliography Module Integration — conservative first-page extraction (2026-11-03)

**User request:** Integrate prior scheduled work from module ② Bibliography Accuracy Lab and module ④ SI Finder & Linker to strengthen LUNA's bibliography fields/title, without blindly merging unfinished Draft modules, changing manually edited metadata, publishing a Release, or uploading private PDFs.

**Verified branch status before integration:** ② Draft PR #18 `agent/bibliography-accuracy-lab` at `0a783f84b8598a0555998e22cbbf4a2c58984f60`, base `feature/citation-metadata-engine`; ④ Draft PR #17 `agent/paper-si-finder-linker` at `4a5110530682684c2d8a7ec3a693192527f05775`. Both prior hourly automations were disabled at inspection and were not changed. Latest independent module ② Actions run `41586651966` **FAILED** Python at `tools/bibliography_accuracy_lab.py:373` due to a literal escaped `\\n` outside a Python string, and **FAILED** Windows QtCore C++ compilation at `qdirlisting.h` because a malformed `luna::Bibliography` scope broke Qt namespace parsing (first error C2804); none of these standalone CI results prove new production-extraction accuracy. Do not merge the module head verbatim or mark it green.

**Production root cause and repairs:** The active LUNA first-page path (`src/lunafirstpage.h`), geometric title classifier (`src/lunatitleextractor.h`) and `Bibliography::firstPageCitation` all used broad `contains("Supporting Information")`-style checks, so a normal ACS article with a top-page SI navigation link could be **misclassified as SI and have its own title, DOI/citation suppressed**. PDF printer spool titles such as `jo5c01153 1..9` could also pass the previous generic title credibility check. RSC/ChemComm two-column footer page ranges and Nature Communications article numbers had no publisher-exact fallback in the current production parser.

**Patch scope:** Isolated new QtCore-only `src/lunafrontmatter.h` implements bounded, anchored SI-cover recognition, explicit ACS/RSC article title fallback requiring publisher masthead, citation/DOI and author boundary, exact RSC/Nature footer formats, and printer spool title exclusion. Integrated the same classifier into the actual PDF worker, title-layout analyzer and bibliography model. Publisher cover titles do not override a manually edited title; they may correct a conflicting machine-owned PDF Info title with field-level provenance. Existing geometric title extraction still takes priority, and unclear cases abstain. Article numbers are saved separately from page ranges. `tools/apply_overlay.py` explicitly deploys the new header into PDF4QT, while `tools/preflight_public.py` adds only the new public source and regression test. `tests/native_dialog/bibliography.cpp` now includes simulated ACS main+SI-navigation, true SI cover, RSC title and complete footer, Nature article number, DOI-only Nature main cover, publisher spool metadata, trusted-vs-manual title, user state and replay tests. `tests/test_scheduled_bibliography_integration.py` verifies the production wiring and asserts no new network or PDF disclosure code. User's previously provided ACS/RSC SI, Angew and Nature PDF examples were inspected **locally** for realistic cover features; their bytes, text and filenames were **not** checked into GitHub.

**Validation gates:** The new code is NOT considered ready until exact branch SHA passes the full existing Python preflight, 8+ Windows native Qt tests (including `native_bibliography`), PDF4QT MSVC compile, the real LUNA UI smoke and portable ZIP hash provenance. A synthetic source test alone never counts as real publisher PDF extraction accuracy. The independent ② CI and actual PDF goldset evaluation remain separate, so do not claim the module's 24/24 synthetic proxy equals measured production precision/recall.


### Windows Build #244 source-contract failure and correction

- [Run #244](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41590065398), exact source SHA `179f61ed7f32c8a7a243c63a67af2c1eab4682b2`, `windows-packaging-preflight` failed at Python `test_citation_contract.CitationContractTests.test_no_prose_or_missing_info_is_invented` (`tests/test_citation_contract.py:89`): an obsolete source-only assertion searched `src/lunabibliography.h` for a literal `QStringLiteral("Supporting Information")` that is **correctly absent** after strict SI classification moved to shared `FrontMatter::isSupplementCover` in `src/lunafrontmatter.h`. Full CI ran 162 Python tests, **161 PASS / 1 FAIL**, and correctly skipped costly Windows compilation. Public security gate passed. A failed-run synthetic diagnostic artifact was retained as `LUNA-Preflight-Diagnostics` ID `13210035981`.
- Root cause is a stale test fixture, not demonstrated failure of production SI classification. Replaced the brittle literal assertion with actual production callsite invariants in the bibliography model and the first-page PDF4QT extractor. Do not claim a complete Windows PASS until a new exact-SHA run succeeds. Existing C++ Qt native cases remain active and unchanged.
