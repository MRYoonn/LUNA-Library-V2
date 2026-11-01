// SPDX-License-Identifier: MIT
#include "lunafilemove.h"
#include "lunatrashundo.h"
#include "lunalocalsync.h"
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
    // Exercise native Explorer renames with the real OS file identity.
    const QString renamedDir=QDir(root).filePath(QStringLiteral("rename-fixtures"));
    if (!QDir().mkpath(renamedDir)) return fail("Cannot create rename fixture");
    const QString oldName=QDir(renamedDir).filePath(QStringLiteral("original.pdf"));
    const QString newName=QDir(renamedDir).filePath(QStringLiteral("renamed.pdf"));
    const QString oldThumb=QDir(renamedDir).filePath(QStringLiteral("original.luna-thumb.png"));
    const QString newThumb=QDir(renamedDir).filePath(QStringLiteral("renamed.luna-thumb.png"));
    if (!write(oldName,document) || !write(oldThumb,thumbnail))
        return fail("Cannot create rename inputs");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);
    luna::Bibliography bibliography;
    QVariantMap fields; fields.insert("title",QStringLiteral("Synthetic metadata remains attached"));
    if(!bibliography.save(luna::DocumentClaim::identity(oldName),fields))
        return fail("Cannot save synthetic bibliography");
    {
        auto preferences=luna::lunaLocalStore();
        preferences.setValue(QStringLiteral("library/memos/")+luna::lunaPathKey(oldName),
                             QStringLiteral("synced memo"));
        preferences.setValue(QStringLiteral("library/favorites"),QStringList{oldName});
    }
    luna::LocalFolderSync bridge;
    bridge.localSnapshot=[&](const QString& item){
        return luna::capturePortableRecord(item,bibliography);
    };
    bridge.configure(renamedDir,true);
    if(!bridge.publish(oldName) || !QFileInfo::exists(luna::portableSidecar(oldName)))
        return fail("Portable MYBOX metadata was not written");
    const QString id=luna::fileObjectId(oldName);
    if(!luna::renameWithoutReplacement(oldName,newName))
        return fail("Cannot simulate native Explorer rename");
#ifdef Q_OS_WIN
    if(id.isEmpty() || luna::fileObjectId(newName)!=id)
        return fail("Windows file ID changed after Explorer rename");
#endif
    QString renameError;
    if(!luna::reconcileExplorerRename(oldName,newName,bibliography,&renameError) ||
       QFileInfo::exists(oldThumb) || read(newThumb)!=thumbnail ||
       !QFileInfo::exists(luna::portableSidecar(newName)) ||
       bibliography.record(luna::DocumentClaim::identity(newName)).value("title").toString() !=
           QStringLiteral("Synthetic metadata remains attached"))
        return fail("Explorer rename did not migrate PDF metadata and thumbnails");
    {
        auto preferences=luna::lunaLocalStore();
        if(preferences.value(QStringLiteral("library/memos/")+luna::lunaPathKey(newName)).toString()
             != QStringLiteral("synced memo") ||
           !preferences.value(QStringLiteral("library/favorites")).toStringList().contains(newName))
            return fail("Explorer rename dropped memo or favorite");
    }
    // Deterministic fake recycler validates Ctrl+Z without requiring an
    // Explorer Recycle Bin on CI. Production uses QFile::moveToTrash.
    luna::RecentTrashUndo undo;
    const auto recycle=[](const QString& item){return QFile::remove(item);};
    if(!undo.trash(newName,&renameError,recycle) ||
       QFileInfo::exists(newName) || QFileInfo::exists(newThumb) ||
       QFileInfo::exists(luna::portableSidecar(newName)) || !undo.available())
        return fail("Ctrl+Z backup did not preserve the deleted file group");
    if(!undo.undo(&renameError) || read(newName)!=document ||
       read(newThumb)!=thumbnail || !QFileInfo::exists(luna::portableSidecar(newName)))
        return fail("Ctrl+Z did not restore the original bytes and metadata");
    if(!undo.trash(newName,&renameError,recycle)) return fail("Second delete failed");
    if(!write(newName,"collision") || undo.undo(&renameError) || read(newName)!="collision")
        return fail("Ctrl+Z overwrote an existing PDF");
    qInfo() << "[PASS] Undo and MYBOX metadata/Explorer rename contracts, plus Unicode/long-path paired moves";
    return 0;
}
