// SPDX-License-Identifier: MIT
#pragma once
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"
#include "pdfdocumentbuilder.h"
#include "pdfcompiler.h"
#include "pdftextlayout.h"
#include <QColorDialog>
#include <QMenu>
#include <QSettings>
#include <QToolButton>
#include <QMessageBox>
#include <algorithm>

namespace luna {
inline QSettings readerSettings() { return QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library")); }
inline void addAnnotationColors(QMenu* menu, const QString& key, const std::function<void(QColor)>& apply) {
    auto choose = [key, apply](QColor color) {
        if (!color.isValid()) return;
        auto settings = readerSettings(); QStringList recent = settings.value(key + "/recent").toStringList();
        recent.removeAll(color.name()); recent.prepend(color.name()); while (recent.size() > 8) recent.removeLast();
        settings.setValue(key + "/recent", recent); settings.setValue(key, color.name()); apply(color);
    };
    const QStringList defaults{"#fff2a6", "#d7eaff", "#ddf4e1", "#ffe0eb", "#e9ddff", "#ffffff"};
    QStringList colors = defaults; colors.append(readerSettings().value(key + "/recent").toStringList()); colors.removeDuplicates();
    for (const QString& value : colors) {
        QPixmap swatch(18,18); swatch.fill(QColor(value));
        auto* action = menu->addAction(QIcon(swatch), value);
        QObject::connect(action, &QAction::triggered, menu, [choose, value] { choose(QColor(value)); });
    }
    menu->addSeparator(); auto* custom = menu->addAction(QStringLiteral("사용자 지정 색상…"));
    QObject::connect(custom, &QAction::triggered, menu, [menu, key, choose] { choose(QColorDialog::getColor(QColor(readerSettings().value(key, "#fff2a6").toString()), menu->parentWidget(), QStringLiteral("주석 색상"))); });
}

// A single native document transaction, including partial quad edits, feeds
// the same annotation manager/undo stack as PDF4QT's own tools.
inline bool applySelectedHighlight(pdfviewer::PDFEditorMainWindow& window, bool erase) {
    auto* controller = window.getProgramController(); const auto* document = controller->getDocument();
    auto* manager = controller->getPdfWidget()->getAnnotationManager();
    auto* tool = controller->getToolManager()->getSelectTextTool();
    if (!document || !controller->canClose() || !window.isEnabled() || !manager->isModificationAllowed() || tool->lunaTextSelection().isEmpty()) return false;
    const pdf::PDFTextSelection selection = tool->lunaTextSelection(); pdf::PDFTextSelectionPainter painter(&selection);
    pdf::PDFDocumentModifier modifier(document); modifier.markAnnotationsChanged(); auto* builder = modifier.getBuilder();
    for (pdf::PDFInteger page = 0; page < document->getCatalog()->getPageCount(); ++page) {
        if (selection.begin(page) == selection.end(page)) continue;
        auto layout = controller->getPdfWidget()->getDrawWidgetProxy()->getTextLayoutCompiler()->getTextLayoutLazy(page);
        QPolygonF selected; painter.prepareGeometry(page, layout, QTransform(), &selected);
        if (selected.isEmpty()) continue;
        const auto pageRef = document->getCatalog()->getPage(page)->getPageReference();
        if (!erase) { builder->createAnnotationHighlight(pageRef, selected, QColor(readerSettings().value("reader/highlightColor", "#fff2a6").toString())); continue; }
        for (const auto& item : manager->getPageAnnotations(page).annotations) {
            if (item.annotation->getType() != pdf::AnnotationType::Highlight || !manager->canModifyAnnotation(item) || !manager->canDeleteAnnotation(item)) continue;
            const auto* highlight = static_cast<const pdf::PDFHighlightAnnotation*>(item.annotation.get());
            QPolygonF remaining; bool changed = false;
            for (const auto& quad : highlight->getHiglightArea().getQuadrilaterals()) {
                QPolygonF frame; frame << quad[0] << quad[1] << quad[3] << quad[2];
                QPolygonF unit; unit << QPointF(0,0) << QPointF(1,0) << QPointF(1,1) << QPointF(0,1);
                QTransform toLocal; if (!QTransform::quadToQuad(frame, unit, toLocal)) return false;
                const auto fromLocal = toLocal.inverted();
                QList<QPair<double,double>> spans{{0.0,1.0}};
                for (int offset = 0; offset + 3 < selected.size(); offset += 4) {
                    const QRectF cut = toLocal.map(selected.mid(offset,4)).boundingRect();
                    // Text on the same baseline. Never erase another line or
                    // approximate an unrelated slanted/vertical selection.
                    if (cut.bottom() < .5 || cut.top() > .5 || cut.right() <= 0 || cut.left() >= 1) continue;
                    const double left = qBound(0.0, cut.left(), 1.0), right = qBound(0.0, cut.right(), 1.0);
                    QList<QPair<double,double>> next;
                    for (auto span : spans) {
                        if (right <= span.first || left >= span.second) { next.append(span); continue; }
                        changed = true;
                        if (left > span.first + .0001) next.append({span.first, left});
                        if (right < span.second - .0001) next.append({right, span.second});
                    }
                    spans = next;
                }
                for (auto span : spans) remaining << fromLocal.map(QPointF(span.first,0)) << fromLocal.map(QPointF(span.second,0)) << fromLocal.map(QPointF(span.first,1)) << fromLocal.map(QPointF(span.second,1));
            }
            if (changed) {
                if (remaining.isEmpty()) builder->removeAnnotation(pageRef, item.annotation->getSelfReference());
                else { builder->setAnnotationQuadPoints(item.annotation->getSelfReference(), remaining); builder->updateAnnotationAppearanceStreams(item.annotation->getSelfReference()); }
            }
        }
    }
    if (!modifier.finalize()) return false;
    manager->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    if (auto* select = window.findChild<QAction*>("actionSelectText"); select && !select->isChecked()) select->trigger();
    if (auto* deselect = window.findChild<QAction*>("actionDeselect")) deselect->trigger();
    return true;
}
}
