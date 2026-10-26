// SPDX-License-Identifier: MIT
// Drives native Editor actions and widget events; no alternate PDF renderer.
#include "lunaeditorsmoke.h"
#include "lunasavedialog.h"
#include "lunareadertoolbar.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcompiler.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"
#include "pdftextlayout.h"
#include "pdfannotationstyle.h"

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
#include <QClipboard>
#include <QColorDialog>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinterInfo>
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

void key(QWidget* widget, int code, Qt::KeyboardModifiers modifiers) {
    widget->window()->activateWindow(); widget->setFocus();
    QCoreApplication::processEvents();
    QKeyEvent press(QEvent::KeyPress, code, modifiers);
    QKeyEvent release(QEvent::KeyRelease, code, modifiers);
    QApplication::sendEvent(widget, &press);
    QApplication::sendEvent(widget, &release);
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
    auto* note = action("actionStickyNoteComment");
    auto* saveAs = action("actionSave_As");
    if (!select || !selectAll || !highlight || !note || !saveAs || !action("actionSave") ||
        !action("actionBookmarkPage")) return fail("Native Editor actions missing");
    auto* widget = pdfWidget->getDrawWidget()->getWidget();
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    auto click = [&](QAction* a) {
        if (auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(a))) button->click();
        else a->trigger();
    };
    checkpoint("Reader phase: Ctrl+O native file open");
    controller->closeDocument();
    bool opened = false;
    QTimer openDialog;
    QObject::connect(&openDialog, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            if (auto* filename = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) {
                dialog->setDirectory(QFileInfo(original).absolutePath());
                filename->setText(QFileInfo(original).fileName());
                const auto files = dialog->selectedFiles();
                opened = files.size() == 1 && sameExistingFile(files.front(), original);
            }
            QMetaObject::invokeMethod(dialog, opened ? "accept" : "reject", Qt::DirectConnection);
        }
    });
    openDialog.start(50);
    window.activateWindow(); widget->setFocus(); key(widget, Qt::Key_O, Qt::ControlModifier);
    openDialog.stop();
    if (!opened || !waitFor([&] { return controller->getDocument() && controller->getDocument()->getCatalog()->getPageCount() == 2 && select->isChecked(); }))
        return fail("Ctrl+O did not open the two-page PDF in default text-selection mode");
    checkpoint("Reader phase: page navigation, zoom and advanced view menu");
    auto* pageNumber = window.findChild<QSpinBox*>(QStringLiteral("pageNumberSpinBox"));
    proxy->goToPage(0);
    if (!pageNumber || !waitFor([&] { return pageNumber->value() == 1; })) return fail("Native page number widget missing");
    const auto firstView = proxy->getSnapshot();
    if (!firstView.getPageSnapshot(0)) return fail("First page not visible before navigation");
    const qreal initialTop = firstView.getPageSnapshot(0)->rect.top();
    click(action("actionGoToNextPage"));
    if (!waitFor([&] { const auto view = proxy->getSnapshot(); const auto* page = view.getPageSnapshot(0); return !page || page->rect.top() < initialTop; })) return fail("Next page toolbar action failed");
    click(action("actionGoToPreviousPage"));
    if (!waitFor([&] { return pageNumber->value() == 1; })) return fail("Previous page toolbar action failed");
    pageNumber->setValue(2); QMetaObject::invokeMethod(pageNumber,"editingFinished",Qt::DirectConnection);
    if (!waitFor([&] { const auto pages = pdfWidget->getDrawWidget()->getCurrentPages(); return !pages.empty() && pages.front() == 1; })) return fail("Native page entry did not go to page two");
    pageNumber->setValue(1); QMetaObject::invokeMethod(pageNumber,"editingFinished",Qt::DirectConnection);
    if (!waitFor([&] { const auto pages = pdfWidget->getDrawWidget()->getCurrentPages(); return !pages.empty() && pages.front() == 0; })) return fail("Native page entry did not return to page one");
    const double zoom = proxy->getZoom();
    click(action("actionZoom_Out"));
    if (proxy->getZoom() >= zoom) return fail("Zoom out toolbar action failed");
    const double smaller = proxy->getZoom();
    click(action("actionZoom_In"));
    if (proxy->getZoom() <= smaller) return fail("Zoom in toolbar action failed");
    action("actionPageLayoutTwoPages")->trigger();
    if (proxy->getPageLayout() != pdf::PageLayout::TwoPagesLeft && proxy->getPageLayout() != pdf::PageLayout::TwoPagesRight)
        return fail("Advanced two-page menu action disconnected");
    action("actionPageLayoutContinuous")->trigger();
    proxy->goToPage(0); proxy->zoom(1.0);
    if (proxy->getPageLayout() != pdf::PageLayout::OneColumn) return fail("Continuous menu action disconnected");

    checkpoint("Reader phase: Ctrl+F native PDF text search");
    widget->setFocus(); key(widget, Qt::Key_F, Qt::ControlModifier);
    QPointer<QDialog> findDialog;
    for (auto* dialog : window.findChildren<QDialog*>())
        if (dialog->isVisible() && dialog->inherits("pdf::PDFFindTextToolDialog")) findDialog = dialog;
    if (!findDialog) return fail("Ctrl+F did not open native PDF search");
    auto* findText = findDialog->findChild<QLineEdit*>();
    if (!findText) return fail("Native search input missing");
    findText->setText(QStringLiteral("smoke"));
    QMetaObject::invokeMethod(findText, "editingFinished", Qt::DirectConnection);
    if (!waitFor([&] { return findDialog && findDialog->windowTitle().contains(QStringLiteral("1/1")); })) return fail("Native PDF search did not find synthetic text");
    findDialog->reject();
    if (!select->isChecked()) select->trigger();
    checkpoint("Reader phase: native print dialog open and cancel");
    if (!QPrinterInfo::availablePrinters().isEmpty()) {
        bool sawPrint = false;
        QTimer cancelPrint;
        QObject::connect(&cancelPrint,&QTimer::timeout,&window,[&] {
            if (auto* dialog = qobject_cast<QPrintDialog*>(QApplication::activeModalWidget())) {
                sawPrint = true; dialog->reject();
            }
        });
        cancelPrint.start(50); key(widget,Qt::Key_P,Qt::ControlModifier); cancelPrint.stop();
        if (!sawPrint) return fail("Ctrl+P did not open native print dialog");
    } else {
        qInfo("[NOTICE] No system printer on runner: physical print dialog not exercised; native action retained.");
    }

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

    auto points = [&](QPoint& first, QPoint& last, QPoint& notePosition) {
        const auto snapshot = proxy->getSnapshot();
        const auto* page = snapshot.getPageSnapshot(0);
        if (!page) return false;
        first = page->pageToDeviceMatrix.map(pageStart).toPoint();
        last = page->pageToDeviceMatrix.map(pageEnd).toPoint();
        notePosition = page->pageToDeviceMatrix.map(QPointF(150,195)).toPoint();
        return widget->rect().contains(first) && widget->rect().contains(last) && widget->rect().contains(notePosition);
    };
    QPoint start, end, notePoint;
    if (!waitFor([&] { return points(start,end,notePoint); })) return fail("Synthetic text points outside viewport");
    checkpoint("Reader phase: native drag selection and Ctrl+C");
    mouse(widget,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    const QString selectedText = controller->getToolManager()->getSelectTextTool()->getSelectedText();
    if (!selectedText.contains(QStringLiteral("LUNA"))) return fail("Native text drag selection failed");
    QApplication::clipboard()->clear(); widget->setFocus(); key(widget, Qt::Key_C, Qt::ControlModifier);
    if (!waitFor([&] { return QApplication::clipboard()->text() == selectedText; })) return fail("Ctrl+C did not copy selected PDF text");
    auto chooseColor = [&](QColor color) {
        pdf::PDFAnnotationStyleWidget* style = nullptr;
        for (auto* candidate : window.findChildren<pdf::PDFAnnotationStyleWidget*>())
            if (candidate->isVisible()) style = candidate;
        if (!style) return false;
        auto* button = style->findChild<QPushButton*>();
        if (!button) return false;
        bool chosen = false;
        QTimer choose;
        QObject::connect(&choose, &QTimer::timeout, &window, [&] {
            if (auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
                chosen = true; dialog->setCurrentColor(color); dialog->accept();
            }
        });
        choose.start(50); button->click(); choose.stop();
        return chosen && style->getStyle().strokeColor == color;
    };
    // The native highlighter selects text by dragging in its own tool mode.
    // Zoom out temporarily so the synthetic line fits the hosted desktop.
    proxy->zoom(1.0);
    checkpoint("Editor phase: native highlight drag");
    click(highlight);
    if (!highlight->isChecked() || !controller->getToolManager()->getActiveTool())
        return fail("Native highlighter action did not activate its tool");
    if (!chooseColor(Qt::green)) return fail("Native highlighter color chooser failed");
    if (!waitFor([&] { return points(start,end,notePoint); })) return fail("Synthetic annotation points outside viewport");
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
    if (!waitFor([&] { return select->isChecked(); })) return fail("Highlighter did not return to text selection");

    checkpoint("Editor phase: native sticky note input");
    click(note);
    QString noteText = QStringLiteral("LUNA synthetic note round trip");
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
    if (!waitFor([&] { return select->isChecked(); })) return fail("Sticky note did not return to text selection");
    auto annotationPoint = [&](pdf::AnnotationType type) {
        const auto snapshot = proxy->getSnapshot();
        const auto* page = snapshot.getPageSnapshot(0);
        if (page) for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations) {
            if (item.annotation->getType() != type) continue;
            QRectF rectangle = item.annotation->getRectangle();
            const auto matrix = pdfWidget->getAnnotationManager()->prepareTransformations(page->pageToDeviceMatrix,
                pdfWidget, item.annotation->getEffectiveFlags(), controller->getDocument()->getCatalog()->getPage(0), rectangle);
            return matrix.mapRect(rectangle).center().toPoint();
        }
        return QPoint(-1,-1);
    };
    checkpoint("Reader phase: single-click native note popup and edit");
    bool sawNote = false, editedNote = false;
    const QString revised = QStringLiteral("LUNA synthetic edited note");
    QTimer editNote;
    QObject::connect(&editNote, &QTimer::timeout, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        if (auto* edit = dialog->findChild<QPushButton*>(QStringLiteral("LunaEditNote"))) {
            sawNote = true; edit->click(); return;
        }
        if (dialog->inherits("pdf::PDFEditObjectDialog")) {
            for (auto* text : dialog->findChildren<QTextBrowser*>()) if (text->toPlainText() == noteText && !text->isReadOnly() && text->isEnabled()) {
                text->setPlainText(revised); editedNote = true; dialog->accept(); return;
            }
        }
    });
    editNote.start(50);
    const QPoint noteClick = annotationPoint(pdf::AnnotationType::Text);
    mouse(widget,QEvent::MouseButtonPress,noteClick,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,noteClick,Qt::LeftButton,Qt::NoButton);
    editNote.stop();
    if (!sawNote || !editedNote) return fail("Single-click native note view/edit failed");
    noteText = revised;
    bool cancelledExit = false;
    QTimer cancelExit;
    QObject::connect(&cancelExit, &QTimer::timeout, &window, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            if (auto* cancel = box->button(QMessageBox::Cancel)) { cancelledExit = true; cancel->click(); }
    });
    checkpoint("Reader phase: unsaved window close confirmation");
    cancelExit.start(50); const bool closed = window.close(); cancelExit.stop();
    if (!cancelledExit || closed || !window.isVisible()) return fail("Unsaved close confirmation not preserved");

    checkpoint("Editor phase: native Save As dialog");
    if (!waitFor([&] { return saveAs->isEnabled() && window.isEnabled(); }))
        return fail("Native Save As action remained unavailable");
    bool choseCopy = false;
    QString selectionError;
    QTimer chooseFile;
    QObject::connect(&chooseFile, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            choseCopy = selectSyntheticCopy(*dialog, saved, selectionError);
            if (!choseCopy) { dialog->reject(); return; }
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    chooseFile.start(50);
    saveAs->trigger();
    chooseFile.stop();
    if (!choseCopy) { error = selectionError.isEmpty() ? QStringLiteral("Native Save As did not open its dialog") : selectionError; return false; }
    if (!QFileInfo::exists(saved)) return fail("Native Save As returned without the expected PDF copy");
    if (readFile(saved).isEmpty()) return fail("Native Save As wrote an empty PDF copy");
    if (!sameExistingFile(controller->getOriginalFileName(), saved))
        return fail("Native Save As did not switch the current document to the copy");
    if (readFile(original) != originalBytes) return fail("Save As modified synthetic original");
    if (!select->isChecked()) select->trigger();
    if (!controller->askForSaveDocumentBeforeClose()) return fail("Saved document remains dirty");
    checkpoint("Editor phase: reopen annotated copy from disk");
    controller->closeDocument();
    controller->openDocument(saved);
    if (!waitFor([&] {
        const auto* document = controller->getDocument();
        return document && document->getCatalog()->getPageCount() == 2 && controller->canClose();
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

    checkpoint("Reader phase: annotation-only deletion and Ctrl+S persistence");
    proxy->zoom(1.0); proxy->goToPage(0);
    if (!waitFor([&] { return select->isChecked(); })) return fail("Default selection missing after reopen");
    auto erase = [&](pdf::AnnotationType type) {
        click(action("actionDeleteAnnotation"));
        const QPoint point = annotationPoint(type);
        mouse(widget,QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);
        mouse(widget,QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);
    };
    erase(pdf::AnnotationType::Text);
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Text) == 0 && select->isChecked(); })) return fail("Native note deletion failed");
    action("actionUndo")->trigger();
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Text) == 1; })) return fail("Native note-delete undo failed");
    erase(pdf::AnnotationType::Highlight);
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 0 && select->isChecked(); })) return fail("Native highlight deletion failed");
    selectAll->trigger();
    if (!controller->getToolManager()->getSelectTextTool()->getSelectedText().contains(QStringLiteral("LUNA synthetic smoke test")))
        return fail("Annotation eraser damaged PDF text");
    int saves = 0;
    if (!waitFor([&] { return action("actionSave")->isEnabled() && window.isEnabled(); })) return fail("Native save remained busy after erasing");
    const auto saveConnection = QObject::connect(action("actionSave"), &QAction::triggered, &window, [&] { ++saves; });
    widget->setFocus(); key(widget, Qt::Key_S, Qt::ControlModifier);
    QObject::disconnect(saveConnection);
    if (saves != 1) return fail("Ctrl+S shortcut did not invoke native save");
    controller->closeDocument(); controller->openDocument(saved);
    if (!waitFor([&] { return controller->getDocument() && select->isChecked(); }) || annotations(pdf::AnnotationType::Highlight) != 0 || annotations(pdf::AnnotationType::Text) != 1)
        return fail("Deleted annotation returned after save and reopen");
    // Restore a native highlight for the final reading-layout captures.
    proxy->zoom(1.0); proxy->goToPage(0); click(highlight);
    if (!chooseColor(Qt::yellow)) return fail("Native highlight color reset failed");
    if (!waitFor([&] { return points(start,end,notePoint); })) return fail("Final glyph points outside viewport");
    mouse(widget,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 1 && select->isChecked(); })) return fail("Native highlight recreation failed");
    if (!waitFor([&] { return action("actionSave")->isEnabled() && window.isEnabled(); })) return fail("Native final save remained busy");
    widget->setFocus(); key(widget,Qt::Key_S,Qt::ControlModifier);
    if (readFile(original) != originalBytes) return fail("Reader regressions modified original PDF");
    proxy->setPageLayout(pdf::PageLayout::OneColumn);
    proxy->zoom(2.0);
    return true;
}
}
