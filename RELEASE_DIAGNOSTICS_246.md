# LUNA v0.9.0.246 release operations and root-cause log

Date: 2026-11-03 (KST).

## Immutable candidate
- Windows source run: [#246 / 41592183528](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41592183528)
- Source commit: `24e00a79fad0aecf3286695d94800f24862b8501`
- Candidate branch: `feature/bibliography-scheduled-module-integration-20261103` (Draft PR #31, built on #22, #19 and #16)
- Source ZIP: `LUNA-Windows-Portable` from #246, not rebuilt by release orchestration.
- Source LUNA.exe SHA-256: `f4899afd7e32a1d88624d50373541b68d39b182cc75f479f50925711b6f75a42`.
- Next tag: `v0.9.0.246`. Earlier public latest: `v0.9.0.244`.
- GitHub main is behind stacked Draft feature PRs; DO NOT merge main or mark all Draft modules production-ready as a side effect of release packaging.

## Verified before this release operation
- #246 GitHub Windows preflight and build-windows jobs both completed/success.
- The original 136-member Actions archive passed ZIP CRC and contained EXE bytes matching build-info.json.
- PDF4QT/MSVC, 162 synthetic Python and 8 Qt native tests, GUI/tab/annotation/restart/layout/DPI/CDB gates completed in #246.
- Optional actual publisher PDF and long-duration physical PC tests remain unverified; `betaComplete` is still `false` and must not be altered to imply otherwise.

## Release safety requirements
- Checkout exact source SHA and require source run #246 completed success, branch + SHA match, and both required Windows jobs success.
- Run exact-source public preflight and Python regression tests.
- Use the audited source-branch `tools.prepare_release` to form semantically tagged package without changing EXE bytes. The published v0.9.0.244 updater rejects the standalone `Luna-Crash-Tools.ps1`; package must omit that script and embed the approved helper functions in the two existing diagnostic scripts. Do not loosen the updater allowlist.
- Verify ZIP CRC, manifest file sizes and hashes, EXE SHA, build source identity, release tag, and no user PDFs/data. Stage and confirm the draft's exact two GitHub assets before publication.
- Test the new release on GitHub-hosted Windows by updating from the actual public v0.9.0.244 ZIP with synthetic sentinel PDF/thumbnail/metadata. Check download, stage, replace, healthy restart, no repeat offers, and data preservation.
- On live-update failure, block or reverse latest-feed eligibility of v0.9.0.246 and preserve failed run / artifact / root-cause evidence.
- Release workflow must not modify users' real PDF files, QSettings, or existing assets.

## Root causes, fixes, and new outcome
- Previous v0.9.0.243 -> v0.9.0.244 package compatibility issue: published updater rejected `Luna-Crash-Tools.ps1`; fixed by inlining its bounded functions in approved diagnostic scripts in the audited packager (see source tests and the preceding release history). No previous published assets overwritten.
- This release: pending exact workflow result; consult one-shot workflow run logs, `LUNA-v090246-Release-Diagnostics` and `LUNA-v090246-Live-Update-Diagnostics` artifacts before marking public update validated.
