// SPDX-License-Identifier: MIT
#pragma once
#ifndef LUNA_PERFORMANCE_H
#define LUNA_PERFORMANCE_H
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
namespace luna {
inline qint64 performanceNow() { QElapsedTimer t; t.start(); return t.msecsSinceReference(); }
inline void performanceEvent(const char* stage, qint64 start, qint64 end = performanceNow()) {
    if (!qEnvironmentVariableIsSet("LUNA_PERF")) return;
    qInfo().noquote() << "LUNA_PERF" << QJsonDocument(QJsonObject{{"stage", QString::fromLatin1(stage)}, {"startMs", start}, {"endMs", end}, {"durationMs", end-start}}).toJson(QJsonDocument::Compact);
}
class PerformanceSpan {
public:
    explicit PerformanceSpan(const char* name) : name_(name), start_(performanceNow()) {}
    ~PerformanceSpan() { performanceEvent(name_, start_); }
private: const char* name_; qint64 start_;
};
}
#endif // LUNA_PERFORMANCE_H
