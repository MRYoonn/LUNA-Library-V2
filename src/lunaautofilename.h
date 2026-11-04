// SPDX-License-Identifier: MIT
#pragma once
// Conservative, Windows-safe PDF basename derived only from a verified or
// manually corrected title. Never treat unverified PDF-info titles as authority.
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QVariantMap>

namespace luna::AutoFileName {
inline QString fromVerifiedTitle(const QVariantMap& record, const QString& folder) {
    if (record.value(QStringLiteral("documentKind")).toString() ==
            QStringLiteral("supporting_information") ||
        record.value(QStringLiteral("manualSI")).toBool())
        return {};
    static const QSet<QString> sources {
        QStringLiteral("first_page_layout"),
        QStringLiteral("publisher_cover_verified"),
        QStringLiteral("first_page_verified"),
        QStringLiteral("Crossref"), QStringLiteral("OpenAlex"),
        QStringLiteral("Crossref + OpenAlex"), QStringLiteral("manual")
    };
    if (!sources.contains(record.value(QStringLiteral("titleSource")).toString()))
        return {};
    QString name = record.value(QStringLiteral("title")).toString()
        .normalized(QString::NormalizationForm_C).simplified();
    if (name.size() < 8) return {};
    // Replace, rather than drop, separators so two words do not concatenate.
    name.replace(QRegularExpression(QStringLiteral(
        R"REGEX([<>:"/\\|?*\x{0000}-\x{001F}])REGEX")), QStringLiteral(" "));
    name.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    name = name.trimmed();
    const int available = qMin(150, 235 - QFileInfo(folder).absoluteFilePath().size());
    if (available < 32) return {}; // Avoid unportable Windows paths.
    name = name.left(available).trimmed();
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        name.chop(1);
    if (name.size() < 8) return {};
    static const QRegularExpression reserved(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(name).hasMatch()) name.prepend(QStringLiteral("Paper - "));
    return name + QStringLiteral(".pdf");
}
} // namespace luna::AutoFileName
