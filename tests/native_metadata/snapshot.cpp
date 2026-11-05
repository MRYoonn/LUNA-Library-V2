// SPDX-License-Identifier: MIT
#include "../../src/lunafirstpageevidence.h"
#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) return 2;
    QFile input(QString::fromLocal8Bit(argv[1]));
    if (!input.open(QIODevice::ReadOnly)) return 3;
    QJsonParseError error;
    auto data = QJsonDocument::fromJson(input.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !data.isArray()) return 4;
    QJsonObject results;
    for (const auto& value : data.array()) {
        const auto row = value.toObject();
        QStringList pages;
        for (const auto& page : row["pages"].toArray()) pages.append(page.toString());
        QList<luna::TitleExtraction::Line> lines;
        for (const auto& v : row["lines"].toArray()) {
            const auto l = v.toObject();
            lines.append({l["text"].toString(), l["x"].toDouble(), l["y"].toDouble(), l["fontSize"].toDouble()});
        }
        results[row["id"].toString()] = QJsonObject::fromVariantMap(luna::firstPageEvidence(
            pages, lines, row["height"].toDouble(), row["infoText"].toString(), row["embeddedTitle"].toString()));
    }
    QSaveFile output(QString::fromLocal8Bit(argv[2]));
    if (!output.open(QIODevice::WriteOnly)) return 5;
    return output.write(QJsonDocument(results).toJson()) > 0 && output.commit() ? 0 : 6;
}
