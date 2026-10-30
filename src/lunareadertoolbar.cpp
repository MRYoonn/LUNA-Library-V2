// SPDX-License-Identifier: MIT
// Presentation and input adapters only; PDF4QT owns all document operations.
#include "lunareadertoolbar.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"

#include <QAction>
#include <QFile>
#include <QSvgRenderer>
#include <QComboBox>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFont>
#include <QIconEngine>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <memory>
#include <utility>

namespace luna {
namespace {
// One scalable stroke style, including disabled/checked states and high DPI.
class ReaderIcon final : public QIconEngine {
public:
    explicit ReaderIcon(QString name) {
        star_ = name == QStringLiteral("star");
        const bool inlineMemo = name == QStringLiteral("memo-inline");
        if (inlineMemo) name = QStringLiteral("note");
        if (name == QStringLiteral("panel")) name = QStringLiteral("panel-close");
        ink_ = name == QStringLiteral("library") ? QByteArray("#2671C6") :
            (QStringList{QStringLiteral("folder"), QStringLiteral("documents"), QStringLiteral("recent"), QStringLiteral("star")}.contains(name) ? QByteArray("#265A9C") : QByteArray("#173869"));
        if (inlineMemo) ink_ = "#6C8CB6";
        QFile source(QStringLiteral(":/luna/icons/ui-%1.svg").arg(name));
        if (source.open(QIODevice::ReadOnly)) svg_ = source.readAll();
    }
    QIconEngine* clone() const override { return new ReaderIcon(*this); }
    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size); result.fill(Qt::transparent);
        QPainter painter(&result); paint(&painter, QRect(QPoint(), size), mode, state); return result;
    }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        QByteArray svg = svg_;
        if (star_ && mode == QIcon::Selected) svg.replace("fill=\"none\"", "fill=\"currentColor\"");
        svg.replace("currentColor", mode == QIcon::Disabled ? QByteArray(star_ ? "#91A3BD" : "#9CAAC0") :
            (state == QIcon::On || mode == QIcon::Selected ? QByteArray(star_ ? "#337ADA" : "#1C65BC") : ink_));
        QSvgRenderer renderer(svg); renderer.setAspectRatioMode(Qt::KeepAspectRatio);
        const QRectF bounds = rect.width() > rect.height() ? QRectF(rect.left() + 2, rect.top(), rect.height(), rect.height()) : QRectF(rect);
        renderer.render(painter, bounds);
    }
private:
    QByteArray svg_, ink_;
    bool star_ = false;
};

QAction* nativeAction(pdfviewer::PDFEditorMainWindow& window, const char* name) {
    return window.findChild<QAction*>(QString::fromLatin1(name));
}

bool inMenu(pdfviewer::PDFEditorMainWindow& window, QAction* action) {
    for (auto* menu : window.findChildren<QMenu*>()) if (menu->actions().contains(action)) return true;
    return false;
}

