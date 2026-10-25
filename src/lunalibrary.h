// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
namespace pdfviewer { class PDFViewerMainWindow; }
namespace luna {
void installLibrary(pdfviewer::PDFViewerMainWindow& window);
// Used only by the explicit CI mode with a temporary synthetic library.
bool verifyLibraryUi(pdfviewer::PDFViewerMainWindow& window, QString& error);
}
