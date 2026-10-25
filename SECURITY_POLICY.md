# LUNA Library V2 — Public build security

**Fail closed. If there is any suspicion of private data exposure, halt publication/build.**

This public repository is only for open-source PDF4QT-based UI code and a Windows build workflow. It is not a document store, personal-data service, cloud synchronization endpoint, or mirror of any private source repository.

## Never publish
- Research PDFs, scans, lab notes, paper titles from the user's library, annotations, bookmarks, thumbnails, or databases.
- Personal names, emails, phone numbers, local user paths, passwords, access tokens, API keys, cloud-login sessions, private keys, or environment files.
- Private LUNA, Recall, or unrelated Git histories, source snapshots, patch sets or files.
- Logs or screenshots capturing user documents or identifiable machine details.

## Safe build requirements
- Pin PDF4QT source to a specific public commit and check the revision.
- Run only via manual `workflow_dispatch`; never run code from outside pull requests automatically.
- Set `permissions: contents: read`. No secrets, cloud keys, authenticated sync, or push credentials.
- Source-only allowlist; reject PDFs, DBs, binary uploads and personal identifiers.
- Build only with synthetic or public test inputs; no real user files.
- Keep original PDF4QT and third-party copyright/license notices.
- Keep personal sync strictly local to the user's separately installed desktop application; do not implement GitHub as a sync service.
- Do not claim Windows binaries are tested until the actual Windows build and runtime test pass.

If an unexpected file, suspicious string or build dependency appears, **STOP** and review privately. Automatic scanning reduces risk but cannot guarantee the absence of all personal data.

## Visibility warning
GitHub username, public repository source, commit history, build logs and published artifacts may be visible publicly. Public files cannot be reliably recalled after third-party copies.