class ReadingInteraction final : public QObject {
public:
    explicit ReadingInteraction(pdfviewer::PDFEditorMainWindow& window) : QObject(&window), window_(window) {
        auto* controller = window_.getProgramController();
        draw_ = controller->getPdfWidget()->getDrawWidget()->getWidget();
        draw_->installEventFilter(this);
        auto afterAnnotation = [this](const pdf::PDFModifiedDocument&) {
            if (annotationMode()) QTimer::singleShot(0, this, [this] { selectText(); });
        };
        connect(controller->getToolManager(), &pdf::PDFToolManager::documentModified, this, afterAnnotation);
        connect(controller->getPdfWidget()->getAnnotationManager(), &pdf::PDFWidgetAnnotationManager::documentModified, this, afterAnnotation);
        connect(&window_, &QWidget::windowTitleChanged, this, [this] {
            target_ = {}; // A document/title transition cancels an unfinished click.
            QTimer::singleShot(0, this, [this] {
                auto* c = window_.getProgramController();
                if (c->getDocument() && !c->getToolManager()->getActiveTool()) selectText();
            });
        });
    }
private:
    bool checked(const char* name) const { auto* a = nativeAction(window_, name); return a && a->isChecked(); }
    bool annotationMode() const { return checked("actionCreateTextHighlight") || checked("actionStickyNoteComment") || checked("actionDeleteAnnotation"); }
    void selectText() {
        if (auto* action = nativeAction(window_, "actionSelectText"))
            if (action->isEnabled() && !action->isChecked()) action->trigger();
    }
    pdf::PDFObjectReference hit(QPoint point, bool deleting, bool* editable) {
        auto* c = window_.getProgramController();
        if (!c->getDocument()) return {};
        auto* widget = c->getPdfWidget(); auto* manager = widget->getAnnotationManager();
        const auto snapshot = widget->getDrawWidgetProxy()->getSnapshot();
        for (const auto& page : snapshot.items) {
            const auto& annotations = manager->getPageAnnotations(page.pageIndex).annotations;
            for (auto it = annotations.rbegin(); it != annotations.rend(); ++it) {
                const auto& item = *it;
                if (deleting ? !manager->canDeleteAnnotation(item) :
                    (item.annotation->getType() != pdf::AnnotationType::Text || !manager->isAnnotationSelectable(item))) continue;
                QRectF rectangle = item.annotation->getRectangle();
                const auto matrix = manager->prepareTransformations(page.pageToDeviceMatrix, widget,
                    item.annotation->getEffectiveFlags(), c->getDocument()->getCatalog()->getPage(page.pageIndex), rectangle);
                if (matrix.mapRect(rectangle).contains(point)) {
                    *editable = manager->canModifyAnnotation(item);
                    return item.annotation->getSelfReference();
                }
            }
        }
        return {};
    }
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object != draw_) return false;
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) &&
            static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            target_ = {}; selectText(); return false;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            target_ = {};
            if (mouse->button() != Qt::LeftButton || (!checked("actionSelectText") && !checked("actionDeleteAnnotation"))) return false;
            deleting_ = checked("actionDeleteAnnotation");
            target_ = hit(mouse->pos(), deleting_, &editable_); press_ = mouse->pos();
            return target_.isValid();
        }
        if (event->type() == QEvent::MouseButtonRelease && target_.isValid()) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const auto reference = target_; target_ = {};
            if (mouse->button() != Qt::LeftButton || (mouse->pos() - press_).manhattanLength() >= QApplication::startDragDistance()) return true;
            if (!checked(deleting_ ? "actionDeleteAnnotation" : "actionSelectText")) return true;
            auto* c = window_.getProgramController(); auto* manager = c->getPdfWidget()->getAnnotationManager();
            manager->selectAnnotation(reference, true);
            if (deleting_) { manager->deleteSelectedAnnotations(); selectText(); return true; }
            // Native note popup plus a route to its existing annotation editor.
            struct PopupState { bool waiting = true; bool edit = false; };
            auto state = std::make_shared<PopupState>();
            QTimer::singleShot(0, this, [this, state] {
                if (!state->waiting) return;
                auto* popup = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!popup) popup = qobject_cast<QDialog*>(QApplication::activePopupWidget());
                if (!popup || popup->parentWidget() != draw_ || !popup->windowFlags().testFlag(Qt::Popup) || !popup->layout()) return;
                auto* button = new QPushButton(QStringLiteral("메모 편집…"), popup);
                button->setObjectName(QStringLiteral("LunaEditNote")); button->setEnabled(editable_);
                popup->layout()->addWidget(button);
                connect(button, &QPushButton::clicked, popup, [popup, state] { state->edit = true; popup->accept(); });
                popup->adjustSize();
            });
            QMouseEvent open(QEvent::MouseButtonDblClick, mouse->position(), mouse->globalPosition(),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            open.ignore(); manager->mouseDoubleClickEvent(draw_, &open);
            state->waiting = false;
            if (state->edit) {
                c->getToolManager()->setActiveTool(nullptr);
                manager->selectAnnotation(reference, true);
                QKeyEvent key(QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier);
                manager->keyPressEvent(draw_, &key);
            }
            manager->clearSelection(); selectText(); return true;
        }
        return false;
    }
    pdfviewer::PDFEditorMainWindow& window_;
    QWidget* draw_ = nullptr;
    pdf::PDFObjectReference target_;
    QPoint press_;
    bool deleting_ = false, editable_ = false;
};

