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
#include <QAbstractNativeEventFilter>
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
#include <QKeyEvent>
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
#include <QClipboard>
#include <QToolTip>
#include <QScrollBar>
#include "pdfwidgettool.h"
#include <QElapsedTimer>
#include <QDateTime>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QListView>
#include <QMouseEvent>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QScreen>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QFontMetrics>
#include <QThread>
#include <QWindow>
#include <QtMath>
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
// Presentation only: bound the entire tab, including its icon and close button.
class CompactPdfTabBar final : public QTabBar {
public:
    using QTabBar::QTabBar;
protected:
    QSize tabSizeHint(int index) const override {
        const QSize natural = QTabBar::tabSizeHint(index);
        return {qBound(160, natural.width(), 190), qMax(30, fontMetrics().height() + 12)};
    }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
};
class PdfTabs final : public QTabWidget {
public:
    explicit PdfTabs(QWidget* parent) : QTabWidget(parent) { setTabBar(new CompactPdfTabBar(this)); }
};
struct Session {
    QPointer<Editor> editor;
    QString identity;
    bool loading = false, dispatchingOpen = false;
    bool observedDocument = false;
    std::unique_ptr<DocumentClaim> claim, saveClaim;
};
class NativeTabs final : public QMainWindow, public QAbstractNativeEventFilter {
public:
    ~NativeTabs() override {
        trace("shell-destroy"); poll_.stop(); qApp->removeEventFilter(this);
        qApp->removeNativeEventFilter(this);
        QObject::disconnect(qApp, nullptr, this, nullptr);
        for (auto* editor : findChildren<Editor*>()) {
            QObject::disconnect(editor, nullptr, this, nullptr);
            QObject::disconnect(editor->getProgramController(), nullptr, this, nullptr);
        }
    }
    NativeTabs() {
        setObjectName(QStringLiteral("LunaNativeTabs"));
        qApp->installNativeEventFilter(this);
        setWindowTitle(QStringLiteral("LUNA Library"));
        setWindowIcon(QApplication::windowIcon());
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { setProperty("LunaAboutToQuit", property("LunaAboutToQuit").toInt() + 1); trace("about-to-quit"); });
        connect(qApp, &QGuiApplication::lastWindowClosed, this, [this] { trace("last-window-closed"); });
        tabs_ = new PdfTabs(this); tabs_->setObjectName(QStringLiteral("LunaPdfTabs"));
        tabs_->setTabsClosable(true); tabs_->setDocumentMode(true); tabs_->setMovable(true);
        tabs_->setElideMode(Qt::ElideMiddle);
        tabs_->tabBar()->setObjectName(QStringLiteral("LunaPdfTabBar"));
        tabs_->tabBar()->setExpanding(false);
        tabs_->tabBar()->setUsesScrollButtons(true);
        auto* initial = createEditor();
        installLibraryShell(*this, *initial);
        auto* frame = centralWidget();
        auto* corner = findChild<QWidget*>(QStringLiteral("LunaTabCorner"));
        if (corner) { frame->layout()->removeWidget(corner); tabs_->setCornerWidget(corner, Qt::TopLeftCorner); }
        static_cast<QVBoxLayout*>(frame->layout())->addWidget(tabs_);
        open_ = new QAction(QStringLiteral("PDF 열기…"), this); open_->setObjectName(QStringLiteral("LunaOpenDocuments"));
        open_->setShortcut(QKeySequence(QStringLiteral("Ctrl+O"))); addAction(open_);
        open_->setToolTip(QStringLiteral("여러 PDF를 탭으로 열기 (Ctrl+O)"));
        connect(open_, &QAction::triggered, this, [this] { chooseFiles(); });
        if (corner) {
            auto* addCorner = new QWidget(tabs_);
            auto* addRow = new QHBoxLayout(addCorner); addRow->setContentsMargins(0, 0, 4, 0);
            auto* plus = new QToolButton(addCorner); plus->setObjectName(QStringLiteral("LunaNewTab")); plus->setText(QStringLiteral("＋"));
            plus->setAccessibleName(QStringLiteral("새 PDF 탭 열기")); plus->setToolTip(QStringLiteral("새 PDF 탭 열기")); plus->setMinimumSize(30, 30);
            addRow->addWidget(plus); tabs_->setCornerWidget(addCorner, Qt::TopRightCorner);
            connect(plus, &QToolButton::clicked, open_, &QAction::trigger);
            corner->setVisible(findChild<QAction*>(QStringLiteral("LunaFocusMode"))->isChecked());
        }
        quit_ = new QAction(QStringLiteral("끝내기"), this); quit_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q"))); addAction(quit_);
        connect(quit_, &QAction::triggered, this, [this] { trace("shell-quit-action"); close(); });
        for (const auto& route : {qMakePair("actionSave", "Ctrl+S"), qMakePair("actionSave_As", "Ctrl+Shift+S"),
            qMakePair("actionFind", "Ctrl+F"), qMakePair("actionPrint", "Ctrl+P")}) {
            auto* key = new QAction(this); key->setShortcut(QKeySequence(QString::fromLatin1(route.second))); addAction(key);
            connect(key, &QAction::triggered, this, [this, name = QString::fromLatin1(route.first)] {
                if (auto* e = current()) if (auto* action = e->findChild<QAction*>(name); action && action->isEnabled()) action->trigger();
            });
        }
        // Ctrl+W is reserved for closing only the active tab. Do not expose a
        // competing QAction shortcut: embedded PDF4QT windows can also register
        // document/window Close actions, and Qt may choose another shortcut.
        // Our application event filter intercepts ShortcutOverride + KeyPress.
        finishEditor(initial);
        setLibraryDocumentRouter(*this, [this](const QString& path) { open(path, "library-open"); },
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
    Editor* current() const { return tabs_ ? qobject_cast<Editor*>(tabs_->currentWidget()) : nullptr; }
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
#ifdef Q_OS_WIN
        const auto* msg = static_cast<MSG*>(message);
        const bool shell = msg->hwnd == reinterpret_cast<HWND>(effectiveWinId());
        if (msg->message == WM_QUIT || (shell && (msg->message == WM_CLOSE || msg->message == WM_DESTROY ||
            msg->message == WM_NCDESTROY || msg->message == WM_SYSCOMMAND || msg->message == WM_SHOWWINDOW)) ||
            ((msg->message == WM_KEYDOWN || msg->message == WM_KEYUP) && msg->wParam == 'W')) {
            setProperty("LunaNativeMessage", int(msg->message)); setProperty("LunaNativeCommand", int(msg->wParam));
            setProperty("LunaNativeRepeat", bool(msg->lParam & (1L << 30)));
            setProperty("LunaNativeControl", bool(GetKeyState(VK_CONTROL) & 0x8000));
            trace(shell ? "native-shell-message" : "native-input-or-quit-message");
        }
#else
        Q_UNUSED(message);
#endif
        return false;
    }
    Editor* open(const QString& path, const char* origin = "api-or-startup") {
        setProperty("LunaOpenOrigin", QString::fromLatin1(origin));
        setProperty("LunaRequestedDocument", QString::fromLatin1(QCryptographicHash::hash(DocumentClaim::identity(path).toUtf8(), QCryptographicHash::Sha256).toHex().left(16)));
        trace("document-open-request", current());
        const QFileInfo file(path);
        if (!file.isFile() || file.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive)) {
            QMessageBox::warning(this, QStringLiteral("PDF 열기 실패"), QStringLiteral("읽을 수 있는 PDF 파일을 선택해 주세요.")); return nullptr;
        }
        if (auto* existing = find(path)) { trace("document-open-existing-tab", existing->editor); activate(existing->editor); return existing->editor; }
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
        editor->setProperty("LunaLoadGeneration", editor->property("LunaLoadGeneration").toInt() + 1);
        editor->setProperty("LunaOpenOrigin", QString::fromLatin1(origin));
        trace("document-open-dispatched", editor);
        { QScopedValueRollback<bool> dispatch(s->dispatchingOpen, true);
          editor->getProgramController()->openDocument(file.absoluteFilePath()); }
        activate(editor); updateTitle(editor);
        return editor;
    }
    void trace(const char* event, Editor* editor = nullptr) {
        const QString path = qApp->property("LunaLifecycleTracePath").toString(); if (path.isEmpty()) return;
        setProperty("LunaTraceSequence", property("LunaTraceSequence").toInt() + 1);
        QJsonObject row{{"sequence", property("LunaTraceSequence").toInt()}, {"ctrlWKeys", property("LunaCtrlWKeys").toInt()}, {"keyTarget", property("LunaKeyTarget").toString()}, {"event", QString::fromLatin1(event)}, {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"visible", isVisible()}, {"closing", closing_}, {"tabs", tabs_ ? tabs_->count() : 0},
            {"documents", documentCount()}, {"creations", property("LunaEditorCreations").toInt()},
            {"quitOnLastWindowClosed", qApp->quitOnLastWindowClosed()},
            {"pid", qApp->applicationPid()}, {"nativeMessage", property("LunaNativeMessage").toInt()},
            {"nativeCommand", property("LunaNativeCommand").toInt()},
            {"activeEditor", current() ? current()->property("LunaEditorSerial").toInt() : 0},
            {"destroyedEditor", property("LunaLastDestroyedEditor").toInt()},
            {"openOrigin", property("LunaOpenOrigin").toString()}, {"requestedDocumentId", property("LunaRequestedDocument").toString()},
            {"minimized", isMinimized()}};
        row["qtAutoRepeat"] = property("LunaAutoRepeat").toBool();
        row["nativePreviousKeyDown"] = property("LunaNativeRepeat").toBool();
        row["nativeControlDown"] = property("LunaNativeControl").toBool();
#ifdef Q_OS_WIN
        DWORD processCode = 0;
        row["nativeProcessActive"] = bool(GetExitCodeProcess(GetCurrentProcess(), &processCode)) && processCode == STILL_ACTIVE;
        row["nativeWindowVisible"] = bool(IsWindowVisible(reinterpret_cast<HWND>(effectiveWinId())));
#endif
        if (editor) { row["editor"] = editor->property("LunaEditorSerial").toInt(); row["editorIsWindow"] = editor->isWindow();
            row["editorQuitOnClose"] = editor->testAttribute(Qt::WA_QuitOnClose); row["document"] = bool(editor->getProgramController()->getDocument());
            row["loadGeneration"] = editor->property("LunaLoadGeneration").toInt();
            row["editorOpenOrigin"] = editor->property("LunaOpenOrigin").toString();
            row["editorVisible"] = editor->isVisible();
            if (auto* s = session(editor)) { row["loading"] = s->loading; row["documentId"] = QString::fromLatin1(QCryptographicHash::hash(s->identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(16)); }
        }
        QFile file(path); if (file.open(QIODevice::WriteOnly | QIODevice::Append)) { file.write(QJsonDocument(row).toJson(QJsonDocument::Compact)); file.write("\n"); }
    }
    bool closeTab(int index) {
        if (QApplication::activeModalWidget()) return false;
        auto* editor = qobject_cast<Editor*>(tabs_->widget(index)); if (!editor) return false;
        trace("close-tab-request", editor);
        auto* owned = session(editor);
        if (!owned || owned->loading || (!editor->getProgramController()->getDocument() && owned->identity.isEmpty())) { trace("empty-or-loading-close-noop", editor); return false; }
        activate(editor); auto* controller = editor->getProgramController();
        if (!controller->canClose() || !controller->askForSaveDocumentBeforeClose()) return false;
        if (tabs_->count() == 1) {
            // Keep the already-owned Editor/model alive as the empty reader.
            for (auto* popup : editor->findChildren<QDialog*>()) if (popup->isVisible() && popup->windowFlags().testFlag(Qt::Popup)) popup->reject();
            controller->closeDocument(); owned->claim.reset(); owned->saveClaim.reset();
            owned->identity.clear(); owned->loading = false;
            editor->setProperty("LunaRememberedPath", QString());
            updateTitle(editor); trace("last-document-cleared", editor); return true;
        }
        dispose(editor); return true;
    }
    int documentCount() const { int count = 0; for (const auto& s : sessions_) if (!s->identity.isEmpty()) ++count; return count; }
private:
    Session* session(Editor* e) const { for (const auto& s : sessions_) if (s->editor == e) return s.get(); return nullptr; }
    Session* find(const QString& path) const {
        const QString id = DocumentClaim::identity(path);
        for (const auto& s : sessions_) {
            auto* c = s->editor->getProgramController();
            const bool present = bool(c->getDocument());
            if (present != s->observedDocument) {
                s->observedDocument = present;
                trace(present ? "observed-document-present" : "observed-document-absent", s->editor);
            }
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
        const int serial = property("LunaEditorCreations").toInt() + 1; setProperty("LunaEditorCreations", serial); editor->setProperty("LunaEditorSerial", serial);
        connect(editor, &QObject::destroyed, this, [this, serial] { setProperty("LunaLastDestroyedEditor", serial); trace("editor-destroyed"); });
        editor->setAttribute(Qt::WA_DeleteOnClose, false); editor->setAttribute(Qt::WA_QuitOnClose, false); editor->installEventFilter(this);
        auto s = std::make_unique<Session>(); s->editor = editor; sessions_.push_back(std::move(s));
        tabs_->addTab(editor, QApplication::windowIcon(), QStringLiteral("PDF 열기"));
        const int index = tabs_->indexOf(editor);
        for (auto side : {QTabBar::LeftSide, QTabBar::RightSide}) if (auto* old = tabs_->tabBar()->tabButton(index, side)) {
            tabs_->tabBar()->setTabButton(index, side, nullptr); old->deleteLater();
        }
        auto* close = new QToolButton(tabs_->tabBar()); close->setObjectName(QStringLiteral("LunaCloseTab"));
        close->setText(QStringLiteral("×")); close->setFixedSize(18, 18); close->setToolTip(QStringLiteral("이 PDF 탭 닫기"));
        QPointer<Editor> guarded(editor); connect(close, &QToolButton::clicked, this, [this, guarded] { if (guarded) closeTab(tabs_->indexOf(guarded)); });
        tabs_->tabBar()->setTabButton(index, QTabBar::RightSide, close);
        trace("editor-created", editor); return editor;
    }
    void finishEditor(Editor* editor) {
        prepareReaderSession(*editor, *this);
        auto* controller = editor->getProgramController();
        for (const char* name : {"actionClose", "actionQuit"})
            if (auto* action = editor->findChild<QAction*>(QString::fromLatin1(name))) connect(action, &QAction::triggered, this, [this, editor, name] {
                trace(name, editor);
                // The existing native slot runs first. Only an accepted close
                // clears its document; synchronize the tab/claim afterwards.
                if (QByteArray(name) == "actionClose" && !editor->getProgramController()->getDocument()) closeTab(tabs_->indexOf(editor));
            });
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
            trace("controller-open-request", editor);
            auto* s = session(editor); if (!s) { *handled = true; return; }
            const QString identity = DocumentClaim::identity(path);
            if (s->dispatchingOpen) return;
            if (identity == s->identity && (editor->property("LunaMoveRestorePending").toBool() || !editor->getProgramController()->getDocument())) {
                s->loading = true; return;
            }
            *handled = true; open(path, "native-controller-request");
        }, Qt::DirectConnection);
        connect(controller, &pdfviewer::PDFProgramController::lunaDocumentLoadFinished, this, [this, editor](bool success) {
            trace(success ? "document-load-completed" : "document-load-failed", editor);
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
            if (action->shortcuts().contains(QKeySequence("Ctrl+W")) || action->shortcuts().contains(QKeySequence("Ctrl+Q"))) {
                setProperty("LunaKeyTarget", action->objectName()); trace("competing-editor-close-shortcut", editor);
            }
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
            // Keep tabs compact even when a single PDF has an extremely long filename.
            // The full filename remains available in the tooltip and window title.
            const QString label = tabs_->tabBar()->fontMetrics().elidedText(text, Qt::ElideMiddle, 156);
            tabs_->setTabText(index, label); tabs_->setTabToolTip(index, display);
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
        trace("editor-dispose", editor);
        // Rebind the one library before deleting its previous model/controller.
        if (current() == editor) for (const auto& s : sessions_) if (s->editor != editor) { activate(s->editor); break; }
        editor->getProgramController()->closeDocument(); editor->removeEventFilter(this);
        trace("disposed-document-closed", editor);
        tabs_->removeTab(tabs_->indexOf(editor));
        for (auto it = sessions_.begin(); it != sessions_.end(); ++it) if ((*it)->editor == editor) { sessions_.erase(it); break; }
        trace("editor-delete-deferred", editor); editor->deleteLater();
    }
    void chooseFiles() {
        const QString currentPath = current() ? current()->getProgramController()->getOriginalFileName() : QString();
        const QString start = currentPath.isEmpty() ? QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library")).value(QStringLiteral("library/root")).toString() : QFileInfo(currentPath).absolutePath();
        const auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("PDF 열기"), start, QStringLiteral("PDF (*.pdf *.PDF)"));
        for (const auto& path : paths) open(path, "file-dialog");
    }
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::KeyRelease && static_cast<QKeyEvent*>(event)->key() == Qt::Key_W) trace("ctrl-w-release", current());
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange) {
            if (object == this) trace(event->type() == QEvent::Show ? "shell-show" : event->type() == QEvent::Hide ? "shell-hide" : "shell-window-state");
            else if (auto* editor = qobject_cast<Editor*>(object)) trace(event->type() == QEvent::Show ? "editor-show" : event->type() == QEvent::Hide ? "editor-hide" : "editor-window-state", editor);
        }
        if (event->type() == QEvent::Quit) {
            trace("application-quit-event");
            if (!closing_) { trace("unapproved-quit-blocked"); event->ignore(); return true; }
        }
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
            auto* target = qobject_cast<QWidget*>(object);
            auto* key = static_cast<QKeyEvent*>(event);
            bool ownedInput = target && (target == this || isAncestorOf(target));
            if (auto* nativeWindow = qobject_cast<QWindow*>(object)) {
                for (auto* ancestor = nativeWindow; ancestor; ancestor = ancestor->parent() ? ancestor->parent() : ancestor->transientParent())
                    if (ancestor == windowHandle()) { ownedInput = true; break; }
            }
            if (event->type() == QEvent::KeyPress && key->key() == Qt::Key_W && key->modifiers() == Qt::ControlModifier) {
                setProperty("LunaAutoRepeat", key->isAutoRepeat());
                setProperty("LunaKeyTarget", QString::fromLatin1(object->metaObject()->className())); trace("ctrl-w-arrival", current());
            }
            // Intercept Ctrl+W before native Editor shortcuts and ensure it is
            // never interpreted as a top-level window close.
            if (!closing_ && !QApplication::activeModalWidget() &&
                ownedInput &&
                key->key() == Qt::Key_W && key->modifiers() == Qt::ControlModifier) {
                if (event->type() == QEvent::KeyPress) { setProperty("LunaCtrlWKeys", property("LunaCtrlWKeys").toInt() + 1); trace("ctrl-w-key", current()); }
                key->accept();
                if (event->type() == QEvent::KeyPress && !key->isAutoRepeat())
                    closeTab(tabs_->currentIndex());
                return true;
            }
        }
        if (event->type() == QEvent::Close) if (auto* editor = qobject_cast<Editor*>(object)) {
            static_cast<QCloseEvent*>(event)->ignore(); QPointer<Editor> guarded(editor);
            QTimer::singleShot(0, this, [this, guarded] { if (guarded) closeTab(tabs_->indexOf(guarded)); }); return true;
        }
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::Drop) {
            auto* widget = qobject_cast<QWidget*>(object); if (!widget || widget->window() != this) return false;
            auto* drop = static_cast<QDropEvent*>(event); const auto urls = drop->mimeData()->urls(); if (urls.isEmpty()) return false;
            for (const auto& url : urls) if (!url.isLocalFile() || QFileInfo(url.toLocalFile()).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive)) return false;
            if (event->type() == QEvent::Drop) for (const auto& url : urls) open(url.toLocalFile(), "drop");
            drop->acceptProposedAction(); return true;
        }
        return QMainWindow::eventFilter(object, event);
    }
    void closeEvent(QCloseEvent* event) override {
        trace("shell-close-event", current());
        if (QApplication::activeModalWidget()) { event->ignore(); return; }
        for (const auto& s : sessions_) {
            activate(s->editor); auto* c = s->editor->getProgramController();
            if (!c->canClose() || !c->askForSaveDocumentBeforeClose()) { trace("shell-close-denied", s->editor); event->ignore(); return; }
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
        event->accept(); trace("shell-close-approved"); qApp->quit();
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
static bool integrationMove(NativeTabs& host, QWidget* widget, QPoint point) {
    const QPoint local = widget->mapTo(&host, point); POINT physical{0, 0}; ClientToScreen(reinterpret_cast<HWND>(host.winId()), &physical);
    physical.x += qRound(local.x() * host.devicePixelRatioF()); physical.y += qRound(local.y() * host.devicePixelRatioF());
    return SetCursorPos(physical.x, physical.y);
}
static bool integrationClick(NativeTabs& host, QWidget* widget, QPoint point, bool right = false) {
    if (!integrationMove(host, widget, point)) return false;
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
static void integrationHighlight(NativeTabs& host, Editor* e, const QString& report) {
    auto* proxy = e->getProgramController()->getPdfWidget()->getDrawWidgetProxy(); const double zoom = proxy->getZoom(); proxy->zoom(1.0); proxy->goToPage(0);
    QPoint from, to; QRect selectionBounds;
    integrationRequire(integrationWait([&] {
        pdf::PDFTextLayout layout = proxy->getTextLayoutCompiler()->getTextLayoutLazy(0);
        if (layout.getTextBlocks().empty() || layout.getTextBlocks().front().getLines().empty()) return false;
        const auto& chars = layout.getTextBlocks().front().getLines().front().getCharacters(); const auto view = proxy->getSnapshot(); const auto* page = view.getPageSnapshot(0);
        if (chars.size() < 8 || !page) return false;
        selectionBounds = page->pageToDeviceMatrix.mapRect(chars[0].boundingBox.boundingRect().united(chars[7].boundingBox.boundingRect())).toAlignedRect().adjusted(-4, -4, 4, 4);
        from = page->pageToDeviceMatrix.map(chars[0].boundingBox.boundingRect().center() - QPointF(1, 0)).toPoint();
        to = page->pageToDeviceMatrix.map(chars[7].boundingBox.boundingRect().center() + QPointF(1, -2)).toPoint();
        return integrationSurface(e)->rect().contains(from) && integrationSurface(e)->rect().contains(to);
    }), "Integrated highlight geometry unavailable");
    auto* drawSurface = integrationSurface(e);
    auto* select = e->findChild<QAction*>(QStringLiteral("actionSelectText"));
    integrationRequire(select && select->isEnabled(), "Text selection is unavailable");
    if (!select->isChecked()) select->trigger();
    auto* tools = e->getProgramController()->getToolManager();
    integrationRequire(select->isChecked() && tools->getActiveTool() == tools->getSelectTextTool(), "Native text-selection mode is not active before drag");
    const QImage beforeImage = drawSurface->grab().toImage();
    auto bluePixels = [&](const QImage& image, int* newDark = nullptr) {
        const qreal dpr = image.devicePixelRatio();
        const QRect physical(qFloor(selectionBounds.x() * dpr), qFloor(selectionBounds.y() * dpr),
            qCeil(selectionBounds.width() * dpr), qCeil(selectionBounds.height() * dpr));
        const QRect area = physical.intersected(image.rect()).intersected(beforeImage.rect()); int count = 0;
        if (newDark) *newDark = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) for (int x = area.left(); x <= area.right(); ++x) {
            const QColor c = image.pixelColor(x, y), old = beforeImage.pixelColor(x, y);
            if (c.blue() - c.red() > 35 && c.blue() - c.green() > 15 && c.red() > 100) ++count;
            if (newDark && old.red() > 225 && old.green() > 225 && old.blue() > 225 && qGray(c.rgb()) < 80) ++*newDark;
        }
        return count;
    };
    drawSurface->setFocus(); integrationRequire(integrationMove(host, drawSurface, from), "Text drag cursor positioning failed");
    INPUT mouse{}; mouse.type = INPUT_MOUSE; mouse.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    integrationRequire(SendInput(1, &mouse, sizeof(INPUT)) == 1, "Text drag press failed");
    QCoreApplication::processEvents();
    for (int step = 1; step <= 8; ++step) {
        integrationRequire(integrationMove(host, drawSurface, from + (to - from) * step / 8), "Text drag motion failed");
        QCoreApplication::processEvents(); QThread::msleep(15);
    }
    const bool liveBlue = integrationWait([&] { return bluePixels(drawSurface->grab().toImage()) > 30; });
    mouse.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    integrationRequire(SendInput(1, &mouse, sizeof(INPUT)) == 1, "Text drag release failed");
    integrationRequire(liveBlue, "Dragging did not display translucent blue selection");
    auto* nativeSelection = e->getProgramController()->getToolManager()->getSelectTextTool();
    integrationRequire(integrationWait([&] { return nativeSelection->getSelectedText().contains(QStringLiteral("LUNA")); }), "Real mouse text selection failed");
    int newDark = 0;
    integrationRequire(bluePixels(drawSurface->grab().toImage(), &newDark) > 30 && newDark == 0, "Selection did not persist or added a dark outline");
    QApplication::clipboard()->clear();
    integrationRequire(integrationKey('C', true) && integrationWait([&] { return QApplication::clipboard()->text() == nativeSelection->getSelectedText(); }), "Real Ctrl+C did not copy selected text");
    integrationRequire(host.screen()->grabWindow(0).save(report + ".selection.png"), "Selection screenshot failed");
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
    host.raise(); host.activateWindow(); SetForegroundWindow(reinterpret_cast<HWND>(host.winId()));
    if (host.current()) integrationSurface(host.current())->setFocus();
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
        phase("menu vertical alignment and library chrome");
        auto* menu = host.menuBar();
        integrationRequire(menu->height() >= 40 && menu->height() <= 44, "Menu bar height is outside 40-44 logical pixels");
        const QImage menuImage = menu->grab().toImage(); const qreal menuDpr = menuImage.devicePixelRatio(); QJsonArray centers;
        for (auto* action : menu->actions()) {
            if (!action->isVisible() || action->isSeparator()) continue;
            const QRect item = menu->actionGeometry(action);
            integrationRequire(qAbs(item.center().y() - menu->rect().center().y()) <= 1, "Menu hit rectangle is not vertically centered");
            const QRect pixels(QPoint(qRound(item.left() * menuDpr), qRound(item.top() * menuDpr)), QSize(qRound(item.width() * menuDpr), qRound(item.height() * menuDpr)));
            int inkTop = menuImage.height(), inkBottom = -1;
            for (int y = pixels.top(); y <= pixels.bottom() && y < menuImage.height(); ++y) for (int x = pixels.left(); x <= pixels.right() && x < menuImage.width(); ++x) {
                const QColor c = menuImage.pixelColor(x, y);
                if (c.red() < 100 && c.green() < 110 && c.blue() < 140) { inkTop = qMin(inkTop, y); inkBottom = qMax(inkBottom, y); }
            }
            integrationRequire(inkBottom >= inkTop && qAbs((inkTop + inkBottom) / (2.0 * menuDpr) - (menu->height() - 1) / 2.0) <= 3, "Painted menu glyphs are not vertically centered");
            centers.append(QJsonObject{{"menu", action->text()}, {"hitCenter", item.center().y()}, {"inkCenter", (inkTop + inkBottom) / (2.0 * menuDpr)}});
        }
        for (const char* name : {"LunaBrand", "LunaBrandIcon"}) {
            auto* brand = host.findChild<QWidget*>(QString::fromLatin1(name));
            integrationRequire(brand && qAbs(brand->mapTo(menu, brand->rect().center()).y() - menu->rect().center().y()) <= 1, "Brand icon/text is not vertically centered with menus");
        }
        auto* library = host.findChild<QWidget*>(QStringLiteral("LunaLibraryPanel")); integrationRequire(library, "Shared library missing");
        for (auto* widget : library->findChildren<QWidget*>()) integrationRequire(!widget->inherits("QProgressBar"), "Disk usage progress widget remains in the library");
        for (auto* label : library->findChildren<QLabel*>()) integrationRequire(!label->text().contains(QStringLiteral("디스크 사용 공간")) && !label->text().contains(QStringLiteral("저장 공간 정보 없음")), "Disk usage label remains in the library");
        result["menuGlyphAndBrandVerticalCenters"] = centers; result["diskUsageUiAbsent"] = true;
        integrationRequire(menu->grab().save(report + ".menu.png"), "Menu alignment capture failed");
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
            auto fitTabInput = [&] {
                focus->setChecked(true);
                const QSize available = host.screen()->availableGeometry().size();
                host.resize(qMin(760, available.width() - 24), available.height() - 48); host.move(8, 8);
                QCoreApplication::processEvents(); host.raise(); host.activateWindow();
                SetForegroundWindow(reinterpret_cast<HWND>(host.winId()));
            };
            phase("five compact tabs, scrolling and independent views");
            const QString directoryPath = QFileInfo(files[0]).absolutePath();
            const QStringList extraNames = QDir(directoryPath).entryList({QStringLiteral("UI-long-*.pdf")}, QDir::Files, QDir::Name);
            integrationRequire(extraNames.size() == 3, "Five-document fixture missing");
            QList<QPointer<Editor>> extra;
            for (const QString& name : extraNames) {
                QPointer<Editor> editor = host.open(QDir(directoryPath).filePath(name));
                integrationRequire(editor && integrationWait([&] { return ready(editor); }), "Extra PDF did not load"); extra.append(editor);
            }
            integrationRequire(host.documentCount() == 5, "Five PDFs are not simultaneously open");
            for (const QString& path : QStringList{files[0], files[1]} + QDir(directoryPath).entryList({QStringLiteral("UI-long-*.pdf")}, QDir::Files)) {
                const QString fullPath = QFileInfo(path).isAbsolute() ? path : QDir(directoryPath).filePath(path);
                QListWidgetItem* card = nullptr;
                integrationRequire(integrationList(host, fullPath, &card) && card && !card->data(Qt::UserRole + 3).toString().isEmpty(), "Opened date disappeared across Windows path spellings");
            }
            fitTabInput(); // Keep real OS input inside the hosted runner's 1024px desktop, including at 150%.
            QList<QPointer<Editor>> documents{a, b}; documents.append(extra);
            QList<int> scrollPositions; QList<double> zoomValues;
            for (int i = 0; i < documents.size(); ++i) {
                auto* editor = documents[i].data(); tabs->setCurrentWidget(editor);
                const double zoom = 1.25 + i * .1; auto* widget = editor->getProgramController()->getPdfWidget();
                widget->getDrawWidgetProxy()->zoom(zoom); QCoreApplication::processEvents();
                auto* scroll = widget->getVerticalScrollbar(); scroll->setValue(scroll->maximum() / 3);
                scrollPositions.append(scroll->value()); zoomValues.append(zoom);
                const QRect tab = tabs->tabBar()->tabRect(tabs->indexOf(editor));
                integrationRequire(tab.width() >= 160 && tab.width() <= 190 && tab.height() <= 38, "Whole tab bounds exceed compact design");
            }
            for (int turn = 0; turn < 10; ++turn) for (int i = 0; i < documents.size(); ++i) {
                tabs->setCurrentWidget(documents[i]); QCoreApplication::processEvents();
                auto* widget = documents[i]->getProgramController()->getPdfWidget();
                integrationRequire(qAbs(widget->getDrawWidgetProxy()->getZoom() - zoomValues[i]) < .001 && widget->getVerticalScrollbar()->value() == scrollPositions[i], "Five-tab switch changed independent position or zoom");
            }
            auto* tabBar = tabs->tabBar();
            integrationRequire(tabBar->usesScrollButtons() && tabBar->elideMode() == Qt::ElideMiddle, "Tab scroll/elision mode changed");
            bool scrolling = false; for (auto* button : tabBar->findChildren<QToolButton*>()) scrolling |= button->isVisible();
            integrationRequire(scrolling && tabs->tabText(4).contains(QChar(0x2026)), "Long tabs did not elide and scroll");
            QToolTip::hideText();
            integrationRequire(integrationMove(host, tabBar, tabBar->tabRect(4).center()) && integrationWait([&] { return QToolTip::isVisible() && QToolTip::text() == tabs->tabToolTip(4); }), "Actual tab hover did not show full filename tooltip");
            QToolTip::hideText();
            integrationRequire(host.screen()->grabWindow(0).save(report + ".five-tabs.png"), "Five-tab desktop capture failed");
            focus->setChecked(false); integrationLayouts(host, report + ".five-tabs"); fitTabInput();
            tabs->setCurrentWidget(extra[0]); integrationSurface(extra[0])->setFocus();
            integrationRequire(integrationKey('W', true) && integrationWait([&] { return host.isVisible() && host.documentCount() == 4 && (!extra[0] || tabs->indexOf(extra[0]) == -1); }), "Ctrl+W did not close only the middle of five tabs");
            tabs->setCurrentWidget(extra[1]);
            QWidget* closeButton = tabBar->tabButton(tabs->indexOf(extra[1]), QTabBar::RightSide);
            if (!closeButton) closeButton = tabBar->tabButton(tabs->indexOf(extra[1]), QTabBar::LeftSide);
            integrationRequire(closeButton && integrationClick(host, closeButton, closeButton->rect().center()) && integrationWait([&] { return host.isVisible() && host.documentCount() == 3 && (!extra[1] || tabs->indexOf(extra[1]) == -1); }), "Tab X closed the wrong document or window");
            integrationRequire(extra[2] && host.closeTab(tabs->indexOf(extra[2])) && host.documentCount() == 2, "Extra-tab cleanup damaged original documents");
            tabs->setCurrentWidget(a); pa->zoom(1.25); pb->zoom(1.5);
            result["fiveTabsBoundedElidedTooltipScrollAndIndependentViews"] = true;
            focus->setChecked(false);
            auto* toggle = host.findChild<QToolButton*>(QStringLiteral("LunaCombinedPanelToggle"));
            integrationRequire(toggle, "Combined panel button missing");
            for (int turn = 0; turn < 10; ++turn) {
                integrationRequire(!focus->isChecked() && toggle->text() == QStringLiteral("≪") && integrationClick(host, toggle, toggle->rect().center()) && integrationWait([&] { return focus->isChecked() && toggle->isVisible() && qAbs(toggle->mapTo(&host, QPoint()).x() - host.centralWidget()->x()) <= 8; }), "Real collapse click failed");
                integrationRequire(toggle->text() == QStringLiteral("☰") && qAbs(toggle->mapTo(&host, QPoint()).x() - host.centralWidget()->x()) <= 8, "Restore button moved away from left edge");
                integrationRequire(integrationClick(host, toggle, toggle->rect().center()) && integrationWait([&] { return !focus->isChecked(); }), "Real expand click failed");
            }
            result["tenRealPanelToggleCyclesPinnedLeft"] = true;
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
            phase("A highlight and real Ctrl+S"); integrationHighlight(host, a, report); result["realDragBlueBorderlessSelectionAndCtrlC"] = true;
            integrationRequire(integrationHighlights(b) == 0, "Integrated A annotation crossed to B");
            integrationRequire(integrationWait([&] { return ready(a); }), "A remained busy before close-cancel check");
            int cancelled = 0; QTimer cancel;
            QObject::connect(&cancel, &QTimer::timeout, &host, [&] {
                if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    if (auto* button = box->button(QMessageBox::Cancel)) { ++cancelled; button->click(); }
            });
            cancel.start(30); integrationSurface(a)->setFocus();
            integrationRequire(integrationKey('W', true), "Unsaved Ctrl+W injection failed");
            const bool cancelledKey = integrationWait([&] { return cancelled == 1 && !QApplication::activeModalWidget(); }); cancel.stop();
            integrationRequire(cancelledKey && host.isVisible() && host.documentCount() == 2 && a->getProgramController()->hasUnsavedChanges(), "Integrated Ctrl+W Cancel lost A");
            const int annotationsBeforeRetry = integrationHighlights(a);
            cancelled = 0; cancel.start(30); integrationSurface(a)->setFocus();
            integrationRequire(integrationKey('W', true) && integrationWait([&] { return cancelled == 1 && !QApplication::activeModalWidget(); }), "Repeated dirty Ctrl+W did not show Cancel"); cancel.stop();
            integrationRequire(host.current() == a && host.documentCount() == 2 && a->getProgramController()->hasUnsavedChanges() && integrationHighlights(a) == annotationsBeforeRetry, "Repeated Cancel changed the document or annotations");
            result["repeatedDirtyCtrlWCancelPreservesAnnotations"] = true;
            cancelled = 0; cancel.start(30);
            // Native caption metrics follow the Windows non-client DPI, which
            // need not equal QT_SCALE_FACTOR used for the client contents.
            const HWND nativeHost = reinterpret_cast<HWND>(host.winId());
            host.raise(); host.activateWindow(); SetForegroundWindow(nativeHost);
            integrationRequire(integrationWait([&] { return GetForegroundWindow() == nativeHost; }), "Caption close target did not become the foreground window");
            TITLEBARINFOEX captionInfo{}; captionInfo.cbSize = sizeof(captionInfo);
            SendMessage(nativeHost, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&captionInfo));
            RECT captionCloseRect = captionInfo.rgrect[5];
            POINT closePoint{(captionCloseRect.left + captionCloseRect.right) / 2, (captionCloseRect.top + captionCloseRect.bottom) / 2};
            RECT captionFrame{}; GetWindowRect(nativeHost, &captionFrame);
            MONITORINFO captionMonitor{}; captionMonitor.cbSize = sizeof(captionMonitor);
            integrationRequire(GetMonitorInfo(MonitorFromWindow(nativeHost, MONITOR_DEFAULTTONEAREST), &captionMonitor), "Caption monitor work area unavailable");
            // A Qt minimum width can exceed the hosted desktop at 150%.
            // Keep the actual native X on-screen rather than letting Windows
            // clamp SetCursorPos to another application's caption button.
            if (!PtInRect(&captionMonitor.rcWork, closePoint)) {
                const int dx = qBound(int(captionMonitor.rcWork.left + 4), int(closePoint.x), int(captionMonitor.rcWork.right - 4)) - closePoint.x;
                const int dy = qBound(int(captionMonitor.rcWork.top + 4), int(closePoint.y), int(captionMonitor.rcWork.bottom - 4)) - closePoint.y;
                integrationRequire(SetWindowPos(nativeHost, HWND_TOP, captionFrame.left + dx, captionFrame.top + dy, 0, 0, SWP_NOSIZE), "Could not bring native caption on-screen");
                QCoreApplication::processEvents();
                SendMessage(nativeHost, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&captionInfo));
                captionCloseRect = captionInfo.rgrect[5];
                closePoint = {(captionCloseRect.left + captionCloseRect.right) / 2, (captionCloseRect.top + captionCloseRect.bottom) / 2};
            }
            integrationRequire(captionCloseRect.right > captionCloseRect.left && captionCloseRect.bottom > captionCloseRect.top &&
                SendMessage(nativeHost, WM_NCHITTEST, 0, MAKELPARAM(closePoint.x, closePoint.y)) == HTCLOSE,
                "Windows caption close button geometry did not hit HTCLOSE");
            integrationRequire(SetCursorPos(closePoint.x, closePoint.y), "Could not move to native caption close button");
            POINT captionCursor{}; GetCursorPos(&captionCursor);
            const HWND captionTarget = GetAncestor(WindowFromPoint(captionCursor), GA_ROOT);
            result["captionCloseInput"] = QJsonObject{{"x", int(closePoint.x)}, {"y", int(closePoint.y)},
                {"actualX", int(captionCursor.x)}, {"actualY", int(captionCursor.y)}, {"targetIsShell", captionTarget == nativeHost}}; write();
            integrationRequire(captionCursor.x == closePoint.x && captionCursor.y == closePoint.y && captionTarget == nativeHost,
                "Native caption click did not target the shell at the requested position");
            INPUT caption[2]{}; caption[0].type = caption[1].type = INPUT_MOUSE; caption[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; caption[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            integrationRequire(SendInput(2, caption, sizeof(INPUT)) == 2 && integrationWait([&] { return cancelled == 1 && !QApplication::activeModalWidget(); }), "Native title-bar X did not run close confirmation");
            const bool windowClosed = !host.isVisible(); cancel.stop();
            integrationRequire(!windowClosed && cancelled == 1 && host.documentCount() == 2, "Integrated window Cancel removed a tab");
            SetWindowPos(nativeHost, HWND_TOP, captionFrame.left, captionFrame.top, 0, 0, SWP_NOSIZE);
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
            const QByteArray savedA = integrationBytes(files[0]); int warnings = 0, unexpectedSaveMessages = 0;
            auto* nativeSaveAs = b->findChild<QAction*>(QStringLiteral("actionSave_As"));
            integrationRequire(integrationWait([&] { return ready(b) && nativeSaveAs && nativeSaveAs->isEnabled(); }), "B Save As remained busy after search");
            auto saveAs = [&](const QString& target) {
                bool selected = false; QTimer picker;
                QObject::connect(&picker, &QTimer::timeout, &host, [&] {
                    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                        if (auto* yes = box->button(QMessageBox::Yes)) yes->click();
                        else if (auto* ok = box->button(QMessageBox::Ok)) {
                            if (box->windowTitle() == QStringLiteral("저장 경로 충돌")) ++warnings;
                            else ++unexpectedSaveMessages;
                            result["saveAsMessageTitle"] = box->windowTitle(); write(); ok->click();
                        }
                    } else if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                        if (!selected) { selected = true; dialog->setDirectory(QFileInfo(target).absolutePath()); dialog->selectFile(target);
                            if (auto* field = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) field->setText(QFileInfo(target).absoluteFilePath());
                            QTimer::singleShot(0, dialog, [dialog] { QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection); }); }
                    }
                }); picker.start(30); b->findChild<QAction*>(QStringLiteral("actionSave_As"))->trigger(); picker.stop(); return selected;
            };
            const bool selectedCollision = saveAs(files[0]);
            result["collisionDialogSelected"] = selectedCollision; result["collisionWarnings"] = warnings;
            result["unexpectedSaveMessages"] = unexpectedSaveMessages;
            result["collisionAUnchanged"] = integrationBytes(files[0]) == savedA;
            result["collisionBPathUnchanged"] = DocumentClaim::identity(b->getProgramController()->getOriginalFileName()) == DocumentClaim::identity(files[1]); write();
            integrationRequire(selectedCollision && warnings == 1 && unexpectedSaveMessages == 0 && result["collisionAUnchanged"].toBool() && result["collisionBPathUnchanged"].toBool(), "Save As collision changed A or rebound B");
            const QString copy = QFileInfo(files[1]).absolutePath() + "/B-copy.pdf";
            integrationRequire(saveAs(copy) && unexpectedSaveMessages == 0 && QFileInfo::exists(copy) && DocumentClaim::identity(b->getProgramController()->getOriginalFileName()) == DocumentClaim::identity(copy), "Save As did not update B path");
            integrationRequire(host.open(copy) == b && host.documentCount() == 2, "Saved path was not deduplicated");
            auto* originalB = host.open(files[1]); integrationRequire(originalB && originalB != b && host.documentCount() == 3, "Old B path was not released after Save As");
            integrationRequire(integrationWait([&] { return ready(originalB); }), "Original B did not load after Save As");
            // Real Windows Ctrl+W must close the active PDF tab, not the LUNA
            // application or any other tab.
            originalB->getProgramController()->getPdfWidget()->setFocus();
            integrationRequire(integrationKey('W', true), "Real Ctrl+W injection failed for original B");
            integrationRequire(integrationWait([&] {
                return host.isVisible() && host.documentCount() == 2 &&
                    tabs->indexOf(originalB) == -1 && tabs->indexOf(a) >= 0 &&
                    tabs->indexOf(b) >= 0;
            }), "Ctrl+W closed LUNA or the wrong PDF tab");
            tabs->setCurrentWidget(b);
            b->getProgramController()->getPdfWidget()->setFocus();
            integrationRequire(integrationKey('W', true), "Real Ctrl+W injection failed for B copy");
            integrationRequire(integrationWait([&] {
                return host.isVisible() && host.documentCount() == 1 &&
                    tabs->indexOf(b) == -1 && tabs->indexOf(a) >= 0;
            }), "Second Ctrl+W closed LUNA or damaged A");
            result["ctrlWClosesOnlyActiveTabAndPreservesWindow"] = true;
            result["rootShortcutsSearchSaveAsAndSurvivor"] = true;
            phase("inline memo, search and paired move");
            integrationRequire(integrationWait([&] { list = integrationList(host, files[0], &paper); return list && paper; }), "A paper card missing before memo"); list->scrollToItem(paper);
            const QRect card = list->visualItemRect(paper);
            integrationRequire(card.height() == 96, "Paper card height differs from the compact design");
            integrationRequire(integrationClick(host, list->viewport(), {card.left() + 100, card.top() + 81}), "Real memo mouse input failed");
            QLineEdit* edit = nullptr; integrationRequire(integrationWait([&] { edit = list->findChild<QLineEdit*>(QStringLiteral("LunaPaperMemoEditor")); return edit && edit->hasFocus(); }), "Inline memo editor did not focus");
            integrationRequire(integrationType(QStringLiteral("초기 메모")) && integrationKey(VK_RETURN), "Real memo typing/Enter failed");
            integrationRequire(integrationWait([&] { return paper->data(Qt::UserRole + 4).toString() == QStringLiteral("초기 메모"); }), "Memo Enter did not persist");
            integrationRequire(integrationClick(host, list->viewport(), {card.left() + 100, card.top() + 81}), "Memo edit mouse input failed");
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
        phase("repeated Ctrl+W across two, one and empty documents");
        const QString reopenPath = host.current()->getProgramController()->getOriginalFileName();
        auto keyClose = [&](int focusKind) {
            auto* focus = host.findChild<QAction*>(QStringLiteral("LunaFocusMode")); focus->setChecked(focusKind == 0 || focusKind == 3);
            if (focus->isChecked()) { const QSize available = host.screen()->availableGeometry().size(); host.resize(available - QSize(24, 48)); host.move(8, 8); }
            host.raise(); host.activateWindow(); SetForegroundWindow(reinterpret_cast<HWND>(host.winId())); QCoreApplication::processEvents();
            if (focusKind == 0) integrationSurface(host.current())->setFocus();
            else if (focusKind == 1) {
                auto* query = host.findChild<QLineEdit*>(QStringLiteral("LunaGlobalSearch"));
                integrationRequire(integrationClick(host, query, query->rect().center()), "Search focus click failed");
                integrationRequire(integrationWait([&] { return query->hasFocus(); }), "Search did not receive focus");
            } else if (focusKind == 2) {
                QListWidgetItem* card = nullptr; auto* list = integrationList(host, reopenPath, &card);
                integrationRequire(list && integrationClick(host, list->viewport(), QPoint(20, 20)), "Paper-list focus click failed");
                integrationRequire(integrationWait([&] { return list->hasFocus(); }), "Paper list did not receive focus");
            } else if (auto* widget = QApplication::focusWidget()) widget->clearFocus();
            const int delivered = host.property("LunaCtrlWKeys").toInt();
            integrationRequire(integrationKey('W', true) && integrationWait([&] { return host.property("LunaCtrlWKeys").toInt() == delivered + 1; }), "Ctrl+W was not intercepted in the actual focus context");
            QElapsedTimer settle; settle.start(); integrationWait([&] { return settle.elapsed() >= 120; }, 2000);
            DWORD processCode = 0;
            integrationRequire(host.isVisible() && IsWindowVisible(reinterpret_cast<HWND>(host.winId())) &&
                GetExitCodeProcess(GetCurrentProcess(), &processCode) && processCode == STILL_ACTIVE && !host.property("LunaAboutToQuit").toInt(), "Ctrl+W terminated the native window/process");
            host.trace("close-key-observed", host.current());
        };
        if (!restore) {
            auto* second = host.open(files[1]); integrationRequire(second && integrationWait([&] { return ready(second); }) && host.documentCount() == 2, "Two-document repeated-close setup failed");
            keyClose(0); integrationRequire(host.documentCount() == 1, "First of two Ctrl+W keys closed the wrong document");
            keyClose(1); integrationRequire(host.documentCount() == 0, "Second Ctrl+W did not leave an empty window");
            keyClose(2); integrationRequire(host.documentCount() == 0, "Third Ctrl+W changed the empty document state");
        } else {
            keyClose(0); integrationRequire(host.documentCount() == 0, "Single-document Ctrl+W did not leave an empty window");
            keyClose(1); keyClose(2);
        }
        result["ctrlWLastPdfLeavesEmptyLunaWindow"] = true;
        const QPointer<Editor> emptyEditor = host.current(); const int creations = host.property("LunaEditorCreations").toInt();
        for (int press = 0; press < 10; ++press) {
            host.trace("empty-repeat-before", host.current()); keyClose(press % 4); host.trace("empty-repeat-after", host.current());
            integrationRequire(host.current() == emptyEditor && host.property("LunaEditorCreations").toInt() == creations && host.documentCount() == 0, "Empty Ctrl+W repeatedly creates and disposes Editors");
        }
        result["emptyCtrlWEditorCreationsBefore"] = creations; result["emptyCtrlWEditorCreationsAfter"] = host.property("LunaEditorCreations").toInt();
        result["emptyCtrlWTenStable"] = true;
        auto* reopened = host.open(reopenPath);
        integrationRequire(reopened == emptyEditor && integrationWait([&] { return ready(reopened); }) && integrationHighlights(reopened) == 1, "Reopening after repeated empty Ctrl+W lost the document or annotation");
        auto* query = host.findChild<QLineEdit*>(QStringLiteral("LunaGlobalSearch")); query->setFocus();
        integrationRequire(integrationKey('F', true), "PDF search focus setup failed"); QPointer<QDialog> find;
        integrationRequire(integrationWait([&] { for (auto* d : reopened->findChildren<QDialog*>()) if (d->isVisible() && d->inherits("pdf::PDFFindTextToolDialog")) find = d; return bool(find); }), "PDF search popup did not reopen");
        const int delivered = host.property("LunaCtrlWKeys").toInt();
        integrationRequire(find->findChild<QLineEdit*>()->hasFocus() && integrationKey('W', true) && integrationWait([&] { return host.property("LunaCtrlWKeys").toInt() == delivered + 1 && host.documentCount() == 0 && host.isVisible(); }), "Ctrl+W from PDF search did not safely close its document");
        result["repeatedCtrlWFocusReopenAndPdfSearch"] = true;
        auto* fileCloseEditor = host.open(reopenPath);
        integrationRequire(fileCloseEditor && integrationWait([&] { return ready(fileCloseEditor); }), "Native File Close reopen failed");
        auto* fileClose = fileCloseEditor->findChild<QAction*>(QStringLiteral("actionClose"));
        integrationRequire(fileClose && fileClose->isEnabled(), "Original native File Close is unavailable"); fileClose->trigger();
        integrationRequire(integrationWait([&] { return host.isVisible() && host.documentCount() == 0 && host.current() == fileCloseEditor && !fileCloseEditor->getProgramController()->getDocument(); }), "Native File Close left stale ownership or closed LUNA");
        host.trace("native-file-close-observed", host.current()); result["nativeFileCloseKeepsEmptyReader"] = true;
        phase("user sequence: close third and initial Editor, preserve second");
        QTemporaryDir sequenceFiles; integrationRequire(sequenceFiles.isValid(), "User-sequence temporary directory failed");
        QStringList sequencePaths;
        for (const auto* name : {"A.pdf", "B.pdf", "C.pdf"}) {
            const QString path = sequenceFiles.filePath(QString::fromLatin1(name));
            integrationRequire(QFile::copy(reopenPath, path), "User-sequence disposable PDF copy failed"); sequencePaths.append(path);
        }
        QPointer<Editor> sequenceA = host.open(sequencePaths[0], "user-sequence-explicit-open");
        integrationRequire(sequenceA && integrationWait([&] { return ready(sequenceA); }), "Sequence A did not load");
        QPointer<Editor> sequenceB = host.open(sequencePaths[1], "user-sequence-explicit-open");
        integrationRequire(sequenceB && integrationWait([&] { return ready(sequenceB); }), "Sequence B did not load");
        QPointer<Editor> sequenceC = host.open(sequencePaths[2], "user-sequence-explicit-open");
        integrationRequire(sequenceC && integrationWait([&] { return ready(sequenceC); }), "Sequence C did not load");
        integrationSurface(sequenceC)->setFocus(); integrationRequire(integrationKey('W', true) && integrationWait([&] { return !sequenceC && host.documentCount() == 2; }), "Sequence third Editor did not close");
        tabs->setCurrentWidget(sequenceA); integrationSurface(sequenceA)->setFocus();
        integrationRequire(integrationKey('W', true) && integrationWait([&] { return !sequenceA && host.current() == sequenceB && host.documentCount() == 1; }), "Sequence initial Editor close damaged second Editor");
        integrationRequire(ready(sequenceB) && integrationHighlights(sequenceB) == 1 && IsWindowVisible(reinterpret_cast<HWND>(host.winId())), "Sequence surviving document or native shell disappeared");
        integrationRequire(integrationKey('F', true) && integrationWait([&] { for (auto* dialog : sequenceB->findChildren<QDialog*>()) if (dialog->isVisible() && dialog->inherits("pdf::PDFFindTextToolDialog")) { dialog->reject(); return true; } return false; }), "Search failed after destroying the initial Editor");
        host.trace("user-sequence-survivor", sequenceB); result["userSequenceThirdThenInitialEditorDestroyedSurvivorSearch"] = true;
        integrationSurface(sequenceB)->setFocus(); integrationRequire(integrationKey('W', true) && integrationWait([&] { return host.documentCount() == 0 && host.current() == sequenceB; }), "Sequence final close did not retain empty Editor");
        integrationRequire(integrationKey('Q', true) && integrationWait([&] { return !host.isVisible() && host.property("LunaClosing").toBool(); }), "Real Ctrl+Q did not exit LUNA normally");
        result["ctrlQNormalExit"] = true; result["accepted"] = true; phase("passed"); qApp->exit(0);
    } catch (const std::exception& error) {
        result["error"] = QString::fromUtf8(error.what()); result["accepted"] = false; write();
        if (host.screen()) host.screen()->grabWindow(0).save(report + ".failure.png"); qApp->exit(1);
    }
}

#endif
int runTabbedLibrary(const QStringList& files, const QString& report, bool restore) {
    qApp->setProperty("lunaNativeSessions", true);
    if (!report.isEmpty()) qApp->setProperty("LunaLifecycleTracePath", report + ".lifecycle.jsonl");
    const bool previousQuitPolicy = qApp->quitOnLastWindowClosed();
    qApp->setQuitOnLastWindowClosed(false);
    NativeTabs host;
    int opened = 0;
    const QStringList initial = !report.isEmpty() && !restore ? files.mid(0, 1) : files;
    for (const auto& file : initial) if (host.open(file)) ++opened;
    if (!files.isEmpty() && !opened) { qApp->setQuitOnLastWindowClosed(previousQuitPolicy); return 0; }
    host.show();
    if (!report.isEmpty()) {
#ifdef Q_OS_WIN
        QTimer::singleShot(0, &host, [&] { verifyIntegration(host, files, report, restore); });
#else
        return 2;
#endif
    }
    const int result = qApp->exec();
    qApp->setQuitOnLastWindowClosed(previousQuitPolicy); return result;
}
}
