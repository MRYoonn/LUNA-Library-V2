#!/usr/bin/env python3
"""Isolated LUNA UI overlay for the pinned public PDF4QT Editor source.

Never imports private source or documents. Refuses unexpected upstream changes.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import sys

PIN = "67b6b9903cb4e318803342f2d428f02e1bb124e3"
BLOBS = {
    "Pdf4QtEditor/main.cpp": "208c36b730ec158854b614e48c868e518aea5cde",
    "Pdf4QtEditor/CMakeLists.txt": "9ca656581680c7af16cee3ed35a32332e44e2dab",
}
CMAKE_ADDITION = """
# LUNA_LIBRARY_V2_TARGET_BEGIN
add_executable(LUNA luna_main.cpp lunalibrary.cpp icon.rc app.qrc)
target_link_libraries(LUNA PRIVATE Pdf4QtLibCore Pdf4QtLibWidgets Pdf4QtLibGui
    Qt6::Core Qt6::Gui Qt6::Widgets)
if(MSVC)
    target_compile_options(LUNA PRIVATE /utf-8)
endif()
set_target_properties(LUNA PROPERTIES
    WIN32_EXECUTABLE ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})
install(TARGETS LUNA RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR}
    LIBRARY DESTINATION ${PDF4QT_INSTALL_LIB_DIR})
# LUNA_LIBRARY_V2_TARGET_END
"""

def git_blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

def verify_file(path: Path, expected: str) -> bool:
    if not path.is_file() or path.is_symlink():
        return False
    data = path.read_bytes()
    return git_blob(data) == expected or (
        b"\r\n" in data and git_blob(data.replace(b"\r\n", b"\n")) == expected)

def replace_exact(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError("Unrecognized pinned PDF4QT main.cpp; stopped")
    return source.replace(old, new, 1)

def make_main(src: str) -> str:
    src = replace_exact(src,
        '#include "pdfeditormainwindow.h"',
        '#include "pdfeditormainwindow.h"\n#include "lunalibrary.h"')
    src = replace_exact(src,
        '#include <QCommandLineParser>',
        '#include <QCommandLineParser>\n#include <QPainter>\n#include <QPainterPath>\n#include <QPixmap>')
    src = replace_exact(src,
        'QCoreApplication::setOrganizationName("MelkaJ");',
        'QCoreApplication::setOrganizationName("LUNA");')
    src = replace_exact(src,
        'QCoreApplication::setApplicationName("PDF4QT Editor");',
        'QCoreApplication::setApplicationName("LUNA Library");')
    src = replace_exact(src,
        'QApplication::setApplicationDisplayName(QApplication::translate("Application", "PDF4QT Editor"));',
        'QApplication::setApplicationDisplayName(QStringLiteral("LUNA Library"));')
    src = replace_exact(src,
        'isLightGui = parser.isSet(lightGui);',
        'isLightGui = !parser.isSet(darkGui);')
    src = replace_exact(src,
        'QIcon appIcon(":/app-icon.svg");\n    QApplication::setWindowIcon(appIcon);',
        'QPixmap lunaIcon(64, 64);\n'
        '    lunaIcon.fill(Qt::transparent);\n'
        '    {\n'
        '        QPainter painter(&lunaIcon);\n'
        '        painter.setRenderHint(QPainter::Antialiasing);\n'
        '        QPainterPath outer; outer.addEllipse(QRectF(8, 7, 49, 49));\n'
        '        QPainterPath inner; inner.addEllipse(QRectF(24, 1, 47, 47));\n'
        '        painter.fillPath(outer.subtracted(inner), QColor("#3B82F6"));\n'
        '    }\n'
        '    QApplication::setWindowIcon(QIcon(lunaIcon));')
    src = replace_exact(src,
        'pdfviewer::PDFEditorMainWindow mainWindow;\n    mainWindow.show();',
        'pdfviewer::PDFEditorMainWindow mainWindow;\n    luna::installLibrary(mainWindow);\n    mainWindow.show();')
    return src

def apply(root: Path, dry_run=False) -> None:
    root = root.resolve(strict=True)
    folder = root / "Pdf4QtEditor"
    main, cmake = folder / "main.cpp", folder / "CMakeLists.txt"
    for rel, sha in BLOBS.items():
        if not verify_file(root / rel, sha):
            raise ValueError("PDF4QT source pin mismatch or unsafe symlink: " + rel)
    own = Path(__file__).resolve().parents[1] / "src"
    files = {
        folder / "lunalibrary.cpp": (own / "lunalibrary.cpp").read_bytes(),
        folder / "lunalibrary.h": (own / "lunalibrary.h").read_bytes(),
        folder / "luna_main.cpp": make_main(main.read_text(encoding="utf-8")
            .replace("\r\n", "\n")).encode("utf-8"),
    }
    if "LUNA_LIBRARY_V2_TARGET_BEGIN" in cmake.read_text(encoding="utf-8"):
        raise ValueError("LUNA target already applied; stopped")
    for path, data in files.items():
        if path.exists():
            raise ValueError("Refusing to overwrite existing " + str(path))
    if dry_run:
        print("[PASS] Upstream and destination gates")
        return
    for path, data in files.items():
        path.write_bytes(data)
    cmake.write_bytes(cmake.read_bytes().rstrip(b"\r\n") + b"\n\n" +
                      CMAKE_ADDITION.encode("utf-8"))
    print("[PASS] Standalone LUNA target applied, PDF4QT engine untouched")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("pdf4qt_root", type=Path)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    try:
        apply(args.pdf4qt_root, args.dry_run)
    except (OSError, ValueError) as e:
        print("[BLOCK]", e, file=sys.stderr)
        raise SystemExit(1)
