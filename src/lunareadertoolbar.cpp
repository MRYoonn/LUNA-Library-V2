// SPDX-License-Identifier: MIT
// Presentation and input adapters only; PDF4QT owns all document operations.
#include "lunareadertoolbar.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"
#include "pdftextlayout.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocument.h"

#include <QAction>
#include <QFile>
#include <QSvgRenderer>
#include <QComboBox>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QFontComboBox>
#include <QColorDialog>
#include <QCheckBox>
#include <QRubberBand>
#include <QMessageBox>
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
#include <QSettings>
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
        // Selection-first highlight: the native drag-to-highlight action remains
        // available in Insert, but is not the compact LUNA reading action.
        highlight_ = new QAction(readerIcon(QStringLiteral("highlight")), QStringLiteral("형광펜"), &window_);
        highlight_->setObjectName(QStringLiteral("LunaHighlightSelected"));
        highlight_->setToolTip(QStringLiteral("텍스트를 먼저 드래그 선택한 뒤 클릭해 강조합니다. 이미 강조한 부분은 중복 표시하지 않습니다."));
        highlight_->setEnabled(false);
        QColor stored(QSettings().value(QStringLiteral("annotations/highlightColor"),
            QStringLiteral("#FFE049")).toString());
        if (stored.isValid()) color_ = stored;
        chooseColor_ = new QAction(QStringLiteral("형광펜 색상 변경…"), &window_);
        chooseColor_->setObjectName(QStringLiteral("LunaHighlightColor"));
        chooseColor_->setToolTip(QStringLiteral("선택형 형광펜의 색상을 설정합니다. 툴바 버튼은 추가하지 않습니다."));
        connect(chooseColor_, &QAction::triggered, this, [this] {
            const QColor next = QColorDialog::getColor(color_, draw_,
                QStringLiteral("형광펜 색상"));
            if (!next.isValid()) return;
            color_ = next;
            QSettings().setValue(QStringLiteral("annotations/highlightColor"), color_.name(QColor::HexRgb));
        });
        // Keep the toolbar compact while making the four reading colors
        // available without repeatedly opening the system QColorDialog.
        auto* toolsMenu = window_.findChild<QMenu*>(QStringLiteral("menuTools"));
        if (!toolsMenu && window_.menuBar()) {
            toolsMenu = window_.menuBar()->addMenu(QStringLiteral("LUNA 도구"));
            toolsMenu->setObjectName(QStringLiteral("LunaToolsMenu"));
        }
        if (toolsMenu) {
            auto* palette = toolsMenu->addMenu(QStringLiteral("형광펜 색상"));
            palette->setObjectName(QStringLiteral("LunaHighlightPalette"));
            struct Preset { const char* label; const char* color; };
            constexpr Preset presets[] = {
                {"노란색", "#FFE049"}, {"초록색", "#A3E6A4"},
                {"분홍색", "#FFB3CF"}, {"파란색", "#9FCAFF"}
            };
            for (const auto& preset : presets) {
                auto* choice = palette->addAction(QString::fromUtf8(preset.label));
                choice->setCheckable(true);
                choice->setData(QString::fromLatin1(preset.color));
                connect(choice, &QAction::triggered, this, [this, choice] {
                    color_ = QColor(choice->data().toString());
                    QSettings().setValue(QStringLiteral("annotations/highlightColor"),
                        color_.name(QColor::HexRgb));
                });
            }
            connect(palette, &QMenu::aboutToShow, this, [this, palette] {
                for (auto* choice : palette->actions())
                    if (choice->isCheckable())
                        choice->setChecked(QColor(choice->data().toString()) == color_);
            });
            palette->addSeparator();
            palette->addAction(chooseColor_);
        }
        connect(highlight_, &QAction::triggered, this, [this] { applyHighlight(); });
        // A PDF FreeText annotation is a true on-page text box rather than a
        // sticky-note icon. PDF4QT's modifier owns its save/undo machinery.
        textBox_ = new QAction(readerIcon(QStringLiteral("note")), QStringLiteral("메모 상자"), &window_);
        textBox_->setObjectName(QStringLiteral("LunaFreeTextBox"));
        textBox_->setCheckable(true);
        textBox_->setToolTip(QStringLiteral("PDF 페이지에서 드래그해 투명 메모 상자를 만듭니다. 글꼴·크기 선택 가능. Esc: 취소."));
        rubber_ = new QRubberBand(QRubberBand::Rectangle, draw_);
        rubber_->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(textBox_, &QAction::toggled, this, [this](bool enabled) {
            if (enabled && !canAnnotate()) { textBox_->setChecked(false); return; }
            if (enabled) { clearPendingSelection(); selectText(); draw_->setCursor(Qt::CrossCursor); }
            else { rubber_->hide(); drawingBox_ = false; draw_->unsetCursor(); }
        });
        auto afterAnnotation = [this](const pdf::PDFModifiedDocument&) {
            if (annotationMode()) QTimer::singleShot(0, this, [this] { selectText(); });
        };
        connect(controller->getToolManager(), &pdf::PDFToolManager::documentModified, this, afterAnnotation);
        connect(controller->getPdfWidget()->getAnnotationManager(), &pdf::PDFWidgetAnnotationManager::documentModified, this, afterAnnotation);
        connect(&window_, &QWidget::windowTitleChanged, this, [this] {
            target_ = {}; // A document/title transition cancels an unfinished click.
            clearPendingSelection();
            textBox_->setChecked(false);
            QTimer::singleShot(0, this, [this] {
                auto* c = window_.getProgramController();
                if (c->getDocument() && !c->getToolManager()->getActiveTool()) selectText();
            });
        });
    }
