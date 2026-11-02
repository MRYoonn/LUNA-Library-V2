# LUNA General Symbol Icons — original user-supplied pack v1.0

The 40 SVG colour symbols are copied **losslessly** from
`LUNA_Symbol_Icons_40_v1.0_Manager_Package.zip`. They are *not* emoji or
re-rendered approximations. The GitHub-safe ASCII transport uses four ordered
`svg_bundle.part*.b85` segments: Base85(zlib(JSON mapping stable symbol name
to original SVG XML)). The Windows build must verify the uncompressed JSON
SHA-256 before unpacking.

- SHA-256 of canonical UTF-8 JSON:
  `025b271e0c08746f1edac3fffa6d07fcf68fd91438b1caefe69bc519ecfa9e73`
- 40 symbols: `symbol:star` through `symbol:rocket`; original 20 folder
  SVGs remain separate, unchanged.
- Build: `tools/apply_overlay.py` expands the SVG resources to the pinned
  upstream build tree and registers `luna_symbol_icons.qrc`.
- Application: only stable IDs are stored as `iconId` in
  `LUNA.folder.json`; never store machine-specific paths.
- Unknown or corrupt IDs fail closed to the ordinary folder icon. Existing
  folder metadata, importance, and favorites remain untouched.

**Attribution:** original SVG silhouettes from Font Awesome Free 6.7.2,
Copyright Fonticons, Inc.; distributed under CC BY 4.0
(https://creativecommons.org/licenses/by/4.0/). The source package modifies
colours, gradients, highlights and shadow effects. No font files are included.
See `LICENSE_NOTICE.md`.
