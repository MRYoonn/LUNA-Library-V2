// SPDX-License-Identifier: MIT
#pragma once
// PDF-relative, sync-friendly LUNA management files. The .luna directory is
// hidden on Windows; it is deliberately inside the synced PDF parent folder.
// No network calls, user-document writes or global state.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace luna::SidecarStorage {
inline QString managementRoot(const QString& pdf) {
    return QDir(QFileInfo(pdf).absolutePath()).filePath(QStringLiteral(".luna"));
}
inline QString legacyMetadata(const QString& pdf) {
    const QFileInfo f(pdf);
    return QDir(f.absolutePath()).filePath(f.completeBaseName() + QStringLiteral(".luna-meta.json"));
}
inline QString metadata(const QString& pdf) {
    const QFileInfo f(pdf);
    return QDir(managementRoot(pdf)).filePath(
        QStringLiteral("metadata/") + f.completeBaseName() + QStringLiteral(".luna-meta.json"));
}
inline QString legacyThumbnail(const QString& pdf, const QString& extension) {
    const QFileInfo f(pdf);
    return QDir(f.absolutePath()).filePath(f.completeBaseName() + QStringLiteral(".luna-thumb") + extension);
}
inline QString thumbnail(const QString& pdf, const QString& extension) {
    const QFileInfo f(pdf);
    return QDir(managementRoot(pdf)).filePath(
        QStringLiteral("thumbnails/") + f.completeBaseName() + QStringLiteral(".luna-thumb") + extension);
}
inline bool safeExistingFile(const QString& path) {
    const QFileInfo f(path);
    return f.isFile() && !f.isSymLink();
}
inline bool safeManagedDirectory(const QString& pdf) {
    const QFileInfo root(managementRoot(pdf));
    if (root.isSymLink() || (root.exists() && !root.isDir())) return false;
#ifdef Q_OS_WIN
    if (root.exists()) {
        const QString native = QDir::toNativeSeparators(root.absoluteFilePath());
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
    }
#endif
    return true;
}
inline QString metadataForRead(const QString& pdf) {
    if (safeManagedDirectory(pdf) && safeExistingFile(metadata(pdf))) return metadata(pdf);
    return legacyMetadata(pdf);
}
inline QString thumbnailForRead(const QString& pdf, const QString& extension) {
    if (safeManagedDirectory(pdf) && safeExistingFile(thumbnail(pdf, extension)))
        return thumbnail(pdf, extension);
    return legacyThumbnail(pdf, extension);
}
// Create folders only when the caller is actually writing or migrating.
// Reject links/reparse points and NEVER follow a user-created .luna symlink.
inline bool ensureArea(const QString& pdf, bool forMetadata) {
    if (!safeManagedDirectory(pdf)) return false;
    const QString parent = managementRoot(pdf);
    if (!QFileInfo(parent).isDir() && !QDir().mkpath(parent)) return false;
    if (!safeManagedDirectory(pdf)) return false;
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(QFileInfo(parent).absoluteFilePath());
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        (!(attributes & FILE_ATTRIBUTE_HIDDEN) &&
         !SetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()),
                             attributes | FILE_ATTRIBUTE_HIDDEN))) return false;
#endif
    const QString folder = QDir(parent).filePath(
        forMetadata ? QStringLiteral("metadata") : QStringLiteral("thumbnails"));
    const QFileInfo check(folder);
    if (check.isSymLink() || (check.exists() && !check.isDir())) return false;
    if (!check.isDir() && !QDir().mkpath(folder)) return false;
    return QFileInfo(folder).isDir() && !QFileInfo(folder).isSymLink();
}
inline bool prepareTarget(const QString& pdf, const QString& target) {
    const QString root = QDir(managementRoot(pdf)).absolutePath() + QLatin1Char('/');
    if (!QFileInfo(target).absoluteFilePath().startsWith(root, Qt::CaseInsensitive)) return true;
    return ensureArea(pdf, QFileInfo(target).absolutePath().endsWith(QStringLiteral("/metadata"),
                                                                       Qt::CaseInsensitive));
}
// Both layouts are recognized, so a user never loses annotation/citation data
// when an older LUNA version, Explorer or MYBOX touches the same directory.
inline QList<QPair<QString, QString>> companions(const QString& fromPdf,
                                                  const QString& toPdf, bool includeMetadata=true) {
    QList<QPair<QString, QString>> pairs;
    if (includeMetadata) {
        pairs.append({metadata(fromPdf), metadata(toPdf)});
        pairs.append({legacyMetadata(fromPdf), legacyMetadata(toPdf)});
    }
    for (const QString& ext : {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")}) {
        pairs.append({thumbnail(fromPdf, ext), thumbnail(toPdf, ext)});
        pairs.append({legacyThumbnail(fromPdf, ext), legacyThumbnail(toPdf, ext)});
    }
    return pairs;
}
inline QStringList allCompanions(const QString& pdf) {
    QStringList paths;
    for (const auto& pair : companions(pdf, pdf)) paths.append(pair.first);
    return paths;
}
// Atomic no-overwrite rename on the SAME volume. A collision is preserved
// untouched for manual recovery: never pick a "winner" and delete a sidecar.
inline bool migrateOne(const QString& pdf, const QString& from, const QString& to, bool isMetadata) {
    const QFileInfo old(from);
    if (!old.exists() && !old.isSymLink()) return true;
    if (!old.isFile() || old.isSymLink()) return false;
    if (!ensureArea(pdf, isMetadata)) return false;
    const QFileInfo target(to);
    if (target.exists() || target.isSymLink()) return false;
    return QFile::rename(from, to); // Qt refuses replacement; no copy/delete fallback.
}
inline bool migrateLegacy(const QString& pdf) {
    bool ok = migrateOne(pdf, legacyMetadata(pdf), metadata(pdf), true);
    for (const QString& ext : {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")})
        ok = migrateOne(pdf, legacyThumbnail(pdf, ext), thumbnail(pdf, ext), false) && ok;
    return ok;
}
} // namespace luna::SidecarStorage
