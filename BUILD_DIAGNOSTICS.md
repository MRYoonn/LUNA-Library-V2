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

**Current branch limitation:** These guarantees become effective for builds
using the commit containing this workflow. Earlier historical builds do not
retroactively acquire these diagnostics. No workflow is triggered automatically:
the repository remains manual-only, and draft PRs are not merged automatically.
