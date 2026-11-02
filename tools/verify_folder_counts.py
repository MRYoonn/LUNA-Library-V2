#!/usr/bin/env python3
"""Fail-closed, source-only guard for LUNA's direct-folder PDF count contract.

This is NOT a native Windows run. Runtime acceptance is the separate Qt smoke
test. The report records fixed check names, not local paths or PDF contents.
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def verify_source(source: str) -> list[str]:
    failures = []
    def region(start: str, end: str) -> str:
        if start not in source or end not in source:
            return ""
        return source.split(start, 1)[1].split(end, 1)[0]

    model = region("class LunaFolderModel final", "class PaperFileList final")
    counter = region("void beginRecount()", "QTimer recountTimer_")
    folder = region("void selectFolder(const QString& path)", "void scanAll()")
    all_docs = region("void scanAll()", "void scanBatch(bool all)")
    smoke = region('checkpoint("count-start");', 'checkpoint("count-recovered");')

    checks = {
        "qt_worker_outside_gui": 'QThread::create([root, result]' in counter
            and "QDirIterator::Subdirectories" in counter,
        "count_only_pdf_parent": 'pdf.absolutePath()' in counter
            and 'result->insert(folder, result->value(folder) + 1);' in counter
            and "while (folder" not in counter
            and "folder = QFileInfo(folder).absolutePath()" not in counter
            and "folder = QDir(folder).absolutePath()" not in counter,
        "stale_worker_result_blocked": "if (generation_ != version) return;" in model,
        "folder_view_nonrecursive": "QDirIterator" in folder
            and "QDirIterator::Subdirectories" not in folder,
        "all_documents_recursive": "QDirIterator::Subdirectories" in all_docs,
        "synthetic_child_positive": "pdfCount(subfolder.path()) == 1" in smoke
            and "pdfCount(countRoot) == baselineCount" in smoke,
        "synthetic_child_removed": "pdfCount(childFolderPath) == 0" in smoke
            and 'checkpoint("count-deleted");' in smoke
            and 'library->model_->refreshPdfCounts(countRoot);' in smoke,
    }
    for label, passed in checks.items():
        if not passed:
            failures.append(label)
    return failures

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Synthetic-only JSON report destination")
    args = parser.parse_args()
    text = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
    failures = verify_source(text)
    report = {
        "schema": 1,
        "gate": "source-only-direct-folder-count",
        "status": "failed" if failures else "passed",
        "checks": 7,
        "failed_checks": failures,
        "native_windows_validated": False,
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("[BLOCK]" if failures else "[PASS]", json.dumps(report, sort_keys=True))
    return 1 if failures else 0

if __name__ == "__main__":
    raise SystemExit(main())
