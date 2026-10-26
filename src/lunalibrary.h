// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
namespace pdfviewer { class PDFEditorMainWindow; }
namespace luna {
void installLibrary(pdfviewer::PDFEditorMainWindow& window);
// Used only by the explicit CI mode with a temporary synthetic library.
bool verifyLibraryUi(pdfviewer::PDFEditorMainWindow& window, QString& error, const QString& capturePath = {});
}
