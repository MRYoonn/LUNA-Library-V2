#!/usr/bin/env python3
"""Public LUNA release gate. Stop on any suspicious file or source content."""
from pathlib import Path
import hashlib
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
APPROVED = {
    ".gitattributes",
    "assets/icons/ui-library.svg",
    "tools/capture_ui_reference_windows.mjs",
    'design/REFERENCE.md',
    'design/LUNA_Library_UI_v0.9.1_OneLineMemo.html',
    'assets/icons/ui-next.svg',
    'assets/icons/ui-open.svg',
    'assets/icons/ui-panel-close.svg',
    'assets/icons/ui-print.svg',
    'assets/icons/ui-highlight.svg',
    'assets/icons/ui-save.svg',
    'assets/icons/ui-folder.svg',
    'assets/icons/ui-panel-open.svg',
    'assets/icons/ui-erase.svg',
    'assets/icons/ui-documents.svg',
    'assets/icons/ui-plus.svg',
    'assets/icons/ui-search.svg',
    'assets/icons/ui-minus.svg',
    'assets/icons/ui-select.svg',
    'assets/icons/ui-star.svg',
    'assets/icons/ui-recent.svg',
    'assets/icons/ui-crop.svg',
    'assets/icons/ui-close.svg',
    'assets/icons/ui-note.svg',
    'assets/icons/ui-previous.svg',
    'assets/icons/ui-file-add.svg',
    'assets/icons/ui-add.svg',

    "tests/native_dialog/windowstate.cpp",
    "tools/probe_editor_lifetime_windows.py",
    ".github/workflows/p0-editor-lifetime.yml",
    "tools/Luna-Crash-Tools.ps1", "tools/Start-Luna-Crash-Diagnostics.ps1", "tools/Start-Luna-Crash-Diagnostics.cmd",
    "tools/Analyze-Luna-Crash.ps1", "tools/Analyze-Luna-Crash.cmd", "tools/probe_crash_collector_windows.py",
    "tests/crash_collector/CMakeLists.txt", "tests/crash_collector/main.cpp", "CRASH_DIAGNOSTICS_KO.md",
    "tools/Start-Luna-Diagnostics.ps1", "tools/Start-Luna-Diagnostics.cmd",
    "tools/probe_tabs_integration_windows.py",
    "src/lunatabs.cpp", "src/lunatabs.h",
    "experiments/native_tabs/prepare.py", "experiments/native_tabs/main.cpp",
    "experiments/native_tabs/run_windows.py", "experiments/native_tabs/README.md",
    "experiments/native_tabs/HANDOFF_KO.md",
    "tests/test_native_tabs.py",
    "tests/native_dialog/windows_job.py",
    "src/lunapagegrid.h", "tests/native_dialog/pagegrid.cpp",
    "experiments/tab_host/main.cpp", "experiments/tab_host/CMakeLists.txt",
    "experiments/tab_host/README.md", "tools/probe_tab_host_windows.py",
    "README.md", "SECURITY_POLICY.md", "LICENSE_NOTICE.md", ".gitignore",
    "USER_GUIDE_KO.md",
    ".github/workflows/build-windows.yml",
    "src/lunalibrary.cpp", "src/lunalibrary.h", "src/lunathumbnailcache.h",
    "tests/native_dialog/thumbnails.cpp",
    "src/lunafilemove.h", "tests/native_dialog/filemoves.cpp",
    "src/lunadocumentclaim.h", "tests/native_dialog/documentclaims.cpp",
    "tools/probe_documents_windows.py",
    "tools/probe_research_windows.py",
    "tools/apply_overlay.py", "tools/preflight_public.py",
    "tests/test_public_gate.py",
    "tools/verify_portable.py", "tests/test_portable.py",
    "tools/smoke_windows.py",
    "tools/stage_msvc_runtime.ps1",
    "tools/probe_print_windows.ps1",
    "src/lunalibrary.qss",
    "src/lunareadertoolbar.cpp", "src/lunareadertoolbar.h",
    "src/lunaeditorsmoke.cpp", "src/lunaeditorsmoke.h",
    "src/lunasavedialog.h", "tests/native_dialog/CMakeLists.txt", "tests/native_dialog/main.cpp",
}
APPROVED_BINARY = {'assets/icons/LUNA-Moon.ico': '5c1dec76af47b7c37e3d41e5fb05d62a02783f4f4af0247503096feb4cd7b51f', 'assets/icons/LUNA-Moon.png': '2d08386badd6d1d3a80f6d53213ccc53296fb46590a6c45b734a83e457aba4c3'}
APPROVED.update(APPROVED_BINARY)
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
        if name in APPROVED_BINARY:
            if hashlib.sha256(path.read_bytes()).hexdigest() != APPROVED_BINARY[name]:
                errors.append("Unapproved icon content: " + name)
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
    print("[PASS] " + str(len(APPROVED)) + " approved source/icon files, no flagged private items")
    print("[NOTICE] Heuristic scan is not a guarantee of zero sensitive data.")
