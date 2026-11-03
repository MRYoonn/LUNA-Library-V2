// SPDX-License-Identifier: MIT
#pragma once
// Offline read-only projection of an ALREADY-LOADED Bibliography::record().
// No QSettings, filesystem, network, PDF, or mutation API is used here.
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

namespace luna::Accuracy {
inline bool validOpaqueId(const QString& id) {
    static const QRegularExpression re(QStringLiteral(R"(^[A-Za-z0-9_.:-]{1,96}$)"));
    return re.match(id).hasMatch();
}

inline QJsonObject captureCase(const QString& opaqueId, const QVariantMap& visibleRecord,
                               const QString& localPdfSha256 = QString()) {
    if (!validOpaqueId(opaqueId)) return {};
    // Optional hash supplied by the LOCAL caller at capture time. This pure
    // adapter never reads a PDF; a blank digest is allowed for synthetic and
    // regression-only snapshots, but cannot pass the scorer's --pdf-dir gate.
    static const QRegularExpression digestPattern(QStringLiteral(R"(^[0-9a-fA-F]{64}$)"));
    if (!localPdfSha256.isEmpty() &&
        (!digestPattern.match(localPdfSha256).hasMatch() ||
         localPdfSha256 == QString(64, QLatin1Char('0'))))
        return {};
    QJsonObject observed;
    // Whitelist ONLY bibliography values; never serialize identities, file
    // paths, annotations, legacy backups, PDF contents or arbitrary map keys.
    for (const char* key : {"doi", "title", "journal", "journal_abbreviation",
                            "year", "volume", "issue", "page", "article_number"}) {
        const QString field = QString::fromLatin1(key);
        const QString value = visibleRecord.value(field).toString().trimmed();
        // Explicit null means this whitelisted field was captured and absent.
        // Omitting it would let the scorer falsely count a known-negative
        // gold label as a true negative without any native observation.
        observed.insert(field, value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value));
    }
    const QString kind = visibleRecord.value(QStringLiteral("documentKind")).toString();
    // Preserve invalid nonempty classifications for the scorer to flag;
    // never silently convert an unexpected native value into a missing key.
    observed.insert(QStringLiteral("document_kind"),
                    kind.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(kind));

    // Match the last LISTED author, not a user-selected senior-author override.
    // Only use authors when Bibliography::record() left that field visible;
    // a suppressed legacy author must never be resurrected by this adapter.
    const QString authors = visibleRecord.value(QStringLiteral("authors")).toString().trimmed();
    const QString source = visibleRecord.value(QStringLiteral("authorsSource")).toString();
    QString terminal;
    if (!authors.isEmpty()) {
        const QStringList structured = visibleRecord.value(QStringLiteral("authors_structured")).toStringList();
        if (source != QStringLiteral("manual") && !structured.isEmpty())
            terminal = structured.last().trimmed();
        else
            terminal = authors.section(QLatin1Char(','), -1).trimmed();
    }
    observed.insert(QStringLiteral("terminal_author"),
                    terminal.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(terminal));

    QJsonObject audit;
    for (const char* key : {"metadata_status", "authorsSource",
                            "titleSource", "journalSource", "doiSource", "authorEvidence"}) {
        const QString field = QString::fromLatin1(key);
        const QString value = visibleRecord.value(field).toString().trimmed();
        if (!value.isEmpty()) audit.insert(field, value);
    }
    // Whitelist nested provider provenance, never dump arbitrary field_sources.
    const QVariantMap sources = visibleRecord.value(QStringLiteral("field_sources")).toMap();
    QJsonObject fieldSources;
    for (const char* key : {"doi", "title", "authors", "journal", "journal_abbreviation",
                            "year", "volume", "issue", "page", "article_number"}) {
        const QString field = QString::fromLatin1(key);
        const QString provider = sources.value(field).toString();
        if (provider == QStringLiteral("manual") || provider == QStringLiteral("Crossref") ||
            provider == QStringLiteral("OpenAlex") ||
            provider == QStringLiteral("Crossref + OpenAlex") ||
            provider == QStringLiteral("first_page_verified") ||
            provider == QStringLiteral("pdf_metadata"))
            fieldSources.insert(field, provider);
    }
    if (!fieldSources.isEmpty()) audit.insert(QStringLiteral("field_sources"), fieldSources);
    audit.insert(QStringLiteral("manual_citation_active"),
                 !visibleRecord.value(QStringLiteral("citation")).toString().trimmed().isEmpty());
    // labCitation() also accepts a selected senior_author_index. That
    // changes the displayed author even with no text override, so it must
    // not be counted as automatic terminal-author extraction accuracy.
    const QStringList structuredAuthors =
        visibleRecord.value(QStringLiteral("authors_structured")).toStringList();
    const int seniorIndex =
        visibleRecord.value(QStringLiteral("senior_author_index"), -1).toInt();
    const bool selectedSeniorAuthor =
        seniorIndex >= 0 && seniorIndex < structuredAuthors.size();
    audit.insert(QStringLiteral("senior_author_override_active"),
                 !visibleRecord.value(QStringLiteral("senior_author_override")).toString().trimmed().isEmpty()
                     || selectedSeniorAuthor);
    // A citation override may make the CARD differ from record fields. The
    // scorer must label these as record snapshots, not screenshot assertions.
    QJsonObject captured{{QStringLiteral("id"), opaqueId},
                         {QStringLiteral("observed"), observed},
                         {QStringLiteral("audit"), audit}};
    if (!localPdfSha256.isEmpty())
        captured.insert(QStringLiteral("source_sha256"), localPdfSha256.toLower());
    return captured;
}

inline QJsonObject predictionDocument(const QString& datasetId,
                                      const QString& exactCommit,
                                      const QJsonArray& cases) {
    if (!validOpaqueId(datasetId) || cases.isEmpty()) return {};
    static const QRegularExpression sha(QStringLiteral(R"(^[0-9a-fA-F]{40}$)"));
    if (!sha.match(exactCommit).hasMatch()) return {};
    return {{QStringLiteral("schema_version"), 1},
            {QStringLiteral("kind"), QStringLiteral("predictions")},
            {QStringLiteral("dataset_id"), datasetId},
            {QStringLiteral("engine_commit"), exactCommit.toLower()},
            {QStringLiteral("capture_kind"), QStringLiteral("qt_record_snapshot_read_only")},
            {QStringLiteral("cases"), cases}};
}
} // namespace luna::Accuracy
