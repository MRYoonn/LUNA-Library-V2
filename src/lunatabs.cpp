// SPDX-License-Identifier: MIT
// Native Editor sessions: ownership and close flow validated by Astra's proof.
#include "lunatabs.h"
#include "lunalibrary.h"
#include "lunadocumentclaim.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include <QApplication>
#include <QAction>
#include <QCloseEvent>
#include <QDialog>
#include <QDockWidget>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QUrl>
#include <QKeySequence>
#include <QScopedValueRollback>
#include <QSettings>
#include <QPointer>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>
#include <vector>

#include <QAbstractButton>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMouseEvent>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QThread>
#include "pdfwidgetannotation.h"
#include "pdfcompiler.h"
#include "pdftextlayout.h"
#include <stdexcept>
#include <algorithm>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace luna {
using Editor = pdfviewer::PDFEditorMainWindow;
struct Session {
    QPointer<Editor> editor;
    QString identity;
    bool loading = false, dispatchingOpen = false;
    std::unique_ptr<DocumentClaim> claim, saveClaim;
};
class NativeTabs final : public QMainWindow {
public:
    NativeTabs() {
        setObjectName(QStringLiteral("LunaNativeTabs"));
        setWindowTitle(QStringLiteral("LUNA Library"));
        setWindowIcon(QApplication::windowIcon());
        tabs_ = new QTabWidget(this); tabs_->setObjectName(QStringLiteral("LunaPdfTabs"));
        tabs_->setTabsClosable(true); tabs_->setDocumentMode(true); tabs_->setMovable(true);
        tabs_->setElideMode(Qt::ElideMiddle);
        auto* initial = createEditor();
        installLibraryShell(*this, *initial);
        auto* frame = centralWidget();
        auto* corner = findChild<QWidget*>(QStringLiteral("LunaTabCorner"));
        if (corner) { frame->layout()->removeWidget(corner); tabs_->setCornerWidget(corner); }
        static_cast<QVBoxLayout*>(frame->layout())->addWidget(tabs_);
        open_ = new QAction(QStringLiteral("PDF 열기…"), this); open_->setObjectName(QStringLiteral("LunaOpenDocuments"));
        open_->setShortcut(QKeySequence(QStringLiteral("Ctrl+O"))); addAction(open_);
        open_->setToolTip(QStringLiteral("여러 PDF를 탭으로 열기 (Ctrl+O)"));
        connect(open_, &QAction::triggered, this, [this] { chooseFiles(); });
        if (corner) {
            auto* plus = new QToolButton(corner); plus->setText(QStringLiteral("＋"));
            plus->setAccessibleName(QStringLiteral("새 PDF 탭 열기")); plus->setToolTip(QStringLiteral("새 PDF 탭 열기")); plus->setMinimumSize(30, 30);
            static_cast<QHBoxLayout*>(corner->layout())->addWidget(plus);
            connect(plus, &QToolButton::clicked, open_, &QAction::trigger);
            corner->setProperty("lunaHasOpenButton", true); corner->show();
        }
        quit_ = new QAction(QStringLiteral("끝내기"), this); quit_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q"))); addAction(quit_);
        connect(quit_, &QAction::triggered, this, &QWidget::close);
        for (const auto& route : {qMakePair("actionSave", "Ctrl+S"), qMakePair("actionSave_As", "Ctrl+Shift+S"),
            qMakePair("actionFind", "Ctrl+F"), qMakePair("actionPrint", "Ctrl+P")}) {
            auto* key = new QAction(this); key->setShortcut(QKeySequence(QString::fromLatin1(route.second))); addAction(key);
            connect(key, &QAction::triggered, this, [this, name = QString::fromLatin1(route.first)] {
                if (auto* e = current()) if (auto* action = e->findChild<QAction*>(name); action && action->isEnabled()) action->trigger();
            });
        }
        auto* close = new QAction(this); close->setShortcut(QKeySequence(QStringLiteral("Ctrl+W"))); addAction(close);
        connect(close, &QAction::triggered, this, [this] { closeTab(tabs_->currentIndex()); });
        finishEditor(initial);
        setLibraryDocumentRouter(*this, [this](const QString& path) { open(path); },
            [this](const QString& path) {
                auto* existing = find(path);
                if (existing && existing->editor != current()) { activate(existing->editor); return true; }
                return false;
            }, [this](Editor* editor, const QString&, const QString& destination) {
                auto* s = session(editor); if (!s) return false;
                auto claim = makeClaim(editor);
                if (claim->acquire(destination) != DocumentClaim::Result::Acquired) {
                    s->claim.reset(); s->identity.clear(); s->loading = false;
                    QMessageBox::warning(this, QStringLiteral("이동된 PDF 열기"), QStringLiteral("파일은 이동했지만 다른 LUNA 창에서 열고 있습니다. 해당 창에서 확인해 주세요."));
                    return false;
                }
                s->claim = std::move(claim); s->identity = DocumentClaim::identity(destination); s->loading = true;
                return true;
            });
        connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { activateCurrent(); });
        connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
        setAcceptDrops(true); qApp->installEventFilter(this);
        connect(&poll_, &QTimer::timeout, this, [this] { synchronize(); }); poll_.start(100);
        activateCurrent();
    }
    Editor* current() const { return qobject_cast<Editor*>(tabs_->currentWidget()); }
    Editor* open(const QString& path) {
        const QFileInfo file(path);
        if (!file.isFile() || file.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive)) {
            QMessageBox::warning(this, QStringLiteral("PDF 열기 실패"), QStringLiteral("읽을 수 있는 PDF 파일을 선택해 주세요.")); return nullptr;
        }
        if (auto* existing = find(path)) { activate(existing->editor); return existing->editor; }
        Editor* editor = nullptr;
        for (const auto& s : sessions_) if (!s->loading && s->identity.isEmpty() && !s->editor->getProgramController()->getDocument()) { editor = s->editor; break; }
        bool created = false;
        if (!editor) { editor = createEditor(); finishEditor(editor); created = true; }
        auto* s = session(editor); auto claim = makeClaim(editor);
        const auto acquired = claim->acquire(path);
        if (acquired != DocumentClaim::Result::Acquired) {
            if (created) dispose(editor);
            if (acquired == DocumentClaim::Result::Error) QMessageBox::warning(this, QStringLiteral("PDF 열기 실패"), QStringLiteral("문서 사용권을 확보하지 못했습니다. 파일은 변경하지 않았습니다."));
            return nullptr;
        }
        s->identity = DocumentClaim::identity(path); s->claim = std::move(claim); s->loading = true;
        { QScopedValueRollback<bool> dispatch(s->dispatchingOpen, true);
          editor->getProgramController()->openDocument(file.absoluteFilePath()); }
        activate(editor); updateTitle(editor);
        return editor;
    }
    bool closeTab(int index) {
        if (QApplication::activeModalWidget()) return false;
        auto* editor = qobject_cast<Editor*>(tabs_->widget(index)); if (!editor) return false;
        activate(editor); auto* controller = editor->getProgramController();
        if (!controller->canClose() || !controller->askForSaveDocumentBeforeClose()) return false;
        if (tabs_->count() == 1) { auto* empty = createEditor(); finishEditor(empty); activate(empty); }
        dispose(editor); return true;
    }
    int documentCount() const { int count = 0; for (const auto& s : sessions_) if (!s->identity.isEmpty()) ++count; return count; }
