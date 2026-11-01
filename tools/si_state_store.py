"""Opt-in, local-only crash-safe persistence for the existing SI AssociationIndex.

This module does not discover SI, move PDFs, change QSettings or create links.
The caller must obtain user confirmation and use AssociationIndex.confirm_link.
Only a fully validated v1 AssociationIndex is committed; stale revisions fail.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import stat
import tempfile

try:
    from tools.si_associations import AssociationIndex
except ImportError:  # standalone execution with tools/ on sys.path
    from si_associations import AssociationIndex

STATE_NAME = ".luna-si-associations.json"
LOCK_NAME = ".luna-si-associations.lock"
MAX_BYTES = 8_000_000
MISSING_REVISION = "missing"


class StoreError(ValueError):
    """A stable, non-sensitive reason code; never contains a local path."""


@dataclass(frozen=True)
class Snapshot:
    index: AssociationIndex
    revision: str


def _root(folder: str | os.PathLike[str]) -> Path:
    root = Path(folder)
    if root.is_symlink() or not root.is_dir():
        raise StoreError("unsafe_library_root")
    return root.resolve(strict=True)


def _read(root: Path) -> Snapshot:
    target = root / STATE_NAME
    try:
        info = target.lstat()
    except FileNotFoundError:
        return Snapshot(AssociationIndex(), MISSING_REVISION)
    if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_BYTES:
        raise StoreError("unsafe_state_file")
    # O_NOFOLLOW prevents symlink substitution on POSIX; lstat is a second gate.
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        fd = os.open(target, flags)
        with os.fdopen(fd, "rb") as stream:
            opened = os.fstat(stream.fileno())
            if not stat.S_ISREG(opened.st_mode) or opened.st_size > MAX_BYTES:
                raise StoreError("unsafe_state_file")
            data = stream.read(MAX_BYTES + 1)
    except OSError as exc:
        raise StoreError("state_unreadable") from exc
    if len(data) > MAX_BYTES:
        raise StoreError("unsafe_state_file")
    try:
        index = AssociationIndex.from_json(data.decode("utf-8"))
    except (UnicodeError, ValueError, TypeError) as exc:
        raise StoreError("invalid_state") from exc
    return Snapshot(index, hashlib.sha256(data).hexdigest())


def read(folder: str | os.PathLike[str]) -> Snapshot:
    """Read without creating any state, lock or directory."""
    return _read(_root(folder))


def commit(folder: str | os.PathLike[str], index: AssociationIndex,
           expected_revision: str, *, user_confirmed: bool = False) -> Snapshot:
    """Atomic compare-and-swap. The caller must opt in after user confirmation.

    The lock is exclusive across cooperating processes. Stale lock removal is
    deliberately manual; an unexpected crash must not enable silent writes.
    """
    if not user_confirmed:
        raise StoreError("confirmation_required")
    if not isinstance(index, AssociationIndex):
        raise StoreError("invalid_index")
    if not isinstance(expected_revision, str) or not expected_revision:
        raise StoreError("invalid_revision")
    data = index.to_json().encode("utf-8")
    if len(data) > MAX_BYTES:
        raise StoreError("state_too_large")
    # Reject corrupted/forged in-memory state before touching the filesystem.
    try:
        AssociationIndex.from_json(data.decode("utf-8"))
    except (ValueError, TypeError) as exc:
        raise StoreError("invalid_index") from exc
    root = _root(folder)
    lock = root / LOCK_NAME
    try:
        fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError as exc:
        raise StoreError("store_busy") from exc
    except OSError as exc:
        raise StoreError("lock_unavailable") from exc
    tmp: str | None = None
    try:
        os.close(fd)
        current = _read(root)
        if current.revision != expected_revision:
            raise StoreError("stale_revision")
        fd, tmp = tempfile.mkstemp(prefix=".luna-si-associations.", suffix=".tmp", dir=root)
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(tmp, root / STATE_NAME)
        tmp = None
        # Directory fsync is supported on POSIX; Windows atomic replacement is
        # relied upon without claiming power-loss durability on every filesystem.
        if os.name == "posix":
            # Replacement has already committed; directory-sync failure must
            # not be misreported as a rolled-back write.
            try:
                dirfd = os.open(root, os.O_RDONLY)
                try:
                    os.fsync(dirfd)
                finally:
                    os.close(dirfd)
            except OSError:
                pass
        return Snapshot(index, hashlib.sha256(data).hexdigest())
    except OSError as exc:
        raise StoreError("state_write_failed") from exc
    finally:
        if tmp is not None:
            try:
                os.unlink(tmp)
            except OSError:
                pass
        try:
            os.unlink(lock)
        except OSError:
            pass
