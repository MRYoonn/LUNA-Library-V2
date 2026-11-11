// SPDX-License-Identifier: MIT
// Explicit local diagnostic only. Never enable for a user's PDF in public CI.
#pragma once
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>

namespace luna::RenderTrace {
inline bool enabled() {
    static const bool active = !qEnvironmentVariableIsEmpty("LUNA_PDF_RENDER_TRACE");
    return active;
}
inline QJsonArray rect(const QRectF& r) { return {r.x(), r.y(), r.width(), r.height()}; }
inline QJsonArray matrix(const QTransform& m) {
    return {m.m11(), m.m12(), m.m13(), m.m21(), m.m22(), m.m23(), m.m31(), m.m32(), m.m33()};
}
inline QJsonObject path(const QPainterPath& p) {
    QJsonArray elements;
    for (int i = 0; i < qMin(p.elementCount(), 64); ++i) {
        auto e = p.elementAt(i);
        elements.append(QJsonArray{int(e.type), e.x, e.y});
    }
    return {{"bounds", rect(p.boundingRect())}, {"controlBounds", rect(p.controlPointRect())},
        {"empty", p.isEmpty()}, {"fillRule", int(p.fillRule())},
        {"count", p.elementCount()}, {"elements", elements}};
}
inline QJsonObject pen(const QPen& p) {
    const QColor c = p.color();
    return {{"rgba", QJsonArray{c.redF(), c.greenF(), c.blueF(), c.alphaF()}},
        {"width", p.widthF()}, {"cosmetic", p.isCosmetic()}, {"style", int(p.style())},
        {"cap", int(p.capStyle())}, {"join", int(p.joinStyle())}};
}
inline QJsonObject painter(QPainter* p) {
    return {{"world", matrix(p->worldTransform())}, {"device", matrix(p->deviceTransform())},
        {"clip", path(p->clipPath())}, {"hasClip", p->hasClipping()},
        {"opacity", p->opacity()}, {"pen", pen(p->pen())},
        {"deviceType", p->device() ? p->device()->devType() : -1}};
}
inline void event(const char* stage, QJsonObject data) {
    if (!enabled()) return;
    static QMutex mutex;
    static int sequence = 0;
    QMutexLocker lock(&mutex);
    if (sequence >= 6000) return;
    data.insert(QStringLiteral("stage"), QString::fromLatin1(stage));
    data.insert(QStringLiteral("sequence"), sequence++);
    QFile file(qEnvironmentVariable("LUNA_PDF_RENDER_TRACE"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write(QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n');
}
inline void operatorPath(const char* stage, const QPainterPath& p, const QTransform& m) {
    if (enabled()) event(stage, {{"path", path(p)}, {"ctm", matrix(m)}});
}
}
