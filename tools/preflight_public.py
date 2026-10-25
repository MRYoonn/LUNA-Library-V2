#!/usr/bin/env python3
"""Public LUNA release gate. Stop on any suspicious file or source content."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
APPROVED = {
    "README.md", "SECURITY_POLICY.md", "LICENSE_NOTICE.md", ".gitignore",
    ".github/workflows/build-windows.yml",
    "src/lunalibrary.cpp", "src/lunalibrary.h",
    "tools/apply_overlay.py", "tools/preflight_public.py",
    "tests/test_public_gate.py",
    "tools/verify_portable.py", "tests/test_portable.py",
    "tools/smoke_windows.py",
}
FORBIDDEN = [
    ("email", re.compile(r"\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b", re.I)),
    ("phone", re.compile(r"(?<!\d)01[016789][-. ]?\d{3,4}[-. ]?\d{4}(?!\d)")),
    ("GitHub credential", re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,})\b")),
    ("AWS credential", re.compile(r"\b(?:AKIA|ASIA)[0-9A-Z]{16}\b")),
    ("API secret", re.compile(r"\bsk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{20,}")),
    ("private key", re.compile(r"-----BEGIN (?:OPENSSH|RSA|EC|DSA|PGP|ENCRYPTED|PRIVATE) [A-Z ]*KEY-----")),
    ("Bearer token", re.compile(r"Authorization\s*[:=]\s*[\"']?Bearer\s+[A-Za-z0-9._~+/=-]{12,}", re.I)),
    ("credential assignment", re.compile(r"\b(?:MYBOX_TOKEN|SUPABASE_SERVICE_ROLE_KEY|SERVICE_ROLE_KEY|CLIENT_SECRET|ACCESS_TOKEN|REFRESH_TOKEN|PASSWORD)\s*[:=]\s*[\"'][^\"'\s]{5,}", re.I)),
    ("Windows user path", re.compile(r"[A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/][^\s\\/'\"]+", re.I)),
    ("Unix user path", re.compile(r"/(?:home|Users)/[A-Za-z][A-Za-z0-9._-]{1,}/", re.I)),
]
def check(root=ROOT):
    errors = []
    present = set()
    for path in root.rglob("*"):
        name = path.relative_to(root).as_posix()
        if name == ".git" or name.startswith(".git/"):
            continue
        if path.is_symlink():
            errors.append("Symlink blocked: " + name)
            continue
        if not path.is_file():
            continue
        present.add(name)
        if name not in APPROVED:
            errors.append("Unexpected potentially private file: " + name)
            continue
        if path.stat().st_size > 200_000:
            errors.append("Unexpected file size: " + name)
            continue
        try:
            data = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError):
            errors.append("Non-text/unreadable file: " + name)
            continue
        for label, pattern in FORBIDDEN:
            if pattern.search(data):
                errors.append("Sensitive indicator (" + label + "): " + name)
    for name in sorted(APPROVED - present):
        errors.append("Missing source file: " + name)
    workflow = root / ".github/workflows/build-windows.yml"
    if workflow.is_file():
        data = workflow.read_text(encoding="utf-8")
        if not re.search(r"(?m)^on:\s*\n\s+workflow_dispatch:\s*$", data):
            errors.append("Manual-only workflow required")
        if re.search(r"(?m)^\s*(push|pull_request|pull_request_target|schedule|repository_dispatch|workflow_call):", data):
            errors.append("Unsafe automatic workflow trigger")
        if not re.search(r"(?m)^permissions:\s*\n\s+contents:\s*read\s*$", data):
            errors.append("Workflow token must be read-only")
        if re.search(r"\$\{\{\s*secrets\.", data, re.I):
            errors.append("Workflow may not read secrets")
        if "67b6b9903cb4e318803342f2d428f02e1bb124e3" not in data:
            errors.append("Pinned PDF4QT commit required")
    return errors

if __name__ == "__main__":
    failures = check()
    for error in sorted(set(failures)):
        print("[BLOCK] " + error, file=sys.stderr)
    if failures:
        print("[BLOCK] PUBLIC RELEASE HALTED", file=sys.stderr)
        sys.exit(1)
    print("[PASS] " + str(len(APPROVED)) + " public text files, no flagged private items")
    print("[NOTICE] Heuristic scan is not a guarantee of zero sensitive data.")
