# Approved LUNA Folder Icons v1.0 (2026-10-31)

This directory contains the **user-supplied** LUNA Folder Icons v1.0 artwork, losslessly packed in `svg_bundle.b85` for the text-only GitHub content workflow. Do not replace it with generated approximations.

- `svg_bundle.b85`: Base85(zlib(UTF-8 JSON)). The JSON maps the original 20 individual SVG filenames plus `importance_overlay.svg` to their original XML text.
- Decoded JSON SHA-256: `37244af73e35e0925d2347a9dc7657ba3bd3a52d87b2d7dc1d300efe99496c8a`
- `tools/apply_overlay.py`: checksum-validates and unpacks all SVG assets to the pinned PDF4QT build directory, then generates `luna_folder_icons.qrc`.
- The LUNA C++ folder picker identifies icons by stable English `iconId`. It never writes machine-specific icon file paths to metadata.
- `LUNA.folder.json` resides *inside* the user-selected folder so it can follow folder moves and MYBOX folder sync. **No MYBOX API, credential or automatic sync activation is implemented.**
- The previous `LUNA.folder-icon.png` files, if any, are intentionally not deleted. They are not used by the new built-in palette.
- Any new pack version requires a new source checksum, UI regression verification, Windows compile, and explicit user approval.

The approved v1.0 visual IDs, in order:
`general, important, favorite, research, project, review, mechanism, synthesis, spectroscopy, computational, data, reference, ideas, drafting, completed, shared, external, archive, private, temporary`.

Note: The important/favorite glyph types are independent of data fields `isImportant` and other future folder classifications.
