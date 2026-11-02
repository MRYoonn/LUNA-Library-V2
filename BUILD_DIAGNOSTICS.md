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