// Shell-owned presentation only. Native QAction ownership and each Editor's
// QWidgetAction/defaultWidget remain in that Editor. Never share a native
// defaultWidget between two QToolBars or reparent a document's toolbar.
// Keep the native spin-box validator and keyboard handling. Its frameless
// editor needs explicit bounds inside the styled, buttonless toolbar field.
class CommonPageInput final : public QSpinBox {
protected:
    bool event(QEvent* event) override {
        const bool handled = QSpinBox::event(event);
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::StyleChange) {
            lineEdit()->setGeometry(rect().adjusted(4, 1, -4, -1));
            lineEdit()->show();
        }
        return handled;
    }
};

class CommonReaderToolbar final : public QObject {
public:
    explicit CommonReaderToolbar(QMainWindow& shell) : QObject(&shell), shell_(shell) {
        setObjectName(QStringLiteral("LunaCommonReaderAdapter"));
        bar_ = new QToolBar(QStringLiteral("읽기"), &shell);
        bar_->setObjectName(QStringLiteral("LunaCommonReaderToolbar"));
        bar_->setMovable(false); bar_->setFloatable(false);
        bar_->setAllowedAreas(Qt::TopToolBarArea);
        bar_->toggleViewAction()->setVisible(false);
        bar_->setToolButtonStyle(Qt::ToolButtonIconOnly); bar_->setIconSize(QSize(21, 21));
        bar_->setFixedHeight(51); // Exact final HTML toolbar height.
        shell.addToolBar(Qt::TopToolBarArea, bar_);
        page_ = new CommonPageInput; page_->setObjectName(QStringLiteral("LunaCommonPage"));
        page_->setButtonSymbols(QAbstractSpinBox::NoButtons); page_->setAlignment(Qt::AlignCenter);
        page_->setFixedWidth(68); page_->setToolTip(QStringLiteral("현재 페이지 / 전체 페이지 · Enter로 이동"));
        page_->setAccessibleName(QStringLiteral("현재 페이지 / 전체 페이지"));
        pageAction_ = new QWidgetAction(bar_); pageAction_->setText(QStringLiteral("페이지 이동")); pageAction_->setDefaultWidget(page_);
        zoom_ = new QComboBox; zoom_->setObjectName(QStringLiteral("LunaCommonZoom"));
        zoom_->setEditable(true); zoom_->setFixedWidth(80);
        zoom_->addItems({QStringLiteral("100%"), QStringLiteral("125%"), QStringLiteral("150%"), QStringLiteral("200%"), QStringLiteral("250%")});
        zoom_->setToolTip(QStringLiteral("PDF 확대 비율 · 직접 입력 가능")); zoom_->setAccessibleName(QStringLiteral("확대 비율"));
        zoomAction_ = new QWidgetAction(bar_); zoomAction_->setText(QStringLiteral("확대 비율")); zoomAction_->setDefaultWidget(zoom_);
        for (auto& separator : separators_) { separator = new QAction(bar_); separator->setSeparator(true); }
        connect(page_, &QSpinBox::editingFinished, this, [this] {
            if (!editor_ || !page_->isEnabled()) return;
            if (auto* native = editor_->findChild<QSpinBox*>(QStringLiteral("pageNumberSpinBox"))) {
                native->setValue(page_->value());
                // The Editor handler is an ordinary private method, not a
                // meta-object slot. Use the native widget's existing connection.
                native->editingFinished();
            }
        });
        auto commitZoom = [this] {
            if (!editor_ || !zoom_->isEnabled()) return;
            QString value = zoom_->currentText(); value.remove(QLatin1Char('%'));
            bool ok = false; const double percent = value.trimmed().toDouble(&ok);
            if (ok) if (auto* native = editor_->findChild<QDoubleSpinBox*>(QStringLiteral("pageZoomSpinBox"))) {
                native->setValue(qBound(native->minimum(), percent, native->maximum()));
                native->editingFinished();
            }
        };
        connect(zoom_, qOverload<int>(&QComboBox::activated), this, [commitZoom](int) { commitZoom(); });
        connect(zoom_->lineEdit(), &QLineEdit::editingFinished, this, commitZoom);
        connect(&poll_, &QTimer::timeout, this, [this] { synchronize(); }); poll_.start(100);
    }
    void bind(pdfviewer::PDFEditorMainWindow& editor) {
        if (editor_ == &editor) { bar_->show(); synchronize(); return; }
        // Finish an old tab's view edit before changing its recipient. A late
        // focus-out must never apply one document's page/zoom text to another.
        page_->clearFocus(); zoom_->lineEdit()->clearFocus(); zoom_->clearFocus();
        if (editor_) editor_->removeEventFilter(this);
        for (auto* action : bar_->actions()) bar_->removeAction(action);
        editor_ = &editor;
        editor.installEventFilter(this);
        auto add = [this](const char* name) { if (auto* action = nativeAction(*editor_, name)) bar_->addAction(action); };
        if (auto* open = shell_.findChild<QAction*>(QStringLiteral("LunaOpenDocuments"), Qt::FindDirectChildrenOnly)) bar_->addAction(open);
        add("actionSave"); bar_->addAction(separators_[0]);
        add("actionGoToPreviousPage"); bar_->addAction(pageAction_); add("actionGoToNextPage"); bar_->addAction(separators_[1]);
        add("actionZoom_Out"); bar_->addAction(zoomAction_); add("actionZoom_In"); bar_->addAction(separators_[2]);
        add("actionSelectText"); add("actionCreateTextHighlight"); add("actionStickyNoteComment"); add("actionDeleteAnnotation"); add("LunaCropThumbnail");
        bar_->addAction(separators_[3]); add("actionFind"); add("actionPrint");
        bar_->show();
        synchronize();
    }
    void unbind() {
        poll_.stop(); if (editor_) editor_->removeEventFilter(this); editor_.clear();
        for (auto* action : bar_->actions()) bar_->removeAction(action);
    }
private:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == editor_.data() && event->type() == QEvent::EnabledChange) bar_->setEnabled(editor_->isEnabled());
        return QObject::eventFilter(watched, event);
    }
    void synchronize() {
        if (!editor_ || !editor_->getProgramController()) return;
        bar_->setEnabled(editor_->isEnabled());
        auto* nativePage = editor_->findChild<QSpinBox*>(QStringLiteral("pageNumberSpinBox"));
        auto* nativeCount = editor_->findChild<QLabel*>(QStringLiteral("pageNumberLabel"));
        auto* nativeZoom = editor_->findChild<QDoubleSpinBox*>(QStringLiteral("pageZoomSpinBox"));
        if (!nativePage || !nativeCount || !nativeZoom) return;
        page_->setEnabled(nativePage->isEnabled()); zoom_->setEnabled(nativePage->isEnabled());
        {
            const QSignalBlocker block(page_);
            const bool hasDocument = bool(editor_->getProgramController()->getDocument());
            const int minimum = hasDocument ? nativePage->minimum() : 0;
            const int maximum = hasDocument ? nativePage->maximum() : 0;
            if (page_->minimum() != minimum || page_->maximum() != maximum) page_->setRange(minimum, maximum);
            const QString suffix = nativeCount->text().isEmpty() ? QStringLiteral(" / 0") : nativeCount->text();
            // setSuffix refreshes the line editor even for unchanged text.
            // Polling must preserve a focused user's selection and pending input.
            if (page_->suffix() != suffix) page_->setSuffix(suffix);
            // A new window can initially focus this control before loading
            // completes. Update its valid range/count even then, without
            // overwriting a user's in-progress page value on later ticks.
            if (!page_->hasFocus()) page_->setValue(hasDocument ? nativePage->value() : 0);
        }
        if (!zoom_->hasFocus() && !zoom_->lineEdit()->hasFocus()) {
            const QSignalBlocker block(zoom_); const double value = nativeZoom->value();
            zoom_->setCurrentText(QString::number(value, 'f', qFuzzyIsNull(value - qRound(value)) ? 0 : 1) + QLatin1Char('%'));
        }
    }
    QMainWindow& shell_;
    QToolBar* bar_ = nullptr;
    QPointer<pdfviewer::PDFEditorMainWindow> editor_;
    QSpinBox* page_ = nullptr;
    QComboBox* zoom_ = nullptr;
    QWidgetAction *pageAction_ = nullptr, *zoomAction_ = nullptr;
    QAction* separators_[4]{};
    QTimer poll_;
};
}

