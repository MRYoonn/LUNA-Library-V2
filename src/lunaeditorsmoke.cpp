// SPDX-License-Identifier: MIT
// Drives native Editor actions and widget events; no alternate PDF renderer.
#include "lunaeditorsmoke.h"
#include "lunasavedialog.h"
#include "lunalibrary.h"
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
#include "pdfpage.h"
#include "pdfundoredomanager.h"

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
#include <QPainterPath>
#include <QTimer>
#include <QClipboard>
#include <QColorDialog>
#include <QMessageBox>
#include <QMenu>
#include <QSettings>
#include <QStringList>
#include <cmath>
#include <QAbstractButton>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinterInfo>
#include <QScreen>
#include <QIcon>
#include <functional>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <atomic>
#include <chrono>
#include <thread>
#endif

namespace luna {
namespace {
#ifdef Q_OS_WIN
struct PrintCancellation {
    DWORD process;
    std::atomic_bool seen{false};
};

BOOL CALLBACK cancelNativePrintWindow(HWND window, LPARAM parameter) {
    auto& state = *reinterpret_cast<PrintCancellation*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != state.process || !IsWindowVisible(window)) return TRUE;
    wchar_t windowClass[32] = {}, title[128] = {};
    GetClassNameW(window, windowClass, 32); GetWindowTextW(window, title, 128);
    // The hosted Windows image is English. Never send Print/OK or touch
    // another process; only cancel our own standard Windows Print dialog.
    if (lstrcmpW(windowClass, L"#32770") || lstrcmpW(title, L"Print")) return TRUE;
    HWND cancel = GetDlgItem(window, IDCANCEL);
    if (cancel && IsWindowEnabled(cancel) && PostMessageW(window, WM_COMMAND,
        MAKEWPARAM(IDCANCEL, BN_CLICKED), reinterpret_cast<LPARAM>(cancel))) {
        state.seen = true;
        return FALSE;
    }
    return TRUE;
}
#endif

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

bool verifyResearchRoundTrip(pdfviewer::PDFEditorMainWindow& window,
                             const QString& reportPath, QString& error) {
    if (qEnvironmentVariableIsSet("LUNA_PUBLIC_BIBLIOGRAPHY_PROBE"))
        return verifyBibliographyUi(window, error, reportPath);
    auto fail = [&](const char* message) { error = QString::fromLatin1(message); return false; };
    auto* controller = window.getProgramController();
    auto* pdfWidget = controller->getPdfWidget();
    auto* proxy = pdfWidget->getDrawWidgetProxy();
    auto* widget = pdfWidget->getDrawWidget()->getWidget();
    const QString original = controller->getOriginalFileName();
    const QByteArray originalBytes = readFile(original);
    const QString saved = reportPath + QStringLiteral(".annotated.pdf");
    if (originalBytes.isEmpty() || QFileInfo::exists(saved) || !controller->getDocument() ||
        controller->getDocument()->getCatalog()->getPageCount() != 5) return fail("Unexpected public research fixture");
    auto action = [&](const char* name) { return window.findChild<QAction*>(QString::fromLatin1(name)); };
    auto* select = action("actionSelectText");
    auto* highlight = action("actionCreateTextHighlight");
    auto* saveAs = action("actionSave_As");
    if (!select || !highlight || !saveAs) return fail("Native research actions missing");
    if (auto* focus = action("LunaFocusMode"); focus && !focus->isChecked()) focus->trigger();
    proxy->zoom(1.0); proxy->setPageLayout(pdf::PageLayout::OneColumn);
    auto highlights = [&] {
        int count = 0;
        for (const auto& annotation : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations)
            if (annotation.annotation->getType() == pdf::AnnotationType::Highlight) ++count;
        return count;
    };
    const int initialHighlights = highlights();
    for (int sample = 0; sample < 3; ++sample) {
        const int pageIndex = sample == 2 ? 1 : 0;
        proxy->goToPage(pageIndex);
        QPoint start, end;
        QString expected;
        if (!waitFor([&] {
            if (!window.isEnabled() || !select->isEnabled()) return false;
            pdf::PDFTextLayout layout = proxy->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
            const auto snapshot = proxy->getSnapshot();
            const auto* page = snapshot.getPageSnapshot(pageIndex);
            if (!page) return false;
            const auto box = controller->getDocument()->getCatalog()->getPage(pageIndex)->getMediaBox();
            for (const auto& block : layout.getTextBlocks()) for (const auto& line : block.getLines()) {
                const auto& chars = line.getCharacters();
                if (chars.size() < 12 || chars.front().fontSize > 14 || chars.front().fontSize < 6) continue;
                const auto first = chars.front().boundingBox.boundingRect().center();
                const auto last = chars.back().boundingBox.boundingRect().center();
                QString text;
                for (const auto& character : chars) text += character.character;
                if (sample == 0 && (last.x() >= box.width() / 2 || first.y() > box.height() * .75)) continue;
                if (sample == 1 && (first.x() < box.width() / 2 || first.y() > box.height() * .75)) continue;
                // The native layout stores painted glyphs. PDF word spacing
                // need not contain a literal space character in that stream.
                if (sample == 2 && !QString(text).remove(QChar(' ')).contains(QStringLiteral("Figure1"))) continue;
                const QPointF from = first - QPointF(1,0);
                const QPointF to = chars[8].boundingBox.boundingRect().center() + QPointF(1,-2);
                start = page->pageToDeviceMatrix.map(from).toPoint();
                end = page->pageToDeviceMatrix.map(to).toPoint();
                if (!widget->rect().contains(start) || !widget->rect().contains(end)) {
                    if (sample == 2) {
                        proxy->goToPageAndEnsureVisible(pageIndex, QRectF(from, to).normalized().adjusted(-10,-10,10,10));
                        return false;
                    }
                    continue;
                }
                const auto inverse = page->pageToDeviceMatrix.inverted();
                expected = layout.getTextFromSelection(layout.createTextSelection(pageIndex,
                    inverse.map(QPointF(start)), inverse.map(QPointF(end))), pageIndex).simplified();
                if (expected.size() >= 4) return true;
            }
            return false;
        })) { error = QStringLiteral("Public paper column/caption glyphs unavailable: sample %1, page %2").arg(sample).arg(pageIndex); return false; }
        if (!select->isChecked()) select->trigger();
        mouse(widget,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
        mouse(widget,QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
        mouse(widget,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
        if (controller->getToolManager()->getSelectTextTool()->getSelectedText().simplified() != expected)
            return fail("Public paper native column/caption drag selected unexpected text");
        QApplication::clipboard()->clear(); key(widget,Qt::Key_C,Qt::ControlModifier);
        if (!waitFor([&] { return QApplication::clipboard()->text().simplified() == expected; }))
            return fail("Public paper selected text did not copy");
        if (sample < 2) {
            highlight->trigger();
            mouse(widget,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
            mouse(widget,QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
            mouse(widget,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
            if (!waitFor([&] { return highlights() == initialHighlights + sample + 1 && select->isChecked(); }))
                return fail("Public paper column highlight failed");
        }
    }
    if (!waitFor([&] { return window.isEnabled() && saveAs->isEnabled(); })) return fail("Public paper save remained busy");
    bool chosen = false;
    QTimer choose;
    QObject::connect(&choose, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            chosen = selectSyntheticCopy(*dialog, saved, error);
            QMetaObject::invokeMethod(dialog, chosen ? "accept" : "reject", Qt::DirectConnection);
        }
    });
    choose.start(30); saveAs->trigger(); choose.stop();
    if (!chosen || !QFileInfo::exists(saved) || readFile(original) != originalBytes)
        return fail("Public paper Save As did not preserve original and produce a copy");
    controller->closeDocument(); controller->openDocument(saved);
    if (!waitFor([&] { return controller->getDocument() && window.isEnabled() && controller->getDocument()->getCatalog()->getPageCount() == 5; }) ||
        highlights() != initialHighlights + 2) return fail("Public paper annotations did not survive native reopen");
    proxy->zoom(.7); proxy->goToPage(0);
    QCoreApplication::processEvents();
    if (!window.grab().save(reportPath + QStringLiteral(".png"))) return fail("Public paper capture failed");
    return true;
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
    // Document creation completes before the queued LUNA reading defaults.
    // A small two-page fixture can fit at the temporary fit zoom, so native
    // NavigateNextPage intentionally does nothing while its scrollbar is hidden.
    // Exercise the button only once the actual requested 200% view is usable.
    auto* vertical = pdfWidget->getVerticalScrollbar();
    auto* nextPage = action("actionGoToNextPage");
    if (!waitFor([&] { return window.isEnabled() && nextPage && nextPage->isEnabled() &&
        qAbs(proxy->getZoom() - 2.0) < 0.001 && proxy->getPageLayout() == pdf::PageLayout::OneColumn &&
        vertical->isVisible() && vertical->maximum() > vertical->minimum() && vertical->pageStep() > 0; }))
        return fail("Reopened PDF did not settle into enabled continuous 200% reading view");
    checkpoint("Reader phase: vertical middle-click autoscroll");
    // Middle-click should use the LUNA up/down marker, not PDF4QT's
    // four-direction hand-pan. Escape must cancel without altering PDF data.
    const QPoint middle = widget->rect().center();
    mouse(widget, QEvent::MouseButtonPress, middle, Qt::MiddleButton, Qt::MiddleButton);
    auto* autoscrollMarker = window.findChild<QWidget*>(QStringLiteral("LunaVerticalAutoscrollMarker"));
    if (!autoscrollMarker || !autoscrollMarker->isVisible())
        return fail("Middle click did not show the vertical up/down autoscroll marker");
    mouse(widget, QEvent::MouseButtonRelease, middle, Qt::MiddleButton, Qt::NoButton);
    key(widget, Qt::Key_Escape, Qt::NoModifier);
    if (autoscrollMarker->isVisible()) return fail("Escape did not cancel vertical autoscroll");
    checkpoint("Reader phase: page navigation, zoom and advanced view menu");
    auto* pageNumber = window.findChild<QSpinBox*>(QStringLiteral("pageNumberSpinBox"));
    proxy->goToPage(0);
    if (!pageNumber || !waitFor([&] { return pageNumber->value() == 1; })) return fail("Native page number widget missing");
    const auto firstView = proxy->getSnapshot();
    if (!firstView.getPageSnapshot(0)) return fail("First page not visible before navigation");
    const qreal initialTop = firstView.getPageSnapshot(0)->rect.top();
    const int initialScroll = vertical->value();
    int nextTriggers = 0;
    const auto nextConnection = QObject::connect(nextPage, &QAction::triggered, &window, [&] { ++nextTriggers; });
    click(nextPage);
    QObject::disconnect(nextConnection);
    if (nextTriggers != 1) return fail("Enabled next-page toolbar button did not trigger its native QAction");
    if (!waitFor([&] { const auto view = proxy->getSnapshot(); const auto* page = view.getPageSnapshot(0);
        return vertical->value() > initialScroll && (!page || page->rect.top() < initialTop); })) {
        error = QStringLiteral("Next-page action triggered but viewport did not advance: scroll %1 -> %2, max %3, step %4, zoom %5")
            .arg(initialScroll).arg(vertical->value()).arg(vertical->maximum()).arg(vertical->pageStep()).arg(proxy->getZoom());
        return false;
    }
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

    // A colored marker is the tool itself: click color, drag glyphs, release
    // to write a real PDF highlight without an extra apply button.
    checkpoint("Reader phase: eight one-click highlight pens and recolor");
    auto* lunaErase = action("LunaEraseSelected");
    auto* lunaTextBox = action("LunaFreeTextBox");
    if (!lunaErase || !lunaTextBox || !lunaTextBox->isCheckable() ||
        action("LunaHighlightSelected"))
        return fail("Separate highlight action still present or LUNA reader tools missing");
    auto* palette = window.findChild<QMenu*>(QStringLiteral("LunaHighlightPalette"));
    if (!palette) return fail("LUNA highlight palette missing from native Tools menu");
    const auto choices = palette->actions();
    const QStringList expectedColors = {
        QStringLiteral("#FFE049"), QStringLiteral("#A3E6A4"),
        QStringLiteral("#FFB3CF"), QStringLiteral("#9FCAFF"),
        QStringLiteral("#FFC58A"), QStringLiteral("#C7B2FF"),
        QStringLiteral("#A3E8DF"), QStringLiteral("#CED4DE")
    };
    if (choices.size() < expectedColors.size() + 2)
        return fail("Eight highlight colors and custom palette entry missing");
    auto* readerBar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    if (!readerBar) return fail("Native reader toolbar not found");
    const auto toolbarActions = readerBar->actions();
    const int printPos = toolbarActions.indexOf(action("actionPrint"));
    const int erasePos = toolbarActions.indexOf(lunaErase);
    if (printPos < 0 || erasePos != printPos + 2 ||
        !toolbarActions[printPos + 1]->isSeparator())
        return fail("Print / eraser / eight colored marker order regressed");
    for (int i = 1; i <= 8; ++i) {
        auto* marker = action(QStringLiteral("LunaHighlightSwatch%1").arg(i).toLatin1().constData());
        if (!marker || !marker->isCheckable() ||
            toolbarActions.indexOf(marker) != erasePos + i)
            return fail("Eight colored marker icons are missing or out of order");
    }
    const QString settingKey = QStringLiteral("annotations/highlightColor");
    const QVariant previousColor = QSettings().value(settingKey);
    for (qsizetype i = 0; i < expectedColors.size(); ++i) {
        choices[i]->trigger();
        if (!select->isChecked() ||
            QSettings().value(settingKey).toString().compare(expectedColors[i], Qt::CaseInsensitive))
            return fail("Color menu did not arm native text selection and persist");
        auto* marker = action(QStringLiteral("LunaHighlightSwatch%1").arg(i + 1).toLatin1().constData());
        if (!marker || !marker->isChecked())
            return fail("Active color marker indicator did not synchronize");
    }
    // Toolbar activation must also arm text selection and persist the color.
    action("LunaHighlightSwatch5")->trigger();
    if (!select->isChecked() ||
        QSettings().value(settingKey).toString().compare(QStringLiteral("#FFC58A"), Qt::CaseInsensitive) ||
        !action("LunaHighlightSwatch5")->isChecked())
        return fail("Inline highlighter icon failed to arm text drag");

    choices[1]->trigger(); // green
    mouse(widget,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 1; }))
        return fail("Colored marker did not auto-highlight dragged text");
    bool greenWritten = false;
    for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations) {
        if (item.annotation->getType() != pdf::AnnotationType::Highlight) continue;
        const auto components = item.annotation->getColor();
        greenWritten = components.size() >= 3 &&
            std::abs(components[0] - 163.0 / 255.0) < 0.02 &&
            std::abs(components[1] - 230.0 / 255.0) < 0.02 &&
            std::abs(components[2] - 164.0 / 255.0) < 0.02;
    }
    if (!greenWritten) return fail("Drag highlight color not written to native PDF annotation");
    QRectF initialGreenBounds;
    for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations) {
        if (item.annotation->getType() != pdf::AnnotationType::Highlight) continue;
        const auto* ann = dynamic_cast<const pdf::PDFHighlightAnnotation*>(item.annotation.data());
        if (ann) initialGreenBounds = ann->getHiglightArea().getPath().boundingRect().normalized();
    }
    if (!initialGreenBounds.isValid()) return fail("Initial native highlight had no saved geometry");
    auto originalGreenRestored = [&]() {
        if (annotations(pdf::AnnotationType::Highlight) != 1) return false;
        for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations) {
            if (item.annotation->getType() != pdf::AnnotationType::Highlight) continue;
            const auto* ann = dynamic_cast<const pdf::PDFHighlightAnnotation*>(item.annotation.data());
            if (!ann) return false;
            const QRectF now = ann->getHiglightArea().getPath().boundingRect().normalized();
            const auto rgb = ann->getColor();
            return rgb.size() >= 3 && qAbs(rgb[1] - 230.0/255.0) < 0.02 &&
                qAbs(now.left() - initialGreenBounds.left()) < 0.12 &&
                qAbs(now.right() - initialGreenBounds.right()) < 0.12 &&
                qAbs(now.top() - initialGreenBounds.top()) < 0.12 &&
                qAbs(now.bottom() - initialGreenBounds.bottom()) < 0.12;
        }
        return false;
    };
    // Same-color drag must not stack transparency.
    mouse(widget,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    QCoreApplication::processEvents();
    if (annotations(pdf::AnnotationType::Highlight) != 1)
        return fail("Repeated same-color marker duplicated existing annotation");

    // Partially repaint the center, retaining the first and last green words
    // from the original note. Then undo once to restore exact old color.
    checkpoint("Reader phase: recolor only selected overlap, preserve outer segments");
    if (characters.size() < 13) return fail("Synthetic text too short for recolor regression");
    const auto snapshot = proxy->getSnapshot();
    const auto* selectedPage = snapshot.getPageSnapshot(0);
    if (!selectedPage) return fail("Recolor test page not visible");
    const QPoint middleStart = selectedPage->pageToDeviceMatrix.map(
        characters[2].boundingBox.boundingRect().center() - QPointF(1, 0)).toPoint();
    const QPoint middleEnd = selectedPage->pageToDeviceMatrix.map(
        characters[5].boundingBox.boundingRect().center() + QPointF(1, -2)).toPoint();
    if (!widget->rect().contains(middleStart) || !widget->rect().contains(middleEnd))
        return fail("Recolor regression glyphs outside viewport");
    choices[2]->trigger(); // pink
    mouse(widget,QEvent::MouseButtonPress,middleStart,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseMove,middleEnd,Qt::NoButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,middleEnd,Qt::LeftButton,Qt::NoButton);
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 2; }))
        return fail("Partly overlapping recolor failed to retain old and new colors");
    bool oldColorStillPresent = false, pinkPresent = false;
    QPainterPath oldPath, newPath;
    for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations) {
        if (item.annotation->getType() != pdf::AnnotationType::Highlight) continue;
        const auto* ann = dynamic_cast<const pdf::PDFHighlightAnnotation*>(item.annotation.data());
        if (!ann) return fail("Recolor geometry missing");
        const auto rgb = ann->getColor();
        if (rgb.size() < 3) continue;
        if (qAbs(rgb[1] - 230.0/255.0) < 0.02) {
            oldColorStillPresent = true; oldPath = ann->getHiglightArea().getPath();
        }
        if (qAbs(rgb[0] - 255.0/255.0) < 0.02 && qAbs(rgb[1] - 179.0/255.0) < 0.02) {
            pinkPresent = true; newPath = ann->getHiglightArea().getPath();
        }
    }
    if (!oldColorStillPresent || !pinkPresent || oldPath.intersects(newPath))
        return fail("Recolor lost unselected old color or stacked alpha on new color");
    action("actionUndo")->trigger();
    if (!waitFor(originalGreenRestored))
        return fail("Recolor Undo changed original saved highlight color or QuadPoints");
    // Dragging existing and previously uncolored text must repaint the full
    // selected range in new color, not silently skip the overlap.
    const QPoint extendedEnd = selectedPage->pageToDeviceMatrix.map(
        characters[12].boundingBox.boundingRect().center() + QPointF(1, -2)).toPoint();
    if (!widget->rect().contains(extendedEnd)) return fail("Expanded recolor endpoint offscreen");
    choices[2]->trigger();
    mouse(widget,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseMove,extendedEnd,Qt::NoButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,extendedEnd,Qt::LeftButton,Qt::NoButton);
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 1; }))
        return fail("Old plus new text was not recolored to the selected marker");
    action("actionUndo")->trigger();
    if (!waitFor(originalGreenRestored))
        return fail("Expanded recolor Undo changed original highlight geometry");

    // Escape returns to plain selection without auto-highlighting, so the
    // existing select-then-erase button remains available.
    key(widget, Qt::Key_Escape, Qt::NoModifier);
    if (!lunaErase->isCheckable()) return fail("Eraser is not an armed drag tool");
    click(lunaErase);
    if (!lunaErase->isChecked() || !select->isChecked())
        return fail("Eraser did not arm native text selection");
    // A selected substring must remove only that substring, not the entire
    // multi-word green highlight annotation.
    checkpoint("Reader phase: eraser removes only chosen glyph range");
    mouse(widget,QEvent::MouseButtonPress,middleStart,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseMove,middleEnd,Qt::NoButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,middleEnd,Qt::LeftButton,Qt::NoButton);
    if (!waitFor([&] {
        if (annotations(pdf::AnnotationType::Highlight) != 1) return false;
        for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations)
            if (item.annotation->getType() == pdf::AnnotationType::Highlight) {
                const auto* ann = dynamic_cast<const pdf::PDFHighlightAnnotation*>(item.annotation.data());
                return ann && ann->getHiglightArea().getQuadrilaterals().size() >= 2;
            }
        return false;
    })) return fail("Eraser removed nonselected words from same highlight");
    action("actionUndo")->trigger();
    if (!waitFor(originalGreenRestored))
        return fail("Partial eraser Undo did not restore original QuadPoints and color");
    // Earlier recolor/Undo operations can leave an annotation spanning
    // MORE glyphs than the first 0..7 test drag. Selecting only those first
    // glyphs must preserve the uncovered suffix, not delete it. For the
    // *whole-mark* erase check, derive the drag from the actual saved
    // QuadPoints rather than assuming an old synthetic click endpoint.
    QRectF markedBeforeErase;
    int firstMarkedGlyph = -1, lastMarkedGlyph = -1;
    bool originalGreen = false;
    for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations) {
        if (item.annotation->getType() != pdf::AnnotationType::Highlight) continue;
        const auto* ann = dynamic_cast<const pdf::PDFHighlightAnnotation*>(item.annotation.data());
        if (!ann) continue;
        markedBeforeErase = ann->getHiglightArea().getPath().boundingRect().normalized();
        const auto rgb = ann->getColor();
        originalGreen = rgb.size() >= 3 &&
            qAbs(rgb[0] - 163.0/255.0) < 0.02 &&
            qAbs(rgb[1] - 230.0/255.0) < 0.02 &&
            qAbs(rgb[2] - 164.0/255.0) < 0.02;
    }
    if (!originalGreen || !markedBeforeErase.isValid())
        return fail("Recolor/Undo did not restore original green highlight before erase");
    for (qsizetype i = 0; i < characters.size(); ++i) {
        const QRectF glyph = characters[i].boundingBox.boundingRect().normalized();
        const QRectF hit = glyph.intersected(markedBeforeErase);
        if (glyph.width() < 1 || glyph.height() < 1 ||
            hit.width() < glyph.width() * 0.45 ||
            hit.height() < glyph.height() * 0.40) continue;
        if (firstMarkedGlyph < 0) firstMarkedGlyph = int(i);
        lastMarkedGlyph = int(i);
    }
    if (firstMarkedGlyph < 0 || lastMarkedGlyph < 0 || firstMarkedGlyph >= lastMarkedGlyph)
        return fail("Could not find original saved highlight glyph range");
    const auto freshSnapshot = proxy->getSnapshot();
    const auto* pageForErase = freshSnapshot.getPageSnapshot(0);
    if (!pageForErase) return fail("Whole-annotation erase page not visible");
    const QPoint wholeStart = pageForErase->pageToDeviceMatrix.map(
        characters[firstMarkedGlyph].boundingBox.boundingRect().center() - QPointF(1, 0)).toPoint();
    const QPoint wholeEnd = pageForErase->pageToDeviceMatrix.map(
        characters[lastMarkedGlyph].boundingBox.boundingRect().center() + QPointF(1, -2)).toPoint();
    if (!widget->rect().contains(wholeStart) || !widget->rect().contains(wholeEnd))
        return fail("Whole-annotation erase glyph range outside viewport");
    mouse(widget,QEvent::MouseButtonPress,wholeStart,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseMove,wholeEnd,Qt::NoButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,wholeEnd,Qt::LeftButton,Qt::NoButton);
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 0; })) {
        // Report real geometry and tool mode in the exact CI failure rather
        // than repeating an ambiguous error across repeated Windows builds.
        QStringList residues;
        for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations) {
            if (item.annotation->getType() != pdf::AnnotationType::Highlight) continue;
            const auto* ann = dynamic_cast<const pdf::PDFHighlightAnnotation*>(item.annotation.data());
            if (!ann) continue;
            for (const auto& quad : ann->getHiglightArea().getQuadrilaterals()) {
                QPolygonF poly;
                for (const auto& point : quad) poly << point;
                const QRectF remaining = poly.boundingRect().normalized();
                residues << QStringLiteral("[%1,%2 to %3,%4]").arg(remaining.left(), 0, 'f', 2)
                    .arg(remaining.top(), 0, 'f', 2).arg(remaining.right(), 0, 'f', 2)
                    .arg(remaining.bottom(), 0, 'f', 2);
            }
        }
        error = QStringLiteral("Click-then-drag eraser did not remove previous highlight: count=%1 armed=%2 select=%3 selectedChars=%4 remnants=%5 firstGlyph=%6 lastGlyph=%7 originalBounds=%8,%9..%10,%11")
            .arg(annotations(pdf::AnnotationType::Highlight))
            .arg(lunaErase->isChecked()).arg(select->isChecked())
            .arg(controller->getToolManager()->getSelectTextTool()->getSelectedText().size())
            .arg(residues.join(QStringLiteral("; ")))
            .arg(firstMarkedGlyph).arg(lastMarkedGlyph)
            .arg(markedBeforeErase.left(), 0, 'f', 2).arg(markedBeforeErase.top(), 0, 'f', 2)
            .arg(markedBeforeErase.right(), 0, 'f', 2).arg(markedBeforeErase.bottom(), 0, 'f', 2);
        return false;
    }
    action("actionUndo")->trigger();
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 1; }))
        return fail("Selection-first eraser did not preserve native undo");
    action("actionUndo")->trigger();
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 0; }))
        return fail("Initial colored marker did not preserve native undo");
    if (previousColor.isValid()) QSettings().setValue(settingKey, previousColor);
    else QSettings().remove(settingKey);

    checkpoint("Reader phase: transparent on-page FreeText note");
    bool freeTextEditor = false;
    QTimer fillFreeText;
    QObject::connect(&fillFreeText, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            if (dialog->objectName() == QStringLiteral("LunaFreeTextEditor")) {
                if (auto* input = dialog->findChild<QPlainTextEdit*>(QStringLiteral("LunaFreeTextContents"))) {
                    freeTextEditor = true;
                    input->setPlainText(QStringLiteral("LUNA transparent on-page note"));
                    dialog->accept();
                }
            }
        }
    });
    fillFreeText.start(30);
    click(lunaTextBox);
    if (!lunaTextBox->isChecked()) return fail("LUNA on-page note mode did not activate");
    mouse(widget,QEvent::MouseButtonPress,notePoint,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,notePoint,Qt::LeftButton,Qt::NoButton);
    fillFreeText.stop();
    if (!freeTextEditor || !waitFor([&] { return annotations(pdf::AnnotationType::FreeText) == 1; }) ||
        lunaTextBox->isChecked() || !select->isChecked())
        return fail("LUNA transparent FreeText note missing or did not restore selection");
    // A single click near the edge of a 300x220 PDF used to create the
    // default 180x68 box partially beyond the sheet. Check actual PDF geometry,
    // not just an in-memory annotation count.
    const QRectF pageCrop = controller->getDocument()->getCatalog()->getPage(0)->getCropBox().normalized();
    bool defaultBoxWithinPage = false;
    for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations) {
        if (item.annotation->getType() == pdf::AnnotationType::FreeText) {
            const QRectF box = item.annotation->getRectangle().normalized();
            defaultBoxWithinPage = box.width() >= 8 && box.height() >= 8 && pageCrop.contains(box);
        }
    }
    if (!defaultBoxWithinPage) return fail("LUNA default FreeText escaped PDF crop bounds");
    action("actionUndo")->trigger();
    if (!waitFor([&] { return annotations(pdf::AnnotationType::FreeText) == 0; }))
        return fail("LUNA FreeText note did not participate in native undo");
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
        if (!dialog) dialog = qobject_cast<QDialog*>(QApplication::activePopupWidget());
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
    if (!verifyCancelledLibraryMove(window, error)) return false;
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
    click(action("actionDeleteAnnotation"));
    const QPoint cancelPoint = annotationPoint(pdf::AnnotationType::Text);
    mouse(widget,QEvent::MouseButtonPress,cancelPoint,Qt::LeftButton,Qt::LeftButton);
    key(widget,Qt::Key_Escape,Qt::NoModifier);
    mouse(widget,QEvent::MouseButtonRelease,cancelPoint,Qt::LeftButton,Qt::NoButton);
    if (annotations(pdf::AnnotationType::Text) != 1 || !select->isChecked()) return fail("Escape did not cancel annotation deletion before mouse release");
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
#ifdef Q_OS_WIN
    checkpoint("Reader phase: locked-file save failure preserves disk and unsaved edits");
    const QByteArray beforeFailedSave = readFile(saved);
    auto* undoState = controller->findChild<pdfviewer::PDFUndoRedoManager*>();
    if (!undoState || undoState->isCurrentSaved()) return fail("Save-failure fixture has no unsaved edits");
    const HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(saved.utf16()), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (locked == INVALID_HANDLE_VALUE) return fail("Cannot lock synthetic save-failure target");
    bool sawSaveError = false;
    QTimer dismissError;
    QObject::connect(&dismissError, &QTimer::timeout, &window, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            sawSaveError = box->icon() == QMessageBox::Critical;
            box->accept();
        }
    });
    dismissError.start(30); widget->setFocus(); key(widget,Qt::Key_S,Qt::ControlModifier); dismissError.stop();
    CloseHandle(locked);
    if (!sawSaveError || readFile(saved) != beforeFailedSave || undoState->isCurrentSaved() ||
        annotations(pdf::AnnotationType::Highlight) != 0 || annotations(pdf::AnnotationType::Text) != 1)
        return fail("Native failed save lost original bytes or unsaved annotation state");
