// SPDX-License-Identifier: MIT
// Presentation and input adapters only; PDF4QT owns all document operations.
#include "lunareadertoolbar.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcompiler.h"
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
#include <QScrollBar>
#include <QCursor>
#include <QtMath>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
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

// Visual anchor for browser-like vertical autoscroll. Unlike PDF4QT's
// four-way panning cursor, this shows only the up/down directions.
class VerticalAutoscrollMarker final : public QWidget {
public:
    explicit VerticalAutoscrollMarker(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("LunaVerticalAutoscrollMarker"));
        setFixedSize(36, 48);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#315277"), 1.2));
        p.setBrush(QColor(248, 251, 255, 235));
        p.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 11, 11);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#315277"));
        QPolygon up; up << QPoint(18, 7) << QPoint(11, 17) << QPoint(25, 17);
        QPolygon down; down << QPoint(11, 31) << QPoint(25, 31) << QPoint(18, 41);
        p.drawPolygon(up);
        p.drawPolygon(down);
        p.setBrush(QColor("#FFFFFF"));
        p.setPen(QPen(QColor("#315277"), 1.2));
        p.drawEllipse(QPoint(18, 24), 4, 4);
    }
};

struct HighlightPreset { const char* label; const char* value; };
constexpr HighlightPreset kHighlightPresets[] = {
    {"노란색", "#FFE049"}, {"초록색", "#A3E6A4"},
    {"분홍색", "#FFB3CF"}, {"파란색", "#9FCAFF"},
    {"주황색", "#FFC58A"}, {"보라색", "#C7B2FF"},
    {"민트색", "#A3E8DF"}, {"회색", "#CED4DE"}
};

class HighlightSwatchIcon final : public QIconEngine {
public:
    explicit HighlightSwatchIcon(QColor color) : color_(color) {}
    QIconEngine* clone() const override { return new HighlightSwatchIcon(*this); }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size); result.fill(Qt::transparent);
        QPainter painter(&result); paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode, QIcon::State state) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.center());
        painter->rotate(-32);
        painter->setPen(QPen(QColor("#57677C"), 0.9));
        painter->setBrush(QColor("#F8FAFC"));
        painter->drawRoundedRect(QRectF(-4.4, -9.0, 8.8, 13.0), 1.7, 1.7);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color_);
        painter->drawRect(QRectF(-3.3, -2.5, 6.6, 5.4));
        QPolygonF nib;
        nib << QPointF(-3.8, 4) << QPointF(3.8, 4)
            << QPointF(2.4, 8.7) << QPointF(-2.4, 8.7);
        painter->drawPolygon(nib);
        painter->restore();
        if (state == QIcon::On) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(QColor("#174C83"), 1.5));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(QRectF(rect).adjusted(0.8, 0.8, -0.8, -0.8), 3, 3);
            painter->restore();
        }
    }
private:
    QColor color_;
};

