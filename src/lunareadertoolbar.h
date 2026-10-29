// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
#include <QColor>
class QIcon;
class QAction;
class QToolBar;
namespace pdfviewer { class PDFEditorMainWindow; }
namespace luna {
QIcon readerIcon(const QString& name, const QColor& tint = {});
void installReaderToolbar(pdfviewer::PDFEditorMainWindow& window, QToolBar& bar, QAction& libraryToggle);
void hideAdvancedToolbars(pdfviewer::PDFEditorMainWindow& window);
bool verifyReaderToolbar(pdfviewer::PDFEditorMainWindow& window, QString& error, bool geometry);
}
