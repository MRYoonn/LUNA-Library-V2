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
    explicit ReaderIcon(QString name) : name_(std::move(name)) {}
    QIconEngine* clone() const override { return new ReaderIcon(name_); }
    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size); result.fill(Qt::transparent);
        QPainter painter(&result); paint(&painter, QRect(QPoint(), size), mode, state); return result;
    }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        p->save(); p->setRenderHint(QPainter::Antialiasing);
        p->translate(rect.topLeft()); p->scale(rect.width() / 24.0, rect.height() / 24.0);
        const QColor color(mode == QIcon::Disabled ? "#A5ADBA" :
                           (state == QIcon::On || mode == QIcon::Selected ? "#4F70B5" : "#263247"));
        p->setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        auto line = [p](qreal a, qreal b, qreal c, qreal d) { p->drawLine(QPointF(a,b), QPointF(c,d)); };
        if (name_ == "panel") { p->drawRoundedRect(QRectF(3,4,18,16),2,2); line(9,4,9,20); }
        else if (name_ == "open") {
            QPainterPath path; path.moveTo(3,19); path.lineTo(3,6); path.lineTo(9,6); path.lineTo(11,8);
            path.lineTo(20,8); path.lineTo(20,11); path.moveTo(3,19); path.lineTo(6,11);
            path.lineTo(22,11); path.lineTo(19,19); path.closeSubpath(); p->drawPath(path);
        } else if (name_ == "save") {
            QPainterPath path; path.moveTo(4,3); path.lineTo(17,3); path.lineTo(21,7); path.lineTo(21,21);
            path.lineTo(4,21); path.closeSubpath(); p->drawPath(path); p->drawRect(QRectF(8,3,8,6)); p->drawRect(QRectF(8,14,9,7));
        } else if (name_ == "previous" || name_ == "next") {
            const bool next = name_ == "next"; line(next?9:15,5,next?16:8,12); line(next?16:8,12,next?9:15,19);
        } else if (name_ == "minus" || name_ == "plus" || name_ == "search") {
            p->drawEllipse(QRectF(3,3,13,13)); line(15,15,21,21);
            if (name_ != "search") line(6,9.5,13,9.5);
            if (name_ == "plus") line(9.5,6,9.5,13);
        } else if (name_ == "select") {
            line(9,4,15,4); line(12,4,12,20); line(9,20,15,20); line(5,8,5,16); line(3,8,7,8); line(3,16,7,16);
        } else if (name_ == "highlight") {
            QPainterPath path; path.moveTo(5,15); path.lineTo(14,4); path.lineTo(20,9); path.lineTo(11,20);
            path.closeSubpath(); p->drawPath(path); line(7,13,13,18); line(4,21,12,21);
        } else if (name_ == "note") {
            QPainterPath path; path.moveTo(4,4); path.lineTo(20,4); path.lineTo(20,15); path.lineTo(13,15);
            path.lineTo(8,20); path.lineTo(8,15); path.lineTo(4,15); path.closeSubpath(); p->drawPath(path);
            line(8,8,16,8); line(8,11,13,11);
        } else if (name_ == "erase") {
            QPainterPath path; path.moveTo(3,15); path.lineTo(13,4); path.lineTo(21,11); path.lineTo(12,21);
            path.lineTo(9,21); path.closeSubpath(); p->drawPath(path); line(7,11,16,18); line(13,21,22,21);
        } else if (name_ == "print") {
            p->drawRoundedRect(QRectF(3,8,18,10),2,2); p->drawRect(QRectF(7,3,10,5)); p->drawRect(QRectF(7,14,10,7)); line(17,11,18,11);
        }
        p->restore();
    }
private:
    QString name_;
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
        if (event->type() == QEvent::KeyRelease && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            selectText(); return false;
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
            auto* c = window_.getProgramController(); auto* manager = c->getPdfWidget()->getAnnotationManager();
            manager->selectAnnotation(reference, true);
            if (deleting_) { manager->deleteSelectedAnnotations(); selectText(); return true; }
            // Native note popup plus a route to its existing annotation editor.
            struct PopupState { bool waiting = true; bool edit = false; };
            auto state = std::make_shared<PopupState>();
            QTimer::singleShot(0, this, [this, state] {
                if (!state->waiting) return;
                auto* popup = qobject_cast<QDialog*>(QApplication::activeModalWidget());
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
    const QStringList allowed = {"LunaFocusMode", "actionOpen", "actionSave", "actionGoToPreviousPage", "actionGoToNextPage", "actionZoom_Out", "actionZoom_In", "actionSelectText", "actionCreateTextHighlight", "actionStickyNoteComment", "actionDeleteAnnotation", "actionFind", "actionPrint"};
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
        auto* toggle = bar->widgetForAction(window.findChild<QAction*>(QStringLiteral("LunaFocusMode")));
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