class ReadingInteraction final : public QObject {
public:
    explicit ReadingInteraction(pdfviewer::PDFEditorMainWindow& window) : QObject(&window), window_(window) {
        auto* controller = window_.getProgramController();
        draw_ = controller->getPdfWidget()->getDrawWidget()->getWidget();
        lastDocumentSource_ = controller->getOriginalFileName();
        draw_->installEventFilter(this);
        // Each color button is a complete text-selection highlighter tool.
        // There is no separate "apply highlight" toolbar action.
        reloadHighlightColor();
        chooseColor_ = new QAction(QStringLiteral("형광펜 색상 변경…"), &window_);
        chooseColor_->setObjectName(QStringLiteral("LunaHighlightColor"));
        chooseColor_->setToolTip(QStringLiteral("선택형 형광펜의 색상을 설정합니다. 툴바 버튼은 추가하지 않습니다."));
        connect(chooseColor_, &QAction::triggered, this, [this] {
            reloadHighlightColor(); // Other native tabs may have changed the shared preference.
            const QColor next = QColorDialog::getColor(color_, draw_,
                QStringLiteral("형광펜 색상"));
            if (!next.isValid()) return;
            color_ = next;
            armHighlighter(next);
        });
        // Keep the toolbar compact while exposing reading colors
        // available without repeatedly opening the system QColorDialog.
        auto* toolsMenu = window_.findChild<QMenu*>(QStringLiteral("menuTools"));
        if (!toolsMenu && window_.menuBar()) {
            toolsMenu = window_.menuBar()->addMenu(QStringLiteral("LUNA 도구"));
            toolsMenu->setObjectName(QStringLiteral("LunaToolsMenu"));
        }
        if (toolsMenu) {
            auto* palette = toolsMenu->addMenu(QStringLiteral("형광펜 색상"));
            palette->setObjectName(QStringLiteral("LunaHighlightPalette"));
            for (const auto& preset : kHighlightPresets) {
                auto* choice = palette->addAction(QString::fromUtf8(preset.label));
                choice->setCheckable(true);
                choice->setData(QString::fromLatin1(preset.value));
                connect(choice, &QAction::triggered, this, [this, choice] {
                    armHighlighter(QColor(choice->data().toString()));
                });
            }
            connect(palette, &QMenu::aboutToShow, this, [this, palette] {
                reloadHighlightColor();
                for (auto* choice : palette->actions())
                    if (choice->isCheckable())
                        choice->setChecked(QColor(choice->data().toString()) == color_);
            });
            palette->addSeparator();
            palette->addAction(chooseColor_);
        }
        // Eight clickable marker icons, each armed independently after Print/eraser.
        for (int i = 0; i < 8; ++i) {
            const QColor presetColor(QString::fromLatin1(kHighlightPresets[i].value));
            auto* swatch = new QAction(QIcon(new HighlightSwatchIcon(presetColor)),
                QString::fromUtf8(kHighlightPresets[i].label), &window_);
            swatch->setObjectName(QStringLiteral("LunaHighlightSwatch%1").arg(i + 1));
            swatch->setCheckable(true);
            swatch->setToolTip(QStringLiteral("%1 형광펜 · 누른 뒤 텍스트를 드래그하면 즉시 적용").arg(QString::fromUtf8(kHighlightPresets[i].label)));
            connect(swatch, &QAction::triggered, this, [this, presetColor] {
                armHighlighter(presetColor);
            });
            swatches_.append(swatch);
        }
        syncSwatches();
        eraser_ = new QAction(readerIcon(QStringLiteral("erase")), QStringLiteral("형광펜 지우개"), &window_);
        eraser_->setObjectName(QStringLiteral("LunaEraseSelected"));
        eraser_->setToolTip(QStringLiteral("지우개를 누른 뒤 텍스트를 드래그하면 해당 범위의 형광펜을 삭제합니다. 메모와 본문은 유지합니다."));
        eraser_->setCheckable(true);
        connect(eraser_, &QAction::triggered, this, [this](bool checked) {
            if (checked) armEraser();
            else { eraserArmed_ = false; selectText(); }
        });
        // Keep the native click-to-delete action accessible in Tools for other
        // annotation types. The compact reader eraser is selection-first.
        if (toolsMenu) {
            auto* nativeDelete = nativeAction(window_, "actionDeleteAnnotation");
            if (nativeDelete && !toolsMenu->actions().contains(nativeDelete))
                toolsMenu->addAction(nativeDelete);
        }
        // Do not register one filter per PDF tab on the *entire* application.
        // This previously intercepted shell and library startup events before
        // the Reader action set was fully initialized. The draw-widget filter
        // above is sufficient for scrolling/notes and keeps tab isolation.
        autoscrollTimer_.setInterval(16);
        connect(&autoscrollTimer_, &QTimer::timeout, this, [this] { tickVerticalAutoscroll(); });
        connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive) stopVerticalAutoscroll();
            });
        // A PDF FreeText annotation is a true on-page text box rather than a
        // sticky-note icon. PDF4QT's modifier owns its save/undo machinery.
        textBox_ = new QAction(readerIcon(QStringLiteral("note")), QStringLiteral("메모 상자"), &window_);
        textBox_->setObjectName(QStringLiteral("LunaFreeTextBox"));
        textBox_->setCheckable(true);
        textBox_->setToolTip(QStringLiteral("PDF 페이지에서 드래그해 투명 메모 상자를 만듭니다. 글꼴·크기 선택 가능. Esc: 취소."));
        rubber_ = new QRubberBand(QRubberBand::Rectangle, draw_);
        rubber_->setAttribute(Qt::WA_TransparentForMouseEvents);
        if (auto* selectAction = nativeAction(window_, "actionSelectText"))
            connect(selectAction, &QAction::triggered, this, [this] {
                if (highlightArmed_) setHighlightArmed(false);
                if (eraserArmed_) { eraserArmed_ = false; eraser_->setChecked(false); }
            });
        connect(textBox_, &QAction::toggled, this, [this](bool enabled) {
            if (enabled) {
                setHighlightArmed(false);
                eraserArmed_ = false; eraser_->setChecked(false);
            }
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
            // A dirty-marker title change is NOT a document transition.
            // Preserve the armed pen/eraser across multiple annotation drags.
            auto* controller = window_.getProgramController();
            const QString source = controller ? controller->getOriginalFileName() : QString();
            if (source == lastDocumentSource_) return;
            lastDocumentSource_ = source;
            target_ = {}; // Real document transition cancels an unfinished click.
            stopVerticalAutoscroll();
            cancelTextBoxDrag();
            clearPendingSelection();
            setHighlightArmed(false);
            eraserArmed_ = false; eraser_->setChecked(false);
            textBox_->setChecked(false);
            QTimer::singleShot(0, this, [this] {
                auto* c = window_.getProgramController();
                if (c->getDocument() && !c->getToolManager()->getActiveTool()) selectText();
            });
        });
    }
