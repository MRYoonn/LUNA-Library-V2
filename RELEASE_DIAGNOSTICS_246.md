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


## Final immutable release and live-update verification (2026-11-03)

- [Prepublish release workflow #41618847752](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41618847752): **completed/success**, commit `62d30db9ee029520b54deea000e9de2bc5c51fb4` (release-orchestration only). Pinned original source `24e00a79fad0aecf3286695d94800f24862b8501`, successful Windows #246, public source gate, Python tests, ZIP-manifest tests, and independent Windows PowerShell parsing/digest validation.
- [Public publish + Windows live update workflow #41619230169](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41619230169): **completed/success**. Publish job success; live-update job success; failure rollback job correctly skipped.
- Released [v0.9.0.246](https://github.com/MRYoonn/LUNA-Library-V2/releases/tag/v0.9.0.246), existing draft numeric ID `432545850` published without replacing earlier release assets. `releases/latest` now reports `v0.9.0.246`, `draft=false`, `prerelease=false`, target commit exact original #246 source SHA.
- Official ZIP: 51,771,757 bytes, SHA-256 `02c8ea4d65d77b0496d11774e873992f85147ddbe4ea047fac89d5a7d0406af4`, 135 files. Versioned `LUNA-Release.json`: 21,539 bytes, SHA-256 `227e0006ee8a9ed83e5fe9fc4881ec880e076fc5963287c4206b56167b137ae5`. Original Windows EXE SHA-256 unchanged: `f4899afd7e32a1d88624d50373541b68d39b182cc75f479f50925711b6f75a42`.
- Real Windows runner used the **already published v0.9.0.244 portable package and its unmodified old updater**, called public `Check`, downloaded and staged new release over HTTPS with manifest and every-file hashes, applied update, and received healthy GUI restart acknowledgement. It checked exact version/run/commit/EXE digest, preserved synthetic sentinel PDF/thumbnail/metadata files, then rechecked feed without a repeat update offer. Independent `LUNA-v090246-Live-Update-Diagnostics` artifact reports all six checks `passed`, `status=passed`, `error=""`.
- Release diagnosis artifacts `LUNA-v090246-Package-Diagnostics`, `LUNA-v090246-Windows-Prepublish-Diagnostics`, `LUNA-v090246-Draft-Diagnostics`, `LUNA-v090246-Publish-Diagnostics`, `LUNA-v090246-Live-Update-Diagnostics` were retained by CI for 90 days.
- No new release-blocking failure occurred in the two release workflows. Prior v0.9.0.243 updater package incompatibility was mitigated by the previously audited helper-inlining change; the real public v0.9.0.244 → v0.9.0.246 upgrade succeeded.
- **Remaining limitations:** user physical PC check, long sessions, actual publisher PDF precision/recall goldset, main branch merging, and `betaComplete=true` declaration have not occurred. Public release success is distinct from closing those product beta acceptance tasks.

## Root causes, fixes, and new outcome
- Previous v0.9.0.243 -> v0.9.0.244 package compatibility issue: published updater rejected `Luna-Crash-Tools.ps1`; fixed by inlining its bounded functions in approved diagnostic scripts in the audited packager (see source tests and the preceding release history). No previous published assets overwritten.
- This release: pending exact workflow result; consult one-shot workflow run logs, `LUNA-v090246-Release-Diagnostics` and `LUNA-v090246-Live-Update-Diagnostics` artifacts before marking public update validated.
