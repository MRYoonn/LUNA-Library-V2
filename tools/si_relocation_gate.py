"""Read-only post-move safety gate for opt-in LUNA Main/SI associations.

Issue #9 owns physical PDF/sidecar movement. This module never moves files,
creates IDs, writes a state store, or guesses a link from a filename.
Only after a successful paired move can the in-memory index be relocated.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import stat
import unicodedata
from uuid import UUID

try:
    from tools.si_associations import AssociationIndex, document_id, relative_pdf_path
except ImportError:  # standalone execution with tools/ on sys.path
    from si_associations import AssociationIndex, document_id, relative_pdf_path

MAX_SIDECAR_BYTES = 65536


@dataclass(frozen=True)
class MoveAudit:
    ok: bool
    code: str  # No absolute paths or local metadata are returned.


def _safe_parts(root: Path, relative: str) -> Path | None:
    """Reject symlinked components, including a symlinked destination leaf."""
    current = root
    for part in relative.split("/"):
        current = current / part
        try:
            mode = current.lstat().st_mode
        except FileNotFoundError:
            continue
        except OSError:
            return None
        if stat.S_ISLNK(mode):
            return None
    return current


def _regular(path: Path) -> bool:
    try:
        return stat.S_ISREG(path.lstat().st_mode)
    except OSError:
        return False


def _exists(path: Path) -> bool:
    # lexists catches dangling symlinks too; a stale alias must not be ignored.
    return os.path.lexists(path)


def audit_paired_move(root: str | Path, old_relative: str,
                      new_relative: str, expected_id: str) -> MoveAudit:
    """Verify an already-finished move; no writes and no implicit repair.

    Rejects stranded old sidecars, duplicated PDFs, missing/corrupt destination
    identities, symlinked paths and stale/ambiguous moves. A failed audit must
    leave the caller's association index unchanged.
    """
    try:
        old = relative_pdf_path(old_relative)
        new = relative_pdf_path(new_relative)
        expected = document_id(expected_id)
        if UUID(expected).version != 4:
            return MoveAudit(False, "invalid_identity")
    except (ValueError, TypeError):
        return MoveAudit(False, "invalid_input")
    # Windows/UI path identities are NFKC + casefold, not plain casefold.
    # A compatibility-only spelling change is not proof of a physical move;
    # accepting it can rebind a saved SI to a pre-existing destination.
    if unicodedata.normalize("NFKC", old).casefold() == unicodedata.normalize("NFKC", new).casefold():
        return MoveAudit(False, "ambiguous_move")
    base = Path(root)
    try:
        if not stat.S_ISDIR(base.lstat().st_mode) or base.is_symlink():
            return MoveAudit(False, "unsafe_root")
    except OSError:
        return MoveAudit(False, "unsafe_root")
    old_pdf = _safe_parts(base, old)
    new_pdf = _safe_parts(base, new)
    if old_pdf is None or new_pdf is None:
        return MoveAudit(False, "unsafe_path")
    old_meta = old_pdf.with_name(old_pdf.stem + ".luna-meta.json")
    new_meta = new_pdf.with_name(new_pdf.stem + ".luna-meta.json")
    if _safe_parts(base, str(old_meta.relative_to(base)).replace(os.sep, "/")) is None or \
       _safe_parts(base, str(new_meta.relative_to(base)).replace(os.sep, "/")) is None:
        return MoveAudit(False, "unsafe_sidecar")
    if _exists(old_pdf):
        return MoveAudit(False, "old_pdf_still_present")
    if _exists(old_meta):
        return MoveAudit(False, "sidecar_stranded")
    if not _regular(new_pdf):
        return MoveAudit(False, "destination_pdf_missing")
    if not _regular(new_meta):
        return MoveAudit(False, "destination_identity_missing")
    try:
        if new_meta.stat().st_size > MAX_SIDECAR_BYTES:
            return MoveAudit(False, "unsafe_sidecar")
        with new_meta.open("rb") as stream:
            payload = stream.read(MAX_SIDECAR_BYTES + 1)
        if len(payload) > MAX_SIDECAR_BYTES:
            return MoveAudit(False, "unsafe_sidecar")
        metadata = json.loads(payload)
    except (OSError, UnicodeError, ValueError):
        return MoveAudit(False, "invalid_sidecar")
    if not isinstance(metadata, dict) or metadata.get("schemaVersion", 1) != 1 or \
       type(metadata.get("schemaVersion", 1)) is not int:
        return MoveAudit(False, "unsupported_sidecar")
    stored = metadata.get("documentId")
    try:
        if document_id(stored) != expected or UUID(stored).version != 4:
            return MoveAudit(False, "identity_mismatch")
    except (ValueError, TypeError, AttributeError):
        return MoveAudit(False, "identity_mismatch")
    return MoveAudit(True, "verified_paired_move")


def apply_verified_relocation(index: AssociationIndex, root: str | Path,
                              id: str, expected_old: str, new_path: str) -> MoveAudit:
    """Update only the in-memory path after a verified physical paired move."""
    try:
        canonical_id = document_id(id)
        document = index.documents.get(canonical_id)
        if document is None or document.relative_path != expected_old:
            return MoveAudit(False, "stale_move")
        audit = audit_paired_move(root, expected_old, new_path, canonical_id)
        if not audit.ok:
            return audit
        index.relocate(canonical_id, expected_old, new_path)
    except (ValueError, TypeError):
        return MoveAudit(False, "index_conflict")
    return audit