private:
    // The color is a library-wide setting, not an isolated per-tab preference.
    // Refresh on menu open and on application so inactive editor tabs cannot
    // keep painting with a stale color after another tab changes it.
    void reloadHighlightColor() {
        const QColor saved(QSettings().value(QStringLiteral("annotations/highlightColor"),
            QStringLiteral("#FFE049")).toString());
        if (saved.isValid()) color_ = saved;
        syncSwatches();
    }
    void syncSwatches() {
        for (int i = 0; i < swatches_.size(); ++i)
            swatches_[i]->setChecked(highlightArmed_ &&
                color_ == QColor(QString::fromLatin1(kHighlightPresets[i].value)));
    }
    void setHighlightArmed(bool enabled) {
        highlightArmed_ = enabled;
        syncSwatches();
    }
    void armHighlighter(const QColor& next) {
        if (!next.isValid() || !canAnnotate()) { setHighlightArmed(false); return; }
        clearPendingSelection();
        if (textBox_->isChecked()) textBox_->setChecked(false);
        selectText();  // Explicitly switch back from any native annotation tool.
        if (!checked("actionSelectText")) { setHighlightArmed(false); return; }
        eraserArmed_ = false; eraser_->setChecked(false);
        color_ = next;
        QSettings().setValue(QStringLiteral("annotations/highlightColor"), color_.name(QColor::HexRgb));
        setHighlightArmed(true);
    }
    void armEraser() {
        if (!canAnnotate()) { eraserArmed_ = false; eraser_->setChecked(false); return; }
        clearPendingSelection();
        if (textBox_->isChecked()) textBox_->setChecked(false);
        selectText();
        if (!checked("actionSelectText")) { eraser_->setChecked(false); return; }
        setHighlightArmed(false);
        eraserArmed_ = true; eraser_->setChecked(true);
    }
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
    }
    void applyHighlight() {
        if (!highlightArmed_ || selectionPage_ < 0) return;
        auto* c = window_.getProgramController();
        if (!canAnnotate() || !checked("actionSelectText")) { clearPendingSelection(); return; }
        reloadHighlightColor();
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

        // PDF4QT may return several quads for one visual line (separate PDF
        // text runs/words). Merge only close, horizontal, same-row runs before
        // clipping, so visible word spaces also receive a continuous highlight.
        // This does NOT alter the selected text or bridge across columns.
        QList<QRectF> bands;
        QPolygonF rotated;
        for (qsizetype i = 0; i + 3 < quads.size(); i += 4) {
            QPolygonF quad;
            for (qsizetype j = 0; j < 4; ++j) quad << quads[i + j];
            const QRectF r = quad.boundingRect().normalized();
            if (!r.isValid() || r.width() < 0.5 || r.height() < 0.5) continue;
            if (qAbs(quad[0].y() - quad[1].y()) > 0.6 ||
                qAbs(quad[2].y() - quad[3].y()) > 0.6) {
                for (const auto& p : quad) rotated << p;
                continue;
            }
            bands.append(r);
        }
        std::sort(bands.begin(), bands.end(), [](const QRectF& a, const QRectF& b) {
            if (a.center().y() != b.center().y())
                return a.center().y() < b.center().y();
            return a.left() < b.left();
        });
        QList<QRectF> joined;
        for (const QRectF& band : bands) {
            bool merged = false;
            for (QRectF& prev : joined) {
                const qreal h = qMin(prev.height(), band.height());
                const qreal gap = qMax(0.0, qMax(prev.left(), band.left()) -
                    qMin(prev.right(), band.right()));
                if (qAbs(prev.center().y() - band.center().y()) <= h * 0.30 &&
                    qAbs(prev.height() - band.height()) <= h * 0.40 &&
                    gap <= qMin(28.0, 2.2 * h)) {
                    prev = prev.united(band);
                    merged = true;
                    break;
                }
            }
            if (!merged) joined.append(band);
        }

        // Recolor only the selected region. PDF Highlight annotations may
        // cover an entire paragraph in a single /QuadPoints array. Keep the
        // segments outside the new drag in their original annotation/color.
        const auto pageIndex = selectionPage_;
        const auto* page = c->getDocument()->getCatalog()->getPage(pageIndex);
        if (!page) { clearPendingSelection(); return; }
        auto* manager = c->getPdfWidget()->getAnnotationManager();
        const auto existing = manager->getPageAnnotations(pageIndex).annotations;
        struct RecolorEdit { pdf::PDFObjectReference ref; QPolygonF remainder; };
        QList<RecolorEdit> edits;
        QList<QRectF> alreadyTargetColor;
        QPainterPath alreadyHighlighted;
        auto isSameRow = [](const QRectF& a, const QRectF& b) {
            const QRectF overlap = a.intersected(b);
            return a.isValid() && b.isValid() &&
                overlap.height() >= qMin(a.height(), b.height()) * 0.45 &&
                overlap.width() > 0.3;
        };
        auto subtractX = [](const QList<QPair<qreal, qreal>>& segments, qreal lo, qreal hi) {
            QList<QPair<qreal, qreal>> next;
            for (const auto& seg : segments) {
                if (hi <= seg.first || lo >= seg.second) { next.append(seg); continue; }
                if (lo - seg.first >= 0.7) next.append({seg.first, lo});
                if (seg.second - hi >= 0.7) next.append({hi, seg.second});
            }
            return next;
        };
        auto appendRect = [](QPolygonF& output, const QRectF& rect) {
            if (rect.isValid() && rect.width() > 0.7 && rect.height() > 0.7)
                output << rect.topLeft() << rect.topRight()
                       << rect.bottomLeft() << rect.bottomRight();
        };

        for (const auto& item : existing) {
            if (item.annotation->getType() != pdf::AnnotationType::Highlight) continue;
            const auto* old = dynamic_cast<const pdf::PDFHighlightAnnotation*>(item.annotation.data());
            if (!old) continue;
            const auto& quads = old->getHiglightArea().getQuadrilaterals();
            const auto components = old->getColor();
            const bool sameColor = components.size() >= 3 &&
                qAbs(components[0] - color_.redF()) < 0.012 &&
                qAbs(components[1] - color_.greenF()) < 0.012 &&
                qAbs(components[2] - color_.blueF()) < 0.012;
            alreadyHighlighted.addPath(old->getHiglightArea().getPath());
            if (quads.empty()) {
                const QRectF oldRect = old->getHiglightArea().getPath().boundingRect();
                bool overlaps = false;
                for (const auto& band : joined)
                    if (isSameRow(oldRect, band)) { overlaps = true; break; }
                if (sameColor && oldRect.isValid()) alreadyTargetColor.append(oldRect);
                // Legacy annotations without QuadPoints cannot safely be split
                // by text position. Never silently stack transparent colors.
                if (overlaps && !sameColor) {
                    clearPendingSelection();
                    QMessageBox::warning(draw_, QStringLiteral("형광펜 색상 교체 제한"),
                        QStringLiteral("기존 PDF 형광펜에 글자 범위 정보(QuadPoints)가 없어 선택한 부분만 교체할 수 없습니다."));
                    return;
                }
                continue;
            }
            QPolygonF retained;
            bool changed = false;
            for (const auto& quad : quads) {
                QPolygonF geometry;
                for (const auto& point : quad) geometry << point;
                if (geometry.size() != 4) continue;
                const QRectF original = geometry.boundingRect().normalized();
                if (sameColor) {
                    alreadyTargetColor.append(original);
                    for (const auto& point : geometry) retained << point;
                    continue;
                }
                QList<QPair<qreal, qreal>> segments{{original.left(), original.right()}};
                const bool rotatedOld = qAbs(geometry[0].y() - geometry[1].y()) > 0.7 ||
                    qAbs(geometry[2].y() - geometry[3].y()) > 0.7;
                for (const QRectF& band : joined) {
                    if (!isSameRow(original, band)) continue;
                    if (rotatedOld) {
                        clearPendingSelection();
                        QMessageBox::warning(draw_, QStringLiteral("형광펜 색상 교체 제한"),
                            QStringLiteral("회전된 기존 형광펜은 선택 영역을 안전하게 분리할 수 없습니다."));
                        return;
                    }
                    segments = subtractX(segments, band.left() - 0.20, band.right() + 0.20);
                    changed = true;
                }
                if (!changed || segments.size() == 1 &&
                    qAbs(segments.front().first - original.left()) < 0.01 &&
                    qAbs(segments.front().second - original.right()) < 0.01) {
                    for (const auto& point : geometry) retained << point;
                } else {
                    for (const auto& segment : segments)
                        appendRect(retained, QRectF(QPointF(segment.first, original.top()),
                            QPointF(segment.second, original.bottom())));
                }
            }
            if (changed) {
                if (!manager->canDeleteAnnotation(item) || !manager->canModifyAnnotation(item)) {
                    clearPendingSelection();
                    QMessageBox::warning(draw_, QStringLiteral("형광펜 수정 불가"),
                        QStringLiteral("수정 권한이 없는 기존 형광펜이 선택 영역에 포함되어 있습니다."));
                    return;
                }
                edits.append({item.annotation->getSelfReference(), retained});
            }
        }
        QPolygonF clean;
        for (const auto& band : joined) {
            QList<QPair<qreal, qreal>> unpainted{{band.left(), band.right()}};
            for (const QRectF& already : alreadyTargetColor) {
                if (!isSameRow(band, already)) continue;
                unpainted = subtractX(unpainted, already.left(), already.right());
                if (unpainted.isEmpty()) break;
            }
            for (const auto& seg : unpainted)
                appendRect(clean, QRectF(QPointF(seg.first, band.top()),
                                        QPointF(seg.second, band.bottom())));
        }
        // For rotated selected glyphs, avoid losing the original geometry;
        // reject overlaps rather than recoloring unsupported affine quads.
        for (qsizetype i = 0; i + 3 < rotated.size(); i += 4) {
            QPolygonF quad;
            for (int j = 0; j < 4; ++j) quad << rotated[i + j];
            QPainterPath part; part.addPolygon(quad); part.closeSubpath();
            if (alreadyHighlighted.intersects(part) || alreadyHighlighted.contains(part)) {
                clearPendingSelection();
                QMessageBox::warning(draw_, QStringLiteral("형광펜 색상 교체 제한"),
                    QStringLiteral("회전된 선택 글자는 기존 형광펜과 안전하게 분리할 수 없습니다."));
                return;
            }
            for (const auto& point : quad) clean << point;
        }
        clearPendingSelection();
        if (clean.isEmpty() && edits.isEmpty()) return;
        pdf::PDFDocumentModifier modifier(c->getDocument());
        for (const auto& edit : edits) {
            if (edit.remainder.isEmpty())
                modifier.getBuilder()->removeAnnotation(page->getPageReference(), edit.ref);
            else {
                modifier.getBuilder()->setAnnotationQuadPoints(edit.ref, edit.remainder);
                modifier.getBuilder()->updateAnnotationAppearanceStreams(edit.ref);
            }
        }
        if (!clean.isEmpty()) {
            const pdf::PDFObjectReference ref = modifier.getBuilder()->createAnnotationHighlight(
                page->getPageReference(), clean, color_);
            modifier.getBuilder()->setAnnotationOpacity(ref, 0.36);
            modifier.getBuilder()->updateAnnotationAppearanceStreams(ref);
        }
        modifier.markAnnotationsChanged();
        if (modifier.finalize())
            Q_EMIT c->getToolManager()->documentModified(
                pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
    void eraseSelectedHighlights() {
        if (!eraserArmed_ || selectionPage_ < 0) return;
        auto* c = window_.getProgramController();
        if (!canAnnotate() || !checked("actionSelectText")) { clearPendingSelection(); return; }
        auto* proxy = c->getPdfWidget()->getDrawWidgetProxy();
        auto* manager = c->getPdfWidget()->getAnnotationManager();
        auto* selected = c->getToolManager()->getSelectTextTool();
        if (!selected || selected->getSelectedText().trimmed().isEmpty()) { clearPendingSelection(); return; }
        pdf::PDFTextLayout layout = proxy->getTextLayoutCompiler()->getTextLayoutLazy(selectionPage_);
        const pdf::PDFTextSelection selection = layout.createTextSelection(
            selectionPage_, selectionStart_, selectionEnd_, color_);
        if (selection.isEmpty()) { clearPendingSelection(); return; }
        pdf::PDFTextLayoutGetter getter = proxy->getTextLayoutCompiler()->getTextLayoutLazy(selectionPage_);
        QPolygonF quads;
        pdf::PDFTextSelectionPainter painter(&selection);
        if (painter.prepareGeometry(selectionPage_, getter, QTransform(), &quads).isEmpty()) {
            clearPendingSelection(); return;
        }
        QPainterPath area;
        for (qsizetype i = 0; i + 3 < quads.size(); i += 4) {
            QPolygonF quad;
            for (qsizetype j = 0; j < 4; ++j) quad << quads[i + j];
            area.addPolygon(quad); area.closeSubpath();
        }
        const auto pageIndex = selectionPage_;
        clearPendingSelection();
        if (area.isEmpty()) return;
        const auto* page = c->getDocument()->getCatalog()->getPage(pageIndex);
        if (!page) return;
        const auto existing = manager->getPageAnnotations(pageIndex).annotations;
        pdf::PDFDocumentModifier modifier(c->getDocument());
        int affected = 0;
        for (const auto& item : existing) {
            if (item.annotation->getType() != pdf::AnnotationType::Highlight ||
                !manager->canDeleteAnnotation(item)) continue;
            const auto* marked = dynamic_cast<const pdf::PDFHighlightAnnotation*>(item.annotation.data());
            if (!marked) continue;
            const auto& path = marked->getHiglightArea().getPath();
            if (!area.intersects(path) && !area.contains(path) && !path.contains(area)) continue;
            // Preserve the other lines of a multi-line highlight. Editing
            // QuadPoints in place keeps the annotation's author, color,
            // opacity and comments, unlike deleting/recreating the mark.
            QPolygonF remaining;
            bool erasedQuadrilateral = false;
            const auto& quadrilaterals = marked->getHiglightArea().getQuadrilaterals();
            for (const auto& quad : quadrilaterals) {
                QPolygonF geometry;
                for (const auto& point : quad) geometry << point;
                QPainterPath region; region.addPolygon(geometry); region.closeSubpath();
                if (area.intersects(region) || area.contains(region) || region.contains(area)) {
                    erasedQuadrilateral = true;
                } else {
                    for (const auto& point : quad) remaining << point;
                }
            }
            if (!erasedQuadrilateral && !quadrilaterals.empty()) continue;
            const auto ref = item.annotation->getSelfReference();
            if (remaining.isEmpty()) {
                modifier.getBuilder()->removeAnnotation(page->getPageReference(), ref);
            } else {
                modifier.getBuilder()->setAnnotationQuadPoints(ref, remaining);
                modifier.getBuilder()->updateAnnotationAppearanceStreams(ref);
            }
            ++affected;
        }
        if (!affected) return;
        modifier.markAnnotationsChanged();
        if (modifier.finalize())
            Q_EMIT c->getToolManager()->documentModified(
                pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
    void startVerticalAutoscroll(const QPoint& point) {
        auto* c = window_.getProgramController();
        if (!c || !c->getDocument() || !draw_->isVisible() || !c->getPdfWidget()) return;
        auto* scroll = c->getPdfWidget()->getVerticalScrollbar();
        if (!scroll || scroll->maximum() <= scroll->minimum()) return;
        scrollOrigin_ = draw_->mapToGlobal(point);
        // Avoid constructing a child overlay during native shell startup.
        if (!autoscrollMarker_) autoscrollMarker_ = new VerticalAutoscrollMarker(draw_);
        autoscrollMarker_->move(point - QPoint(18, 24));
        autoscrollMarker_->show();
        autoscrollMarker_->raise();
        scrollRemainder_ = 0.0;
        autoscrollActive_ = true;
        draw_->setCursor(Qt::SizeVerCursor);
        autoscrollTimer_.start();
    }
    void stopVerticalAutoscroll() {
        if (!autoscrollActive_) return;
        autoscrollActive_ = false;
        autoscrollTimer_.stop();
        if (autoscrollMarker_) autoscrollMarker_->hide();
        draw_->unsetCursor();
    }
    void tickVerticalAutoscroll() {
        auto* c = window_.getProgramController();
        if (!autoscrollActive_ || !draw_->isVisible() || !c || !c->getDocument()) {
            stopVerticalAutoscroll(); return;
        }
        auto* scroll = c->getPdfWidget()->getVerticalScrollbar();
        if (!scroll) { stopVerticalAutoscroll(); return; }
        const int displacement = QCursor::pos().y() - scrollOrigin_.y();
        const int distance = qMax(0, qAbs(displacement) - 13);
        if (!distance) return;
        // Continuous up/down auto-scroll, with a dead zone and smooth
        // acceleration. Bound per-tick work so expensive SI pages stay usable.
        const double pixels = qMin(100.0, 1.0 + double(distance * distance) / 190.0);
        scrollRemainder_ += displacement > 0 ? pixels : -pixels;
        const int move = int(scrollRemainder_);
        scrollRemainder_ -= move;
        if (move) scroll->setValue(qBound(scroll->minimum(), scroll->value() + move, scroll->maximum()));
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
        if (rect.width() < 8 || rect.height() < 8) {
            // Single-click creates a readable default-sized FreeText, but the
            // click may be close to the PDF page edge. Keep the entire box
            // inside the page's crop bounds instead of leaving its edit target
            // outside the visible sheet (e.g. a 300x220 synthetic page).
            const auto page = c->getDocument()->getCatalog()->getPage(pageIndex);
            if (!page) return;
            const QRectF crop = page->getCropBox().normalized();
            if (crop.width() < 8 || crop.height() < 8) return;
            rect = QRectF(start, QSizeF(qMin(180.0, crop.width()), qMin(68.0, crop.height())));
            rect.moveLeft(qBound(crop.left(), rect.left(), crop.right() - rect.width()));
            rect.moveTop(qBound(crop.top(), rect.top(), crop.bottom() - rect.height()));
        }
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
    // Text-selection mode owns regular glyph drags. A FreeText rectangle
    // uses a LUNA-local preview and PDF4QT's *document* mutation API instead
    // of forwarding synthetic mouse gestures into the native drag-and-drop
    // event loop (which can block on Windows).
    void cancelTextBoxDrag() {
        draggingTextBox_ = false;
        boxReference_ = {};
        boxDocument_ = nullptr;
        if (boxPreview_) boxPreview_->hide();
        draw_->unsetCursor();
    }
    QRectF previewTextBox(const QPoint& to) const {
        QRectF device = boxDeviceRect_;
        if (boxHandle_ == 0) {
            device.translate(to - boxStart_);
        } else {
            if (boxHandle_ & 1) device.setLeft(to.x());
            if (boxHandle_ & 2) device.setRight(to.x());
            if (boxHandle_ & 4) device.setTop(to.y());
            if (boxHandle_ & 8) device.setBottom(to.y());
            if (device.width() < 12 || device.height() < 12) return {};
        }
        return device.normalized();
    }
    bool beginTextBoxDrag(QMouseEvent* event) {
        if (event->button() != Qt::LeftButton || !checked("actionSelectText") ||
            !canAnnotate() || textBox_->isChecked()) return false;
        auto* c = window_.getProgramController();
        auto* widget = c->getPdfWidget();
        auto* manager = widget->getAnnotationManager();
        const auto snapshot = widget->getDrawWidgetProxy()->getSnapshot();
        for (const auto& page : snapshot.items) {
            const auto* pdfPage = c->getDocument()->getCatalog()->getPage(page.pageIndex);
            if (!pdfPage) continue;
            const auto& items = manager->getPageAnnotations(page.pageIndex).annotations;
            for (auto it = items.rbegin(); it != items.rend(); ++it) {
                const auto& item = *it;
                if (item.annotation->getType() != pdf::AnnotationType::FreeText ||
                    !manager->isAnnotationSelectable(item) ||
                    !manager->canModifyAnnotation(item)) continue;
                QRectF rect = item.annotation->getRectangle().normalized();
                const auto matrix = manager->prepareTransformations(
                    page.pageToDeviceMatrix, widget, item.annotation->getEffectiveFlags(),
                    pdfPage, rect);
                const QRectF device = matrix.mapRect(rect).normalized();
                const bool selected = manager->isAnnotationSelected(item.annotation->getSelfReference());
                if (!device.adjusted(selected ? -10 : 0, selected ? -10 : 0,
                                     selected ? 10 : 0, selected ? 10 : 0).contains(event->pos()))
                    continue;
                // A new click moves the note. Only an already-selected box
                // exposes resizing from its corners (PPT-like behavior).
                int handle = 0;
                if (selected) {
                    const QPointF p = event->position();
                    const qreal edge = 10.0;
                    if (qAbs(p.x() - device.left()) <= edge) handle |= 1;
                    else if (qAbs(p.x() - device.right()) <= edge) handle |= 2;
                    if (qAbs(p.y() - device.top()) <= edge) handle |= 4;
                    else if (qAbs(p.y() - device.bottom()) <= edge) handle |= 8;
                    if ((handle & 3) == 0 || (handle & 12) == 0) handle = 0;
                }
                clearPendingSelection();
                target_ = {};
                boxDocument_ = c->getDocument();
                boxReference_ = item.annotation->getSelfReference();
                boxPage_ = page.pageIndex;
                boxStart_ = event->pos();
                boxDeviceRect_ = device;
                boxPageToDevice_ = matrix;
                boxHandle_ = handle;
                manager->selectAnnotation(boxReference_, true);
                if (!boxPreview_) {
                    boxPreview_ = new QRubberBand(QRubberBand::Rectangle, draw_);
                    boxPreview_->setAttribute(Qt::WA_TransparentForMouseEvents);
                }
                boxPreview_->setGeometry(device.toAlignedRect());
                boxPreview_->hide();
                draw_->setCursor(handle ? Qt::SizeFDiagCursor : Qt::SizeAllCursor);
                draggingTextBox_ = true;
                return true;
            }
        }
        if (manager->hasSelection()) manager->clearSelection();
        return false;
    }
    void finishTextBoxDrag(const QPoint& to) {
        if (!draggingTextBox_) return;
        const bool moved = (to - boxStart_).manhattanLength() >= QApplication::startDragDistance();
        const auto document = boxDocument_;
        const auto annotation = boxReference_;
        const auto matrix = boxPageToDevice_;
        const QRectF original = boxDeviceRect_;
        const int handle = boxHandle_;
        const QPoint start = boxStart_;
        const pdf::PDFInteger pageIndex = boxPage_;
        cancelTextBoxDrag();
        if (!moved) return; // Click only selects the FreeText rectangle.
        auto* c = window_.getProgramController();
        if (!canAnnotate() || c->getDocument() != document) return;
        const auto* pdfPage = c->getDocument()->getCatalog()->getPage(pageIndex);
        if (!pdfPage) return;
        bool invertible = false;
        const auto inverse = matrix.inverted(&invertible);
        if (!invertible) return;
        QRectF device = original;
        if (handle == 0) {
            device.translate(to - start);
        } else {
            if (handle & 1) device.setLeft(to.x());
            if (handle & 2) device.setRight(to.x());
            if (handle & 4) device.setTop(to.y());
            if (handle & 8) device.setBottom(to.y());
            if (device.width() < 12 || device.height() < 12) return;
        }
        QRectF pageRect = inverse.mapRect(device).normalized();
        const QRectF crop = pdfPage->getCropBox().normalized();
        if (pageRect.width() < 8 || pageRect.height() < 8 ||
            pageRect.width() > crop.width() || pageRect.height() > crop.height()) return;
        if (handle == 0) {
            pageRect.moveLeft(qBound(crop.left(), pageRect.left(), crop.right() - pageRect.width()));
            pageRect.moveTop(qBound(crop.top(), pageRect.top(), crop.bottom() - pageRect.height()));
        } else {
            pageRect = pageRect.intersected(crop);
            if (pageRect.width() < 8 || pageRect.height() < 8) return;
        }
        // Validate the annotation reference still exists on the active page.
        auto* manager = c->getPdfWidget()->getAnnotationManager();
        bool found = false;
        for (const auto& item : manager->getPageAnnotations(pageIndex).annotations)
            if (item.annotation->getSelfReference() == annotation &&
                item.annotation->getType() == pdf::AnnotationType::FreeText &&
                manager->canModifyAnnotation(item)) { found = true; break; }
        if (found && manager->setAnnotationRectangle(annotation, pageRect))
            manager->selectAnnotation(annotation, true); // Keep handles visible after moving.
    }
    bool eventFilter(QObject* object, QEvent* event) override {
        // Handle only the PDF draw widget; never steal mouse/key events
        // globally from the native tab shell, toolbar, menus or QDialogs.
        if (object != draw_) return false;
        if (autoscrollActive_) {
            if (event->type() == QEvent::KeyPress &&
                static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
                stopVerticalAutoscroll();
            if (event->type() == QEvent::MouseButtonPress ||
                event->type() == QEvent::MouseButtonDblClick) {
                const auto* mouse = static_cast<QMouseEvent*>(event);
                stopVerticalAutoscroll();
                if (mouse->button() == Qt::MiddleButton) return true;
            }
        }
        if (draggingTextBox_) {
            if (event->type() == QEvent::MouseMove) {
                const QPoint to = static_cast<QMouseEvent*>(event)->pos();
                const QRectF next = previewTextBox(to);
                if (boxPreview_) {
                    if (next.isValid() &&
                        (to - boxStart_).manhattanLength() >= QApplication::startDragDistance()) {
                        boxPreview_->setGeometry(next.toAlignedRect());
                        boxPreview_->show();
                    } else boxPreview_->hide();
                }
                return true;
            }
            if (event->type() == QEvent::MouseButtonRelease &&
                static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
                finishTextBoxDrag(static_cast<QMouseEvent*>(event)->pos());
                return true;
            }
            if (event->type() == QEvent::KeyPress &&
                static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
                cancelTextBoxDrag();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::MiddleButton) {
                startVerticalAutoscroll(mouse->pos());
                return true; // Prevent PDF4QT's native four-way hand-panning.
            }
        }
        if (event->type() == QEvent::MouseButtonRelease &&
            static_cast<QMouseEvent*>(event)->button() == Qt::MiddleButton)
            return true;
        if (event->type() == QEvent::MouseButtonDblClick && checked("actionSelectText") &&
            !textBox_->isChecked()) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && editTextBoxAt(mouse->pos()))
                return true;
        }
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) &&
            static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            target_ = {}; clearPendingSelection(); textBox_->setChecked(false);
            setHighlightArmed(false);
            eraserArmed_ = false; eraser_->setChecked(false);
            selectText(); return false;
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
            if (beginTextBoxDrag(mouse)) return true;
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
                    const bool ready = canAnnotate() && selected && !selected->getSelectedText().trimmed().isEmpty() &&
                        !textBox_->isChecked() && selectionPage_ >= 0;
                    if (ready && eraserArmed_) eraseSelectedHighlights();
                    else if (ready && highlightArmed_) applyHighlight();
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
    bool deleting_ = false, editable_ = false, drawingBox_ = false, draggingTextBox_ = false;
    QRubberBand* boxPreview_ = nullptr;
    const pdf::PDFDocument* boxDocument_ = nullptr;
    pdf::PDFObjectReference boxReference_;
    pdf::PDFInteger boxPage_ = -1;
    QRectF boxDeviceRect_;
    QTransform boxPageToDevice_;
    QPoint boxStart_;
    int boxHandle_ = 0;
    bool highlightArmed_ = false;
    bool eraserArmed_ = false;
    QString lastDocumentSource_;
    QAction* eraser_ = nullptr;
    QAction* chooseColor_ = nullptr;
    QList<QAction*> swatches_;
    QWidget* autoscrollMarker_ = nullptr;
    QTimer autoscrollTimer_;
    QPoint scrollOrigin_;
    bool autoscrollActive_ = false;
    double scrollRemainder_ = 0.0;
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
        add("actionSelectText"); add("LunaFreeTextBox"); add("LunaCropThumbnail");
        bar_->addAction(separators_[3]); add("actionFind"); add("actionPrint");
        bar_->addAction(separators_[4]); // Print | Eraser | 8 marker colors
        add("LunaEraseSelected");
        for (int i = 1; i <= 8; ++i) {
            const QByteArray name = QStringLiteral("LunaHighlightSwatch%1").arg(i).toLatin1();
            add(name.constData());
        }
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
    QAction* separators_[5]{};
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
    // Native click-to-delete is kept in Tools, not in the compact toolbar.
    if (auto* nativeDelete = nativeAction(window, "actionDeleteAnnotation"))
        nativeDelete->setToolTip(QStringLiteral("기타 주석을 직접 클릭해 삭제합니다. 형광펜은 선택형 지우개를 사용하세요."));
    bar.addSeparator();
    add("LunaCropThumbnail", "crop", QStringLiteral("논문 목록 썸네일 크롭"), QStringLiteral("PDF 화면에서 영역을 선택해 논문 목록 썸네일 PNG로 저장"));
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
    auto* interaction = new ReadingInteraction(window);
    // The actions are created after the adapter to preserve QObject ownership;
    // both the embedded toolbar and the shell's common toolbar bind by name.
    if (auto* note = window.findChild<QAction*>(QStringLiteral("LunaFreeTextBox")))
        bar.insertAction(nativeAction(window, "actionFind"), note);
    bar.addSeparator();
    if (auto* erase = window.findChild<QAction*>(QStringLiteral("LunaEraseSelected"))) bar.addAction(erase);
    for (int i = 1; i <= 8; ++i) {
        if (auto* swatch = window.findChild<QAction*>(QStringLiteral("LunaHighlightSwatch%1").arg(i)))
            bar.addAction(swatch);
    }
    Q_UNUSED(interaction);
}

bool verifyReaderToolbar(pdfviewer::PDFEditorMainWindow& window, QString& error, bool geometry) {
    auto fail = [&](const char* message) { error = QString::fromLatin1(message); return false; };
    auto* bar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    if (!bar || !bar->isVisible() || bar->orientation() != Qt::Horizontal) return fail("Reading toolbar missing");
    const QStringList allowed = {"LunaCropThumbnail", "LunaOpenDocuments", "actionSave", "actionGoToPreviousPage", "actionGoToNextPage", "actionZoom_Out", "actionZoom_In", "actionSelectText", "LunaFreeTextBox", "LunaEraseSelected", "actionFind", "actionPrint", "LunaHighlightSwatch1", "LunaHighlightSwatch2", "LunaHighlightSwatch3", "LunaHighlightSwatch4", "LunaHighlightSwatch5", "LunaHighlightSwatch6", "LunaHighlightSwatch7", "LunaHighlightSwatch8"};
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
    const auto toolbarOrder = bar->actions();
    const int printIndex = toolbarOrder.indexOf(nativeAction(window, "actionPrint"));
    if (printIndex < 0 || printIndex + 10 >= toolbarOrder.size() ||
        !toolbarOrder[printIndex + 1]->isSeparator() ||
        toolbarOrder[printIndex + 2] != nativeAction(window, "LunaEraseSelected") ||
        toolbarOrder[printIndex + 3] != window.findChild<QAction*>(QStringLiteral("LunaHighlightSwatch1")))
        return fail("Print / eraser / colored marker order regressed");
    for (int i = 1; i <= 8; ++i)
        if (toolbarOrder[printIndex + 2 + i] !=
            window.findChild<QAction*>(QStringLiteral("LunaHighlightSwatch%1").arg(i)))
            return fail("Inline highlight palette order regressed");
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
