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
add_executable(LUNA luna_main.cpp lunalibrary.cpp lunaeditorsmoke.cpp icon.rc app.qrc luna_resources.qrc)
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
# Annotation persistence is checked through the native Editor actions.
SMOKE_CHECK = r'''
    if (parser.isSet(lunaSmokeReport))
    {
        if (arguments.size() != 1) return 2;
        auto* poll = new QTimer(&mainWindow);
        poll->setInterval(200);
        QObject::connect(poll, &QTimer::timeout, &mainWindow,
            [&application, &mainWindow, &captureHost, &parser, lunaSmokeReport, poll, attempts = 0, phase = 0]() mutable {
                auto fail = [&](int code, const QByteArray& reason) {
                    mainWindow.grab().save(parser.value(lunaSmokeReport) + ".failure.png", "PNG");
                    QSaveFile diagnostic(parser.value(lunaSmokeReport));
                    if (diagnostic.open(QIODevice::WriteOnly)) {
                        diagnostic.write(reason);
                        diagnostic.commit();
                    }
                    application.exit(code);
                };
                auto* controller = mainWindow.getProgramController();
                const auto* document = controller->getDocument();
                if (++attempts >= 150) { application.exit(3); return; }
                if (attempts < 10 || !mainWindow.isVisible() || !controller->canClose() ||
                    !document || document->getCatalog()->getPageCount() != 1) return;
                if (phase == 0) {
                    poll->stop();
                    QString error;
                    if (!luna::verifyLibraryUi(mainWindow, error)) { fail(5, error.toUtf8()); return; }
                    poll->start();
                    phase = 1;
                    attempts = 0;
                    return;
                }
                auto* proxy = controller->getPdfWidget()->getDrawWidgetProxy();
                if (qAbs(proxy->getZoom() - 2.0) > 0.001 || proxy->getPageLayout() != pdf::PageLayout::OneColumn) {
                    application.exit(6); return;
                }
                if (phase == 1) {
                    poll->stop();
                    QString error;
                    if (!luna::verifyEditorRoundTrip(mainWindow, parser.value(lunaSmokeReport), error)) {
                        fail(9, error.toUtf8()); return;
                    }
                    poll->start();
                    // The real Windows/QPA window was exercised above. Hosted
                    // desktops clamp top-level windows to about 1024 pixels.
                    // Render the same widget tree as a child at exact sizes;
                    // this is a Qt layout capture, not a physical display test.
                    QApplication::setQuitOnLastWindowClosed(false);
                    captureHost = std::make_unique<QWidget>();
                    captureHost->setAttribute(Qt::WA_DontShowOnScreen);
                    captureHost->resize(2048, 1536);
                    captureHost->show();
                    mainWindow.setParent(captureHost.get(), Qt::Widget);
                    mainWindow.resize(1366, 768);
                    mainWindow.show();
                    phase = 2;
                    attempts = 0;
                    return;
                }
                if (phase == 2) {
                    if (mainWindow.size() != QSize(1366, 768)) {
                        fail(8, QString("Expected1366x768 actual%1x%2 minimumHint%3x%4")
                            .arg(mainWindow.width()).arg(mainWindow.height())
                            .arg(mainWindow.minimumSizeHint().width()).arg(mainWindow.minimumSizeHint().height()).toUtf8());
                        return;
                    }
                    if (!mainWindow.grab().save(parser.value(lunaSmokeReport) + ".png", "PNG")) { application.exit(7); return; }
                    mainWindow.resize(1920, 1080);
                    phase = 3;
                    attempts = 0;
                    return;
                }
                if (mainWindow.size() != QSize(1920, 1080)) { fail(8, "Expected1920x1080 layout size"); return; }
                if (!mainWindow.grab().save(parser.value(lunaSmokeReport) + ".wide.png", "PNG")) { application.exit(7); return; }
                QSaveFile report(parser.value(lunaSmokeReport));
                const QByteArray marker("LUNA_SMOKE_EDITOR_ROUNDTRIP_OK\n");
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
        '#include "pdfeditormainwindow.h"',
        '#include "pdfeditormainwindow.h"\n#include "pdfprogramcontroller.h"\n#include "pdfdrawwidget.h"\n#include "pdfdrawspacecontroller.h"\n#include "lunalibrary.h"\n#include "lunaeditorsmoke.h"')
    src = replace_exact(src,
        '#include <QCommandLineParser>',
        '#include <QCommandLineParser>\n#include <QPainter>\n#include <QPainterPath>\n#include <QPixmap>\n#include <QTimer>\n#include <QSaveFile>\n#include <QSettings>\n#include <QFileInfo>\n#include <memory>')
    src = replace_exact(src, '    pdf::PDFSettings::applyCommandLineSettingsPath(parser);',
        '    pdf::PDFSettings::applyCommandLineSettingsPath(parser);\n'
        '    if (parser.isSet(lunaSmokeReport)) {\n'
        '        if (parser.positionalArguments().size() != 1) return 2;\n'
        '        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);\n'
        '        QSettings::setDefaultFormat(QSettings::IniFormat);\n'
        '        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,\n'
        '            QFileInfo(parser.value(lunaSmokeReport)).absolutePath() + "/settings");\n'
        '        QSettings("LUNA", "LUNA Library").setValue("library/root",\n'
        '            QFileInfo(parser.positionalArguments().front()).absolutePath());\n'
        '    }')
    src = replace_exact(src, '    parser.addVersionOption();',
        '    parser.addVersionOption();\n'
        '    QCommandLineOption lunaSmokeReport("luna-smoke-report",\n'
        '        "CI only: verify a one-page synthetic PDF opens, write a marker and exit.", "path");\n'
        '    lunaSmokeReport.setFlags(QCommandLineOption::HiddenFromHelp);\n'
        '    parser.addOption(lunaSmokeReport);')
    src = replace_exact(src, '    return application.exec();',
        SMOKE_CHECK + '\n    return application.exec();')
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
        'std::unique_ptr<QWidget> captureHost;\n    pdfviewer::PDFEditorMainWindow mainWindow;\n    luna::installLibrary(mainWindow);\n    mainWindow.show();')
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
        folder / "lunaeditorsmoke.cpp": (own / "lunaeditorsmoke.cpp").read_bytes(),
        folder / "lunaeditorsmoke.h": (own / "lunaeditorsmoke.h").read_bytes(),
        folder / "lunalibrary.h": (own / "lunalibrary.h").read_bytes(),
        folder / "lunalibrary.qss": (own / "lunalibrary.qss").read_bytes(),
        folder / "luna_resources.qrc": b'<RCC><qresource prefix="/luna"><file>lunalibrary.qss</file></qresource></RCC>\n',
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
    source_newline = b"\r\n" if b"\r\n" in main.read_bytes() else b"\n"
    for path, data in files.items():
        if path.suffix in {".cpp", ".h"}:
            data = data.replace(b"\r\n", b"\n").replace(b"\n", source_newline)
        path.write_bytes(data)
    cmake_data = cmake.read_bytes()
    cmake_newline = b"\r\n" if b"\r\n" in cmake_data else b"\n"
    cmake.write_bytes(cmake_data.rstrip(b"\r\n") + cmake_newline * 2 +
                      CMAKE_ADDITION.encode("utf-8").replace(b"\n", cmake_newline))
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
