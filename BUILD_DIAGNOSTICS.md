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


### Metadata native quickcheck #1 — Qt download infrastructure failure

- [Run 41591182468](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41591182468), exact SHA `d0d8752c7656cc66ed9d333c008deb53d2b52c96`, one-shot `metadata-native-quickcheck-once.yml` **FAILED before Python tests or MSVC compile** at `jurplel/install-qt-action@v4`.
- GitHub-hosted Windows runner installed `aqtinstall 3.3.0` under Python 3.15, `aqt list-qt windows desktop --spec 6.11.3` returned `6.11.3`, then `aqt install-qt` failed to fetch the `qt6_6113/Updates.xml` repository metadata (warning: checksum download, error: `Failed to locate XML data for Qt version '6.11.3'`). This is **unverified Qt download infrastructure/installer provenance failure**, not evidence that new bibliography C++ compiles or fails.
- Retry using pinned historical aqtinstall source from existing production workflow to reduce installer drift; preserve fail-closed checks. Do not relax native tests or claim this run passed.


### Metadata native quickcheck #2 — preflight ran after Qt installation
- [Run 41591472586](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41591472586), exact SHA `0b2bc781e7ece0ce062acaeadbe6e1746fd9c4d7`: pinned aqtinstall successfully installed Qt 6.11.3, but source-only security preflight ran **after** `jurplel/install-qt-action@v4`. Qt files placed under repository `qt/Qt/...` were correctly flagged as hundreds of **unallowlisted external files**, so the runner stopped before native C++ compilation. Root cause is one-shot QA workflow ordering, not a security false-positive in LUNA source. Move Python preflight and source contract tests ahead of Qt installation; do NOT exempt the Qt directory from the public fail-closed release gate. The full Windows production workflow already executes preflight before dependencies and is unaffected.


