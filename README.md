# LUNA Library V2

PDF4QT 기반 Windows 데스크톱 PDF 라이브러리 — LUNA Library.

This **public repository is for a source-only Windows build** of the two-column LUNA UI using the open-source PDF4QT viewer. **No private LUNA or Recall source, user PDF, research data, cloud credentials, notes, databases, or local machine settings belong here.**

## Planned layout

- Left: **폴더 / PDF** switch, folder navigation, PDF document list, filename search.
- Right: upstream **PDF4QT** document renderer, text selection, navigation, search.
- Product name: **LUNA Library**; executable: **LUNA.exe**.
- Research lab ↔ home file sync is **separate and local-only**; GitHub is not a personal file sync service.

## Source licensing

Original PDF4QT by Jakub Melka and contributors, MIT licensed since 2025. Third-party notices must be preserved in redistributed binaries.

## Security gate

**STOP PUBLICATION** whenever a secret, personal identifier, PDF, annotation, local path or private project material is suspected. No CI action may have secrets or write permissions, and no user research data can be uploaded.

See SECURITY_POLICY.md when available.

## Current status

Public repository created. Native Windows compilation and PDF4QT-based LUNA.exe have **not yet been verified**.

[PDF4QT upstream](https://github.com/JakubMelka/PDF4QT)
