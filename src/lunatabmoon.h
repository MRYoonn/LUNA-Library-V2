// SPDX-License-Identifier: MIT
#pragma once
// A thin, intentionally unfilled LUNA crescent for document tabs.
// The EXE's existing blue application icon is NOT changed.
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSize>

namespace luna {
inline QColor tabMoonColor(bool active) {
    return active ? QColor(QStringLiteral("#3473AE")) : QColor(QStringLiteral("#9AA5B3"));
}
inline QIcon tabMoonIcon(bool active) {
    QPixmap pixmap(72,72);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing,true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,true);
    painter.scale(1.5,1.5);
    // A single continuous outline. Transparent inside and outside to match
    // the reference's empty crescent; no solid document/PDF symbol in tabs.
    QPainterPath moon;
    moon.moveTo(28.0,5.8);
    moon.cubicTo(15.4,6.0,7.8,16.2,8.7,28.9);
    moon.cubicTo(9.6,41.7,20.5,49.1,32.9,47.8);
    moon.cubicTo(38.7,47.3,43.7,44.7,46.4,40.2);
    moon.cubicTo(34.7,43.8,23.8,35.4,23.2,24.3);
    moon.cubicTo(22.8,16.4,25.0,10.1,28.0,5.8);
    painter.setBrush(Qt::NoBrush);
    QPen pen(tabMoonColor(active),active?3.25:2.9,Qt::SolidLine,
             Qt::RoundCap,Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawPath(moon);
    painter.end();
    return QIcon(pixmap);
}
} // namespace luna
