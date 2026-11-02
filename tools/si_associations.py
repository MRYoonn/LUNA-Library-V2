"""Local-only, non-destructive Main↔SI association model for LUNA.

This is a reference contract, not wired to LUNA's Qt UI. The caller supplies a
stable per-document UUID and relative location; this code never reads/moves PDFs,
fetches network resources, or writes any local state. Only explicit user
confirmation creates an association. A DOI/filename similarity is never proof.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass
import json
from pathlib import PurePosixPath
import re
from uuid import UUID

try:
    from tools.si_finder import normalize_doi
except ImportError:  # standalone execution with tools/ on sys.path
    from si_finder import normalize_doi

MAX_DOCUMENTS = 20000
MAX_LINKS = 20000
SCHEMA_VERSION = 1
# Keep the local index compatible with Qt's opt-in UUIDv4 sidecar identity.
_RESERVED_DEVICES = {"CON", "PRN", "AUX", "NUL"} | {
    f"{prefix}{digit}" for prefix in ("COM", "LPT") for digit in "123456789¹²³"
}
_DRIVE = re.compile(r"^[a-zA-Z]:")


def document_id(raw: str) -> str:
    if not isinstance(raw, str):
        raise ValueError("invalid_document_id")
    try:
        value = UUID(raw)
    except (ValueError, AttributeError) as exc:
        raise ValueError("invalid_document_id") from exc
    if str(value) != raw.lower() or value.version != 4:
        raise ValueError("invalid_document_id")
    return str(value)


def relative_pdf_path(raw: str) -> str:
    """Canonical slash-relative PDF path; no traversal or Windows aliases."""
    if not isinstance(raw, str) or not raw or len(raw) > 1024 or "\\" in raw:
        raise ValueError("invalid_relative_path")
    if any(not ch.isprintable() for ch in raw) or raw.startswith("/") or _DRIVE.match(raw):
        raise ValueError("invalid_relative_path")
    parts = raw.split("/")
    if any(p in ("", ".", "..") or p.endswith((" ", ".")) or ":" in p
           for p in parts):
        raise ValueError("invalid_relative_path")
    if any(p.split(".", 1)[0].upper() in _RESERVED_DEVICES for p in parts):
        raise ValueError("invalid_relative_path")
    path = str(PurePosixPath(raw))
    if not path.lower().endswith(".pdf"):
        raise ValueError("not_pdf")
    return path


@dataclass(frozen=True)
class LocalDocument:
    id: str
    kind: str  # main | si
    relative_path: str
    doi: str = ""  # Only the verified parent article DOI, for main documents.


@dataclass(frozen=True)
class ConfirmedLink:
    main_id: str
    si_id: str
    parent_doi: str
    source: str = "user_confirmed"


class AssociationIndex:
    """In-memory state; JSON export/import is explicit and never writes files."""

    def __init__(self) -> None:
        self.documents: dict[str, LocalDocument] = {}
        self.links: dict[str, ConfirmedLink] = {}  # one parent per SI

    def add_document(self, id: str, kind: str, relative_path: str, doi: str = "") -> None:
        id = document_id(id)
        path = relative_pdf_path(relative_path)
        if kind not in ("main", "si"):
            raise ValueError("invalid_document_kind")
        canonical_doi = normalize_doi(doi) if doi else ""
        if (kind == "main" and not canonical_doi) or (kind == "si" and doi):
            raise ValueError("invalid_document_doi")
        new = LocalDocument(id, kind, path, canonical_doi)
        old = self.documents.get(id)
        if old is not None:
            if old != new:
                raise ValueError("document_identity_conflict")
            return  # idempotent re-index, never implicitly rename or copy
        if len(self.documents) >= MAX_DOCUMENTS:
            raise ValueError("document_limit")
        if any(d.relative_path.casefold() == path.casefold() for d in self.documents.values()):
            raise ValueError("path_collision")
        self.documents[id] = new

    def confirm_link(self, main_id: str, si_id: str, parent_doi: str, *,
                     user_confirmed: bool = False) -> None:
        if not user_confirmed:
            raise ValueError("confirmation_required")
        main_id, si_id = document_id(main_id), document_id(si_id)
        main, si = self.documents.get(main_id), self.documents.get(si_id)
        if main is None or si is None or main.kind != "main" or si.kind != "si":
            raise ValueError("invalid_link_documents")
        doi = normalize_doi(parent_doi)
        if not doi or doi != main.doi:
            raise ValueError("parent_doi_mismatch")
        new = ConfirmedLink(main_id, si_id, doi)
        existing = self.links.get(si_id)
        if existing is not None:
            if existing != new:
                raise ValueError("si_already_linked")
            return
        if len(self.links) >= MAX_LINKS:
            raise ValueError("link_limit")
        self.links[si_id] = new

    def relocate(self, id: str, expected_old: str, new_path: str) -> None:
        """Update local location only AFTER caller's safe file move succeeds."""
        id = document_id(id)
        old, new = relative_pdf_path(expected_old), relative_pdf_path(new_path)
        doc = self.documents.get(id)
        if doc is None or doc.relative_path != old:
            raise ValueError("stale_move")
        if any(other_id != id and other.relative_path.casefold() == new.casefold()
               for other_id, other in self.documents.items()):
            raise ValueError("path_collision")
        self.documents[id] = LocalDocument(id, doc.kind, new, doc.doi)
        # The link is keyed by stable UUID, not path or filename.

    def linked_si(self, main_id: str) -> tuple[LocalDocument, ...]:
        main_id = document_id(main_id)
        return tuple(self.documents[si_id] for si_id, link in sorted(self.links.items())
                     if link.main_id == main_id)

    def to_json(self) -> str:
        data = {"version": SCHEMA_VERSION,
                "documents": [asdict(self.documents[k]) for k in sorted(self.documents)],
                "links": [asdict(self.links[k]) for k in sorted(self.links)]}
        return json.dumps(data, ensure_ascii=True, sort_keys=True, separators=(",", ":"))

    @classmethod
    def from_json(cls, raw: str) -> "AssociationIndex":
        if not isinstance(raw, str) or len(raw) > 8_000_000:
            raise ValueError("invalid_state")
        try:
            data = json.loads(raw)
        except (ValueError, TypeError) as exc:
            raise ValueError("invalid_state") from exc
        if not isinstance(data, dict) or set(data) != {"version", "documents", "links"} or type(data["version"]) is not int or data["version"] != SCHEMA_VERSION:
            raise ValueError("unsupported_state")
        if (not isinstance(data["documents"], list) or len(data["documents"]) > MAX_DOCUMENTS
                or not isinstance(data["links"], list) or len(data["links"]) > MAX_LINKS):
            raise ValueError("invalid_state")
        index = cls()
        for item in data["documents"]:
            if not isinstance(item, dict) or set(item) != {"id", "kind", "relative_path", "doi"}:
                raise ValueError("invalid_document")
            if item["id"] in index.documents:
                raise ValueError("duplicate_document")
            index.add_document(**item)
        for item in data["links"]:
            if not isinstance(item, dict) or set(item) != {"main_id", "si_id", "parent_doi", "source"}:
                raise ValueError("invalid_link")
            if item["source"] != "user_confirmed" or item["si_id"] in index.links:
                raise ValueError("invalid_link")
            index.confirm_link(item["main_id"], item["si_id"], item["parent_doi"], user_confirmed=True)
        return index
