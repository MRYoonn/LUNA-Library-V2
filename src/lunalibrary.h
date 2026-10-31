// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
#include <functional>
class QMainWindow;
namespace pdfviewer { class PDFEditorMainWindow; }
namespace luna {
void installLibrary(pdfviewer::PDFEditorMainWindow& window);
void installLibraryShell(QMainWindow& shell, pdfviewer::PDFEditorMainWindow& initial);
void bindLibraryEditor(QMainWindow& shell, pdfviewer::PDFEditorMainWindow& editor);
void recordLibraryDocument(QMainWindow& shell, pdfviewer::PDFEditorMainWindow& editor);
void setLibraryDocumentRouter(QMainWindow& shell, std::function<void(const QString&)> open,
    std::function<bool(const QString&)> elsewhere,
    std::function<bool(pdfviewer::PDFEditorMainWindow*, const QString&, const QString&)> moved);
void prepareReaderSession(pdfviewer::PDFEditorMainWindow& editor, QMainWindow& shell);
bool openInitialDocument(pdfviewer::PDFEditorMainWindow& window, const QString& path);
// Used only by the explicit CI mode with a temporary synthetic library.
bool verifyBibliographyUi(pdfviewer::PDFEditorMainWindow& window, QString& error, const QString& capturePath);
bool verifyLibraryUi(pdfviewer::PDFEditorMainWindow& window, QString& error, const QString& capturePath = {});
bool verifyCancelledLibraryMove(pdfviewer::PDFEditorMainWindow& window, QString& error);
}
