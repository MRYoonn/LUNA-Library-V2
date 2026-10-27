// SPDX-License-Identifier: MIT
// Architecture proof only: existing PDF4QT Editor, native QObject ownership.
#include <QtGlobal>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetutils.h"
#include "pdfwidgettool.h"
#include "pdfcompiler.h"
#include "pdftextlayout.h"
#include "pdfplugin.h"
#include <QApplication>
#include <QAction>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <functional>
#include <algorithm>
#include <stdexcept>

using Editor = pdfviewer::PDFEditorMainWindow;
using Controller = pdfviewer::PDFProgramController;

class Tabs final : public QMainWindow {
public:
    QTabWidget* tabs = new QTabWidget(this);
    Tabs() {
        setWindowTitle("LUNA native document sessions - architecture proof");
        resize(1100, 760); move(80, 60);
        setCentralWidget(tabs); tabs->setTabsClosable(true);
        connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });
        connect(tabs, &QTabWidget::currentChanged, this, [this](int i) {
            if (auto* editor = qobject_cast<Editor*>(tabs->widget(i)))
                editor->getProgramController()->getPdfWidget()->setFocus();
        });
    }
    Editor* open(const QString& path) {
        const QString identity = QFileInfo(path).canonicalFilePath();
        if (identity.isEmpty()) throw std::runtime_error("PDF not found");
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->widget(i)->property("lunaPath").toString().compare(identity, Qt::CaseInsensitive) == 0) {
                tabs->setCurrentIndex(i); return qobject_cast<Editor*>(tabs->widget(i));
            }
        // Parent before the first show; no HWND adoption or native reparenting.
        auto* editor = new Editor(tabs);
        editor->setWindowFlags(Qt::Widget);
        editor->setProperty("lunaPath", identity);
        editor->setAttribute(Qt::WA_DeleteOnClose, false);
        editor->installEventFilter(this);
        tabs->addTab(editor, QFileInfo(path).fileName());
        tabs->setCurrentWidget(editor);
        editor->getProgramController()->openDocument(path);
        // Core shortcuts are only eligible inside their own document subtree.
        // Plugin actions retain native context; hidden tab menus are not eligible.
        for (auto* action : editor->findChildren<QAction*>())
            if (!action->shortcut().isEmpty()) {
                editor->addAction(action);
                action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            }
        return editor;
    }
    bool closeTab(int index) {
        auto* editor = qobject_cast<Editor*>(tabs->widget(index));
        if (!editor) return false;
        tabs->setCurrentIndex(index);
        auto* controller = editor->getProgramController();
        if (!controller->canClose() || !controller->askForSaveDocumentBeforeClose()) return false;
        dispose(editor); return true;
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Close) {
            if (auto* editor = qobject_cast<Editor*>(watched)) {
                // File/Quit follows the same tab lifecycle; never hide an orphan.
                static_cast<QCloseEvent*>(event)->ignore();
                QPointer<Editor> guarded = editor;
                QTimer::singleShot(0, this, [this, guarded] {
                    if (guarded && tabs->indexOf(guarded) >= 0) closeTab(tabs->indexOf(guarded));
                });
                return true;
            }
        }
        return QMainWindow::eventFilter(watched, event);
    }
    void closeEvent(QCloseEvent* event) override {
        // Two phases: no tab is removed if any later document refuses.
        for (int i = 0; i < tabs->count(); ++i) {
            auto* editor = qobject_cast<Editor*>(tabs->widget(i));
            tabs->setCurrentIndex(i);
            if (!editor->getProgramController()->canClose() ||
                !editor->getProgramController()->askForSaveDocumentBeforeClose()) {
                event->ignore(); return;
            }
        }
        while (tabs->count()) dispose(qobject_cast<Editor*>(tabs->widget(0)));
        event->accept();
    }
