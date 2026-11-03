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
- 2 ICON LUNA-PDF.ico — new deterministic red document/file association artwork,
  generated in memory from tools/generate_pdf_icon.py during source overlay.

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