QIcon readerIcon(const QString& name) { return QIcon(new ReaderIcon(name)); }

void bindCommonReaderToolbar(QMainWindow& shell, pdfviewer::PDFEditorMainWindow& editor) {
    auto* adapter = dynamic_cast<CommonReaderToolbar*>(shell.findChild<QObject*>(QStringLiteral("LunaCommonReaderAdapter")));
    if (!adapter) adapter = new CommonReaderToolbar(shell);
    adapter->bind(editor);
}
void unbindCommonReaderToolbar(QMainWindow& shell) {
    if (auto* adapter = dynamic_cast<CommonReaderToolbar*>(shell.findChild<QObject*>(QStringLiteral("LunaCommonReaderAdapter")))) adapter->unbind();
}

void hideAdvancedToolbars(pdfviewer::PDFEditorMainWindow& window) {
    for (auto* toolbar : window.findChildren<QToolBar*>()) {
        if (toolbar->objectName() != QStringLiteral("mainToolBar") && toolbar->objectName() != QStringLiteral("LunaLibraryToolbar") && window.toolBarArea(toolbar) != Qt::NoToolBarArea) toolbar->hide();
    }
    if (auto* commandSearch = window.menuBar()->cornerWidget(Qt::TopRightCorner)) commandSearch->hide();
}

