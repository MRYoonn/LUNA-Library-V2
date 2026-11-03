// SPDX-License-Identifier: MIT
#pragma once
// Read-only QtCore SI card projection. No network, PDF, QSettings or file writes.
// Inputs must come from the separately validated opt-in association state.
// A public metadata hit is NEVER evidence that a landing URL was opened.
#include "lunametadataengine.h"  // PR #16: shared DOI normalizer
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>

namespace luna::SiRows {
struct Local {
    QString documentId;
    QString parentId;
    QString relativePath;
    QString source; // must be user_confirmed
};
struct Public {
    qint64 articleId = 0;
    qint64 fileId = 0; // zero means absent
    QString resourceDoi;
    QString title;
    QString filename;
    QString landingUrl;
    QString status;
};
struct Lookup {
    QString doi;
    QString status;
    QList<Public> candidates;
};
struct Projection {
    bool valid = true;
    QString lookupStatus = QStringLiteral("not_checked");
    QString fallbackUrl;
    QList<QJsonObject> localSi;
    QList<QJsonObject> publicSi;
};

inline bool uuidV4(const QString& id) {
    static const QRegularExpression re(
        QStringLiteral(R"(^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$)"));
    return re.match(id).hasMatch();
}
inline bool printable(const QString& value) {
    for (const QChar c : value)
        if (!c.isPrint() || c.category() == QChar::Other_Format)
            return false;
    return true;
}
inline bool safeLabel(const QString& raw, bool filename) {
    if (raw.isEmpty() || raw.size() > (filename ? 255 : 240) ||
        raw != raw.trimmed() || !printable(raw))
        return false;
    if (!filename) return true;
    static const QRegularExpression reserved(
        QStringLiteral(R"(^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])$)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString& name : {raw, raw.normalized(QString::NormalizationForm_KC)}) {
        if (name.isEmpty() || name.size() > 255 || name != name.trimmed() ||
            name.endsWith(QLatin1Char('.')) || !printable(name))
            return false;
        for (const QChar c : name)
            if (QStringLiteral("<>:\"/\\|?*").contains(c))
                return false;
        if (reserved.match(name.section(QLatin1Char('.'), 0, 0).trimmed()).hasMatch())
            return false;
    }
    return true;
}
inline bool safeRelativePdf(const QString& path) {
    if (path.isEmpty() || path.size() > 1024 ||
        !path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        return false;
    const QString nfkc = path.normalized(QString::NormalizationForm_KC);
    if (nfkc.count(QLatin1Char('/')) != path.count(QLatin1Char('/')))
        return false;
    for (const QString& p : {path, nfkc}) {
        if (p.startsWith(QLatin1Char('/')) || p.contains(QLatin1Char('\\')))
            return false;
        for (const QString& segment : p.split(QLatin1Char('/'))) {
            if (segment == QStringLiteral(".") || segment == QStringLiteral("..") ||
                !safeLabel(segment, true))
                return false;
        }
    }
    return true;
}
inline bool officialLanding(const QString& url, qint64 articleId) {
    if (articleId <= 0 || articleId >= 1000000000000000LL || url.size() > 2048)
        return false;
    static const QRegularExpression re(
        QStringLiteral(R"(^https://(?:acs\.)?figshare\.com/articles/[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+)*/[1-9][0-9]*$)"));
    if (!re.match(url).hasMatch()) return false;
    return url.mid(url.lastIndexOf(QLatin1Char('/')) + 1) == QString::number(articleId);
}
inline Projection project(const QString& mainId, const QString& parentDoi,
                          const QList<Local>& locals, const Lookup* lookup = nullptr) {
    Projection out;
    const QString canonical = Metadata::normalizeDoi(parentDoi);
    if (!uuidV4(mainId) || canonical.isEmpty() || canonical != parentDoi ||
        locals.size() > 800) {
        out.valid = false;
        out.lookupStatus = QStringLiteral("needs_review");
        return out;
    }
    out.fallbackUrl = QStringLiteral("https://doi.org/") + canonical;
    QSet<QString> localIds, localPaths;
    for (const Local& row : locals) {
        const QString key = row.relativePath.normalized(QString::NormalizationForm_KC).toCaseFolded();
        if (!uuidV4(row.documentId) || row.documentId == mainId ||
            row.parentId != mainId || row.source != QStringLiteral("user_confirmed") ||
            !safeRelativePdf(row.relativePath) || localIds.contains(row.documentId) ||
            localPaths.contains(key)) {
            out.valid = false;
            out.lookupStatus = QStringLiteral("needs_review");
            out.localSi.clear();
            return out;
        }
        localIds.insert(row.documentId);
        localPaths.insert(key);
        out.localSi.append(QJsonObject{
            {QStringLiteral("document_id"), row.documentId},
            {QStringLiteral("filename"), row.relativePath.section(QLatin1Char('/'), -1)},
            {QStringLiteral("status"), QStringLiteral("user_confirmed")}});
    }
    if (!lookup) return out;
    if (lookup->doi != canonical || lookup->candidates.size() > 800) {
        out.lookupStatus = QStringLiteral("needs_review");
        return out;
    }
    out.lookupStatus = lookup->status;
    static const QSet<QString> visible{
        QStringLiteral("candidates"), QStringLiteral("partial"),
        QStringLiteral("verified"), QStringLiteral("partial_verified"),
        QStringLiteral("needs_review")};
    if (!visible.contains(lookup->status)) {
        if (lookup->status != QStringLiteral("offline") &&
            lookup->status != QStringLiteral("rate_limited") &&
            lookup->status != QStringLiteral("not_found") &&
            lookup->status != QStringLiteral("provider_unavailable") &&
            lookup->status != QStringLiteral("unsupported_publisher"))
            out.lookupStatus = QStringLiteral("needs_review");
        return out;
    }
    QList<Public> accepted;
    QHash<QString, int> nameCounts;
    QHash<qint64, int> fileCounts;
    for (const Public& c : lookup->candidates) {
        if (c.resourceDoi != canonical || c.articleId <= 0 ||
            c.articleId >= 1000000000000000LL || c.fileId < 0 ||
            c.fileId >= 1000000000000000LL || !safeLabel(c.filename, true) ||
            !safeLabel(c.title, false) || !officialLanding(c.landingUrl, c.articleId))
            continue;
        accepted.append(c);
        const QString key = QString::number(c.articleId) + QLatin1Char('\n') +
            c.filename.normalized(QString::NormalizationForm_KC).toCaseFolded();
        ++nameCounts[key];
        if (c.fileId) ++fileCounts[c.fileId];
    }
    bool complete = accepted.size() == lookup->candidates.size();
    for (auto it = nameCounts.cbegin(); it != nameCounts.cend(); ++it)
        if (it.value() > 1) complete = false;
    for (auto it = fileCounts.cbegin(); it != fileCounts.cend(); ++it)
        if (it.value() > 1) complete = false;
    if (!complete) out.lookupStatus = QStringLiteral("needs_review");
    for (const Public& c : accepted) {
        const QString key = QString::number(c.articleId) + QLatin1Char('\n') +
            c.filename.normalized(QString::NormalizationForm_KC).toCaseFolded();
        if (nameCounts.value(key) != 1 || (c.fileId && fileCounts.value(c.fileId) != 1))
            continue;
        QJsonObject row{
            {QStringLiteral("article_id"), static_cast<double>(c.articleId)},
            {QStringLiteral("filename"), c.filename},
            {QStringLiteral("title"), c.title},
            {QStringLiteral("landing_url"), c.landingUrl},
            {QStringLiteral("status"), QStringLiteral("unconfirmed")}};
        if (c.fileId)
            row.insert(QStringLiteral("file_id"), static_cast<double>(c.fileId));
        // Never infer a local link from DOI, filename, or title.
        row.insert(QStringLiteral("local_document_id"), QJsonValue(QJsonValue::Null));
        out.publicSi.append(row);
    }
    // Even a restored "landing_verified" string cannot promote these rows.
    // Only the separately tested HTTP verifier may supply live proof to a UI.
    return out;
}
} // namespace luna::SiRows
