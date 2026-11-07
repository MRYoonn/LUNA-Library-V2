# LUNA PDF association icon / hollow moon tab — 2026-11-04

## Requirement

Keep the original **BLUE LUNA.exe** icon. A Windows Explorer document whose
.pdf default handler the user changes to LUNA should use a separately designed
**RED PDF document with folded page and crescent**. PDF tabs use a thin, EMPTY
blue crescent for the active tab and muted grey crescent for inactive tabs;
document titles and active blue underline are preserved.

## Critical updater compatibility finding

The already published v0.9.0.256 LUNA-Update.ps1 (installed on existing users'
PCs) blocks any new '.ico' *standalone ZIP payload*. Thus shipping
LUNA-PDF.ico beside LUNA.exe breaks a production auto-update, even if tests on
a new PC happen to pass. Fix: embed TWO separate icon groups into the EXE
Windows RC resource, with stable numeric IDs:

- 1 ICON LUNA-Moon.ico — exact unchanged approved app blue logo.
- 2 ICON LUNA-PDF.ico — **the exact user-approved original** in
  `assets/icons/LUNA-PDF-approved.ico` (SHA-256
  `7f89df11a934690f3a6bae8a9b014efdd9f73a3607f3b699e60b44b185349f0f`).
  The overlay checks these original bytes and embeds this asset, not a regenerated image.

The red ICO is an **intermediate build resource only**, not included as an
independent ZIP file. Windows ProgID DefaultIcon references
quoted-absolute-LUNA.exe,-2. This is a documented Win32 negative resource ID,
not a generic index. GitHub Windows workflow inspects packaged LUNA.exe group
resources 1 and 2 and rejects an external ICO. Old updater extension allowlist
and executable filename remain unchanged.

## Windows default program contract

Menu File (fallback Help) > PDF 기본 앱으로 설정… explicitly registers
HKCU Classes/LUNA.Library.PDF, shell open command with executable and one
quoted %1 path, per-user Applications/LUNA.exe, .pdf/OpenWithProgids and
RegisteredApplications/Capabilities. It does not set an HKCU UserChoice key
or override Windows' user's default app selection, and needs no admin rights.
The UI opens Windows Settings > Default Apps so the user can select LUNA
Library for .pdf. A portable folder moved after registration must be
re-registered. No registry mutation occurs at startup or during automatic
update.

## Windows UI contract

A QPainter-drawn QIcon renders a single continuous anti-aliased hollow
crescent, no document silhouette. Active color #3473AE with dark blue text;
inactive #9AA5B3 and muted text. Tab icon/text shades recompute after
currentChanged, tabMoved and editor creation. Existing tab icon bounds,
independent PDF state, close controls, active underline, app taskbar icon and
library icons are unchanged.

## Tests and release gate

- Python source tests verify multi-resolution 16/24/32/48/64/128/256
  PNG-backed ICO format, resource identities and native UI/registry contracts.
- Native Qt Windows CTest performs real isolated **HKCU test-root** writes,
  verifies ProgID, DefaultIcon -2, quoted launch command, OpenWithProgids and
  RegisteredApplications with NO UserChoice; removes only its own fixture.
- Full native Windows PDF4QT/MSVC build must pass all old regression suites
  and Qt GUI 2/5/10-document tab switching with active blue/inactive grey
  actual icon pixel distinction.
- Full Windows packaging checks the actual embedded group IDs and no
  standalone .ico; ZIP manifest compatibility and prior v256 updater
  Check -> Stage -> Apply -> restart must succeed before public stable release.
- Failures must be diagnosed from exact first-failed step, compile log, CTest
  output and source SHA; never claim physical Windows user acceptance from
  synthetic test alone.

## First Windows CI compiler failure — resolved in subsequent source revision

[CI #41724677280](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41724677280) passed its Python/privacy gate, but the independent native HKCU CTest failed at the C++ compile step, **before any registry mutation**. MSVC error C2065 for SHCNE_ASSOCCHANGED / SHCNF_IDLIST and C3861 SHChangeNotify. Root cause: shellapi.h does not declare the SHChangeNotify API family; shlobj.h is the appropriate Win32 header. Added shlobj.h to src/lunapdfassociation.h and retained full CTest. The first failure was not due to user PDFs, icon artwork, registry permissions or a live default-app change. Older in-flight full Windows Build #257 was dispatched at the prior SHA and is **not valid acceptance evidence** for this fix; new final-source CI is required.