private:
    bool checked(const char* name) const { auto* a = nativeAction(window_, name); return a && a->isChecked(); }
    // Match the upstream PDF4QT native tool security gate. A custom action
    // must never bypass PDF permission flags even if text selection works.
    bool canAnnotate() const {
        auto* c = window_.getProgramController();
        return c && c->getDocument() && c->getPdfWidget() &&
            c->getPdfWidget()->getAnnotationManager() &&
            c->getPdfWidget()->getAnnotationManager()->isModificationAllowed();
    }
    bool annotationMode() const { return checked("actionCreateTextHighlight") || checked("actionStickyNoteComment") || checked("actionDeleteAnnotation") || textBox_->isChecked(); }
    void clearPendingSelection() {
        selectionPage_ = -1;
        selectionStart_ = {};
        selectionEnd_ = {};
        highlight_->setEnabled(false);
    }
    void applyHighlight() {
        if (!highlight_->isEnabled() || selectionPage_ < 0) return;
        auto* c = window_.getProgramController();
        if (!canAnnotate() || !checked("actionSelectText")) { clearPendingSelection(); return; }
        auto* proxy = c->getPdfWidget()->getDrawWidgetProxy();
        auto* selected = c->getToolManager()->getSelectTextTool();
        if (!selected || selected->getSelectedText().trimmed().isEmpty()) { clearPendingSelection(); return; }
        pdf::PDFTextLayout layout = proxy->getTextLayoutCompiler()->getTextLayoutLazy(selectionPage_);
        pdf::PDFTextSelection selection = layout.createTextSelection(selectionPage_, selectionStart_, selectionEnd_, color_);
        if (selection.isEmpty()) { clearPendingSelection(); return; }
        pdf::PDFTextLayoutGetter getter = proxy->getTextLayoutCompiler()->getTextLayoutLazy(selectionPage_);
        QPolygonF quads;
        pdf::PDFTextSelectionPainter painter(&selection);
        if (painter.prepareGeometry(selectionPage_, getter, QTransform(), &quads).isEmpty()) { clearPendingSelection(); return; }

        // The PDF format stores one quadrilateral per group of four points.
        // Conservatively skip any quadrilateral intersecting an existing mark,
        // rather than stacking transparencies and making text increasingly dark.
        const auto existing = c->getPdfWidget()->getAnnotationManager()->getPageAnnotations(selectionPage_).annotations;
        QPainterPath marked;
        for (const auto& item : existing) {
            if (item.annotation->getType() != pdf::AnnotationType::Highlight) continue;
            const auto* highlight = dynamic_cast<const pdf::PDFHighlightAnnotation*>(item.annotation.data());
            if (highlight) marked.addPath(highlight->getHiglightArea().getPath());
        }
        QPolygonF clean;
        for (qsizetype i = 0; i + 3 < quads.size(); i += 4) {
            QPolygonF quad;
            quad.reserve(4);
            for (qsizetype j = 0; j < 4; ++j) quad << quads[i + j];
            QPainterPath part; part.addPolygon(quad); part.closeSubpath();
            if (!marked.intersects(part) && !marked.contains(part))
                for (const QPointF& point : quad) clean << point;
        }
        const auto pageIndex = selectionPage_;
        clearPendingSelection();
        if (clean.isEmpty()) return;
        pdf::PDFDocumentModifier modifier(c->getDocument());
        const auto page = c->getDocument()->getCatalog()->getPage(pageIndex);
        if (!page) return;
        const pdf::PDFObjectReference ref = modifier.getBuilder()->createAnnotationHighlight(
            page->getPageReference(), clean, color_);
        modifier.getBuilder()->setAnnotationOpacity(ref, 0.2);
        modifier.getBuilder()->updateAnnotationAppearanceStreams(ref);
        modifier.markAnnotationsChanged();
        if (modifier.finalize())
            Q_EMIT c->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
    void createTextBox(const QPoint& from, const QPoint& to) {
        if (!canAnnotate()) return;
        auto* c = window_.getProgramController();
        const auto* originalDocument = c->getDocument();
        auto* proxy = c->getPdfWidget()->getDrawWidgetProxy();
        QPointF start, end;
        const auto pageIndex = proxy->getPageUnderPoint(from, &start);
        if (!c->getDocument() || pageIndex < 0 || proxy->getPageUnderPoint(to, &end) != pageIndex) return;
        QRectF rect(start, end); rect = rect.normalized();
        if (rect.width() < 8 || rect.height() < 8)
            rect = QRectF(start, QSizeF(180.0, 68.0)); // Single-click default.
        QDialog dialog(draw_);
        dialog.setObjectName(QStringLiteral("LunaFreeTextEditor"));
        dialog.setWindowTitle(QStringLiteral("페이지 메모 상자"));
        dialog.setMinimumWidth(430);
        QVBoxLayout root(&dialog);
        QPlainTextEdit input(&dialog);
        input.setObjectName(QStringLiteral("LunaFreeTextContents"));
        input.setPlaceholderText(QStringLiteral("PDF 위에 표시할 메모를 입력하세요."));
        input.setMinimumHeight(125);
        root.addWidget(&input);
        QFormLayout settings;
        QFontComboBox family(&dialog);
        family.setObjectName(QStringLiteral("LunaFreeTextFont"));
        family.setCurrentFont(QFont(QStringLiteral("Helvetica")));
        QDoubleSpinBox size(&dialog);
        size.setObjectName(QStringLiteral("LunaFreeTextSize"));
        size.setRange(6.0, 72.0); size.setValue(11.0); size.setSuffix(QStringLiteral(" pt"));
        settings.addRow(QStringLiteral("글꼴"), &family);
        settings.addRow(QStringLiteral("크기"), &size);
        root.addLayout(&settings);
        QLabel transparent(QStringLiteral("배경: 투명 · PDF 표준 FreeText 주석으로 저장"), &dialog);
        root.addWidget(&transparent);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        root.addWidget(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        input.setFocus();
        if (dialog.exec() != QDialog::Accepted || input.toPlainText().trimmed().isEmpty()) return;
        // A document/tab could have closed during a modal edit. Never write to
        // an unrelated PDF when the editor session has changed.
        if (!canAnnotate() || c->getDocument() != originalDocument ||
            pageIndex >= c->getDocument()->getCatalog()->getPageCount()) return;
        pdf::PDFFreeTextStyle style;
        style.fontFamily = family.currentFont().family();
        style.fontSize = size.value();
        style.textColor = Qt::black;
        style.textAlignment = Qt::AlignLeft | Qt::AlignTop;
        pdf::PDFDocumentModifier modifier(c->getDocument());
        const auto page = c->getDocument()->getCatalog()->getPage(pageIndex);
        modifier.getBuilder()->createAnnotationFreeText(page->getPageReference(), rect,
            QStringLiteral("LUNA Library"), QString(), input.toPlainText(), style, false);
        modifier.markAnnotationsChanged();
        if (modifier.finalize())
            Q_EMIT c->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
    // Double-click edits only LUNA's on-page FreeText content. Native single
    // click and drag stay untouched so handles, resize and move still work.
    bool editTextBoxAt(const QPoint& position) {
        if (!canAnnotate()) return false;
        auto* c = window_.getProgramController();
        auto* widget = c->getPdfWidget();
        auto* manager = widget->getAnnotationManager();
        const auto snapshot = widget->getDrawWidgetProxy()->getSnapshot();
        for (const auto& page : snapshot.items) {
            const auto& items = manager->getPageAnnotations(page.pageIndex).annotations;
            for (auto it = items.rbegin(); it != items.rend(); ++it) {
                const auto& item = *it;
                if (item.annotation->getType() != pdf::AnnotationType::FreeText ||
                    !manager->isAnnotationSelectable(item)) continue;
                QRectF rect = item.annotation->getRectangle();
                const auto matrix = manager->prepareTransformations(page.pageToDeviceMatrix,
                    widget, item.annotation->getEffectiveFlags(),
                    c->getDocument()->getCatalog()->getPage(page.pageIndex), rect);
                if (!matrix.mapRect(rect).contains(position)) continue;
                if (!manager->canModifyAnnotation(item)) return false;
                const auto annotationRef = item.annotation->getSelfReference();
                const QString existing = item.annotation->getContents();
                const auto* originalDocument = c->getDocument();
                QDialog dialog(draw_);
                dialog.setObjectName(QStringLiteral("LunaFreeTextEditDialog"));
                dialog.setWindowTitle(QStringLiteral("메모 상자 수정"));
                dialog.setMinimumWidth(420);
                QVBoxLayout root(&dialog);
                QPlainTextEdit input(&dialog);
                input.setObjectName(QStringLiteral("LunaFreeTextEditContents"));
                input.setPlainText(existing);
                input.setMinimumHeight(125);
                root.addWidget(&input);
                QCheckBox changeFont(QStringLiteral("글꼴·크기도 변경"), &dialog);
                root.addWidget(&changeFont);
                QFormLayout settings;
                QFontComboBox family(&dialog);
                family.setObjectName(QStringLiteral("LunaFreeTextEditFont"));
                family.setCurrentFont(QFont(QStringLiteral("Helvetica")));
                QDoubleSpinBox size(&dialog);
                size.setObjectName(QStringLiteral("LunaFreeTextEditSize"));
                size.setRange(6.0, 72.0); size.setValue(11.0);
                family.setEnabled(false); size.setEnabled(false);
                connect(&changeFont, &QCheckBox::toggled, &family, &QWidget::setEnabled);
                connect(&changeFont, &QCheckBox::toggled, &size, &QWidget::setEnabled);
                settings.addRow(QStringLiteral("글꼴"), &family);
                settings.addRow(QStringLiteral("크기 (pt)"), &size);
                root.addLayout(&settings);
                QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
                root.addWidget(&buttons);
                connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                input.setFocus();
                if (dialog.exec() != QDialog::Accepted) return true;
                if (!canAnnotate() || c->getDocument() != originalDocument) return true;
                const QString nextText = input.toPlainText();
                if (nextText == existing && !changeFont.isChecked()) return true;
                // Validate the annotation still belongs to this live PDF.
                bool found = false;
                for (const auto& candidate : manager->getPageAnnotations(page.pageIndex).annotations)
                    if (candidate.annotation->getSelfReference() == annotationRef &&
                        candidate.annotation->getType() == pdf::AnnotationType::FreeText &&
                        manager->canModifyAnnotation(candidate)) { found = true; break; }
                if (!found) return true;
                pdf::PDFDocumentModifier modifier(c->getDocument());
                modifier.getBuilder()->setAnnotationContents(annotationRef, nextText);
                if (changeFont.isChecked()) {
                    pdf::PDFFreeTextStyle style;
                    style.fontFamily = family.currentFont().family();
                    style.fontSize = size.value();
                    style.textAlignment = Qt::AlignLeft | Qt::AlignTop;
                    modifier.getBuilder()->setFreeTextAnnotationStyle(annotationRef, style);
                }
                modifier.getBuilder()->updateAnnotationAppearanceStreams(annotationRef);
                modifier.markAnnotationsChanged();
                if (modifier.finalize())
                    Q_EMIT c->getToolManager()->documentModified(
                        pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                return true;
            }
        }
        return false;
    }
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
                    (item.annotation->getType() != pdf::AnnotationType::Text ||
                     !manager->isAnnotationSelectable(item))) continue;
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
        if (event->type() == QEvent::MouseButtonDblClick && checked("actionSelectText") &&
            !textBox_->isChecked()) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && editTextBoxAt(mouse->pos()))
                return true;
        }
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) &&
            static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            target_ = {}; clearPendingSelection(); textBox_->setChecked(false); selectText(); return false;
        }
        if (textBox_->isChecked()) {
            if (event->type() == QEvent::MouseButtonPress) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                if (mouse->button() != Qt::LeftButton) return false;
                press_ = mouse->pos(); drawingBox_ = true;
                rubber_->setGeometry(QRect(press_, QSize())); rubber_->show(); return true;
            }
            if (event->type() == QEvent::MouseMove && drawingBox_) {
                rubber_->setGeometry(QRect(press_, static_cast<QMouseEvent*>(event)->pos()).normalized()); return true;
            }
            if (event->type() == QEvent::MouseButtonRelease && drawingBox_) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                if (mouse->button() != Qt::LeftButton) return true;
                const auto from = press_, to = mouse->pos();
                drawingBox_ = false; rubber_->hide(); textBox_->setChecked(false);
                createTextBox(from, to);
                selectText(); return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            target_ = {};
            if (mouse->button() != Qt::LeftButton) return false;
            clearPendingSelection();
            deleting_ = checked("actionDeleteAnnotation");
            if (!checked("actionSelectText") && !deleting_) return false;
            target_ = hit(mouse->pos(), deleting_, &editable_);
            press_ = mouse->pos();
            if (target_.isValid()) return true;
            if (!deleting_ && checked("actionSelectText")) {
                auto* proxy = window_.getProgramController()->getPdfWidget()->getDrawWidgetProxy();
                selectionPage_ = proxy->getPageUnderPoint(press_, &selectionStart_);
            }
            return false;
        }
        if (event->type() == QEvent::MouseButtonRelease && !target_.isValid() &&
            selectionPage_ >= 0 && checked("actionSelectText")) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            QPointF end;
            auto* proxy = window_.getProgramController()->getPdfWidget()->getDrawWidgetProxy();
            const bool good = mouse->button() == Qt::LeftButton &&
                (mouse->pos() - press_).manhattanLength() >= QApplication::startDragDistance() &&
                proxy->getPageUnderPoint(mouse->pos(), &end) == selectionPage_;
            if (!good) clearPendingSelection();
            else {
                selectionEnd_ = end;
                QTimer::singleShot(0, this, [this] {
                    auto* selected = window_.getProgramController()->getToolManager()->getSelectTextTool();
                    highlight_->setEnabled(canAnnotate() && selected && !selected->getSelectedText().trimmed().isEmpty() &&
                        !textBox_->isChecked() && selectionPage_ >= 0);
                });
            }
            return false; // Native text-selection tool must receive the release.
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
    bool deleting_ = false, editable_ = false, drawingBox_ = false;
    QAction* highlight_ = nullptr;
    QAction* chooseColor_ = nullptr;
    QColor color_ = QColor(255, 224, 73);
    QAction* textBox_ = nullptr;
    QRubberBand* rubber_ = nullptr;
    pdf::PDFInteger selectionPage_ = -1;
    QPointF selectionStart_, selectionEnd_;
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
        // Unbinding stops polling; re-enabling it is necessary on tab reopen.
        if (!poll_.isActive()) poll_.start(100);
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
        add("actionSelectText"); add("LunaHighlightSelected"); add("LunaFreeTextBox"); add("actionDeleteAnnotation"); add("LunaCropThumbnail");
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
    // New reading actions are installed by ReadingInteraction below. Native
    // highlight/sticky-note actions remain reachable from Insert menus.
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
    auto* interaction = new ReadingInteraction(window);
    // The actions are created after the adapter to preserve QObject ownership;
    // both the embedded toolbar and the shell's common toolbar bind by name.
    if (auto* hl = window.findChild<QAction*>(QStringLiteral("LunaHighlightSelected"))) bar.insertAction(
        nativeAction(window, "actionDeleteAnnotation"), hl);
    if (auto* note = window.findChild<QAction*>(QStringLiteral("LunaFreeTextBox"))) bar.insertAction(
        nativeAction(window, "actionDeleteAnnotation"), note);
    Q_UNUSED(interaction);
}

bool verifyReaderToolbar(pdfviewer::PDFEditorMainWindow& window, QString& error, bool geometry) {
    auto fail = [&](const char* message) { error = QString::fromLatin1(message); return false; };
    auto* bar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    if (!bar || !bar->isVisible() || bar->orientation() != Qt::Horizontal) return fail("Reading toolbar missing");
    const QStringList allowed = {"LunaCropThumbnail", "LunaOpenDocuments", "actionSave", "actionGoToPreviousPage", "actionGoToNextPage", "actionZoom_Out", "actionZoom_In", "actionSelectText", "LunaHighlightSelected", "LunaFreeTextBox", "actionDeleteAnnotation", "actionFind", "actionPrint"};
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
