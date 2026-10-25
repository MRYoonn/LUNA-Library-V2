#!/usr/bin/env python3
"""Isolated LUNA UI overlay for the pinned public PDF4QT Viewer source.

Never imports private source or documents. Refuses unexpected upstream changes.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import sys

PIN = "67b6b9903cb4e318803342f2d428f02e1bb124e3"
BLOBS = {
    "Pdf4QtViewer/main.cpp": "6c343865e20e983bf942e63e3ca2d3aab074a3d1",
    "Pdf4QtViewer/CMakeLists.txt": "08270333862c8906c2281a325b8a0f4efe94cf4d",
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

# Explicit CI-only mode: exercise the real window and asynchronous PDF reader.
# A marker is written only after a one-page synthetic document has been loaded.
# This does not claim visual rendering or annotation correctness.
SMOKE_CHECK = r'''
    if (parser.isSet(lunaSmokeReport))
    {
        if (arguments.size() != 1) return 2;
        auto* poll = new QTimer(&mainWindow);
        poll->setInterval(200);
        QObject::connect(poll, &QTimer::timeout, &mainWindow,
            [&application, &mainWindow, &parser, lunaSmokeReport, poll, attempts = 0]() mutable {
                auto* controller = mainWindow.getProgramController();
                const auto* document = controller->getDocument();
                if (++attempts >= 150) { application.exit(3); return; }
                if (attempts < 10 || !mainWindow.isVisible() || !controller->canClose() ||
                    !document || document->getCatalog()->getPageCount() != 1) return;
                QSaveFile report(parser.value(lunaSmokeReport));
                const QByteArray marker("LUNA_SMOKE_PDF_OPEN_OK\n");
                const bool saved = report.open(QIODevice::WriteOnly) &&
                    report.write(marker) == marker.size() && report.commit();
                poll->stop();
                application.exit(saved ? 0 : 4);
            });
        poll->start();
    }
'''

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
        '#include "pdfviewermainwindow.h"',
        '#include "pdfviewermainwindow.h"\n#include "pdfprogramcontroller.h"\n#include "lunalibrary.h"')
    src = replace_exact(src,
        '#include <QCommandLineParser>',
        '#include <QCommandLineParser>\n#include <QPainter>\n#include <QPainterPath>\n#include <QPixmap>\n#include <QTimer>\n#include <QSaveFile>')
    src = replace_exact(src, '    parser.addVersionOption();',
        '    parser.addVersionOption();\n'
        '    QCommandLineOption lunaSmokeReport("luna-smoke-report",\n'
        '        "CI only: verify a one-page synthetic PDF opens, write a marker and exit.", "path");\n'
        '    parser.addOption(lunaSmokeReport);')
    src = replace_exact(src, '    return application.exec();',
        SMOKE_CHECK + '\n    return application.exec();')
    src = replace_exact(src,
        'QCoreApplication::setOrganizationName("MelkaJ");',
        'QCoreApplication::setOrganizationName("LUNA");')
    src = replace_exact(src,
        'QCoreApplication::setApplicationName("PDF4QT Viewer");',
        'QCoreApplication::setApplicationName("LUNA Library");')
    src = replace_exact(src,
        'QApplication::setApplicationDisplayName(QApplication::translate("Application", "PDF4QT Viewer"));',
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
        'pdfviewer::PDFViewerMainWindow mainWindow;\n    mainWindow.show();',
        'pdfviewer::PDFViewerMainWindow mainWindow;\n    luna::installLibrary(mainWindow);\n    mainWindow.show();')
    return src

def apply(root: Path, dry_run=False) -> None:
    root = root.resolve(strict=True)
    folder = root / "Pdf4QtViewer"
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