### Metadata quickcheck #3 — exact-source native validation PASS
- [Windows QtCore quickcheck #3, run 41591815772](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41591815772), exact SHA `715d1bf3279932063d1e8cdb61c0ffdff56f59b9` was **completed / success**. Source security preflight and Python scheduled metadata/title contracts passed **before** installing Qt, then pinned Qt 6.11.3 downloaded and Windows MSVC compiled the real `tests/native_dialog/bibliography.cpp` target successfully. Qt native `ctest -R '^native_bibliography$'` passed, including new ACS/RSC/SI/Nature/manual-owner regression cases.
- This is a real Windows C++ fixture, not proof of precision/recall on the user's real PDFs. Full production PDF4QT compile, executable and UI integration remain a separate required release gate.

## Hidden .luna sidecar storage migration — 2026-11-03

**User-visible regression:** every local PDF created a visible `<paper>.luna-meta.json` beside the document; thumbnail crops similarly created `<paper>.luna-thumb.png`. Explorer displayed research PDFs mixed with dozens of management files. This was the original intentional sync-sidecar layout, not a corrupted or duplicate PDF.

**Implemented in feature/hidden-sidecar-storage-20261103 (not yet released):**
- New `src/lunasidecarpaths.h`: one hidden Windows `.luna` directory within each actual PDF folder, split into `metadata/` and `thumbnails/`. Windows FILE_ATTRIBUTE_HIDDEN applied; .luna and its children cannot be symlink/reparse-point redirectors.
- Preserve filename-based portable metadata, existing document UUID and the user's memo, favorite, bibliography. Preexisting visible files are atomically renamed into hidden layout only when the destination is vacant; collisions/failures retain both originals, never overwrite.
- Existing old-layout sidecars remain readable; background library scan migrates in bounded (8 PDFs / 12ms) UI turns to avoid freezing 1000+ PDF lists. MYBOX uses the same PDF-relative .luna folder, no cloud API or PDF upload.
- PDF move, copy, Explorer rename, multi-delete, Ctrl+Z and crop/preview use a shared companion-path enumeration. Copy intentionally creates independent bibliography identity; moves and trash protect BOTH old and hidden variants.
- New `native_hidden_sidecars` Windows CTest: migration, Windows hidden attribute, legacy conflict preservation, PDF+metadata+thumbnail physical move, copy identity, Ctrl+Z. Existing native file-move, MYBOX and Windows GUI smoke tests remain mandatory.

**Verification boundary:** source changes are not Windows PASS until exact source SHA succeeds in Python security/preflight, Qt C++ tests, PDF4QT MSVC, real LUNA synthetic UI, DPI/CDB/print and packaging. If CI fails, preserve the first failed step, `failure-analysis.md` and `build-diagnostics.json`, fix and re-run on new SHA. Physical MYBOX hidden-folder synchronization and real-user-PC migration are still untested; never automatically delete a conflicting sidecar.


### Windows Build #247 — native UI smoke destination-path mismatch

- [Build #247](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41625367862), exact SHA `29c0d9b57f6d9bb2c6b0f18de754bf6d4c7d4c87`: source/public preflight passed, Windows MSVC PDF4QT compile passed, 9/9 native dialog CTests passed (including `native_hidden_sidecars` and `native_file_moves`), original ZIP staging succeeded. Full native GUI smoke **FAILED** at `Opened PDF and sidecar did not move/reopen together` during the opened-document paired move. The workflow correctly blocked publication. GitHub preserved `LUNA-Build-Diagnostics` and `LUNA-UI-Synthetic`, and created durable [failure Issue #33](https://github.com/MRYoonn/LUNA-Library-V2/issues/33).
- **Proven direct cause:** `src/lunalibrary.cpp`'s integration smoke `movedThumbnail` still used `QDir(movedFolder).filePath(QFileInfo(thumbnail).fileName())` (legacy visible location), whereas the new production move correctly placed the thumbnail at `SidecarStorage::thumbnail(movedPath, ".png")` inside `.luna/thumbnails/`. Therefore a correct moved thumbnail was checked at the wrong location. This is a stale **integration assertion**, not evidence of failed PDF move. Corrected the smoke to check the real hidden destination without suppressing the move/reopen condition.
- Additional risk discovered in review: `LocalFolderSync::publish` called migration without examining failure. When a legacy and managed metadata JSON existed simultaneously, it could silently overwrite the managed record. Changed publishing to reject such conflicts and retain BOTH files with an explicit error; added a Windows native mixed-version MYBOX collision regression. `safeManagedDirectory` now rejects reparse points in nested `metadata` and `thumbnails` directories as well as the parent `.luna`.
- Verification requirement: run all exact-source Python/preflight, native Qt (including the new MYBOX conflict case), full PDF4QT native UI/annotations/tabs and portable artifact checks again on a new SHA. Only then may a new version be proposed. User PCs and real MYBOX clients remain unverified.

### Additional hidden-thumbnail integration audit after #247

- Whole-source check found **three more legacy direct thumbnail paths** in `src/lunatabs.cpp`'s native multi-tab integration checks: saved thumbnail after process restart, newly cropped A/B thumbnail isolation, and A.pdf paired move destination `movedThumb`. These assertions would target the old same-directory file instead of the actual `.luna/thumbnails` location.
- Updated only the test/fixture paths to `SidecarStorage::thumbnail(pdf, ".png")` in the same source used by production, preserving all existing checks (crop dimensions, tab ownership, EXE save/reopen, exact PDF and PNG bytes). Added source-level regression assertions so old direct paths cannot silently return.
- First rerun Build #248 was dispatched before this additional audit and therefore **does not contain the multi-tab smoke correction**; its outcome cannot validate final source HEAD. A new exact-branch full Windows build is mandatory.

## LUNA manual Supporting Information attachment — 2026-11-04

**User requirement:** Only original papers for which the user explicitly chooses a Supporting Information PDF should gain a collapsible SI section. The user does NOT want broad automatic SI discovery, network DOI lookup, or an SI element added to every paper.

**Implementation:** Original paper right-click `SI 추가…` launches the native PDF file picker. `src/lunamanualsi.h` safely **copies** (never moves) the chosen PDF into the original paper's real folder with `SI · <sanitized article title>.pdf` and ` (2)`, ` (3)` suffixes on collisions; never overwrite existing documents or old/new thumbnail/metadata sidecars. Reject symlinks, invalid parents and the article being selected as its own SI. A successful copy is followed by an explicit `Bibliography::attachManualSI`, saved to the existing QSettings bibliography and portable `.luna/metadata/` sidecar through LocalFolderSync.

**UI:** `DocumentCard` exposes a 29px `Supporting Information (N)` parent footer only when manually attached children exist, in both 322px card and 112px compact-list modes. On click, show 46px indented child PDF rows. Keep default folded state. Child double-click uses the existing PDF open handler; each PDF retains separate notes/annotations/thumbnail. Search can reveal SI children and show their parent for context without changing the user's folded state. SI without a same-folder parent remains an independent PDF rather than disappearing. `SI 연결 해제 (파일 유지)` removes only relation metadata. Rename watcher follows parent PDF filename changes; generic copy of an SI to another folder strips the relation to prevent accidental attachment to a different article.

**Safety and limitations:** No upstream PDF4QT changes, external service or private document upload. Distinguish ordinary file copy from SI attach. Folder-only browsing remains same-folder only. The SI/manual-parent relation uses the physical parent filename in the SI's portable record; when moving just the parent PDF to a different folder while leaving its SI behind, the SI remains a standalone item; this version does not silently move additional user files. Physical folder moves carry both when together, while Explorer same-folder original PDF rename updates the SI link.

**Tests:** `tests/native_dialog/manualsi.cpp` exercises actual native Windows PDF copy, Unicode and illegal Windows filename punctuation, collision index, orphan sidecar protection, same-file rejection, settings persistence and unlink. `tests/test_manual_si_contract.py` pins context menus, dynamic list/card geometry, strict manual association and source overlay. Source privacy gate remains active, with a narrowly scoped library source-size cap raised to 264KB; other sources retain the 200KB default.

**First failure:** [Manual SI bridge #41706880365](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41706880365) failed one *old* Python layout test that required the literal constant-size return `return {1, compact_ ? kCompactHeight : kCardHeight};`. Variable-size SI children and parent footer intentionally change the return while retaining the 322/112px normal rows. Updated this source test to require the new explicit role-dependent row sizing; no GUI behavior or safety guard was disabled. [Rerun #41707076701](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41707076701) passed Python/public-source gates and dispatched full Build #254. Subsequent review added self-attachment rejection; final exact-source validation must rerun.

**Release gate:** Real Windows Qt native CTest, full PDF4QT/MSVC, PDF/annotation/reopen/tabs/synthetic UI tests and portable ZIP must pass on the identical final source SHA before publishing. Previous stable v0.9.0.249 remains unchanged. Do not infer Windows physical-user acceptance or MYBOX two-PC sync from synthetic tests.


### Manual SI same-folder reuse — revised behavior (2026-11-04)

**User clarification:** SI selected from the original paper's own folder must NOT be copied back into the same folder. Reuse the existing PDF; if needed rename it in place to the article-derived SI filename. SI from another folder is still copied while its original remains unchanged.

- The manual SI context menu now calls ManualSI::placeNextToParent, which compares canonical folders. For an already correctly named SI, reusedExisting=true without changing any file. Otherwise, chooses the first collision-free sanitized filename and calls the existing no-overwrite Windows renameWithNoReplacement operation. Source == article is rejected.
- A target with an existing PDF, old/new portable metadata, or any PNG/JPG/JPEG crop sidecar is skipped; no stale user data is adopted. Source metadata and thumb links are checked for symlink tampering before rename.
- After a same-folder rename, the UI calls reconcileExplorerRename(old,new,bibliography) to move .luna metadata, thumbnails, QSettings memo, favorites, opened dates and bibliography record. If reconciliation fails, it attempts a no-overwrite reverse rename and reverse reconciliation, and reports failures rather than deleting the user's existing PDF or sidecars.
- The action refuses to rename an SI that is open in the PDF editor and refuses to steal an SI already manually linked to another article. Only files newly copied from a different directory can be deleted on failed metadata attachment; an existing same-folder SI is never deleted.
- Extended Windows Qt native manualsi.cpp checks no duplicate file counts, same-folder rename and idempotent reuse, true metadata/thumbnail/memo persistence and foreign-folder copy with original preservation. Source privacy gate and full PDF4QT/MSVC application build still required before release. Existing public v0.9.0.249 is unchanged.


## LUNA smaller moon + verified-title filename synchronization — 2026-11-04

**Reported issue:** The hollow crescent was clipped within 23x23 tab icon boxes. The left paper card used `Bibliography::record(identity).title` while the tab used only `QFileInfo(originalPath).fileName()`, giving contradictory visible paper names. Actual disk PDFs remained at opaque provider filenames.

**Root cause and correction:** Resize the tab icon to 19x19 with intrinsic transparent padding; keep blue EXE and red PDF association resources untouched. Cache each tab's real bibliography title, invalidate on a shell-level bibliography revision emitted from LibraryPanel; keep original real path in the tab tooltip. Reuse the existing paired PDF/metadata/thumbnail no-overwrite move for *same-folder* verified-title renames. Enqueue after metadata changes or first opening a previously indexed paper. Skip SI, guessed PDF Info titles, missing/colliding destinations, symlinks, and non-library paths. Never mutate an opened native PDF/held DocumentClaim; retry when the reader closes. The existing rename reconciliation updates recent/favorite/memo keys, bibliography identity and manually attached SI-parent filename.

**Safety:** Rejected destinations do not overwrite an existing document or metadata. File and sidecar moves roll back if a native rename stage fails; a QSettings migration failure after a successful paired move is an acknowledged residual risk requiring full Windows testing. Old source path is retained for diagnostics rather than deleting any file. Filename truncation is bounded for Windows paths; long card titles remain intact. Existing QSettings writes remain on the UI thread. Privacy and Python source checks retain the baseline 200 KB cap except this specific library UI source (270 KB, previously 264 KB) to accommodate real rename orchestration.

**Acceptance tests:** Updated active/inactive moon icon source contract and native GUI icon-size assertion; native Windows paired-move tests now cover same-folder rename, existing-file collisions, SI exclusion, metadata provenance, and thumbnail integrity. Existing PDF annotation, multi-tab, source privacy, user note and updater tests must still pass on the exact new commit before release. No Windows success or physical user filesystem validation is claimed by this source edit alone.


### Verified-title v1 source-gate failure — run 41768692441

- Run: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41768692441 ; commit `9ca694e71530b36618a601c39c0bc96d77497a18`.
- First failed required job: `source-gate` / `Public source and full Python regressions`. Public preflight passed; Python suite: 179 tests, 177 pass, 2 fail. Both failures were old hard-coded 264,000 byte caps in `tests/test_public_gate.py` despite the reviewed scoped source cap already being 270,000 in `tools/preflight_public.py`. Actual UI source 266,315 bytes. `native-filemoves` and full Windows dispatch were correctly skipped; no Windows build succeeded.
- Corrective action: align both source-size regression asserts with 270,000 bytes, retain all security/secret/PDF/default 200KB gates. Retest exact new SHA, record Win32+PDF4QT status independently.

## v0.9.0.260 proposed 36px original-position panel folding — 2026-11-04

- User request: transplant PR #35 into latest shipped v0.9.0.259 while retaining blue EXE/red PDF icons, 19px moon tabs, live bibliography title and verified filename/sidecar rename.
- Historical Build #250, GitHub run 41653228254, completed full MSVC and general native UI but failed production tabs integration: `Expanded dock width was not persisted after manual resize`.
- The v259-based branch gives QSplitter two permanent original-order children (LunaNavigationSlot and LunaDocumentsSlot). Each child owns a 36 logical-pixel folded restore control; no floating PDF reader/tab bar restore rail remains.
- A real QDockWidget resize does not emit the inner QSplitter::splitterMoved. Handle parent dock Resize and persist the settled user width only when both panels are expanded; keep the existing deferred width timer.
- Native integration still enforces 10 actual mouse click cycles, 36px original slots, independent restore controls, user width persistence and PDF-only tabs; no timeout or acceptance gate was weakened.
- Scoped public UI source cap changes from 270000 to 280000 bytes; all other 200000-byte file caps and secret/symlink/privacy exclusions remain unchanged.
- Release only after exact-source Windows PDF4QT/MSVC, native annotations, GUI tabs, updater compatibility, portable ZIP/manifest and SHA checks pass.

### v260 first exact-source gate failure — Actions run 41804918192

- Public source preflight passed; full Python regression suite ran 182 tests and failed two legacy source-text assertions in tests/test_library_view_modes.py. These still expected the old QSplitter children-collapsible flag, a zero-width hidden pane, and a restore widget inside PDF tabs; source deliberately uses two visible 36px original-position child slots instead.
- Corrected tests now explicitly require noncollapsible ordered splitter slots, exact 36px sizes, live dock width persistence on resize, and no PDF-tab restore rail. No Win32 build was dispatched from the failed source gate. Existing source feature code and user data were untouched.

### v260 Windows build failure — exact SHA 8e7d32b942b37d796c65c927e2c782168562d6a4

- GitHub Windows run [41805543137](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41805543137) finished FAILURE; all preflight, Python, PDF4QT compilation, native probe and packaging gates preceding production-tab integration passed.
- Real integration failed again in phase `ten real independent library/paper panel toggle cycles` before clicking folding buttons: `Expanded dock width was not persisted after manual resize`. The automatically gated v260 release run [41805490691](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41805490691) correctly blocked publication; latest stable remains v259.
- New correction: Qt's native dock geometry can be coalesced without the splitterMoved event or a dependable dock Resize event. A 200ms timer now checks the settled *actual* QDockWidget width while both contents are open, syncs `library/expandedDockWidth` only when it changes, and preserves inner splitter settings. This is not a test bypass, fake resize, or timer delay increase. A new full exact-source Windows run with the same strict native checks is mandatory before publishing.

### Follow-up Windows failure and sampled-transient diagnosis — 2026-11-04

- Exact SHA `b8ab5bba439f79404eeefe5bbdab1b437284a096` Windows run [41841853404](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41841853404) compiled/packaged but failed unchanged native condition "Expanded dock width was not persisted after manual resize" (release gate [41841799230](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41841799230) blocked publication).
- Independently downloaded the `LUNA-UI-Synthetic` Actions artifact 13329947378. The actual `integrated/scale-1/settings/LUNA/LUNA Library.ini` *contains `expandedDockWidth=690`*, so persistence did occur. The test's `manualDockWidthDiagnostics` was captured immediately after `resizeDocks({dock},{690})` and shows `dockWidth=562`, confirming asynchronous layout progression.
- The test previously accepted `dock->width()>=620`, sampling the first intermediary width, saving that as `userDockWidth`, then requiring the persisted *final* width to equal the transient. This is not reliable native-QDockWidget behavior.
- Corrected the test to require the actual requested 690px width to settle within 4px *before* capturing `userDockWidth` and asserting exact persisted equality. Adds `settledDockWidthDiagnostics` with both values to native JSON. No feature code deleted, no real input/click test bypassed, and no window-size or persistence check disabled.
- v260 release remains blocked until the new source's full Windows UI integration and SHA-verified release pass.

### v260 native widget source-gate failure — third full build #41846832732

- SHA `c74ccdc2bdec4869621721857ffd4b6612710ebc` completed Windows compilation and all pre-integrated tests but failed the panel-width check. The verified integrated artifact `LUNA-UI-Synthetic` #13331798517 contains `integrated/scale-1/settings/LUNA/LUNA Library.ini` with `expandedDockWidth=690`, `expandedNavigationWidth=225` and `expandedDocumentsWidth=453`.
- The preceding correction waited for `abs(actual-690) <= 4`, then snapshotted the first such width. That transient could be 686..689, while the timer correctly stored 690. The test then compared the stale snapshot to the final persisted width. This is a test synchronization defect, not evidence that the persisted width is missing.
- The corrected native check now waits for **simultaneous** matching of the live dock width and QSettings value within the 690px target tolerance (same integrated event-loop iteration) before proceeding with all ten real click-driven independent fold/unfold cycles. It still fails if geometry cannot settle or settings do not agree; no skipping, mocking, or reduced UI coverage.
- The v260 release gate remains strict and unchanged.

### Root cause identified — mismatched Qt QSettings constructors, 2026-11-05

Full Windows exact-source run [41849614748](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41849614748) compiled and packaged successfully but failed the strict real UI integration at "Expanded dock width was not persisted after manual resize". Its archived `LUNA-UI-Synthetic` INI had `expandedDockWidth=690`.

**Actual defect:** the INI writer uses `QSettings(QSettings::defaultFormat(), QSettings::UserScope, "LUNA", "LUNA Library")`. The Windows integration reader and two dock restore sites previously used `QSettings("LUNA", "LUNA Library")`. Per official Qt QSettings documentation, **that two-string constructor always uses QSettings::NativeFormat regardless of QSettings::setDefaultFormat**. The UI audit sets `QSettings::setDefaultFormat(QSettings::IniFormat)` and `QSettings::setPath(...)`, therefore it wrote the correct 690px value to the isolated INI while the test/restore code read the Windows registry instead. Earlier attempts wrongly blamed timing/geometry, explaining repeated test failures even after the final INI was correct.

**Fix:** Replace both short-constructor reads in `src/lunatabs.cpp` and both restore reads in `src/lunalibrary.cpp` with the same explicit `QSettings::defaultFormat(), QSettings::UserScope` used by the writer. Add permanent static regression assertions in `tests/test_inline_fold_v260.py` disallowing the two-string constructor in the dock-width path. The real UI 690px persistence requirement and all ten mouse-driven collapse/restore cycles remain enabled. Release is still blocked until exact SHA Windows build and asset SHA verification succeed.

Reference: https://doc.qt.io/qt-6/qsettings.html ; QSettings(const QString &organization, const QString &application) constructor is always NativeFormat.

### v261 bibliography fidelity and evidence gating (2026-11-05)

Base: immutable public Windows release `v0.9.0.260` at `bdb9d07dc801651ba8c36eb11472781d6829c059`. Changes are additive in a separate branch and never mutate the previous release.

- A multi-line Nature-family headline whose PDF exporter uses ordinary (nonprominent) font size can now be recognized via **publisher/DOI cue + standalone Article/Research Article + short heading + a clearly delimited author list**. A publisher cue alone, missing author boundary, body abstract, or SI cover remains an abstention. No generic body scan or first-article-guess fallback.
- RSC final-author parsing now supports text-flow splits between an explicit terminal `and` and the final named author, while still requiring adjacent named-author-list evidence and a valid final name. No PDF Info author is ever presumed the final author.
- The existing typography algorithm's `needs_review/titleCandidate` output is now carried through `extractFirstPage` and bibliography storage *separately* from trusted `title`. A user can view and explicitly accept that proposal in the paper's bibliography editor. Review-only proposals never trigger automatic physical filename changes or overwrite a manual value.
- New Python source contracts and actual Qt-native C++ fixture assertions cover Nature positive/negative, explicit split final-author, SI boundary, candidate persistence, manual author/title protection. All checks must pass in the unmodified production Windows workflow before publication; include the exact source SHA and release asset digest.
- This is not a measured title/author accuracy percentage across a real user-selected PDF corpus. Run a blind goldset before making any quantitative accuracy claim.


## v262 panel-before capture: run 42035124237

- Source: `2eeef96f95a532d2cef63f9ecb99bff30a59b8c7`.
- Failed step: Capture actual pre-fix native windows at all scales and panel states.
- Actual error: pywinauto UIA wrapper has no `move_window` method. This is a
  capture harness API error, not an application build or panel test result.
- Function: `tools/capture_panel_comparison_windows.py::run`.
- Correction: use typed Win32 `MoveWindow` for the actual top-level HWND,
  retaining UIA accessible-name clicks and actual screenshot capture. Serialize
  RECT coordinates explicitly. Capture workflow now retains source/run/status
  in `build-diagnostics.json` and `failure-analysis.md` on every outcome.
- Revalidation: pending a new exact-source capture run; no baseline captures
  from the failed run are claimed successful.


## v262 panel-before capture: run 42035364711

- Source: `bb738c3` (full SHA is retained in the run diagnostic artifact).
- Actual failure: expected visible named panel control was not found when UIA
  descendants were restricted to control type Button. All-open screenshot was
  captured, but no folded states passed.
- Correction: query accessible names across UIA roles, including checkable
  Qt tool controls; retain visible control names/types on failure for diagnosis.
- Revalidation: new-source capture pending. No failed capture is a passing test.


### v262 baseline capture revalidation succeeded

Run [42035687734](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/42035687734),
source `df109b8e14f60a7e8574b04de9c847ef3b01ce25`, completed successfully.
The exact published v261 EXE source was verified as
`c40470847d3f22fbb7c993d2c91dfae6b6b50c4c`; its EXE hash matched build-info.
Artifact `LUNA-v261-Before-Panels` retains all panel states at 1/1.25/1.5 scales,
window height changes, geometry, failure-analysis and build-diagnostics.
At 100%, expanded controls were at screen Y=159 (Library) / 153 (Papers);
both restores moved to Y=515, then Y=465 when the window was shortened by 100px.
This directly reproduces the actual native pre-fix centering defect.


## Windows Build #262 / real public PDF gate: 42034621774

- Source `f08a2515b66e18d348378016ef9f4548b7f97366`.
- First failed step: Evaluate checksum-pinned real public PDF bibliography corpus.
- Public preflight, Qt metadata, native dialog suite, MSVC compilation, portable
  checks, ordinary launch/toolbar and PDF-open benchmark passed. Subsequent
  annotation/tabs/DPI/page-heap gates were skipped, so this run is NOT a full pass.
- All 31 checksum-pinned public PDFs were acquired and processed. Actual PDF4QT
  result: 8/31 whole-record exact; title 27 correct / 3 missing / 1 wrong;
  author 13/18/0; journal/year/volume 30/1/0; pages 28/3/0; DOI 29/2/0;
  document type 29/2/0. These differ from PyMuPDF snapshot measurements.
- Confirmed wrong title: PMC10025926 emitted `zebra sh` for `zebrafish`.
  The PDF uses multi-codepoint ToUnicode `00660069` (fi); the pinned text path
  stores one QChar and drops unmapped visible glyphs. Bibliography now observes
  text sequences before layout and marks matching headline runs unreliable.
  Such titles stay review-only; old machine-owned damaged titles are archived.
  Rendering, selection and original PDF bytes are not modified. New Windows
  validation must confirm this catches the actual glyph loss.
- Confirmed DOI conflict: the two Wiley PDFs' metadata contains journal-level
  `10.1002/(ISSN)1521-3773`, competing with printed article DOI. Serial ISSN DOIs
  are now excluded from article-identity candidates; DOI normalization itself
  remains valid/general. Native Qt regression covers the actual DOI pair.
- PMC10821893-SI2 returned `unreadable_pdf`. Import/evaluation used the strict
  reader while the production viewer uses PDFDocumentReader permissive=true.
  Align read-only bibliography parsing with the viewer; do not relax metadata
  confidence or skip the fixture. Exact reader error was not emitted in #262,
  so the parser-policy hypothesis awaits Windows revalidation. Failed reads
  now retain a bounded error message with input names/paths removed.
- Original `failure-analysis.md`, `build-diagnostics.json`, raw 31 public
  predictions and score are preserved in LUNA-Build-Diagnostics / LUNA-UI-Synthetic.
  The original diagnosis said "none recorded" because the new gate lacked an
  outcome ID; add goldset/panel-capture outcomes to diagnostic provenance.
- Tests: new Qt executable regressions passed locally. Existing acceptance
  requirements (zero wrong automatic DOI/title/author, complete fixture set,
  no reader errors, explicit accuracy floors) are retained. New full Windows
  execution on the corrective SHA is required before publication.


## Windows Build #272 / capture dependency failure: 42040799852

- Source `1ae8a98c7e71770c8b044489b1a172074f668920`.
- First failed step: Capture final native panel states for direct v261 screenshot comparison.
- Actual error: Pillow 11.3.0 had no wheel for the Python 3.15.2 selected by
  install-qt; pip attempted a source build and failed (RequiredDependencyException:
  zlib / failed-wheel-build-for-install). No panel screenshots from this step
  were produced, and downstream tests were skipped.
- Correction: explicitly select Python 3.12 after install-qt for capture and
  packaging tools. This is the same interpreter ABI that successfully captured
  all real v261 baseline states. Keep pinned Pillow/pywinauto and every UI check.
- MSVC, Qt metadata/dialog suite, portable layout and ordinary launch/toolbar
  passed on this SHA. These are partial results, not full Windows acceptance.
- The failed log plus LUNA-Build-Diagnostics and LUNA-UI-Synthetic artifacts are
  retained; new full Windows execution on the corrective SHA is required.


### Actual color audit of #272 ordinary screenshots

The new stylesheet alone did not change the native navigation fill: both v261
and #272 screenshots had dominant RGB (243,246,252), despite the intended
historical (247,248,252) rule. Transparent QScrollArea content/viewport and
background drawing attributes prevented the intended scoped fill.
Explicitly paint the navigation viewport/content with the historical color and
enable styled background on document/fold widgets. No PDF/toolbar/tab selector
is changed. Exact color revalidation requires the next native screenshots;
source token changes alone are not counted as visual success.


## Windows Build #274 / remaining ligature title: 42045574469

- Source `b5354636658e7062b289f37e29406a74127c29e4`.
- Capture dependency fix verified: all four actual panel states plus height
  changes and restores were captured at 100/125/150%. Screen Y stayed
  159/153, 187/180, 214/205 respectively; native navigation RGB is now
  exactly (247,248,252). Fold borders render historical (224,228,237).
- DOI 31/31, type 30/31 (image-only cover remains unclassified); all reader
  errors resolved by the viewer-compatible read policy. Actual PDF4QT exact
  rate 16/31; title 29 correct / 1 missing / 1 wrong; author 21/10/0.
- Failure remains the same PMC10025926 `zebra sh` title. No glyph-status
  marker was emitted: observing only non-empty outlines was insufficient.
  Include missing-CID advances/content streams in the reliability check and
  add a bounded, explicitly opted-in headline/codepoint audit for this one
  checksum-pinned public fixture. Publish this small diagnostic artifact
  immediately before the full unchanged 31-PDF gate for causal inspection.
- Expanded panel children covered their parent's right stroke. Reserve its
  one-pixel inner edge; retain exact historical border color. Further actual
  screenshots must confirm expanded borders too.
- No test/fixture/accuracy threshold is removed or relaxed. Revalidate on a
  new full-source Windows run before release.


### #274 confidence propagation correction

The actual wrong `zebra sh` result already carried `titleStatus=needs_review`,
but also returned `title` with a trusted layout source. The importer therefore
confirmed a value that its own conflict detector had withheld confidence from.
On a genuine printed/embedded title conflict, return only the preferred printed
review candidate, never a trusted title/rename source. Archive old local
machine titles while preserving manual/provider fields. Add a native regression
for this exact status/field contradiction; keep every goldset threshold intact.
The bounded glyph/codepoint audit remains to document the underlying decoder
limitation; confidence containment does not claim to reconstruct missing letters.


### Native affiliation whitespace regression coverage

The native PDF4QT reconstruction inserts spaces before raised affiliation digits
and keeps comma-separated final markers on the same row; PyMuPDF snapshots may
place those digits separately. Accept whitespace before numeric markers and
commas only *inside* affiliation-marker groups, not a dangling author-list comma.
Retain the explicit terminal conjunction and preceding author-list proof. New
native assertions cover both the supported form and an incomplete-list negative.


### Font-unit consistency in public title audit

The first public title audit contained reconstructed headline lines at 13.4495pt
but no text-run samples. The observer filtered raw PDF font sizes whereas the
native layout computes size after Tm/CTM transformation (PDFTextLayout::addCharacter).
Some PDFs use font size 1 with a scaled text matrix. Use the same mapped vertical
font vector length for reliability matching and audit filtering, and retain both
raw/mapped sizes in the bounded public diagnostic. This corrects units rather
than changing confidence thresholds or any golden answer.


## Windows Build #277 / floating final-author ampersand: 42053722965

- Source `01a521ff36f6bf5505bcdb2d124deef9e1620dc7`.
- Title containment verified: 29 correct / 2 missing / 0 wrong. The public
  PMC10025926 audit returns only titleCandidate and needs_review, never title.
- DOI 31/31, publication fields unchanged, parser errors remain resolved.
- Failed goldset author guard: PMC10766651 emitted Eleonora Aronica, while the
  actual final listed author is Rob C. I. Wüst. Adding numeric marker support
  exposed a standalone/floating & in native Y-order and confirmed its wrong
  neighboring name. The other new native recovery (Bernd Bodenmiller) was correct.
- Do not infer the final author from a floating ampersand without a same-row
  named prefix. Keep word-and RSC continuations, explicitly anchored ampersands,
  and affiliation-number support. Add the incomplete-order negative regression.
  Ambiguous cases abstain rather than guessing the next personal name.
- Include only bounded author-like rows/ref markers (no contact rows) in the
  opted-in public audit; add this checksum-pinned fixture to that diagnostic.
- Original logs/predictions/score and build diagnostic artifacts are retained.
  Goldset thresholds and all original fixtures remain unchanged. A fresh full
  Windows run on the corrective SHA must pass before publication.

## Windows Build #279 / public fixture acquisition HTTP failure: 42058893645

- Source `dfcf1db39b1b97a1798f04fb2e7c97e129ea93bc`. Preflight, native Qt
  metadata/dialog tests, MSVC, portable structure, and all panel captures passed.
- The full goldset was blocked before extraction: PMC11586709 download raised
  HTTPError. No incomplete-corpus score or downstream UI success is claimed.
  Original downloader recorded the exception class but not its HTTP status;
  that historical status cannot be reconstructed from the artifact.
- Same official URL was independently fetched again: HTTP 200, 20,762,850 bytes,
  exact pinned SHA256 `0407162c598a998a28d1b22f9cf8d7b7c01819ebd4826b42dff34056b78adc48`.
- Add at most three same-URL attempts for HTTP 408/429/500/502/503/504 and
  connection/timeouts, with 1s/2s bounded backoff. Do not retry permanent HTTP
  errors, bypass access controls, change hashes, skip members, or relax scoring.
  Subsequent failures record HTTP status. Behavioral tests cover retry success,
  permanent 404 failure, and rejection of changed bytes.
- The early direct-PDF audit confirms PMC10025926 has only a review candidate
  plus unmapped-glyph status; PMC10766651 no longer confirms Eleonora Aronica.
  Its actual native stream has a standalone & before an intermediate name and
  the true final name much later. Bounded diagnostics support abstention.
- Preserve this run's LUNA-UI-Synthetic, LUNA-Public-Title-Audit and build
  diagnostics. Rerun every full Windows gate on the new exact source SHA.
