// SPDX-License-Identifier: MIT
// Presentation and input adapters only; PDF4QT owns all document operations.
#include "lunareadertoolbar.h"
#include "lunaannotationtools.h"
#include "lunastickynotes.h"
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
    explicit ReaderIcon(QString name, QColor tint = {}) : name_(std::move(name)), tint_(tint) {}
    QIconEngine* clone() const override { return new ReaderIcon(name_); }
    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size); result.fill(Qt::transparent);
        QPainter painter(&result); paint(&painter, QRect(QPoint(), size), mode, state); return result;
    }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        p->save(); p->setRenderHint(QPainter::Antialiasing);
        p->translate(rect.topLeft()); p->scale(rect.width() / 24.0, rect.height() / 24.0);
        const QColor color = mode == QIcon::Disabled ? QColor("#A5ADBA") : tint_.isValid() ? tint_ : QColor(state == QIcon::On || mode == QIcon::Selected ? "#4F70B5" : "#263247");
        p->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        auto line = [p](qreal a, qreal b, qreal c, qreal d) { p->drawLine(QPointF(a,b), QPointF(c,d)); };
        if (name_ == "close") { line(6,6,18,18); line(18,6,6,18); }
        else if (name_ == "add") { line(12,4,12,20); line(4,12,20,12); }
        else if (name_ == "folder") {
            QPainterPath path; path.moveTo(3,19); path.lineTo(3,6); path.lineTo(9,6); path.lineTo(11,8); path.lineTo(21,8); path.lineTo(21,19); path.closeSubpath(); p->drawPath(path);
        } else if (name_ == "documents") {
            p->drawRoundedRect(QRectF(5,3,14,18),2,2); line(8,8,16,8); line(8,12,16,12); line(8,16,13,16);
        } else if (name_ == "recent") {
            p->drawEllipse(QRectF(3,3,18,18)); line(12,6,12,12); line(12,12,16,14);
        } else if (name_ == "star") {
            QPainterPath path; path.moveTo(12,3); path.lineTo(15,9); path.lineTo(22,10); path.lineTo(17,15); path.lineTo(18,22); path.lineTo(12,18); path.lineTo(6,22); path.lineTo(7,15); path.lineTo(2,10); path.lineTo(9,9); path.closeSubpath(); p->drawPath(path);
        } else if (name_ == "panel") { p->drawRoundedRect(QRectF(3,4,18,16),2,2); line(9,4,9,20); }
        else if (name_ == "crop") { line(7,3,7,17); line(7,17,21,17); line(3,7,17,7); line(17,7,17,21); }
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
    QColor tint_;
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
            focusStickyNote(window_, reference); manager->clearSelection(); selectText(); return true;

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

QIcon readerIcon(const QString& name, const QColor& tint) {
    if (name == "panel-left-close" || name == "panel-left-open") return QIcon(QStringLiteral(":/luna/") + name + QStringLiteral(".svg"));
    return QIcon(new ReaderIcon(name, tint));
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
    // Original actions stay in their advanced menus with original connections.
    for (auto pair : {qMakePair("actionCreateTextHighlight", false), qMakePair("actionDeleteAnnotation", true)}) {
        auto* original = nativeAction(window, pair.first); if (!original) continue;
        auto* action = new QAction(readerIcon(pair.second ? "erase" : "highlight"), pair.second ? QStringLiteral("선택 범위 형광펜 지우기") : QStringLiteral("선택 문장 강조"), &window);
        action->setObjectName(pair.second ? "LunaEraseHighlightSelection" : "LunaApplyHighlight");
        action->setToolTip(pair.second ? QStringLiteral("선택한 텍스트 범위의 형광펜만 지우기. 메모와 PDF 원본은 유지합니다.") : QStringLiteral("선택 문장 강조. 길게 누르기: 색상 선택"));
        bar.insertAction(original, action); bar.removeAction(original);
        QObject::connect(action, &QAction::triggered, &window, [&window, erase = pair.second] { applySelectedHighlight(window, erase); });
        auto* refresh = new QTimer(action); refresh->setInterval(100);
        QObject::connect(refresh, &QTimer::timeout, action, [&window, action, original] {
            action->setEnabled(original->isEnabled() && !window.getProgramController()->getToolManager()->getSelectTextTool()->lunaTextSelection().isEmpty());
        }); refresh->start(); action->setEnabled(false);
        if (!pair.second) if (auto* button = qobject_cast<QToolButton*>(bar.widgetForAction(action))) {
            auto* menu = new QMenu(button); menu->setObjectName("LunaHighlightColors");
            QObject::connect(menu, &QMenu::aboutToShow, menu, [menu] { menu->clear(); addAnnotationColors(menu, "reader/highlightColor", [](QColor) {}); });
            button->setMenu(menu); button->setPopupMode(QToolButton::DelayedPopup);
        }
    }
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
    new StickyNotes(window); new ReadingInteraction(window);
}

bool verifyReaderToolbar(pdfviewer::PDFEditorMainWindow& window, QString& error, bool geometry) {
    auto fail = [&](const char* message) { error = QString::fromLatin1(message); return false; };
    auto* bar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    if (!bar || !bar->isVisible() || bar->orientation() != Qt::Horizontal) return fail("Reading toolbar missing");
    const QStringList allowed = {"LunaCropThumbnail", "LunaOpenDocuments", "actionSave", "actionGoToPreviousPage", "actionGoToNextPage", "actionZoom_Out", "actionZoom_In", "actionSelectText", "LunaApplyHighlight", "actionStickyNoteComment", "LunaEraseHighlightSelection", "actionFind", "actionPrint"};
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
    for (const char* name : {"actionSave_As","actionPageLayoutContinuous","actionPageLayoutSinglePage","actionPageLayoutTwoPages","actionCreateTextUnderline","actionBookmarkPage","actionCreateTextHighlight","actionDeleteAnnotation"})
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
