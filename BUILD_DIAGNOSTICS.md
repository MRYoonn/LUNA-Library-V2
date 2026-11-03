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
