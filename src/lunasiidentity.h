// SPDX-License-Identifier: MIT
#pragma once
// Isolated, opt-in stable identity for local Main/SI PDFs.
// No PDF parsing, network access, QSettings mutation, or association creation.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QString>
#include <QUuid>

namespace luna::SiIdentity {
struct Result {
    QString id;
    QString error;
    bool created = false;
    bool ok() const { return error.isEmpty() && !id.isEmpty(); }
};

inline bool validId(const QString& value) {
    static const QRegularExpression pattern(QStringLiteral(
        R"(^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$)"));
    return pattern.match(value).hasMatch() && !QUuid(value).isNull();
}

inline QString sidecarPath(const QString& canonicalPdf) {
    const QFileInfo pdf(canonicalPdf);
    return pdf.dir().filePath(pdf.completeBaseName() + QStringLiteral(".luna-meta.json"));
}

inline QString guardedPdf(const QString& root, const QString& pdfPath) {
    const QFileInfo folder(root), pdf(pdfPath);
    if (!folder.isDir() || folder.isSymLink() || !pdf.isFile() || pdf.isSymLink() ||
        pdf.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0)
        return {};
    const QString rootPath = QDir::fromNativeSeparators(folder.canonicalFilePath());
    const QString canonical = QDir::fromNativeSeparators(pdf.canonicalFilePath());
    if (rootPath.isEmpty() || canonical.isEmpty() ||
        !canonical.startsWith(rootPath + QLatin1Char('/'), Qt::CaseInsensitive))
        return {};
    return canonical;
}

// Sidecar is a separate local file already paired with PDF moves by Issue #9.
// Existing unknown fields are preserved. Corrupt, oversized or future-schema
// sidecars are NEVER overwritten; no destructive migration is attempted.
inline Result load(const QString& path, QJsonObject* fields = nullptr) {
    const QFileInfo info(path);
    if (info.isSymLink() || (info.exists() && (!info.isFile() || info.size() > 65536)))
        return {{}, QStringLiteral("unsafe_sidecar"), false};
    if (!info.exists())
        return {{}, QStringLiteral("identity_missing"), false};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, QStringLiteral("sidecar_unreadable"), false};
    const QByteArray bytes = file.read(65537);
    if (bytes.size() > 65536)
        return {{}, QStringLiteral("unsafe_sidecar"), false};
    QJsonParseError parse;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject())
        return {{}, QStringLiteral("invalid_sidecar"), false};
    const QJsonObject obj = json.object();
    if (obj.contains(QStringLiteral("schemaVersion")) &&
        (obj.value(QStringLiteral("schemaVersion")).type() != QJsonValue::Double ||
         obj.value(QStringLiteral("schemaVersion")).toInt(-1) != 1))
        return {{}, QStringLiteral("unsupported_sidecar"), false};
    if (fields) *fields = obj;
    if (!obj.contains(QStringLiteral("documentId")))
        return {{}, QStringLiteral("identity_missing"), false};
    const QJsonValue id = obj.value(QStringLiteral("documentId"));
    if (!id.isString() || !validId(id.toString()))
        return {{}, QStringLiteral("invalid_identity"), false};
    return {id.toString(), {}, false};
}

// Read is strictly side-effect free; callers cannot mistake a filename match
// for a persisted identity. The root boundary rejects traversal/symlink escape.
inline Result read(const QString& root, const QString& pdfPath) {
    const QString canonical = guardedPdf(root, pdfPath);
    if (canonical.isEmpty())
        return {{}, QStringLiteral("unsafe_pdf_path"), false};
    return load(sidecarPath(canonical));
}

// Only call after explicit user action to enroll a local PDF for SI linking.
// A lock prevents two LUNA processes from independently assigning UUIDs.
// QSaveFile guarantees an atomic replacement or no modification.
inline Result ensure(const QString& root, const QString& pdfPath) {
    const QString canonical = guardedPdf(root, pdfPath);
    if (canonical.isEmpty())
        return {{}, QStringLiteral("unsafe_pdf_path"), false};
    const QString path = sidecarPath(canonical);
    QLockFile lock(path + QStringLiteral(".luna-si.lock"));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0))
        return {{}, QStringLiteral("identity_busy"), false};
    QJsonObject fields;
    const Result existing = load(path, &fields);
    if (existing.ok()) return existing;
    if (existing.error != QStringLiteral("identity_missing"))
        return existing;
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!validId(uuid))
        return {{}, QStringLiteral("uuid_generation_failed"), false};
    fields.insert(QStringLiteral("documentId"), uuid);
    if (!fields.contains(QStringLiteral("schemaVersion")))
        fields.insert(QStringLiteral("schemaVersion"), 1);
    const QByteArray bytes = QJsonDocument(fields).toJson(QJsonDocument::Indented);
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(bytes) != bytes.size() || !output.commit())
        return {{}, QStringLiteral("sidecar_write_failed"), false};
    return {uuid, {}, true};
}
} // namespace luna::SiIdentity
