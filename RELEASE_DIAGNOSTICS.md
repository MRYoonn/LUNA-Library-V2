# LUNA Release Bootstrap — Error Analysis and Verification (2026-11-03)

## Immutable source and release identity

- Approved Windows CI: Build #243, run `41577602717` ([logs](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41577602717)).
- Source commit: `0550c5a2b57e4704c6e1a255a3abfe0094880dbe`; underlying feature branch remains unchanged.
- Release: `v0.9.0.243`; GitHub Release ID `432310440`; draft creation workflow [run #1](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41584017047).
- Release source is a CI-validated **development preview** (`betaComplete=false`). A Windows CI pass is not equivalent to long-term user-PC update acceptance.

## Workflow #1 post-create verification failure

- Result: `gh release create v0.9.0.243 --draft` **succeeded** at 2026-11-03 05:27:51 UTC; it returned `releases/tag/untagged-e13f7628a0458271f245`.
- The immediately following call `gh api repos/MRYoonn/LUNA-Library-V2/releases/tags/v0.9.0.243` returned HTTP **404 Not Found**, causing the workflow job to report failure.
- Root cause: GitHub assigns an **untagged draft URL**, and the draft release is not resolvable through the standard release-by-tag REST endpoint before publication. This is a **verification-method error**, not a release-create failure; blindly retrying `gh release create` would duplicate or conflict with the already-created draft.
- Correction: read the existing draft by **numeric release ID `432310440`**, verify `tag_name`, `target_commitish`, `draft=true`, and exact asset identifiers/digests, then publish that same immutable ID. Never recreate the draft or replace assets when the metadata matches.

## Independent release payload verification

The original Build #243 artifact was re-expanded and deterministically repackaged with a `v0.9.0.243` versioned `build-info.json`, exactly following `tools/prepare_release.py`. All 136 members were opened, and every file size and digest matched the generated manifest. These SHA-256 values match the asset digests reported by GitHub for Release `432310440`:

- `LUNA-Windows-Portable.zip`: **51,762,009 bytes**, SHA-256 `d145ac054f985633b1ddc09bc8b0418545079d3af629649dfef384dfc9cee646`.
- `LUNA-Release.json`: **21,693 bytes**, SHA-256 `87eb18220ab89ca1f1f0716b769c19bb403c8608a30715140bbc75f1f4997e2a`.
- Release manifest `tag=v0.9.0.243`, `source_run=41577602717`, `commit=0550c5a2b57e4704c6e1a255a3abfe0094880dbe`. All 136 entries match the exact versioned ZIP content.

## Activation and real-world limitations

Publishing this existing draft as a **non-prerelease/latest** release will make it visible to `https://api.github.com/repos/MRYoonn/LUNA-Library-V2/releases/latest`. A release manifest and ZIP are required by `LUNA-Update.ps1`. Developer Build #243 has the same source run as this first release, so the updater correctly reports **no newer update** for that exact build; an older build or future >243 release is needed to demonstrate a positive-update prompt. Real user-PC restart/rollback remains unverified.
