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
        setModel(native.model());
        auto* temporarySelection = selectionModel();
        setSelectionModel(native.selectionModel());
        if (temporarySelection && temporarySelection != selectionModel() && temporarySelection->parent() == this) temporarySelection->deleteLater();
        thumbnailSize_(qMax(24, qMax(iconSize().width(), iconSize().height()))); doItemsLayout(); scheduleLayout();
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
            if (gridSize() == grid) return;
            setGridSize(grid);
            setIconSize(QSize(previewWidth, previewHeight));
            thumbnailSize_(previewHeight);
            doItemsLayout();
        });
    }
    std::function<void(int)> thumbnailSize_;
    bool scheduled_ = false;
};
}
