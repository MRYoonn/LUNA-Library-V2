# LUNA Library v0.9.0.258 — Verified PDF association and moon tab release

## Published immutable identity
- Official release: https://github.com/MRYoonn/LUNA-Library-V2/releases/tag/v0.9.0.258
- Published stable, not draft and not prerelease; release ID 433295260.
- Original exact program source: d606afa9631d6bdf02500a3fdfc173a6d0de0264 (feature/pdf-association-moon-tabs-20261104, PR #39).
- Full PDF4QT/Qt6 Windows build #258: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41725373320 — completed/success.
- Exact-source icon/Windows registry/Qt tab tests: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41725321110 — completed/success.
- Independent release ZIP, original EXE and manifest Windows audit: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41726066633 — completed/success.
- Public publish and **real old v0.9.0.256 updater** test: https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41763708958 — completed/success. Published=success, live-update=success, rollback/demotion skipped.
- LUNA-Windows-Portable.zip: 51,813,532 bytes, SHA-256 b712017b8e79a2fcfded394bf8fdcd5ac490a4f4cffa54d777ca99f03ce6f06b.
- LUNA-Release.json: 21,539 bytes, SHA-256 ada51f21dcae2bcc95e0bc90cc3fdb5ad9d856e46dce7d7fa4671f38735c664f.
- Source Windows CI EXE was not rebuilt or changed by release packaging; the deterministic manifest binds every file by SHA-256.

## User requirements delivered
- **App icon**: unchanged original BLUE LUNA icon in first Win32 EXE icon group.
- **Windows PDF file association**: separate RED crescent, folded page and PDF lettering, icon group **2 inside LUNA.exe**. ProgID DefaultIcon uses quoted absolute LUNA.exe,-2. No separate .ico file in ZIP, because original publicly installed v256 updater blocks unfamiliar .ico payload extensions. The existing portable/auto-update compatibility is retained.
- **PDF tabs**: QPainter anti-aliased hollow blue moon for current active tab and low-contrast grey moon for inactive tabs, with update after tab switching, editor creation and tab order changes.
- **File association**: Windows per-user opt-in action registers only LUNA's own ProgID, OpenWithProgids and RegisteredApplications/Capabilities, offers Settings > Default Apps; never writes UserChoice, overrides another PDF handler, or needs Administrator rights.
- **Compatibility**: prior manual SI attachment and folder grouping features from v256 stay present. Neither experimental inline left pane folding PR #35 nor automated bibliography SI PR #37 is included.

## Tests and safety
- Windows source and C++ full build: PASS, preserving all existing PDF4QT/annotation/native tabs/Windows GUI smoke checks.
- Windows native file association test: PASS in isolated synthetic HKCU test root. No change to actual default PDF app.
- Actual packaged LUNA.exe Win32 resources: group 1 original blue app icon, group 2 seven-size red PDF document icon. No .ico sidecar. PASS.
- Windows native tab tests verified active and inactive QIcon-rendered moon differences and tab switching. PASS.
- Independent candidate: ZIP CRC, SHA-256 of every file and EXE provenance; Windows PowerShell parser and updater regression. PASS.
- Live upgrade from **published v256**, using **v256's actual LUNA-Update.ps1**, performed Check => verified HTTPS Stage => Apply => healthy native LUNA restart; repeat Check did not re-offer v258. Synthetic user PDF, annotation, SI, hidden and legacy sidecar and thumbnail hashes preserved. PASS.
- Live-update failure safety job would remove v258 from latest stability; it correctly SKIPPED on success. No personal research PDF was uploaded or read by CI.

## Operational notes and limitations
- Existing v256 users: save open PDF edits, then use LUNA menu '업데이트 확인…' to update to v258.
- New users: download the **entire** LUNA-Windows-Portable.zip and extract before launching LUNA.exe.
- To make LUNA the PDF default, choose the app's 'PDF 기본 앱으로 설정…' action, which registers LUNA and opens Windows Settings. Windows decides the user's final .pdf default; this is not silently forced. Portable app folder moves require re-registering the association so Explorer finds the correct EXE/icon.
- Windows icon caching can delay Explorer refresh after a default-app change; avoid deleting/reinstalling the PDF or app to force refresh.
- Real MYBOX cross-PC hidden folder sync, arbitrary third-party PC icon cache behavior and long research sessions remain unverified outside synthetic Windows CI. betaComplete remains false.
- Full root-cause notes on first failed icon CI and Win32 shlobj.h compiler repair are in PDF_ICON_DIAGNOSTICS.md at the original source SHA, not overwritten by a second unvalidated branch.
