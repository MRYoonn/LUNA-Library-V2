// SPDX-License-Identifier: MIT
#pragma once
#include <QListView>
#include <QItemSelectionModel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <functional>

namespace luna {
// Presentation only: the caller supplies PDF4QT's existing model/selection.
class PageGrid final : public QListView {
public:
    PageGrid(QListView& native, std::function<void(int)> thumbnailSize, QWidget* parent)
        : QListView(parent), thumbnailSize_(std::move(thumbnailSize)) {
        setObjectName(QStringLiteral("LunaPageGrid"));
        setModel(native.model());
        auto* temporarySelection = selectionModel();
        setSelectionModel(native.selectionModel());
        if (temporarySelection && temporarySelection != selectionModel() && temporarySelection->parent() == this) temporarySelection->deleteLater();
        setViewMode(QListView::IconMode);
        setFlow(QListView::LeftToRight);
        setWrapping(true);
        setMovement(QListView::Static);
        setResizeMode(QListView::Adjust);
        setUniformItemSizes(true);
        setSpacing(0);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        setSelectionMode(QAbstractItemView::SingleSelection);
        viewport()->installEventFilter(this);
    }
    void bindNative(QListView& native) {
        // A long SI may have hundreds of pages. Do not request thumbnail
        // regeneration for a hidden page grid whenever its PDF tab activates.
        // The visible page view will size the new native model on demand.
        if (model() == native.model() && selectionModel() == native.selectionModel()) return;
        setModel(native.model());
        auto* temporarySelection = selectionModel();
        setSelectionModel(native.selectionModel());
        if (temporarySelection && temporarySelection != selectionModel() && temporarySelection->parent() == this) temporarySelection->deleteLater();
        pendingModelSize_ = true;
        if (isVisible()) scheduleLayout();
    }
protected:
    void resizeEvent(QResizeEvent* event) override {
        QListView::resizeEvent(event);
        scheduleLayout();
    }
    void showEvent(QShowEvent* event) override {
        QListView::showEvent(event);
        scheduleLayout();
    }
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == viewport() && event->type() == QEvent::Resize) scheduleLayout();
        return QListView::eventFilter(object, event);
    }
private:
    void scheduleLayout() {
        if (scheduled_) return;
        scheduled_ = true;
        QTimer::singleShot(0, this, [this] {
            scheduled_ = false;
            // Hidden page grids do not need an item layout or PDF thumbnails.
            // showEvent will schedule the pending work when the user opens it.
            if (!isVisible()) return;
            // QAbstractScrollArea may resize its viewport AFTER resizeEvent,
            // especially when a hidden stacked page becomes visible. Measure
            // the settled viewport, and leave rounding room at the row edge.
            const int cell = qMax(40, (viewport()->width() - 2) / 2);
            // The approved HTML caps page-paper width at 112px, with a
            // portrait preview slot. The native model still supplies the real
            // page image/aspect ratio; this only sizes the two-column cells.
            const int previewWidth = qMin(112, cell - 16);
            const int previewHeight = qRound(previewWidth / .72);
            const QSize grid(cell, previewHeight + 38);
            if (gridSize() == grid && !pendingModelSize_) return;
            setGridSize(grid);
            setIconSize(QSize(previewWidth, previewHeight));
            // Only resize thumbnails while the page view is actually shown.
            // A model switch while hidden must not spend CPU rendering a
            // 200+ page SI before the reader displays its first page.
            if (isVisible()) {
                thumbnailSize_(previewHeight);
                pendingModelSize_ = false;
            }
            doItemsLayout();
        });
    }
    std::function<void(int)> thumbnailSize_;
    bool scheduled_ = false;
    bool pendingModelSize_ = true;
};
}