void installReaderToolbar(pdfviewer::PDFEditorMainWindow& window, QToolBar& bar, QAction& libraryToggle) {
    const auto original = bar.actions();
    QList<QAction*> widgets;
    for (auto* action : original) {
        if (auto* widgetAction = qobject_cast<QWidgetAction*>(action)) {
            if (auto* widget = widgetAction->defaultWidget())
                if (widget->objectName() == QStringLiteral("pageNumberSpinBox") || widget->objectName() == QStringLiteral("pageNumberLabel") || widget->objectName() == QStringLiteral("pageZoomSpinBox")) widgets.append(action);
        }
        bar.removeAction(action); // Retain the QAction and default-widget ownership.
    }
    auto* advanced = window.findChild<QMenu*>(QStringLiteral("menuTools"));
    for (auto* toolbar : window.findChildren<QToolBar*>()) {
        if (toolbar->objectName() == QStringLiteral("LunaLibraryToolbar")) continue;
        if (toolbar != &bar && window.toolBarArea(toolbar) == Qt::NoToolBarArea) continue;
        const auto actions = toolbar == &bar ? original : toolbar->actions();
        for (auto* action : actions)
            if (!action->isSeparator() && !qobject_cast<QWidgetAction*>(action) && !inMenu(window, action) && advanced) advanced->addAction(action);
    }
    auto add = [&](const char* name, const char* icon, const QString& text, const QString& tooltip) {
        if (auto* action = nativeAction(window, name)) {
            action->setIcon(QIcon(new ReaderIcon(QString::fromLatin1(icon))));
            action->setText(text); action->setToolTip(tooltip); bar.addAction(action);
        }
    };
    libraryToggle.setIcon(QIcon(new ReaderIcon(QStringLiteral("panel"))));
    libraryToggle.setToolTip(QStringLiteral("왼쪽 라이브러리 접기 / 펼치기"));
    bar.addAction(&libraryToggle);
    add("actionOpen", "open", QStringLiteral("PDF 열기"), QStringLiteral("PDF 열기 (Ctrl+O)"));
    add("actionSave", "save", QStringLiteral("현재 PDF 저장"), QStringLiteral("현재 열린 PDF 저장 (Ctrl+S). 원본 보존은 파일 → 다른 이름으로 저장."));
    bar.addSeparator();
    add("actionGoToPreviousPage", "previous", QStringLiteral("이전 페이지"), QStringLiteral("이전 페이지"));
    for (auto* action : widgets) if (qobject_cast<QWidgetAction*>(action)->defaultWidget()->objectName() != QStringLiteral("pageZoomSpinBox")) bar.addAction(action);
    add("actionGoToNextPage", "next", QStringLiteral("다음 페이지"), QStringLiteral("다음 페이지"));
    add("actionZoom_Out", "minus", QStringLiteral("축소"), QStringLiteral("PDF 축소"));
    add("actionZoom_In", "plus", QStringLiteral("확대"), QStringLiteral("PDF 확대"));
    for (auto* action : widgets) if (qobject_cast<QWidgetAction*>(action)->defaultWidget()->objectName() == QStringLiteral("pageZoomSpinBox")) bar.addAction(action);
    bar.addSeparator();
    add("actionSelectText", "select", QStringLiteral("텍스트 선택"), QStringLiteral("텍스트 드래그 선택 / 복사 (Ctrl+C)"));
    add("actionCreateTextHighlight", "highlight", QStringLiteral("형광펜"), QStringLiteral("본문을 드래그해 강조합니다. PDF4QT 색상 창에서 색상을 선택하세요. Esc: 텍스트 선택."));
    add("actionStickyNoteComment", "note", QStringLiteral("메모"), QStringLiteral("PDF를 클릭해 메모 추가. 메모 아이콘 클릭: 내용 확인 / 편집."));
    add("actionDeleteAnnotation", "erase", QStringLiteral("주석 삭제"), QStringLiteral("클릭한 주석만 삭제합니다. PDF 본문·원본 객체는 삭제하지 않습니다. Esc: 텍스트 선택."));
    bar.addSeparator();
    add("actionFind", "search", QStringLiteral("본문 검색"), QStringLiteral("PDF 본문 검색 (Ctrl+F)"));
    add("actionPrint", "print", QStringLiteral("인쇄"), QStringLiteral("PDF 인쇄 (Ctrl+P)"));
    add("LunaCropThumbnail", "crop", QStringLiteral("논문 목록 썸네일 크롭"), QStringLiteral("PDF 화면에서 영역을 선택해 논문 목록 썸네일 PNG로 저장"));
    bar.setMovable(false); bar.setFloatable(false); bar.setOrientation(Qt::Horizontal);
    bar.setToolButtonStyle(Qt::ToolButtonIconOnly); bar.setIconSize(QSize(20,20));
    bar.setFont(QFont(QStringLiteral("Segoe UI"), 10));
    bar.setFixedHeight(qMax(42, QFontMetrics(bar.font()).height() + 16));
    if (auto* more = bar.findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button")))
        more->setToolTip(QStringLiteral("도구 더 보기 (라이브러리를 접으면 모든 도구가 표시됩니다)"));
    if (auto* page = window.findChild<QSpinBox*>(QStringLiteral("pageNumberSpinBox"))) {
        page->setFixedWidth(48); page->setToolTip(QStringLiteral("현재 페이지 (숫자 입력 후 Enter)"));
    }
    if (auto* count = window.findChild<QLabel*>(QStringLiteral("pageNumberLabel"))) count->setToolTip(QStringLiteral("전체 페이지 수"));
    if (auto* zoom = window.findChild<QDoubleSpinBox*>(QStringLiteral("pageZoomSpinBox"))) {
        zoom->setFixedWidth(80); zoom->setToolTip(QStringLiteral("확대 비율 (백분율 입력 후 Enter)"));
    }
    if (auto* saveAs = nativeAction(window, "actionSave_As")) {
        saveAs->setText(QStringLiteral("다른 이름으로 저장…"));
        saveAs->setToolTip(QStringLiteral("원본을 보존하는 PDF 복사본 저장 (Ctrl+Shift+S)"));
    }
    hideAdvancedToolbars(window);
    new ReadingInteraction(window);
}

bool verifyReaderToolbar(pdfviewer::PDFEditorMainWindow& window, QString& error, bool geometry) {
    auto fail = [&](const char* message) { error = QString::fromLatin1(message); return false; };
    auto* bar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    if (!bar || !bar->isVisible() || bar->orientation() != Qt::Horizontal) return fail("Reading toolbar missing");
    const QStringList allowed = {"LunaCropThumbnail", "LunaOpenDocuments", "actionSave", "actionGoToPreviousPage", "actionGoToNextPage", "actionZoom_Out", "actionZoom_In", "actionSelectText", "actionCreateTextHighlight", "actionStickyNoteComment", "actionDeleteAnnotation", "actionFind", "actionPrint"};
    int controls = 0;
    for (auto* action : bar->actions()) {
        if (action->isSeparator()) continue;
        if (auto* wa = qobject_cast<QWidgetAction*>(action)) {
            auto* widget = wa->defaultWidget();
            if (!widget || !QStringList{"pageNumberSpinBox","pageNumberLabel","pageZoomSpinBox"}.contains(widget->objectName())) return fail("Unexpected reading toolbar widget");
            ++controls; continue;
        }
        if (!allowed.contains(action->objectName()) || action->toolTip().isEmpty()) return fail("Unexpected reading toolbar action or missing tooltip");
    }
    if (controls != 3) return fail("Native page/zoom widget actions lost");
    for (const auto& name : allowed) if (!bar->actions().contains(window.findChild<QAction*>(name))) return fail("Essential reading action missing");
    for (auto* other : window.findChildren<QToolBar*>())
        if (other != bar && other->objectName() != QStringLiteral("LunaLibraryToolbar") && window.toolBarArea(other) != Qt::NoToolBarArea && other->isVisible()) return fail("Advanced toolbar unexpectedly visible");
    for (const char* name : {"actionSave_As","actionPageLayoutContinuous","actionPageLayoutSinglePage","actionPageLayoutTwoPages","actionCreateTextUnderline","actionBookmarkPage"})
        if (!inMenu(window, nativeAction(window, name))) return fail("Native advanced action no longer reachable in menus");
    if (geometry) {
        if (bar->height() < 40 || bar->height() > qMax(44, QFontMetrics(bar->font()).height() + 16)) return fail("Reading toolbar is not one compact row");
        QList<QRect> occupied;
        for (auto* action : bar->actions()) {
            if (action->isSeparator()) continue;
            auto* widget = bar->widgetForAction(action);
            if (!widget || !widget->isVisible()) continue; // Qt overflow menu keeps narrow windows usable.
            const QRect rect = widget->geometry();
            if (!bar->rect().contains(rect)) return fail("Visible toolbar control is clipped");
            for (const auto& previous : occupied) if (previous.intersects(rect)) return fail("Toolbar controls overlap");
            occupied.append(rect);
        }
        auto* toggle = window.findChild<QToolButton*>(QStringLiteral("LunaCombinedPanelToggle"));
        if (!toggle || !toggle->isVisible()) return fail("Library collapse button hidden by overflow");
        if (auto* more = bar->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button")))
            if (more->isVisible()) for (const auto& rect : occupied)
                if (more->geometry().intersects(rect)) return fail("Toolbar overflow button overlaps a control");
        // At a narrow effective desktop, Qt's overflow stays on one row. The
        // always-visible panel toggle must reveal every essential control.
        if (bar->width() < 700) {
            auto* focus = window.findChild<QAction*>(QStringLiteral("LunaFocusMode"));
            const bool change = !focus->isChecked();
            if (change) { focus->trigger(); QCoreApplication::processEvents(); }
            bool allVisible = true;
            for (auto* action : bar->actions()) if (!action->isSeparator()) {
                auto* control = bar->widgetForAction(action);
                if (!control || !control->isVisible()) allVisible = false;
            }
            if (change) { focus->trigger(); QCoreApplication::processEvents(); }
            if (!allVisible) return fail("Essential tools remain hidden after collapsing library");
        }
    }
    return true;
}
}