#endif
    const auto saveConnection = QObject::connect(action("actionSave"), &QAction::triggered, &window, [&] { ++saves; });
    widget->setFocus(); key(widget, Qt::Key_S, Qt::ControlModifier);
    QObject::disconnect(saveConnection);
    if (saves != 1) return fail("Ctrl+S shortcut did not invoke native save");
    controller->closeDocument(); controller->openDocument(saved);
    if (!waitFor([&] { return controller->getDocument() && select->isChecked(); }) || annotations(pdf::AnnotationType::Highlight) != 0 || annotations(pdf::AnnotationType::Text) != 1)
        return fail("Deleted annotation returned after save and reopen");
    // Restore a native highlight for the final reading-layout captures.
    // A document and the checked selection action can precede completion of
    // asynchronous text indexing. The native controller disables the window
    // while busy; clicking its disabled tool button is correctly ignored.
    if (!waitFor([&] { return window.isEnabled() && highlight->isEnabled(); }))
        return fail("Native reader remained busy after final reopen");
    proxy->zoom(1.0); proxy->goToPage(0); click(highlight);
    if (!highlight->isChecked() || !controller->getToolManager()->getActiveTool())
        return fail("Native highlighter did not activate after final reopen");
    if (!chooseColor(Qt::yellow)) return fail("Native highlight color reset failed");
    if (!waitFor([&] { return points(start,end,notePoint); })) return fail("Final glyph points outside viewport");
    mouse(widget,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 1 && select->isChecked(); })) return fail("Native highlight recreation failed");
    // Persist the new on-page text box through the actual PDF writer, not just
    // Undo. Test its original text and its independent annotation type after a
    // fresh document open. The existing icon-style sticky note must also remain.
    checkpoint("Reader phase: FreeText save and reopen");
    const QString persistedFreeText = QStringLiteral("LUNA persistent transparent text box");
    bool enteredPersistedFreeText = false;
    QTimer persistedTextInput;
    QObject::connect(&persistedTextInput, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            if (dialog->objectName() == QStringLiteral("LunaFreeTextEditor")) {
                if (auto* input = dialog->findChild<QPlainTextEdit*>(QStringLiteral("LunaFreeTextContents"))) {
                    enteredPersistedFreeText = true;
                    input->setPlainText(persistedFreeText);
                    dialog->accept();
                }
            }
        }
    });
    persistedTextInput.start(30);
    if (!waitFor([&] { return lunaTextBox->isEnabled(); }))
        return fail("LUNA FreeText tool unavailable after final highlight");
    click(lunaTextBox);
    if (!lunaTextBox->isChecked()) return fail("LUNA persistent FreeText entry did not start");
    if (!waitFor([&] { return points(start,end,notePoint); }))
        return fail("LUNA persistent FreeText coordinates outside viewport");
    // Test an explicitly dragged, fully visible FreeText box. The earlier
    // single-click/Undo case already covers the default 180x68 PDF-point size;
    // a click near the edge can place that box's *center* outside the viewport,
    // making a subsequent double-click impossible without scrolling.
    const auto visibleSnapshot = proxy->getSnapshot();
    const auto* visiblePage = visibleSnapshot.getPageSnapshot(0);
    if (!visiblePage) return fail("FreeText drag has no visible page snapshot");
    const QRectF visiblePageRect = visiblePage->rect.intersected(QRectF(widget->rect()));
    if (visiblePageRect.width() < 90 || visiblePageRect.height() < 64)
        return fail("FreeText drag has insufficient visible page area");
    const QPoint boxCenter = visiblePageRect.center().toPoint();
    const QPoint boxStart = boxCenter - QPoint(34, 20);
    const QPoint boxEnd = boxCenter + QPoint(34, 20);
    QPointF localBoxStart, localBoxEnd;
    if (!widget->rect().contains(boxStart) || !widget->rect().contains(boxEnd) ||
        proxy->getPageUnderPoint(boxStart, &localBoxStart) != 0 ||
        proxy->getPageUnderPoint(boxEnd, &localBoxEnd) != 0 ||
        QRectF(localBoxStart, localBoxEnd).normalized().width() < 8 ||
        QRectF(localBoxStart, localBoxEnd).normalized().height() < 8)
        return fail("FreeText drag rectangle is not fully inside the visible PDF page");
    mouse(widget,QEvent::MouseButtonPress,boxStart,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseMove,boxEnd,Qt::NoButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,boxEnd,Qt::LeftButton,Qt::NoButton);
    persistedTextInput.stop();
    if (!enteredPersistedFreeText || !waitFor([&] { return annotations(pdf::AnnotationType::FreeText) == 1; }))
        return fail("LUNA persistent FreeText was not added to the document");

    checkpoint("Reader phase: PowerPoint-style FreeText select and drag");
    auto freeTextBox = [&] {
        for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations)
            if (item.annotation->getType() == pdf::AnnotationType::FreeText)
                return item.annotation->getRectangle().normalized();
        return QRectF();
    };
    const QRectF beforeMove = freeTextBox();
    const QPoint moveFrom = annotationPoint(pdf::AnnotationType::FreeText);
    const QPoint moveTo = moveFrom + QPoint(18, 12);
    if (!beforeMove.isValid() || !widget->rect().contains(moveFrom) ||
        !widget->rect().contains(moveTo))
        return fail("FreeText move test point outside PDF viewport");
    mouse(widget,QEvent::MouseButtonPress,moveFrom,Qt::LeftButton,Qt::LeftButton);
    checkpoint("Reader phase: FreeText move pressed");
    mouse(widget,QEvent::MouseMove,moveTo,Qt::NoButton,Qt::LeftButton);
    checkpoint("Reader phase: FreeText move previewed");
    mouse(widget,QEvent::MouseButtonRelease,moveTo,Qt::LeftButton,Qt::NoButton);
    checkpoint("Reader phase: FreeText move released");
    if (!waitFor([&] {
        const QRectF moved = freeTextBox();
        return moved.isValid() &&
            (moved.topLeft() - beforeMove.topLeft()).manhattanLength() > 2 &&
            qAbs(moved.width() - beforeMove.width()) < 0.5 &&
            qAbs(moved.height() - beforeMove.height()) < 0.5;
    })) return fail("FreeText could not be dragged using text-selection mode");

    checkpoint("Reader phase: PowerPoint-style FreeText resize handles");
    const QRectF beforeResize = freeTextBox();
    const auto gripSnapshot = proxy->getSnapshot();
    const auto* gripPage = gripSnapshot.getPageSnapshot(0);
    if (!gripPage) return fail("FreeText resize page is not visible");
    const QPoint grip = gripPage->pageToDeviceMatrix.mapRect(beforeResize).normalized().bottomRight().toPoint();
    const QPoint gripEnd = grip + QPoint(12, 9);
    if (!widget->rect().contains(grip) || !widget->rect().contains(gripEnd))
        return fail("FreeText native resize handle offscreen");
    mouse(widget,QEvent::MouseButtonPress,grip,Qt::LeftButton,Qt::LeftButton);
    checkpoint("Reader phase: FreeText resize pressed");
    mouse(widget,QEvent::MouseMove,gripEnd,Qt::NoButton,Qt::LeftButton);
    checkpoint("Reader phase: FreeText resize previewed");
    mouse(widget,QEvent::MouseButtonRelease,gripEnd,Qt::LeftButton,Qt::NoButton);
    checkpoint("Reader phase: FreeText resize released");
    if (!waitFor([&] {
        const QRectF resized = freeTextBox();
        return resized.isValid() &&
            (qAbs(resized.width() - beforeResize.width()) > 1 ||
             qAbs(resized.height() - beforeResize.height()) > 1);
    })) return fail("FreeText corner handle did not resize the native PDF text box");

    // Editing an existing on-page FreeText must not require raw PDF-object
    // editing, nor should a double-click consume the native resize handles.
    checkpoint("Reader phase: FreeText double-click edit");
    const QString updatedFreeText = QStringLiteral("LUNA edited transparent text box");
    bool editedFreeText = false;
    QTimer reviseFreeText;
    QObject::connect(&reviseFreeText, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            if (dialog->objectName() == QStringLiteral("LunaFreeTextEditDialog")) {
                if (auto* input = dialog->findChild<QPlainTextEdit*>(QStringLiteral("LunaFreeTextEditContents"))) {
                    if (input->toPlainText() == persistedFreeText) {
                        editedFreeText = true;
                        input->setPlainText(updatedFreeText);
                        dialog->accept();
                    }
                }
            }
        }
    });
    const QPoint editPosition = annotationPoint(pdf::AnnotationType::FreeText);
    if (!widget->rect().contains(editPosition)) {
        error = QStringLiteral("LUNA FreeText edit target offscreen: actual=(%1,%2), expected drag center=(%3,%4), viewport=%5x%6")
            .arg(editPosition.x()).arg(editPosition.y()).arg(boxCenter.x()).arg(boxCenter.y())
            .arg(widget->width()).arg(widget->height());
        return false;
    }
    reviseFreeText.start(30);
    mouse(widget, QEvent::MouseButtonDblClick, editPosition, Qt::LeftButton, Qt::LeftButton);
    reviseFreeText.stop();
    if (!editedFreeText || !waitFor([&] {
        for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations)
            if (item.annotation->getType() == pdf::AnnotationType::FreeText)
                return item.annotation->getContents() == updatedFreeText;
        return false;
    })) return fail("LUNA FreeText double-click edit did not update the annotation");

    if (!waitFor([&] { return action("actionSave")->isEnabled() && window.isEnabled(); }))
        return fail("Native final save remained busy");
    widget->setFocus(); key(widget,Qt::Key_S,Qt::ControlModifier);
    if (readFile(original) != originalBytes) return fail("Reader regressions modified original PDF");
    controller->closeDocument(); controller->openDocument(saved);
    if (!waitFor([&] { return controller->getDocument() && select->isChecked() && window.isEnabled(); }))
        return fail("LUNA FreeText persisted document did not reopen");
    if (annotations(pdf::AnnotationType::Highlight) != 1 ||
        annotations(pdf::AnnotationType::Text) != 1 ||
        annotations(pdf::AnnotationType::FreeText) != 1)
        return fail("LUNA FreeText save/reopen changed native annotation counts");
    bool persistedContentsFound = false;
    for (const auto& item : pdfWidget->getAnnotationManager()->getPageAnnotations(0).annotations) {
        if (item.annotation->getType() == pdf::AnnotationType::FreeText)
            persistedContentsFound = item.annotation->getContents() == updatedFreeText;
    }
    if (!persistedContentsFound || readFile(original) != originalBytes)
        return fail("LUNA FreeText text or original PDF bytes changed on disk");
    checkpoint("Reader phase: native print dialog open and cancel");
    if (!QPrinterInfo::availablePrinters().isEmpty()) {
#ifdef Q_OS_WIN
        // QPrintDialog::exec uses PrintDlgEx directly on Windows, bypassing
        // QDialog::exec and QApplication::activeModalWidget(). Its native
        // modal loop also need not dispatch a Qt timer. Cancel from Win32.
        PrintCancellation cancellation{GetCurrentProcessId()};
        std::atomic_bool finished{false};
        std::thread cancelPrint([&] {
            for (int i = 0; i < 200 && !finished; ++i) {
                EnumWindows(cancelNativePrintWindow, reinterpret_cast<LPARAM>(&cancellation));
                if (cancellation.seen) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        key(widget,Qt::Key_P,Qt::ControlModifier);
        finished = true; cancelPrint.join();
        const bool sawPrint = cancellation.seen;
#else
        bool sawPrint = false;
        QTimer cancelPrint;
        QObject::connect(&cancelPrint,&QTimer::timeout,&window,[&] {
            if (auto* dialog = qobject_cast<QPrintDialog*>(QApplication::activeModalWidget())) {
                sawPrint = true; dialog->reject();
            }
        });
        cancelPrint.start(50); key(widget,Qt::Key_P,Qt::ControlModifier); cancelPrint.stop();
#endif
        if (!sawPrint) return fail("Ctrl+P did not open native print dialog");
        qInfo("[PASS] Native print dialog opened and cancelled without a print job.");
    } else {
        qInfo("[NOTICE] No system printer on runner: physical print dialog not exercised; native action retained.");
    }

    proxy->setPageLayout(pdf::PageLayout::OneColumn);
    proxy->zoom(2.0);
#ifdef Q_OS_WIN
    // Verify the live native window exposes both shell icon sizes. Also compare
    // Qt's applied icon pixels with the official resource, not just a filename.
    const HWND nativeWindow = reinterpret_cast<HWND>(window.winId());
    auto iconHandle = [&](WPARAM kind, int fallback) {
        auto handle = SendMessageW(nativeWindow, WM_GETICON, kind, 0);
        return handle ? handle : GetClassLongPtrW(nativeWindow, fallback);
    };
    if (!iconHandle(ICON_BIG, GCLP_HICON) || !iconHandle(ICON_SMALL, GCLP_HICONSM))
        return fail("Native window/titlebar shell icon handles missing");
    const QIcon official(QStringLiteral(":/luna/LUNA-Moon.ico"));
    if (official.isNull() || window.windowIcon().pixmap(32,32).toImage() != official.pixmap(32,32).toImage())
        return fail("Live window icon differs from official moon resource");
    // Actual isolated hosted Windows desktop, before the later off-screen Qt
    // layout captures. This includes shell/taskbar context for visual review.
    window.activateWindow();
    QCoreApplication::processEvents();
    if (!window.screen() || !window.screen()->grabWindow(0).save(reportPath + QStringLiteral(".desktop.png")))
        return fail("Could not capture actual Windows desktop for shell icon review");
#endif
    return true;
}
}