private:
    void dispose(Editor* editor) {
        editor->getProgramController()->closeDocument();
        editor->removeEventFilter(this);
        tabs->removeTab(tabs->indexOf(editor));
        editor->deleteLater();
    }
};

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static bool waitFor(const std::function<bool()>& predicate, int timeout = 12000) {
    QElapsedTimer clock; clock.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate()) return true;
        QThread::msleep(10);
    } while (clock.elapsed() < timeout);
    return false;
}
static QByteArray bytes(const QString& path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
static QWidget* surface(Editor* editor) {
    return editor->getProgramController()->getPdfWidget()->getDrawWidget()->getWidget();
}
static QAction* action(Editor* editor, const char* name) {
    auto* a = editor->findChild<QAction*>(QString::fromLatin1(name));
    require(a != nullptr, "Native action missing"); return a;
}
static int highlights(Editor* editor) {
    int count = 0;
    for (const auto& item : editor->getProgramController()->getPdfWidget()->getAnnotationManager()->getPageAnnotations(0).annotations)
        if (item.annotation->getType() == pdf::AnnotationType::Highlight) ++count;
    return count;
}
static void mouse(QWidget* widget, QEvent::Type type, QPoint p, Qt::MouseButton button, Qt::MouseButtons held) {
    QMouseEvent event(type, QPointF(p), QPointF(widget->mapToGlobal(p)), button, held, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}
static void highlight(Editor* editor) {
    auto* proxy = editor->getProgramController()->getPdfWidget()->getDrawWidgetProxy();
    proxy->zoom(1.0); proxy->goToPage(0);
    QPoint start, end;
    require(waitFor([&] {
        pdf::PDFTextLayout layout = proxy->getTextLayoutCompiler()->getTextLayoutLazy(0);
        if (layout.getTextBlocks().empty() || layout.getTextBlocks().front().getLines().empty()) return false;
        const auto& chars = layout.getTextBlocks().front().getLines().front().getCharacters();
        const auto snapshot = proxy->getSnapshot(); const auto* page = snapshot.getPageSnapshot(0);
        if (chars.size() < 8 || !page) return false;
        start = page->pageToDeviceMatrix.map(chars[0].boundingBox.boundingRect().center() - QPointF(1, 0)).toPoint();
        end = page->pageToDeviceMatrix.map(chars[7].boundingBox.boundingRect().center() + QPointF(1, -2)).toPoint();
        return surface(editor)->rect().contains(start) && surface(editor)->rect().contains(end);
    }), "Highlight glyph geometry missing");
    const int before = highlights(editor);
    if (!action(editor, "actionCreateTextHighlight")->isChecked())
        action(editor, "actionCreateTextHighlight")->trigger();
    mouse(surface(editor), QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
    mouse(surface(editor), QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(surface(editor), QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(surface(editor), QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    require(waitFor([&] { return highlights(editor) == before + 1; }), "Native highlighter did not persist annotation in memory");
}
static bool answer(QMessageBox::StandardButton choice, const std::function<bool()>& operation, int& prompts) {
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            ++prompts; box->done(choice);
        }
    });
    timer.start(30); const bool result = operation(); timer.stop(); return result;
}
static void nativeFind(Tabs& host, Editor* editor, const QString& phrase, const QString& capture) {
#ifdef Q_OS_WIN
    host.tabs->setCurrentWidget(editor); host.raise(); host.activateWindow();
    SetForegroundWindow(reinterpret_cast<HWND>(host.winId()));
    surface(editor)->setFocus(); QCoreApplication::processEvents();
    INPUT input[4]{};
    const WORD keys[] = {VK_CONTROL, 'F', 'F', VK_CONTROL};
    for (int i = 0; i < 4; ++i) {
        input[i].type = INPUT_KEYBOARD; input[i].ki.wVk = keys[i];
        input[i].ki.dwFlags = i >= 2 ? KEYEVENTF_KEYUP : 0;
    }
    require(SendInput(4, input, sizeof(INPUT)) == 4, "Windows Ctrl+F injection failed");
    QPointer<QDialog> dialog;
    require(waitFor([&] {
        for (auto* candidate : editor->findChildren<QDialog*>())
            if (candidate->isVisible() && candidate->inherits("pdf::PDFFindTextToolDialog")) dialog = candidate;
        return !dialog.isNull();
    }), "Real Ctrl+F did not open this document search");
    auto* edit = dialog->findChild<QLineEdit*>();
    require(edit && edit->hasFocus(), "Search input did not receive focus");
    require(host.screen()->availableGeometry().intersects(dialog->frameGeometry()), "Search popup is off screen");
    // Real Unicode keyboard events, not direct setText or action triggering.
    for (QChar ch : phrase) {
        INPUT events[2]{};
        events[0].type = events[1].type = INPUT_KEYBOARD;
        events[0].ki.wScan = events[1].ki.wScan = ch.unicode();
        events[0].ki.dwFlags = KEYEVENTF_UNICODE;
        events[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        require(SendInput(2, events, sizeof(INPUT)) == 2, "Search text input failed");
    }
    INPUT enter[2]{};
    enter[0].type = enter[1].type = INPUT_KEYBOARD;
    enter[0].ki.wVk = enter[1].ki.wVk = VK_RETURN; enter[1].ki.dwFlags = KEYEVENTF_KEYUP;
    require(SendInput(2, enter, sizeof(INPUT)) == 2, "Search Enter failed");
    require(waitFor([&] { return dialog && edit->text() == phrase && dialog->windowTitle().contains("1/1"); }), "Search result is not document-specific 1/1");
    require(host.screen()->grabWindow(0).save(capture), "Search screenshot failed");
    dialog->reject();
    require(waitFor([&] { return dialog.isNull() || !dialog->isVisible(); }), "Search did not close");
#else
    Q_UNUSED(host); Q_UNUSED(editor); Q_UNUSED(phrase); Q_UNUSED(capture);
    throw std::runtime_error("Native Windows required for Ctrl+F acceptance");
#endif
}

static void verify(Tabs& host, Editor* a, Editor* b, const QString& report) {
    QJsonObject result{{"accepted", false}, {"physicalDpi", "unverified"},
        {"libraryIntegration", "not changed/not tested"}, {"allPluginFunctions", "unverified"}};
    auto checkpoint = [&](const char* phase) {
        result["phase"] = phase;
        QSaveFile file(report);
        if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(result).toJson()); file.commit(); }
    };
    try {
        checkpoint("load");
        auto ready = [](Editor* e) { auto* c = e->getProgramController(); return c->getDocument() && c->canClose() && e->isEnabled(); };
        require(waitFor([&] { return ready(a) && ready(b); }), "Documents did not finish loading");
        auto* ca = a->getProgramController(); auto* cb = b->getProgramController();
        require(ca != cb && ca->getDocument() != cb->getDocument() && ca->getToolManager() != cb->getToolManager(), "Document state aliases");
        require(a->window() == &host && b->window() == &host && !a->isWindow() && !b->isWindow(), "Not a single native main window");
        const auto pa = ca->findChildren<pdf::PDFPlugin*>(QString(), Qt::FindDirectChildrenOnly);
        const auto pb = cb->findChildren<pdf::PDFPlugin*>(QString(), Qt::FindDirectChildrenOnly);
        require(pa.size() == 9 && pb.size() == 9, "Nine bundled plugin sessions not loaded in each document");
        for (auto* p : pa) {
            require(p->property("lunaSessionPlugin").toBool() && !pb.contains(p), "Shared plugin instance");
            for (auto* q : pb) {
                const auto otherActions = q->getActions();
                for (auto* x : p->getActions())
                    if (x) require(std::find(otherActions.begin(), otherActions.end(), x) == otherActions.end(), "Shared plugin action");
            }
        }
        result["distinctControllersAndNinePlugins"] = true;
        auto* docA = ca->getDocument(); auto* docB = cb->getDocument();
        ca->getPdfWidget()->getDrawWidgetProxy()->zoom(1.0);
        cb->getPdfWidget()->getDrawWidgetProxy()->zoom(1.5);
        for (int i = 0; i < 40; ++i) {
            host.tabs->setCurrentIndex(i % 2); QCoreApplication::processEvents();
            require(ca->getDocument() == docA && cb->getDocument() == docB, "Switch reopened/lost a document");
        }
        require(qAbs(ca->getPdfWidget()->getDrawWidgetProxy()->getZoom() - 1.0) < .001 &&
            qAbs(cb->getPdfWidget()->getDrawWidgetProxy()->getZoom() - 1.5) < .001, "Zoom state crossed tabs");
        result["fortySwitches"] = true;
        checkpoint("native Ctrl+F");
        nativeFind(host, a, "alpha", report + ".search-a.png");
        nativeFind(host, b, "bravo", report + ".search-b.png");
        result["win32CtrlFAndTypedSearchBoth"] = true;
        checkpoint("plugin-only dirty state");
        host.tabs->setCurrentWidget(a);
        action(a, "editortool_activateAction")->trigger();
        require(ca->hasUnsavedChanges() && !cb->hasUnsavedChanges() &&
            !action(b, "editortool_activateAction")->isChecked(), "Plugin editing state crossed documents");
        int pluginPrompts = 0;
        require(!answer(QMessageBox::Cancel, [&] { return host.closeTab(0); }, pluginPrompts) &&
            pluginPrompts == 1 && host.tabs->count() == 2, "Plugin-only pending edit bypassed close prompt");
        pluginPrompts = 0;
        answer(QMessageBox::No, [&] { action(a, "editortool_activateAction")->trigger(); return true; }, pluginPrompts);
        require(pluginPrompts == 1 && !ca->hasUnsavedChanges(), "Plugin discard did not finish its session");
        result["pluginOnlyPendingEditCancel"] = true;
        checkpoint("highlight isolation and save");
        host.tabs->setCurrentWidget(a); highlight(a);
        require(highlights(b) == 0 && !cb->hasUnsavedChanges(), "A highlight affected B");
        host.tabs->setCurrentWidget(b); highlight(b); highlight(b);
        require(highlights(a) == 1 && highlights(b) == 2, "Annotation counts crossed tabs");
        const QString pathA = ca->getOriginalFileName(), pathB = cb->getOriginalFileName();
#ifdef Q_OS_WIN
        const auto beforeA = bytes(pathA);
        HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(pathA.utf16()), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "Could not lock disposable A for failed-save check");
        int saveErrors = 0;
        const bool savedLocked = answer(QMessageBox::Ok, [&] { return ca->trySave(); }, saveErrors);
        CloseHandle(locked);
        require(!savedLocked && saveErrors > 0 && ca->hasUnsavedChanges() &&
            bytes(pathA) == beforeA && host.tabs->count() == 2, "Failed save lost dirty state or data");
        result["failedSaveRetainsDirtyDocument"] = true;
#endif
        const auto beforeB = bytes(pathB);
        ca->performSave(); require(!ca->hasUnsavedChanges() && bytes(pathB) == beforeB && cb->hasUnsavedChanges(), "A save changed B");
        cb->performSave(); require(!cb->hasUnsavedChanges(), "B save failed");
        ca->closeDocument(); ca->openDocument(pathA);
        cb->closeDocument(); cb->openDocument(pathB);
        require(waitFor([&] { return ready(a) && ready(b); }), "Reopen failed");
        require(highlights(a) == 1 && highlights(b) == 2, "Persisted highlights did not survive reopening");
        result["highlightSaveReopenBoth"] = true;
        checkpoint("cancel and two-phase close");
        host.tabs->setCurrentWidget(a); highlight(a);
        int prompts = 0;
        require(!answer(QMessageBox::Cancel, [&] { return host.closeTab(0); }, prompts) && prompts == 1 && host.tabs->count() == 2 && ca->hasUnsavedChanges(), "Tab Cancel lost document");
        prompts = 0;
        require(!answer(QMessageBox::Cancel, [&] { return host.close(); }, prompts) && host.tabs->count() == 2, "Window Cancel removed tabs");
        const auto savedA = bytes(pathA);
        prompts = 0;
        QPointer<Editor> closedA = a;
        require(answer(QMessageBox::No, [&] { return host.closeTab(0); }, prompts) && prompts == 1 && host.tabs->count() == 1 && bytes(pathA) == savedA, "Discard altered file or removed wrong tab");
        require(waitFor([&] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); return closedA.isNull(); }), "Closed session not destroyed");
        nativeFind(host, b, "bravo", report + ".survivor.png");
        highlight(b); prompts = 0;
        require(answer(QMessageBox::Yes, [&] { return host.closeTab(0); }, prompts) && prompts == 1 && host.tabs->count() == 0, "Save-and-close failed");
        auto* reopened = host.open(pathB);
        require(waitFor([&] { return ready(reopened); }) && highlights(reopened) == 3, "Save-and-close did not persist B");
        result["cancelDiscardSaveCloseAndSurvivor"] = true;
        require(host.screen()->grabWindow(0).save(report + ".final.png"), "Final screenshot failed");
        require(host.closeTab(0), "Final clean close failed");
        result["accepted"] = true; checkpoint("passed"); QCoreApplication::exit(0);
    } catch (const std::exception& error) {
        result["error"] = QString::fromUtf8(error.what()); checkpoint("failed");
        host.screen()->grabWindow(0).save(report + ".failure.png"); QCoreApplication::exit(1);
    }
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setProperty("lunaNativeSessions", true);
    QCoreApplication::setOrganizationName("LUNA-Native-Proof");
    QCoreApplication::setApplicationName("LUNA-Native-Proof");
    const auto args = app.arguments();
    // This executable is synthetic-proof only: output directory must be supplied.
    if (args.size() != 4) return 2;
    QDir().mkpath(QFileInfo(args[3]).absolutePath() + "/settings");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QFileInfo(args[3]).absolutePath() + "/settings");
    pdf::PDFWidgetUtils::setDarkTheme(true, false);
    Tabs host; host.show();
    auto* a = host.open(args[1]); auto* b = host.open(args[2]);
    QTimer::singleShot(0, &host, [&] { verify(host, a, b, args[3]); });
    return app.exec();
}
