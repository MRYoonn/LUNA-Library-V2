// SPDX-License-Identifier: MIT
#pragma once
// Optional local-only PDF byte binding for an ALREADY-LOADED bibliography record.
// No network, QSettings access, PDF text parsing, or report writes.
// The caller must prove this path is the PDF used to obtain visibleRecord.
#include "snapshot_adapter.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace luna::Accuracy {
inline QJsonObject captureCaseFromLocalPdf(const QString& opaqueId,
                                           const QVariantMap& visibleRecord,
                                           const QString& localPdfPath) {
    // Require an explicit absolute path; never infer a PDF from an opaque ID.
    // Reject leaf symlinks and non-files; trusted local callers must also
    // control parent directories and the file's lifetime during capture.
    const QFileInfo info(localPdfPath);
    if (!validOpaqueId(opaqueId) || !info.isAbsolute() || !info.isFile()
        || info.isSymLink() || !info.isReadable())
        return {};

    QFile pdf(localPdfPath);
    if (!pdf.open(QIODevice::ReadOnly)) return {};
    if (!pdf.peek(1024).contains("%PDF-")) return {};

    QCryptographicHash sha(QCryptographicHash::Sha256);
    while (!pdf.atEnd()) {
        const QByteArray chunk = pdf.read(1024 * 1024);
        if (chunk.isEmpty()) return {}; // Includes unexpected read failure.
        sha.addData(chunk);
    }
    if (pdf.error() != QFileDevice::NoError) return {};
    // Only the digest is exported, never path, filename, PDF bytes, or
    // non-whitelisted record keys. Hashing is not proof of publisher identity.
    return captureCase(opaqueId, visibleRecord,
                       QString::fromLatin1(sha.result().toHex()));
}
} // namespace luna::Accuracy
