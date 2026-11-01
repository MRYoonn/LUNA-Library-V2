"""Opt-in post-move reconciliation for local LUNA Main/SI links.

Does not move, copy, rename, delete or open PDFs, and never repairs a failed
physical move. The Issue #9 move owner must first move PDF and UUID sidecar
as one recoverable operation. A failed reconciliation requires explicit repair.
"""
from __future__ import annotations

from dataclasses import dataclass
import os

try:
    from tools.si_associations import AssociationIndex
    from tools.si_relocation_gate import apply_verified_relocation
    from tools.si_state_store import StoreError, commit, read
except ImportError:  # standalone execution with tools/ on sys.path
    from si_associations import AssociationIndex
    from si_relocation_gate import apply_verified_relocation
    from si_state_store import StoreError, commit, read


@dataclass(frozen=True)
class ReconcileResult:
    ok: bool
    code: str  # Stable diagnostic; no local path or private metadata.
    revision: str = ""


def reconcile_completed_move(
    library_root: str | os.PathLike[str], document_uuid: str,
    old_relative: str, new_relative: str, expected_revision: str,
    *, user_confirmed: bool = False,
) -> ReconcileResult:
    """Persist a *verified completed* PDF+sidecar move using optimistic CAS.

    expected_revision MUST be captured before the physical move. This routine
    never performs a move and cannot roll one back. It leaves the original
    association state unchanged on failed audit, stale revision or failed save.
    Callers must block further linked moves and request explicit recovery on
    failure, rather than silently treating the new path as linked.
    """
    if not user_confirmed:
        return ReconcileResult(False, "confirmation_required")
    if not isinstance(expected_revision, str) or not expected_revision:
        return ReconcileResult(False, "invalid_revision")
    try:
        snapshot = read(library_root)
    except StoreError as exc:
        return ReconcileResult(False, str(exc))
    if snapshot.revision != expected_revision:
        return ReconcileResult(False, "stale_revision")
    # A private copy avoids mutating a snapshot exposed to other callers.
    working = AssociationIndex.from_json(snapshot.index.to_json())
    audit = apply_verified_relocation(
        working, library_root, document_uuid, old_relative, new_relative
    )
    if not audit.ok:
        return ReconcileResult(False, audit.code)
    try:
        saved = commit(library_root, working, snapshot.revision, user_confirmed=True)
    except StoreError as exc:
        return ReconcileResult(False, str(exc))
    return ReconcileResult(True, "reconciled", saved.revision)