private:
    Session* session(Editor* e) const { for (const auto& s : sessions_) if (s->editor == e) return s.get(); return nullptr; }
    Session* find(const QString& path) const {
        const QString id = DocumentClaim::identity(path);
        for (const auto& s : sessions_) {
            auto* c = s->editor->getProgramController();
            if ((!id.isEmpty() && s->identity == id) ||
                (!id.isEmpty() && (c->getDocument() || s->loading) && DocumentClaim::identity(c->getOriginalFileName()) == id)) return s.get();
        }
        return nullptr;
    }
    std::unique_ptr<DocumentClaim> makeClaim(Editor* e) {
        QPointer<Editor> guarded(e);
        return std::make_unique<DocumentClaim>([this, guarded] { if (guarded) activate(guarded); });
    }
    Editor* createEditor() {
        auto* editor = new Editor(tabs_); editor->setWindowFlags(Qt::Widget);
        editor->setAttribute(Qt::WA_DeleteOnClose, false); editor->installEventFilter(this);
        auto s = std::make_unique<Session>(); s->editor = editor; sessions_.push_back(std::move(s));
        tabs_->addTab(editor, QApplication::windowIcon(), QStringLiteral("PDF 열기"));
        return editor;
    }
    void finishEditor(Editor* editor) {
        prepareReaderSession(*editor, *this);
        auto* controller = editor->getProgramController();
        auto* original = editor->findChild<QAction*>(QStringLiteral("actionOpen"));
        if (original) {
            original->setShortcut({}); if (open_->icon().isNull()) open_->setIcon(original->icon());
            for (auto* menu : editor->findChildren<QMenu*>()) {
                if (menu->actions().contains(original)) { menu->insertAction(original, open_); menu->removeAction(original); }
            }
            if (auto* bar = editor->findChild<QToolBar*>(QStringLiteral("mainToolBar"))) { bar->insertAction(original, open_); bar->removeAction(original); }
        }
        if (auto* quit = editor->findChild<QAction*>(QStringLiteral("actionQuit"))) {
            quit->setShortcut({});
            for (auto* menu : editor->findChildren<QMenu*>()) if (menu->actions().contains(quit)) { menu->insertAction(quit, quit_); menu->removeAction(quit); }
        }
        connect(controller, &pdfviewer::PDFProgramController::lunaOpenRequested, this, [this, editor](const QString& path, bool* handled) {
            auto* s = session(editor); if (!s) { *handled = true; return; }
            const QString identity = DocumentClaim::identity(path);
            if (s->dispatchingOpen) return;
            if (identity == s->identity && (editor->property("LunaMoveRestorePending").toBool() || !editor->getProgramController()->getDocument())) {
                s->loading = true; return;
            }
            *handled = true; open(path);
        }, Qt::DirectConnection);
        connect(controller, &pdfviewer::PDFProgramController::lunaDocumentLoadFinished, this, [this, editor](bool success) {
            auto* s = session(editor); if (!s || closing_) return;
            s->loading = false;
            if (!success) {
                s->claim.reset(); s->identity.clear();
                QTimer::singleShot(0, editor, [this, editor] {
                    auto* failed = session(editor);
                    if (!closing_ && failed && !failed->loading && failed->identity.isEmpty() && !editor->getProgramController()->getDocument() && tabs_->count() > 1) dispose(editor);
                });
            }
            updateTitle(editor);
        });
        for (const auto* name : {"actionSave", "actionSave_As", "actionFind", "actionPrint", "actionClose"})
            if (auto* action = editor->findChild<QAction*>(QString::fromLatin1(name))) action->setShortcut({});
        for (auto* action : editor->findChildren<QAction*>()) if (!action->shortcut().isEmpty()) {
            editor->addAction(action); action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        }
        connect(editor, &QWidget::windowTitleChanged, this, [this, editor] { QTimer::singleShot(0, editor, [this, editor] { updateTitle(editor); }); });
        connect(controller, &pdfviewer::PDFProgramController::lunaSavePathRequested, this, [this, editor](const QString& path, bool* allowed) {
            auto* s = session(editor); if (!s) { *allowed = false; return; }
            s->saveClaim.reset();
            const QString destination = DocumentClaim::identity(path);
            if (destination == s->identity) return;
            if (destination.isEmpty() || (find(path) && find(path) != s)) {
                *allowed = false;
                QMessageBox::warning(this, QStringLiteral("저장 경로 충돌"), QStringLiteral("다른 탭에서 열린 PDF 경로에는 덮어쓸 수 없습니다.")); return;
            }
            auto reservation = makeClaim(editor);
            if (reservation->acquire(path) != DocumentClaim::Result::Acquired) {
                *allowed = false; QMessageBox::warning(this, QStringLiteral("저장 경로 사용 중"), QStringLiteral("다른 LUNA 창에서 사용 중인 경로입니다.")); return;
            }
            s->saveClaim = std::move(reservation);
        }, Qt::DirectConnection);
        connect(controller, &pdfviewer::PDFProgramController::lunaSavePathFinished, this, [this, editor](const QString& path, bool success) {
            if (auto* s = session(editor)) {
                if (success) {
                    if (s->saveClaim) s->claim = std::move(s->saveClaim);
                    editor->setProperty("LunaPreserveReopenView", true);
                    s->identity = DocumentClaim::identity(path); updateTitle(editor);
                }
                s->saveClaim.reset();
            }
        }, Qt::DirectConnection);
    }
    void activate(Editor* editor) {
        if (!editor || closing_) return;
        if (auto* modal = QApplication::activeModalWidget()) { pendingActivation_ = editor; modal->raise(); modal->activateWindow(); return; }
        tabs_->setCurrentWidget(editor); if (isMinimized()) showNormal(); raise(); activateWindow(); activateCurrent(); }
    void activateCurrent() {
        auto* editor = current(); if (!editor || closing_) return;
        for (const auto& s : sessions_) if (s->editor != editor)
            for (auto* popup : s->editor->findChildren<QDialog*>())
                if (popup->isVisible() && popup->windowFlags().testFlag(Qt::Popup)) popup->reject();
        bindLibraryEditor(*this, *editor);
        menuBar()->clear(); for (auto* action : editor->menuBar()->actions()) menuBar()->addAction(action);
        editor->getProgramController()->getPdfWidget()->setFocus(); updateTitle(editor);
    }
    void updateTitle(Editor* editor) {
        auto* s = session(editor); if (!s || closing_) return;
        auto* c = editor->getProgramController(); const QString path = c->getOriginalFileName();
        const QString display = (c->getDocument() || s->loading) && !path.isEmpty() ? path : s->identity;
        QString text = display.isEmpty() ? QStringLiteral("PDF 열기") : QFileInfo(display).fileName();
        if (c->hasUnsavedChanges()) text += QStringLiteral(" *");
        if (s->loading && !c->getDocument()) text += QStringLiteral(" · 여는 중");
        const int index = tabs_->indexOf(editor); if (index >= 0) {
            tabs_->setTabText(index, text); tabs_->setTabToolTip(index, display);
            for (auto side : {QTabBar::LeftSide, QTabBar::RightSide})
                if (auto* button = qobject_cast<QAbstractButton*>(tabs_->tabBar()->tabButton(index, side)))
                    button->setEnabled(!s->identity.isEmpty() && !s->loading && c->canClose() && editor->isEnabled());
        }
        if (editor == current()) setWindowTitle(QStringLiteral("LUNA Library") + (display.isEmpty() ? QString() : QStringLiteral(" · ") + text));
    }
    void synchronize() {
        if (pendingActivation_ && !QApplication::activeModalWidget()) {
            QPointer<Editor> next = pendingActivation_; pendingActivation_.clear(); if (session(next)) activate(next);
        }
        for (const auto& s : sessions_) {
            auto* c = s->editor->getProgramController();
            if (c->getDocument() && c->canClose() && s->editor->isEnabled()) recordLibraryDocument(*this, *s->editor);
            updateTitle(s->editor);
        }
    }
    void dispose(Editor* editor) {
        // Rebind the one library before deleting its previous model/controller.
        if (current() == editor) for (const auto& s : sessions_) if (s->editor != editor) { activate(s->editor); break; }
        editor->getProgramController()->closeDocument(); editor->removeEventFilter(this);
        tabs_->removeTab(tabs_->indexOf(editor));
        for (auto it = sessions_.begin(); it != sessions_.end(); ++it) if ((*it)->editor == editor) { sessions_.erase(it); break; }
        editor->deleteLater();
    }
    void chooseFiles() {
        const QString currentPath = current() ? current()->getProgramController()->getOriginalFileName() : QString();
        const QString start = currentPath.isEmpty() ? QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library")).value(QStringLiteral("library/root")).toString() : QFileInfo(currentPath).absolutePath();
        const auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("PDF 열기"), start, QStringLiteral("PDF (*.pdf *.PDF)"));
        for (const auto& path : paths) open(path);
    }
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::Close) if (auto* editor = qobject_cast<Editor*>(object)) {
            static_cast<QCloseEvent*>(event)->ignore(); QPointer<Editor> guarded(editor);
            QTimer::singleShot(0, this, [this, guarded] { if (guarded) closeTab(tabs_->indexOf(guarded)); }); return true;
        }
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::Drop) {
            auto* widget = qobject_cast<QWidget*>(object); if (!widget || widget->window() != this) return false;
            auto* drop = static_cast<QDropEvent*>(event); const auto urls = drop->mimeData()->urls(); if (urls.isEmpty()) return false;
            for (const auto& url : urls) if (!url.isLocalFile() || QFileInfo(url.toLocalFile()).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive)) return false;
            if (event->type() == QEvent::Drop) for (const auto& url : urls) open(url.toLocalFile());
            drop->acceptProposedAction(); return true;
        }
        return QMainWindow::eventFilter(object, event);
    }
    void closeEvent(QCloseEvent* event) override {
        if (QApplication::activeModalWidget()) { event->ignore(); return; }
        for (const auto& s : sessions_) {
            activate(s->editor); auto* c = s->editor->getProgramController();
            if (!c->canClose() || !c->askForSaveDocumentBeforeClose()) { event->ignore(); return; }
        }
        closing_ = true; setProperty("LunaClosing", true); poll_.stop();
        qApp->removeEventFilter(this);
        setLibraryDocumentRouter(*this, {}, {}, {});
        for (const auto& s : sessions_) {
            s->editor->removeEventFilter(this);
            s->editor->getProgramController()->closeDocument();
        }
        // Children remain owned by the shell until app.exec returns. This keeps
        // the library's model alive throughout close callbacks and Qt shutdown.
        event->accept();
    }
    QTabWidget* tabs_ = nullptr;
    QAction* open_ = nullptr;
    QAction* quit_ = nullptr;
    QTimer poll_;
    bool closing_ = false;
    QPointer<Editor> pendingActivation_;
    std::vector<std::unique_ptr<Session>> sessions_;
};
#ifdef Q_OS_WIN
// CI-only integration scenario, always uses the Python driver's disposable PDFs.
static void integrationRequire(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static bool integrationWait(const std::function<bool()>& predicate, int timeout = 12000) {
    QElapsedTimer clock; clock.start();
    do { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); if (predicate()) return true; QThread::msleep(10); } while (clock.elapsed() < timeout);
    return false;
}
static QByteArray integrationBytes(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
static bool integrationKey(WORD key, bool control = false) {
    INPUT input[4]{}; int count = 0;
    auto append = [&](WORD vk, bool release) { input[count].type = INPUT_KEYBOARD; input[count].ki.wVk = vk; input[count++].ki.dwFlags = release ? KEYEVENTF_KEYUP : 0; };
    if (control) append(VK_CONTROL, false); append(key, false); append(key, true); if (control) append(VK_CONTROL, true);
    return SendInput(count, input, sizeof(INPUT)) == UINT(count);
}
static bool integrationType(const QString& text) {
    for (QChar ch : text) { INPUT input[2]{}; for (auto& i : input) { i.type = INPUT_KEYBOARD; i.ki.wScan = ch.unicode(); i.ki.dwFlags = KEYEVENTF_UNICODE; } input[1].ki.dwFlags |= KEYEVENTF_KEYUP; if (SendInput(2, input, sizeof(INPUT)) != 2) return false; }
    return true;
}
static bool integrationClick(NativeTabs& host, QWidget* widget, QPoint point, bool right = false) {
    const QPoint local = widget->mapTo(&host, point); POINT physical{0, 0}; ClientToScreen(reinterpret_cast<HWND>(host.winId()), &physical);
    physical.x += qRound(local.x() * host.devicePixelRatioF()); physical.y += qRound(local.y() * host.devicePixelRatioF());
    if (!SetCursorPos(physical.x, physical.y)) return false;
    INPUT input[2]{}; input[0].type = input[1].type = INPUT_MOUSE;
    input[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    input[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    return SendInput(2, input, sizeof(INPUT)) == 2;
}
static QWidget* integrationSurface(Editor* e) { return e->getProgramController()->getPdfWidget()->getDrawWidget()->getWidget(); }
static int integrationHighlights(Editor* e) {
    int count = 0; for (const auto& item : e->getProgramController()->getPdfWidget()->getAnnotationManager()->getPageAnnotations(0).annotations)
        if (item.annotation->getType() == pdf::AnnotationType::Highlight) ++count;
    return count;
}
static void integrationMouse(QWidget* widget, QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons held) {
    QMouseEvent event(type, QPointF(point), QPointF(widget->mapToGlobal(point)), button, held, Qt::NoModifier); QApplication::sendEvent(widget, &event);
}
static void integrationHighlight(Editor* e) {
    auto* proxy = e->getProgramController()->getPdfWidget()->getDrawWidgetProxy(); const double zoom = proxy->getZoom(); proxy->zoom(1.0); proxy->goToPage(0);
    QPoint from, to;
    integrationRequire(integrationWait([&] {
        pdf::PDFTextLayout layout = proxy->getTextLayoutCompiler()->getTextLayoutLazy(0);
        if (layout.getTextBlocks().empty() || layout.getTextBlocks().front().getLines().empty()) return false;
        const auto& chars = layout.getTextBlocks().front().getLines().front().getCharacters(); const auto view = proxy->getSnapshot(); const auto* page = view.getPageSnapshot(0);
        if (chars.size() < 8 || !page) return false;
        from = page->pageToDeviceMatrix.map(chars[0].boundingBox.boundingRect().center() - QPointF(1, 0)).toPoint();
        to = page->pageToDeviceMatrix.map(chars[7].boundingBox.boundingRect().center() + QPointF(1, -2)).toPoint();
        return integrationSurface(e)->rect().contains(from) && integrationSurface(e)->rect().contains(to);
    }), "Integrated highlight geometry unavailable");
    auto* action = e->findChild<QAction*>(QStringLiteral("actionCreateTextHighlight")); integrationRequire(action && action->isEnabled(), "Integrated highlighter unavailable"); action->trigger();
    auto* draw = integrationSurface(e); const int before = integrationHighlights(e);
    integrationMouse(draw, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
    integrationMouse(draw, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    integrationMouse(draw, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    integrationMouse(draw, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    integrationRequire(integrationWait([&] { return integrationHighlights(e) == before + 1; }), "Integrated highlight was not created"); proxy->zoom(zoom);
}
static void integrationDirectory(QFileDialog* dialog, const QString& path) {
    dialog->setDirectory(path);
    if (auto* field = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) field->setText(QStringLiteral("."));
    QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
}
static QListWidget* integrationList(NativeTabs& host, const QString& path, QListWidgetItem** found) {
    auto* pages = host.findChild<QStackedWidget*>(QStringLiteral("LunaDocumentPages"));
    if (!pages) return nullptr;
    for (auto* list : pages->currentWidget()->findChildren<QListWidget*>()) for (int i = 0; i < list->count(); ++i)
        if (DocumentClaim::identity(list->item(i)->data(Qt::UserRole).toString()) == DocumentClaim::identity(path)) { *found = list->item(i); return list; }
    return nullptr;
}
static void integrationLayouts(NativeTabs& host, const QString& report) {
    const QSize oldSize = host.size(); const QPoint oldPosition = host.pos(); const auto flags = host.windowFlags();
    const bool quit = qApp->quitOnLastWindowClosed(); qApp->setQuitOnLastWindowClosed(false);
    QWidget capture; capture.setAttribute(Qt::WA_DontShowOnScreen); capture.resize(2400, 1600); capture.show();
    host.setParent(&capture, Qt::Widget); const double scale = host.devicePixelRatioF(); bool captured = true;
    for (const QSize pixels : {QSize(1366, 768), QSize(1920, 1080)}) {
        const QSize logical(qRound(pixels.width() / scale), qRound(pixels.height() / scale));
        host.resize(logical); host.show();
        if (!integrationWait([&] { return host.size() == logical; }) || !host.grab().save(report + QStringLiteral(".%1.png").arg(pixels.width()))) { captured = false; break; }
    }
    host.setParent(nullptr, flags); host.resize(oldSize); host.move(oldPosition); host.show(); qApp->setQuitOnLastWindowClosed(quit);
    integrationRequire(captured, "Production tab layout size or capture failed");
}
static void verifyIntegration(NativeTabs& host, const QStringList& files, const QString& report, bool restore) {
    QJsonObject result{{"accepted", false}, {"physicalMonitorDpi", "unverified"}, {"input", "Win32 tab clicks, memo typing and shortcuts; Qt crop/highlight and dialog fixtures"}};
    auto write = [&] { QSaveFile output(report); if (output.open(QIODevice::WriteOnly)) { output.write(QJsonDocument(result).toJson()); output.commit(); } };
    auto phase = [&](const char* text) {
        const QString name = QString::fromLatin1(text); result["phase"] = name; host.setProperty("LunaIntegrationPhase", name); write();
        QTimer::singleShot(1500, &host, [&host, report, name] {
            if (host.property("LunaIntegrationPhase").toString() == name && host.isVisible() && host.screen())
                host.screen()->grabWindow(0).save(report + ".pending.png");
        });
    };
    try {
        phase("load and persistent library");
        auto* tabs = host.findChild<QTabWidget*>(QStringLiteral("LunaPdfTabs")); auto* a = host.current();
        auto ready = [](Editor* e) { if (!e) return false; auto* c = e->getProgramController(); return c->getDocument() && c->canClose() && e->isEnabled() && !e->property("LunaRememberedPath").toString().isEmpty(); };
        integrationRequire(tabs && a && integrationWait([&] { return ready(a); }), "Integrated A did not load");
        const QString memo = QStringLiteral("통합 메모 724");
        QListWidgetItem* paper = nullptr; QListWidget* list = nullptr;
        integrationRequire(integrationWait([&] { list = integrationList(host, files[0], &paper); return list && paper; }), "Actual library paper card missing");
        if (restore) {
            integrationRequire(paper->data(Qt::UserRole + 4).toString() == memo, "Paper memo did not survive process restart");
            integrationRequire(integrationHighlights(a) == 1, "Saved A highlight did not survive process restart");
            const QString thumb = QFileInfo(files[0]).absolutePath() + "/A.luna-thumb.png";
            integrationRequire(QFileInfo::exists(thumb) && !QImage(thumb).isNull(), "Moved thumbnail missing after restart");
            result["freshProcessMemoPdfAndThumbnail"] = true;
        } else {
            integrationRequire(files.size() == 2, "Initial integrated fixture requires A and B");
            SetForegroundWindow(reinterpret_cast<HWND>(host.winId()));
            phase("folder selection");
            bool selectedFolder = false;
            QTimer directory; QObject::connect(&directory, &QTimer::timeout, &host, [&] {
                if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                    integrationDirectory(dialog, QFileInfo(files[0]).absolutePath()); selectedFolder = dialog->result() == QDialog::Accepted;
                }
            });
            directory.start(30); auto* location = host.findChild<QPushButton*>(QStringLiteral("LunaLocation")); integrationRequire(location, "Folder picker missing"); location->click(); directory.stop();
            integrationRequire(selectedFolder, "Native folder picker did not accept the library folder");
            phase("open B through public action");
            bool openedB = false; QTimer filePicker; QPointer<QFileDialog> openDialog;
            QObject::connect(&filePicker, &QTimer::timeout, &host, [&] { if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                if (openDialog != dialog) {
                    openDialog = dialog;
                    QTimer::singleShot(8000, dialog, [dialog] { if (dialog->isVisible()) dialog->reject(); });
                }
                dialog->setDirectory(QFileInfo(files[1]).absolutePath()); dialog->selectFile(files[1]);
                if (auto* field = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) field->setText(QFileInfo(files[1]).absoluteFilePath());
                const auto selected = dialog->selectedFiles();
                result["openDialogSelection"] = QJsonArray::fromStringList(selected);
                result["openDialogExpected"] = files[1];
                result["openDialogSelectedIdentity"] = DocumentClaim::identity(selected.value(0));
                result["openDialogExpectedIdentity"] = DocumentClaim::identity(files[1]); write();
                openedB = selected.size() == 1 && DocumentClaim::identity(selected.front()) == DocumentClaim::identity(files[1]);
                if (openedB) QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }});
            filePicker.start(30); host.findChild<QAction*>(QStringLiteral("LunaOpenDocuments"))->trigger(); filePicker.stop();
            integrationRequire(openedB && integrationWait([&] { return host.documentCount() == 2 && host.current() != a && ready(host.current()); }), "Public open action did not create B tab");
            auto* b = host.current(); integrationRequire(host.open(files[1]) == b && host.documentCount() == 2, "Duplicate PDF opened another tab");
            auto* pa = a->getProgramController()->getPdfWidget()->getDrawWidgetProxy(); auto* pb = b->getProgramController()->getPdfWidget()->getDrawWidgetProxy();
            integrationRequire(qAbs(pa->getZoom() - 2.0) < .001 && qAbs(pb->getZoom() - 2.0) < .001, "New integrated documents did not default to 200 percent");
            pa->zoom(1.25); pb->zoom(1.5);
            auto* focus = host.findChild<QAction*>(QStringLiteral("LunaFocusMode"));
            auto collapse = [&] {
                focus->setChecked(true);
                host.resize(host.screen()->availableGeometry().size() - QSize(24, 48)); host.move(8, 8);
                QCoreApplication::processEvents();
            }; collapse();
            auto* bar = tabs->tabBar(); integrationRequire(integrationClick(host, bar, bar->tabRect(tabs->indexOf(a)).center()), "Real tab mouse input failed");
            integrationRequire(integrationWait([&] { return host.current() == a; }), "Tab mouse click did not activate A");
            tabs->setCurrentWidget(b); tabs->setCurrentWidget(a);
            integrationRequire(qAbs(pa->getZoom() - 1.25) < .001 && qAbs(pb->getZoom() - 1.5) < .001, "Library rebinding reset independent document zoom");
            result["singleWindowTabsOpenDedupAndZoom"] = true;
            phase("active page previews and actual crop");
            focus->setChecked(false); tabs->setCurrentWidget(b);
            auto* modes = host.findChild<QTabBar*>(QStringLiteral("LunaContentModes")); auto* grid = host.findChild<QListView*>(QStringLiteral("LunaPageGrid")); modes->setCurrentIndex(1);
            integrationRequire(integrationWait([&] { return grid->model()->rowCount() == 3 && !grid->model()->index(0, 0).data(Qt::DecorationRole).value<QPixmap>().isNull() && grid->visualRect(grid->model()->index(0, 0)).top() == grid->visualRect(grid->model()->index(1, 0)).top(); }), "B native page grid did not rebind to three pages/two columns");
            const auto third = grid->model()->index(2, 0); grid->scrollTo(third);
            integrationRequire(integrationWait([&] { return grid->viewport()->rect().contains(grid->visualRect(third).center()); }) &&
                integrationClick(host, grid->viewport(), grid->visualRect(third).center()), "Real page preview mouse input failed");
            integrationRequire(integrationWait([&] { const auto pages = b->getProgramController()->getPdfWidget()->getDrawWidget()->getCurrentPages(); return std::find(pages.begin(), pages.end(), pdf::PDFInteger(2)) != pages.end(); }), "Page preview did not navigate current B");
            tabs->setCurrentWidget(a); integrationRequire(grid->model()->rowCount() == 2, "A page model did not return after switch"); modes->setCurrentIndex(0);
            collapse(); const QString thumbnail = QFileInfo(files[0]).absolutePath() + "/A.luna-thumb.png";
            auto* crop = a->findChild<QAction*>(QStringLiteral("LunaCropThumbnail")); integrationRequire(crop && crop->isEnabled(), "Active thumbnail crop action disabled"); crop->trigger();
            auto* draw = integrationSurface(a); QWidget* overlay = nullptr;
            integrationRequire(integrationWait([&] { overlay = draw->findChild<QWidget*>(QStringLiteral("LunaThumbnailCrop")); return overlay && overlay->isVisible(); }), "Shared library crop did not target A");
            integrationMouse(overlay, QEvent::MouseButtonPress, {50, 80}, Qt::LeftButton, Qt::LeftButton);
            integrationMouse(overlay, QEvent::MouseMove, {200, 200}, Qt::NoButton, Qt::LeftButton);
            integrationMouse(overlay, QEvent::MouseButtonRelease, {200, 200}, Qt::LeftButton, Qt::NoButton);
            integrationRequire(integrationWait([&] { return QFileInfo::exists(thumbnail) && !QImage(thumbnail).isNull(); }) && !QFileInfo::exists(QFileInfo(files[1]).absolutePath() + "/B.luna-thumb.png"), "Crop wrote to the wrong PDF or no image");
            result["activeNativePagesAndCrop"] = true;
            phase("A highlight and real Ctrl+S"); integrationHighlight(a);
            integrationRequire(integrationHighlights(b) == 0, "Integrated A annotation crossed to B");
            integrationRequire(integrationWait([&] { return ready(a); }), "A remained busy before close-cancel check");
            int cancelled = 0; QTimer cancel;
            QObject::connect(&cancel, &QTimer::timeout, &host, [&] {
                if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    if (auto* button = box->button(QMessageBox::Cancel)) { ++cancelled; button->click(); }
            });
            cancel.start(30); const bool tabClosed = host.closeTab(tabs->indexOf(a)); cancel.stop();
            integrationRequire(!tabClosed && cancelled == 1 && host.documentCount() == 2 && a->getProgramController()->hasUnsavedChanges(), "Integrated tab Cancel lost A");
            cancelled = 0; cancel.start(30); const bool windowClosed = host.close(); cancel.stop();
            integrationRequire(!windowClosed && cancelled == 1 && host.documentCount() == 2, "Integrated window Cancel removed a tab");
            result["integratedTabAndWindowCloseCancel"] = true;
            integrationRequire(integrationWait([&] { return ready(a) && a->findChild<QAction*>(QStringLiteral("actionSave"))->isEnabled(); }), "A did not become ready to save");
            integrationSurface(a)->setFocus(); integrationRequire(integrationKey('S', true), "Real Ctrl+S injection failed");
            integrationRequire(integrationWait([&] { return !a->getProgramController()->hasUnsavedChanges(); }), "Root Ctrl+S did not save A");
            integrationRequire(host.screen()->grabWindow(0).save(report + ".tabs.png"), "Could not capture integrated tabs");
            phase("B search through root shortcut and Save As collisions");
            tabs->setCurrentWidget(b); focus->setChecked(false); host.findChild<QLineEdit*>(QStringLiteral("LunaGlobalSearch"))->setFocus();
            integrationRequire(integrationWait([&] { return ready(b) && b->findChild<QAction*>(QStringLiteral("actionFind"))->isEnabled(); }), "B did not become ready for search");
            integrationRequire(integrationKey('F', true), "Real Ctrl+F injection failed"); QPointer<QDialog> search;
            integrationRequire(integrationWait([&] { for (auto* d : b->findChildren<QDialog*>()) if (d->isVisible() && d->inherits("pdf::PDFFindTextToolDialog")) search = d; return bool(search); }), "Root Ctrl+F did not open B search from library focus");
            integrationRequire(search->findChild<QLineEdit*>()->hasFocus() && integrationType("bravo") && integrationKey(VK_RETURN), "B search input failed");
            integrationRequire(integrationWait([&] { return search && search->windowTitle().contains("1/1"); }), "B typed search did not return 1/1");
            integrationRequire(host.screen()->availableGeometry().contains(search->frameGeometry()), "Integrated search popup extends off screen"); search->reject();
            const QByteArray savedA = integrationBytes(files[0]); int warnings = 0;
            auto* nativeSaveAs = b->findChild<QAction*>(QStringLiteral("actionSave_As"));
            integrationRequire(integrationWait([&] { return ready(b) && nativeSaveAs && nativeSaveAs->isEnabled(); }), "B Save As remained busy after search");
            auto saveAs = [&](const QString& target) {
                bool selected = false; QTimer picker;
                QObject::connect(&picker, &QTimer::timeout, &host, [&] {
                    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                        if (auto* yes = box->button(QMessageBox::Yes)) yes->click();
                        else if (auto* ok = box->button(QMessageBox::Ok)) { ++warnings; ok->click(); }
                    } else if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                        if (!selected) { selected = true; dialog->setDirectory(QFileInfo(target).absolutePath()); dialog->selectFile(target);
                            if (auto* field = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) field->setText(QFileInfo(target).absoluteFilePath());
                            QTimer::singleShot(0, dialog, [dialog] { QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection); }); }
                    }
                }); picker.start(30); b->findChild<QAction*>(QStringLiteral("actionSave_As"))->trigger(); picker.stop(); return selected;
            };
            const bool selectedCollision = saveAs(files[0]);
            result["collisionDialogSelected"] = selectedCollision; result["collisionWarnings"] = warnings;
            result["collisionAUnchanged"] = integrationBytes(files[0]) == savedA;
            result["collisionBPathUnchanged"] = DocumentClaim::identity(b->getProgramController()->getOriginalFileName()) == DocumentClaim::identity(files[1]); write();
            integrationRequire(selectedCollision && warnings == 1 && result["collisionAUnchanged"].toBool() && result["collisionBPathUnchanged"].toBool(), "Save As collision changed A or rebound B");
            const QString copy = QFileInfo(files[1]).absolutePath() + "/B-copy.pdf";
            integrationRequire(saveAs(copy) && QFileInfo::exists(copy) && DocumentClaim::identity(b->getProgramController()->getOriginalFileName()) == DocumentClaim::identity(copy), "Save As did not update B path");
            integrationRequire(host.open(copy) == b && host.documentCount() == 2, "Saved path was not deduplicated");
            auto* originalB = host.open(files[1]); integrationRequire(originalB && originalB != b && host.documentCount() == 3, "Old B path was not released after Save As");
            integrationRequire(integrationWait([&] { return ready(originalB); }), "Original B did not load after Save As");
            integrationRequire(host.closeTab(tabs->indexOf(originalB)) && host.closeTab(tabs->indexOf(b)) && host.documentCount() == 1, "Closing B tabs damaged A");
            result["rootShortcutsSearchSaveAsAndSurvivor"] = true;
            phase("inline memo, search and paired move");
            integrationRequire(integrationWait([&] { list = integrationList(host, files[0], &paper); return list && paper; }), "A paper card missing before memo"); list->scrollToItem(paper);
            const QRect card = list->visualItemRect(paper);
            integrationRequire(integrationClick(host, list->viewport(), {card.left() + 100, card.top() + 114}), "Real memo mouse input failed");
            QLineEdit* edit = nullptr; integrationRequire(integrationWait([&] { edit = list->findChild<QLineEdit*>(QStringLiteral("LunaPaperMemoEditor")); return edit && edit->hasFocus(); }), "Inline memo editor did not focus");
            integrationRequire(integrationType(QStringLiteral("초기 메모")) && integrationKey(VK_RETURN), "Real memo typing/Enter failed");
            integrationRequire(integrationWait([&] { return paper->data(Qt::UserRole + 4).toString() == QStringLiteral("초기 메모"); }), "Memo Enter did not persist");
            integrationRequire(integrationClick(host, list->viewport(), {card.left() + 100, card.top() + 114}), "Memo edit mouse input failed");
            integrationRequire(integrationWait([&] { edit = list->findChild<QLineEdit*>(QStringLiteral("LunaPaperMemoEditor")); return edit && edit->hasFocus(); }), "Memo edit did not reopen");
            integrationRequire(integrationKey('A', true) && integrationType(memo), "Memo replacement input failed");
            auto* query = host.findChild<QLineEdit*>(QStringLiteral("LunaGlobalSearch")); integrationRequire(integrationClick(host, query, query->rect().center()), "Memo focus-out click failed");
            integrationRequire(integrationWait([&] { return paper->data(Qt::UserRole + 4).toString() == memo; }) && paper->toolTip().contains(memo), "Memo focus-out did not save with full tooltip");
            integrationRequire(integrationType(QStringLiteral("724")), "Memo search keyboard input failed");
            integrationRequire(integrationWait([&] { return query->text() == QStringLiteral("724"); }) && !paper->isHidden(), "Paper memo search excluded A"); query->clear();
            integrationRequire(host.screen()->grabWindow(0).save(report + ".memo.png"), "Could not capture integrated memo");
            const QByteArray originalThumb = integrationBytes(thumbnail); const QString moved = QFileInfo(files[0]).absolutePath() + "/Moved"; QDir().mkpath(moved);
            bool menuChosen = false; QTimer moveUi;
            QObject::connect(&moveUi, &QTimer::timeout, &host, [&] {
                if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) integrationDirectory(dialog, moved);
                else if (!menuChosen) if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
                    for (auto* action : menu->actions()) if (action->text().startsWith(QStringLiteral("PDF와 썸네일 이동"))) { menuChosen = true; menu->setActiveAction(action); integrationKey(VK_RETURN); break; }
            }); moveUi.start(30);
            integrationRequire(integrationClick(host, list->viewport(), card.center(), true), "Real move context-menu mouse input failed");
            const QString movedPdf = moved + "/A.pdf", movedThumb = moved + "/A.luna-thumb.png";
            integrationRequire(integrationWait([&] { return QFileInfo::exists(movedPdf) && ready(a) && !a->property("LunaMoveRestorePending").toBool() && DocumentClaim::identity(a->getProgramController()->getOriginalFileName()) == DocumentClaim::identity(movedPdf); }), "PDF move did not reopen the same A tab"); moveUi.stop();
            integrationRequire(menuChosen && !QFileInfo::exists(files[0]) && !QFileInfo::exists(thumbnail) && integrationBytes(movedPdf) == savedA && integrationBytes(movedThumb) == originalThumb && host.documentCount() == 1, "Paired move lost bytes or duplicated the tab");
            QTimer selectMoved; QObject::connect(&selectMoved, &QTimer::timeout, &host, [&] { if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) integrationDirectory(dialog, moved); });
            selectMoved.start(30); location->click(); selectMoved.stop();
            integrationRequire(integrationWait([&] { list = integrationList(host, movedPdf, &paper); return list && paper && paper->data(Qt::UserRole + 4).toString() == memo; }), "Memo did not follow A into its new folder");
            result["memoNativeInputSearchAndPdfThumbnailMove"] = true;
            result["movedPdf"] = movedPdf;
        }
        integrationRequire(host.screen()->grabWindow(0).save(report + ".final.png"), "Could not capture final integrated window");
        integrationLayouts(host, report);
        integrationRequire(integrationWait([&] { return ready(host.current()); }), "Reader remained busy after layout captures");
        integrationRequire(host.close(), "Final integrated window close refused"); result["accepted"] = true; phase("passed"); qApp->exit(0);
    } catch (const std::exception& error) {
        result["error"] = QString::fromUtf8(error.what()); result["accepted"] = false; write();
        if (host.screen()) host.screen()->grabWindow(0).save(report + ".failure.png"); qApp->exit(1);
    }
}

#endif
int runTabbedLibrary(const QStringList& files, const QString& report, bool restore) {
    qApp->setProperty("lunaNativeSessions", true);
    NativeTabs host;
    int opened = 0;
    const QStringList initial = !report.isEmpty() && !restore ? files.mid(0, 1) : files;
    for (const auto& file : initial) if (host.open(file)) ++opened;
    if (!files.isEmpty() && !opened) return 0;
    host.show();
    if (!report.isEmpty()) {
#ifdef Q_OS_WIN
        QTimer::singleShot(0, &host, [&] { verifyIntegration(host, files, report, restore); });
#else
        return 2;
#endif
    }
    return qApp->exec();
}
}
