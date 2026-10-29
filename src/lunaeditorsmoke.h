// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
namespace pdfviewer { class PDFEditorMainWindow; }
namespace luna {
// Explicit CI mode only: synthetic data in a temporary directory.
bool verifyEditorRoundTrip(pdfviewer::PDFEditorMainWindow& window,
                           const QString& reportPath, QString& error);
bool verifyResearchRoundTrip(pdfviewer::PDFEditorMainWindow& window,
                             const QString& reportPath, QString& error);
}
