// SPDX-License-Identifier: MIT
// Drives native Editor actions and widget events; no alternate PDF renderer.
#include "lunaeditorsmoke.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcompiler.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"
#include "pdftextlayout.h"

#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QDir>
#include <QMouseEvent>
#include <QTimer>
#include <functional>

namespace luna {
namespace {
bool waitFor(const std::function<bool()>& predicate) {
    if (predicate()) return true;
    QEventLoop loop;
    QTimer poll;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool success = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (predicate()) { success = true; loop.quit(); }
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(50);
    deadline.start(5000);
    loop.exec();
    return success;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void mouse(QWidget* widget, QEvent::Type type, QPoint point,
           Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, QPointF(point), QPointF(widget->mapToGlobal(point)),
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}
}

bool verifyEditorRoundTrip(pdfviewer::PDFEditorMainWindow& window,
                           const QString& reportPath, QString& error) {
    auto fail = [&](const char* message) { error = QString::fromLatin1(message); return false; };
    auto checkpoint = [&](const char* phase) {
        QFile report(reportPath);
        if (report.open(QIODevice::WriteOnly)) report.write(phase);
    };
    checkpoint("Editor phase: native text selection");
    auto* controller = window.getProgramController();
    auto* pdfWidget = controller->getPdfWidget();
    auto* proxy = pdfWidget->getDrawWidgetProxy();
    const QString original = controller->getOriginalFileName();
    const QString saved = reportPath + QStringLiteral(".annotated.pdf");
    const QByteArray originalBytes = readFile(original);
    if (originalBytes.isEmpty() || QFileInfo::exists(saved)) return fail("Unsafe synthetic test paths");
    auto action = [&](const char* name) { return window.findChild<QAction*>(QString::fromLatin1(name)); };
    auto* select = action("actionSelectText");
    auto* selectAll = action("actionSelectTextAll");
    auto* highlight = action("actionCreateTextHighlight");
    auto* note = action("actionStickyNoteNote");
    auto* saveAs = action("actionSave_As");
    if (!select || !selectAll || !highlight || !note || !saveAs || !action("actionSave") ||
        !action("actionBookmarkPage")) return fail("Native Editor actions missing");

    auto annotations = [&](pdf::AnnotationType type) {
        int count = 0;
        for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations)
            if (item.annotation->getType() == type) ++count;
        return count;
    };
    if (annotations(pdf::AnnotationType::Highlight) || annotations(pdf::AnnotationType::Text))
        return fail("Synthetic input already has annotations");
    if (!select->isChecked()) select->trigger();
    if (!waitFor([&] {
        selectAll->trigger();
        return controller->getToolManager()->getSelectTextTool()->getSelectedText().contains(
            QStringLiteral("LUNA synthetic smoke test"));
    }))
        return fail("Native text selection did not select synthetic text");

    // Use the native glyph geometry rather than guessed font metrics. The
    // upstream selector intersects a rectangle with text blocks, so an exactly
    // horizontal zero-height test drag selects nothing (QRectF::intersects).
    pdf::PDFTextLayout textLayout = proxy->getTextLayoutCompiler()->getTextLayoutLazy(0);
    if (textLayout.getTextBlocks().empty() || textLayout.getTextBlocks().front().getLines().empty())
        return fail("Synthetic native text layout has no lines");
    const auto& characters = textLayout.getTextBlocks().front().getLines().front().getCharacters();
    if (characters.size() < 8) return fail("Synthetic native text layout is incomplete");
    QPointF pageStart = characters.front().boundingBox.boundingRect().center() - QPointF(1, 0);
    QPointF pageEnd = characters[7].boundingBox.boundingRect().center() + QPointF(1, -2);
    if (textLayout.createTextSelection(0, pageStart, pageEnd).isEmpty())
        return fail("Synthetic drag rectangle does not intersect native glyphs");

    // The native highlighter selects text by dragging in its own tool mode.
    // Zoom out temporarily so the synthetic line fits the hosted desktop.
    proxy->zoom(1.0);
    checkpoint("Editor phase: native highlight drag");
    highlight->trigger();
    if (!highlight->isChecked() || !controller->getToolManager()->getActiveTool())
        return fail("Native highlighter action did not activate its tool");
    auto* widget = pdfWidget->getDrawWidget()->getWidget();
    QPoint start, end, notePoint;
    if (!waitFor([&] {
        const auto snapshot = proxy->getSnapshot();
        const auto* page = snapshot.getPageSnapshot(0);
        if (!page) return false;
        start = page->pageToDeviceMatrix.map(pageStart).toPoint();
        end = page->pageToDeviceMatrix.map(pageEnd).toPoint();
        notePoint = page->pageToDeviceMatrix.map(QPointF(150, 195)).toPoint();
        return widget->rect().contains(start) && widget->rect().contains(end) && widget->rect().contains(notePoint);
    })) return fail("Synthetic annotation points are outside native viewport");
    mouse(widget, QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
    mouse(widget, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(widget, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(widget, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 1; }))
        return fail("Native highlight drag did not create one annotation");
    std::vector<pdf::PDFReal> highlightColor;
    for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations)
        if (item.annotation->getType() == pdf::AnnotationType::Highlight) highlightColor = item.annotation->getColor();
    if (highlightColor.empty()) return fail("Highlight has no persisted color");

