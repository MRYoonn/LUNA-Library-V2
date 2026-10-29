// SPDX-License-Identifier: MIT
#include "lunafilemove.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QString root = temporary.path();
    const QString target = root + QStringLiteral("/한글 대상 ") + QString(120, QLatin1Char('x')) +
        QLatin1Char('/') + QString(120, QLatin1Char('y'));
    if (!QDir().mkpath(target)) return 2;
    const QString pdf = root + QStringLiteral("/논문 공백.pdf");
    const QString png = root + QStringLiteral("/논문 공백.luna-thumb.png");
    const QString movedPdf = QDir(target).filePath(QFileInfo(pdf).fileName());
    const QString movedPng = QDir(target).filePath(QFileInfo(png).fileName());
    auto write = [](const QString& path, const QByteArray& bytes) {
        QFile file(path); return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
    };
    auto read = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
    auto fail = [](const char* message) { qCritical() << message; return 3; };
    const QByteArray document("synthetic PDF move fixture"), thumbnail("synthetic sidecar move fixture");
    if (!write(pdf, document) || !write(png, thumbnail)) return fail("Cannot create fixtures");
    if (!write(movedPdf, "existing")) return fail("Cannot create collision");
    if (luna::movePdfWithThumbnails(pdf, target).success || read(pdf) != document || read(png) != thumbnail || read(movedPdf) != "existing")
        return fail("PDF collision modified data");
    QFile::remove(movedPdf);
    if (!write(movedPng, "existing image")) return fail("Cannot create sidecar collision");
    if (luna::movePdfWithThumbnails(pdf, target).success || read(pdf) != document || read(movedPng) != "existing image")
        return fail("Sidecar collision modified data");
    QFile::remove(movedPng);
    auto rejectPdf = [&](const QString& from, const QString& to) {
        return from == pdf ? false : luna::renameWithoutReplacement(from, to);
    };
    if (luna::movePdfWithThumbnails(pdf, target, rejectPdf).success || read(pdf) != document || read(png) != thumbnail || QFileInfo::exists(movedPng))
        return fail("Partial failure did not restore original pair");
    auto rejectRollback = [&](const QString& from, const QString& to) {
        return from == pdf || from == movedPng ? false : luna::renameWithoutReplacement(from, to);
    };
    const auto partial = luna::movePdfWithThumbnails(pdf, target, rejectRollback);
    if (partial.success || read(pdf) != document || read(movedPng) != thumbnail || !partial.error.contains(movedPng))
        return fail("Rollback failure lost data or omitted surviving location");
    if (!luna::renameWithoutReplacement(movedPng, png)) return fail("Cannot restore rollback fixture");
    if (luna::movePdfWithThumbnails(pdf, target + QStringLiteral("/missing")).success || read(pdf) != document)
        return fail("Invalid destination changed source");
    const auto success = luna::movePdfWithThumbnails(pdf, target);
    if (!success.success || success.documentPath != movedPdf || QFileInfo::exists(pdf) || QFileInfo::exists(png) || read(movedPdf) != document || read(movedPng) != thumbnail)
        return fail("Paired move changed bytes or paths");
    if (!luna::movePdfWithThumbnails(movedPdf, target).success) return fail("Same-directory move failed");
    if (luna::movePdfWithThumbnails(pdf, target).success) return fail("Missing source accepted");
    // A destination orphan thumbnail must never become a different PDF's preview.
    if (!write(pdf, document)) return fail("Cannot recreate PDF-only fixture");
    QFile::remove(movedPdf);
    if (luna::movePdfWithThumbnails(pdf, target).success || read(pdf) != document || read(movedPng) != thumbnail)
        return fail("PDF-only move adopted an unrelated destination thumbnail");
    const QString renamePdf = root + "/rename.pdf", renamePng = root + "/rename.luna-thumb.png";
    if (!write(renamePdf, document) || !write(renamePng, thumbnail)) return fail("Rename fixture failed");
    for (const QString& bad : QStringList{"CON", "AUX.pdf", "COM1", "bad/name", "bad:name", "a.", "a ", ""})
        if (!luna::pdfRenameFileName(bad).isEmpty()) return fail("Invalid Windows name accepted");
    const QString renamed = root + QStringLiteral("/새 이름.pdf"), renamedPng = root + QStringLiteral("/새 이름.luna-thumb.png");
    if (!write(renamed, "occupied")) return fail("Rename collision fixture failed");
    if (luna::movePdfWithThumbnails(renamePdf, root, luna::renameWithoutReplacement, "새 이름.pdf").success || read(renamePdf) != document || read(renamePng) != thumbnail || read(renamed) != "occupied") return fail("Rename collision lost data");
    QFile::remove(renamed);
    auto failRenamePdf = [&](const QString& from, const QString& to) { return from != renamePdf && luna::renameWithoutReplacement(from, to); };
    if (luna::movePdfWithThumbnails(renamePdf, root, failRenamePdf, "새 이름").success || read(renamePdf) != document || read(renamePng) != thumbnail || QFileInfo::exists(renamedPng)) return fail("Rename rollback failed");
    if (!luna::movePdfWithThumbnails(renamePdf, root, luna::renameWithoutReplacement, "새 이름").success || read(renamed) != document || read(renamedPng) != thumbnail || QFileInfo::exists(renamePdf) || QFileInfo::exists(renamePng)) return fail("Paired rename lost data");
    qInfo() << "[PASS] Unicode/long paths, paired move/rename, invalid names, collisions and rollback preserve bytes";
    return 0;
}
