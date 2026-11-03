# LUNA Library v0.9.0.256 — Release diagnostics

## Immutable release identity

- GitHub release ID: 433220985; stable v0.9.0.256.
- Public release: https://github.com/MRYoonn/LUNA-Library-V2/releases/tag/v0.9.0.256
- App source commit: 1862adee8802bbf1042647aafa48303f1b6a774f
- App source branch: feature/manual-si-attach-card-grouping-20261104; Draft PR #38 was not merged into main.
- Original full Windows PDF4QT/Qt build #256: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41710621652 — COMPLETED/SUCCESS.
- Independent manual SI Python/privacy and Qt native CTest: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41710542110 — COMPLETED/SUCCESS.
- Deterministic candidate release, Windows offline manifest and updater syntax regression: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41719187642 — COMPLETED/SUCCESS.
- Publish and actual v0.9.0.249-to-v0.9.0.256 Windows updater workflow: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41719595886 — COMPLETED/SUCCESS; publish and live-update both PASS, safeguard demotion correctly SKIPPED.
- LUNA-Windows-Portable.zip: 51,795,858 bytes, SHA-256 600fdc622d5b180efbd5138bc9a4a764f0dbb7d780bb8ceefb539fe722109f7f.
- LUNA-Release.json: 21,539 bytes, SHA-256 e1a6151a9aeed91f173b3e9f37be2204e1389b712ba55da025edf6718d87a9a7.
- Original successful Windows executable is byte-identical to the source CI binary; the release wrapper changes only deterministic package layout and build-info.json version. Manifest schema 2 validates every package member's SHA-256, original EXE digest and source SHA.
- betaComplete stays FALSE; no physical user-PC beta acceptance is implied.

## Scope delivered

1. User right-clicks the original article and chooses SI 추가…, then manually selects a PDF in the native Explorer file chooser.
2. SI already in the original's own folder is reused; if the sanitized SI filename is not already correct, that exact PDF is renamed without making a duplicate. The existing LUNA Explorer-rename reconciliation preserves portable sidecar bibliography, cropped thumbnails, manually written notes/memos, favorites, recent references and file identity. An open SI reader is protected from filename changes.
3. SI from another folder is copied into the article folder; the source is never moved or deleted. Collision-proof numbered names are generated without overwriting PDFs or legacy/hidden metadata.
4. In card/list views, only originals with manually attached SI have a foldable Supporting Information (N) footer; children are compact independent PDF rows. SI PDF content, annotation and thumbnail ownership remain separate.
5. A manual unlink action removes only the association; the SI PDF remains. Orphans and unmatched SI stay visible as standalone items.
6. This release deliberately does NOT include the separately drafted automatic SI bibliography PR #37 or the separate inline left-panel fold PR #35. It is based on the stable hidden-sidecar v249 application source.

## Audited tests

- Build #256: public source/privacy gate and all source Python suites; native Qt dialog and metadata tests; full PDF4QT/MSVC build; genuine Windows Qt reader UI, annotation round trips, 2/5/10 document windows, multi-tab isolation and memo restart tests; DPI 100/125/150% synthetic checks; native Ctrl+W and CDB crash regression; Portable package layout; deterministic release ZIP verification. All succeeded.
- Native SI tests: actual Windows filesystem verifies same-folder rename with unchanged count of PDFs, renamed PDF with same bytes, idempotent re-add, old/new hidden metadata and cropped thumbnails, user-edited bibliography and memo retained, outside-folder original not touched, no-overwrite collisions, unlink preserving PDF. Successful independent CI #41710542110.
- Draft prepublish: exact original Build #256 SHA, run and artifact pinned; independent Windows PowerShell parsing of original updater, member-by-member SHA-256, ZIP integrity and offline updater regression, with immutable draft release and exact uploaded asset digests.
- Live test: actual released v0.9.0.249 ZIP and its original LUNA-Update.ps1 were downloaded on GitHub Windows. New public latest version discovered via Check, downloaded with HTTPS Stage and hash checks, installed with Apply and acknowledged by a healthy restarted LUNA application. Manifest and executable identities verified against the originally tested source. Second Check did not re-offer the same update. Synthetic PDF, old visible sidecars, hidden .luna/metadata, hidden .luna/thumbnails, manual SI PDF/metadata and annotation file digests were unchanged across update.
- Publish recovery: if live upgrade had failed, a separate GitHub job would demote v256 to prerelease. It correctly skipped because both publish and live tests succeeded.

## Constraints and diagnostics

- Do not claim this synthetic CI proves arbitrary real-paper bibliographic accuracy, all third-party publisher SI formats, real MYBOX sync across two physical computers, or weeks-long user work. These are unverified.
- If only the original PDF is moved to a different folder, the SI is deliberately not silently moved; the orphan SI stays visible. Moving both files in the same folder retains their relative relation.
- Production stable user workflow: in an installed v249 application click 업데이트 확인… and explicitly approve the update, or download the whole Windows Portable ZIP, unpack all files and run LUNA.exe. Save all PDFs first and back up research folders containing hidden .luna directories. Do not manually delete/merge old/new SI sidecars.
- Error analysis is permanently retained in BUILD_DIAGNOSTICS.md on the source branch. Source CI run artifacts, audit candidate artifacts and publish/live upgrade artifacts preserve their independent diagnostics. Any new failure must be linked to its exact step/log; no assumptions about causes.