    checkpoint("Editor phase: native sticky note input");
    note->trigger();
    const QString noteText = QStringLiteral("LUNA synthetic note round trip");
    bool enteredNote = false;
    QTimer input;
    QObject::connect(&input, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            enteredNote = true;
            dialog->setTextValue(noteText);
            dialog->accept();
        }
    });
    input.start(50);
    // The native pick tool updates its snap point during mouse movement.
    mouse(widget, QEvent::MouseMove, notePoint, Qt::NoButton, Qt::NoButton);
    mouse(widget, QEvent::MouseButtonPress, notePoint, Qt::LeftButton, Qt::LeftButton);
    mouse(widget, QEvent::MouseButtonRelease, notePoint, Qt::LeftButton, Qt::NoButton);
    input.stop();
    if (!enteredNote || !waitFor([&] { return annotations(pdf::AnnotationType::Text) == 1; }))
        return fail("Native sticky note input did not create one annotation");

    checkpoint("Editor phase: native Save As dialog");
    if (!waitFor([&] { return saveAs->isEnabled() && window.isEnabled(); }))
        return fail("Native Save As action remained unavailable");
    bool choseCopy = false;
    QTimer chooseFile;
    QObject::connect(&chooseFile, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            // selectFile() intentionally leaves a focused, visible filename
            // editor unchanged. Fill the actual native Qt dialog input, then
            // verify its selected path before accepting any write operation.
            auto* filename = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
            if (!filename) { dialog->reject(); return; }
            dialog->setDirectory(QFileInfo(saved).absolutePath());
            filename->setText(QFileInfo(saved).fileName());
            const auto selected = dialog->selectedFiles();
            choseCopy = selected.size() == 1 &&
                QDir::cleanPath(QFileInfo(selected.front()).absoluteFilePath()) ==
                QDir::cleanPath(QFileInfo(saved).absoluteFilePath());
            if (!choseCopy) { dialog->reject(); return; }
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    chooseFile.start(50);
    saveAs->trigger();
    chooseFile.stop();
    if (!choseCopy) return fail("Native Save As dialog did not select the temporary copy path");
    if (!QFileInfo::exists(saved)) return fail("Native Save As returned without the expected PDF copy");
    if (readFile(saved).isEmpty()) return fail("Native Save As wrote an empty PDF copy");
    if (QFileInfo(controller->getOriginalFileName()).canonicalFilePath() != QFileInfo(saved).canonicalFilePath())
        return fail("Native Save As did not switch the current document to the copy");
    if (readFile(original) != originalBytes) return fail("Save As modified synthetic original");
    select->trigger(); // Close the highlighter style window before layout captures.
    if (!controller->askForSaveDocumentBeforeClose()) return fail("Saved document remains dirty");
    checkpoint("Editor phase: reopen annotated copy from disk");
    controller->closeDocument();
    controller->openDocument(saved);
    if (!waitFor([&] {
        const auto* document = controller->getDocument();
        return document && document->getCatalog()->getPageCount() == 1 && controller->canClose();
    })) return fail("Annotated copy did not reopen");
    if (annotations(pdf::AnnotationType::Highlight) != 1 || annotations(pdf::AnnotationType::Text) != 1)
        return fail("Highlight or note lost after disk save and reopen");
    bool noteSurvived = false;
    bool colorSurvived = false;
    for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations) {
        if (item.annotation->getType() == pdf::AnnotationType::Text)
            noteSurvived = item.annotation->getContents() == noteText;
        if (item.annotation->getType() == pdf::AnnotationType::Highlight)
            colorSurvived = item.annotation->getColor() == highlightColor;
    }
    if (!noteSurvived || !colorSurvived || readFile(original) != originalBytes)
        return fail("Annotation contents, color or original-preservation check failed");
    proxy->setPageLayout(pdf::PageLayout::OneColumn);
    proxy->zoom(2.0);
    return true;
}
}
