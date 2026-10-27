// SPDX-License-Identifier: MIT
// Native Editor sessions: ownership and close flow validated by Astra's proof.
#include "lunatabs.h"
#include "lunalibrary.h"
#include "lunadocumentclaim.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfrecentfilemanager.h"
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
#include <QPointer>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <memory>
#include <vector>

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
            if ((!id.isEmpty() && s->identity == id) ||
                (!id.isEmpty() && DocumentClaim::identity(s->editor->getProgramController()->getOriginalFileName()) == id)) return s.get();
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
            if (s->dispatchingOpen ||
                (identity == s->identity && (editor->property("LunaMoveRestorePending").toBool() || !editor->getProgramController()->getDocument()))) return;
            *handled = true; open(path);
        }, Qt::DirectConnection);
        connect(controller, &pdfviewer::PDFProgramController::lunaDocumentLoadFinished, this, [this, editor](bool success) {
            auto* s = session(editor); if (!s || closing_) return;
            s->loading = false;
            if (!success) {
                s->claim.reset(); s->identity.clear();
                QTimer::singleShot(0, editor, [this, editor] { if (!closing_ && session(editor) && tabs_->count() > 1) dispose(editor); });
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
    void activate(Editor* editor) { if (!editor || closing_) return; tabs_->setCurrentWidget(editor); if (isMinimized()) showNormal(); raise(); activateWindow(); activateCurrent(); }
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
        const int index = tabs_->indexOf(editor); if (index >= 0) { tabs_->setTabText(index, text); tabs_->setTabToolTip(index, display); }
        if (editor == current()) setWindowTitle(QStringLiteral("LUNA Library") + (display.isEmpty() ? QString() : QStringLiteral(" · ") + text));
    }
    void synchronize() {
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
        const auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("PDF 열기"), {}, QStringLiteral("PDF (*.pdf *.PDF)"));
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
    std::vector<std::unique_ptr<Session>> sessions_;
};
int runTabbedLibrary(const QStringList& files, const QString& report) {
    Q_UNUSED(report);
    qApp->setProperty("lunaNativeSessions", true);
    NativeTabs host;
    int opened = 0; for (const auto& file : files) if (host.open(file)) ++opened;
    if (!files.isEmpty() && !opened) return 0;
    host.show(); return qApp->exec();
}
}
