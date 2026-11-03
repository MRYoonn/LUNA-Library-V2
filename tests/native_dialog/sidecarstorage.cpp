// SPDX-License-Identifier: MIT
// Native synthetic regression for Windows hidden .luna management directories.
#include "lunasidecarpaths.h"
#include "lunafilemove.h"
#include "lunatrashundo.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDebug>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

int main(int argc, char** argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    auto fail=[](const char* reason) { qCritical() << reason; return 3; };
    auto write=[](const QString& path, const QByteArray& bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes)==bytes.size();
    };
    auto read=[](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const QString pdf=QDir(root.path()).filePath(QStringLiteral("한글 with space.pdf"));
    const QString legacyMeta=luna::SidecarStorage::legacyMetadata(pdf);
    const QString legacyPng=luna::SidecarStorage::legacyThumbnail(pdf,QStringLiteral(".png"));
    const QString meta=luna::SidecarStorage::metadata(pdf);
    const QString png=luna::SidecarStorage::thumbnail(pdf,QStringLiteral(".png"));
    if (!write(pdf,"synthetic user-owned PDF") || !write(legacyMeta,"original citation & notes") ||
        !write(legacyPng,"original cropped thumbnail"))
        return fail("Cannot prepare existing visible sidecars");
    if (luna::SidecarStorage::metadataForRead(pdf)!=legacyMeta ||
        luna::SidecarStorage::thumbnailForRead(pdf,QStringLiteral(".png"))!=legacyPng)
        return fail("Existing LUNA metadata/thumbnail not readable before migration");
    if (!luna::SidecarStorage::migrateLegacy(pdf) ||
        QFileInfo::exists(legacyMeta) || QFileInfo::exists(legacyPng) ||
        read(meta)!="original citation & notes" || read(png)!="original cropped thumbnail" ||
        !luna::SidecarStorage::migrateLegacy(pdf))
        return fail("Lossless idempotent migration into .luna failed");
#ifdef Q_OS_WIN
    const QString dir=QDir::toNativeSeparators(luna::SidecarStorage::managementRoot(pdf));
    const DWORD attributes=GetFileAttributesW(reinterpret_cast<LPCWSTR>(dir.utf16()));
    if (attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_HIDDEN))
        return fail("Windows .luna management directory is visible");
#endif
    // Leave simultaneous old/new names untouched instead of choosing a copy
    // and deleting the user's preferred data.
    if (!write(legacyMeta,"legacy conflicting edit") ||
        luna::SidecarStorage::migrateLegacy(pdf) ||
        read(meta)!="original citation & notes" ||
        read(legacyMeta)!="legacy conflicting edit" ||
        luna::SidecarStorage::metadataForRead(pdf)!=meta)
        return fail("Legacy collision overwrote user data");
    QFile::remove(legacyMeta);
    // Move physical PDF and hidden sidecars as one no-replace transaction.
    const QString dest=QDir(root.path()).filePath(QStringLiteral("second folder"));
    if (!QDir().mkpath(dest)) return fail("Cannot prepare destination");
    const QString movedPdf=QDir(dest).filePath(QFileInfo(pdf).fileName());
    const QString movedMeta=luna::SidecarStorage::metadata(movedPdf);
    const QString movedPng=luna::SidecarStorage::thumbnail(movedPdf,QStringLiteral(".png"));
    const auto success=luna::movePdfWithThumbnails(pdf,dest);
    if (!success.success || success.documentPath!=movedPdf ||
        QFileInfo::exists(pdf) || QFileInfo::exists(meta) || QFileInfo::exists(png) ||
        read(movedMeta)!="original citation & notes" ||
        read(movedPng)!="original cropped thumbnail")
        return fail("Native PDF+hidden-sidecar move lost bytes");
    // A copied document must own a new citation/UUID, not inherit source meta.
    const QString copyFolder=QDir(root.path()).filePath(QStringLiteral("copy folder"));
    if (!QDir().mkpath(copyFolder)) return fail("Cannot prepare copied destination");
    const auto copy=luna::copyPdfWithThumbnails(movedPdf,copyFolder);
    if (!copy.success || read(luna::SidecarStorage::thumbnail(copy.documentPath,QStringLiteral(".png"))) !=
                           "original cropped thumbnail" ||
        QFileInfo::exists(luna::SidecarStorage::metadata(copy.documentPath)) ||
        QFileInfo::exists(luna::SidecarStorage::legacyMetadata(copy.documentPath)))
        return fail("Physical PDF copy corrupted thumbnail or reused document identity");
    const QString collision=QDir(root.path()).filePath(QStringLiteral("collision folder"));
    if(!QDir().mkpath(collision)) return fail("Cannot prepare collision destination");
    const QString collidedPdf=QDir(collision).filePath(QFileInfo(movedPdf).fileName());
    if (!luna::SidecarStorage::ensureArea(collidedPdf,true) ||
        !write(luna::SidecarStorage::metadata(collidedPdf),"unrelated user metadata") ||
        luna::movePdfWithThumbnails(movedPdf,collision).success ||
        read(movedMeta)!="original citation & notes" ||
        read(luna::SidecarStorage::metadata(collidedPdf))!="unrelated user metadata")
        return fail("Collision did not block moving unrelated metadata");
    // Ctrl+Z and multi-delete include files within the hidden folder.
    luna::RecentTrashUndo undo;
    const auto recycler=[](const QString& path) { return QFile::remove(path); };
    QString error;
    if (!undo.trash(movedPdf,&error,recycler) || QFileInfo::exists(movedMeta) ||
        QFileInfo::exists(movedPng) || QFileInfo::exists(movedPdf) ||
        !undo.undo(&error) || read(movedMeta)!="original citation & notes" ||
        read(movedPng)!="original cropped thumbnail" ||
        read(movedPdf)!="synthetic user-owned PDF")
        return fail("Ctrl+Z failed for hidden metadata/thumbnail");
    qInfo() << "[PASS] Hidden .luna metadata/thumbnails, legacy migration, no overwrite, copy, move, undo";
    return 0;
}
