// SPDX-License-Identifier: MIT
#include "lunafilemove.h"
#include "lunatrashundo.h"
#include "lunalocalsync.h"
#include "lunaautofilename.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QLockFile>
#include <QSysInfo>

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
    const QString originalMeta=QDir(root).filePath(QStringLiteral("논문 공백.luna-meta.json"));
    const QString movedMeta=QDir(target).filePath(QStringLiteral("논문 공백.luna-meta.json"));
    if(!write(originalMeta, QByteArray("{\"schemaVersion\":1}")))
        return fail("Cannot create portable metadata fixture");
    const auto success = luna::movePdfWithThumbnails(pdf, target);
    if (!success.success || success.documentPath != movedPdf || QFileInfo::exists(pdf) || QFileInfo::exists(png) ||
        QFileInfo::exists(originalMeta) || read(movedPdf) != document || read(movedPng) != thumbnail ||
        read(movedMeta)!=QByteArray("{\"schemaVersion\":1}"))
        return fail("Paired move changed PDF, thumbnail or MYBOX metadata");
    QFile::remove(movedMeta); // Isolate the subsequent stale-sidecar test.
    if (!luna::movePdfWithThumbnails(movedPdf, target).success) return fail("Same-directory move failed");
    if (luna::movePdfWithThumbnails(pdf, target).success) return fail("Missing source accepted");
    // A destination orphan thumbnail must never become a different PDF's preview.
    if (!write(pdf, document)) return fail("Cannot recreate PDF-only fixture");
    QFile::remove(movedPdf);
    if (luna::movePdfWithThumbnails(pdf, target).success || read(pdf) != document || read(movedPng) != thumbnail)
        return fail("PDF-only move adopted an unrelated destination thumbnail");
    // A stale MYBOX sidecar blocks importing an unrelated document.
    const QString copyTarget=QDir(root).filePath(QStringLiteral("copy-target"));
    if(!QDir().mkpath(copyTarget))return fail("Cannot create copy folder");
    const QString stale=QDir(copyTarget).filePath(QStringLiteral("논문 공백.luna-meta.json"));
    if(!write(stale,"unrelated") || luna::copyPdfWithThumbnails(pdf,copyTarget).success ||
       read(stale)!="unrelated" || !QFileInfo::exists(pdf))
        return fail("Copy adopted an unrelated MYBOX metadata sidecar");
    // A dead LUNA process may leave the old DocumentClaim .lock on disk.
    // Inspect its owner PID with QLockFile, not QFileInfo::exists(lockPath).
    const QString staleClaim=QDir(root).filePath(QStringLiteral("abandoned-pdf.lock"));
    const QByteArray fakeDeadPid=QByteArray("2147483647\nLUNA.exe\n")+
        QSysInfo::machineHostName().toUtf8()+QByteArray("\n");
    if(!write(staleClaim,fakeDeadPid))
        return fail("Cannot create an abandoned LUNA claim");
    {
        QLockFile recovered(staleClaim);
        recovered.setStaleLockTime(0);
        if(!recovered.tryLock(0))
            return fail("A dead owner's stale PDF claim prevented a title rename");
        QLockFile competing(staleClaim);
        competing.setStaleLockTime(0);
        if(competing.tryLock(0))
            return fail("Another process claimed a live title-rename lock");
    }
    {
        QLockFile reopened(staleClaim);
        reopened.setStaleLockTime(0);
        if(!reopened.tryLock(0))
            return fail("Released title-rename claim was not reusable");
    }
    // Title-based rename reuses the same no-overwrite paired move, in place.
    const QVariantMap verifiedTitle{
        {QStringLiteral("title"), QStringLiteral("A Synthetic Title: With Illegal / Characters?")},
        {QStringLiteral("titleSource"), QStringLiteral("publisher_cover_verified")}
    };
    const QString titledName=luna::AutoFileName::fromVerifiedTitle(verifiedTitle,root);
    if(titledName!=QStringLiteral("A Synthetic Title With Illegal Characters.pdf"))
        return fail("Windows title filename normalization changed");
    QVariantMap uncertain=verifiedTitle;
    uncertain[QStringLiteral("titleSource")]=QStringLiteral("pdf_metadata");
    if(!luna::AutoFileName::fromVerifiedTitle(uncertain,root).isEmpty())
        return fail("Unverified PDF metadata renamed an original file");
    uncertain=verifiedTitle;
    uncertain[QStringLiteral("documentKind")]=QStringLiteral("supporting_information");
    if(!luna::AutoFileName::fromVerifiedTitle(uncertain,root).isEmpty())
        return fail("Supporting Information must never be automatically renamed");
    const QString titledSource=QDir(root).filePath(QStringLiteral("title-source.pdf"));
    const QString titledThumbnail=QDir(root).filePath(QStringLiteral("title-source.luna-thumb.png"));
    const QString titledTarget=QDir(root).filePath(titledName);
    const QString titledTargetThumbnail=luna::SidecarStorage::legacyThumbnail(titledTarget,QStringLiteral(".png"));
    if(!write(titledSource,document) || !write(titledThumbnail,thumbnail))
        return fail("Could not create title rename fixture");
    if(!write(titledTarget,"collision")) return fail("Could not create title collision");
    if(luna::movePdfWithThumbnails(titledSource,root,luna::renameWithoutReplacement,titledName).success ||
       read(titledSource)!=document || read(titledTarget)!="collision" ||
       read(titledThumbnail)!=thumbnail)
        return fail("Auto-title rename overwrote a colliding PDF");
    QFile::remove(titledTarget);
    auto titled=luna::movePdfWithThumbnails(titledSource,root,luna::renameWithoutReplacement,titledName);
    if(!titled.success || titled.documentPath!=titledTarget ||
       QFileInfo::exists(titledSource) || QFileInfo::exists(titledThumbnail) ||
       read(titledTarget)!=document || read(titledTargetThumbnail)!=thumbnail)
        return fail("In-place title rename did not preserve PDF and cropped thumbnail");
    if(!luna::movePdfWithThumbnails(titledTarget,root).success)
        return fail("Same-title destination must be an idempotent no-op");
    // Exercise native Explorer renames with the real OS file identity.
    const QString renamedDir=QDir(root).filePath(QStringLiteral("rename-fixtures"));
    if (!QDir().mkpath(renamedDir)) return fail("Cannot create rename fixture");
    const QString oldName=QDir(renamedDir).filePath(QStringLiteral("original.pdf"));
    const QString newName=QDir(renamedDir).filePath(QStringLiteral("renamed.pdf"));
    const QString oldThumb=QDir(renamedDir).filePath(QStringLiteral("original.luna-thumb.png"));
    const QString newThumb=luna::SidecarStorage::thumbnail(newName,QStringLiteral(".png"));
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
    // A mixed-version MYBOX client may write the old visible file again.
    // In that case publishing must never clobber the newer managed record.
    const QByteArray originalPublished=read(luna::portableSidecar(oldName));
    const QString conflictingLegacy=luna::SidecarStorage::legacyMetadata(oldName);
    if(!write(conflictingLegacy,"synthetic divergent legacy metadata"))
        return fail("Cannot set up mixed-version MYBOX collision");
    QString conflictError;
    if(bridge.publish(oldName,&conflictError) ||
       conflictError.isEmpty() ||
       read(luna::portableSidecar(oldName))!=originalPublished ||
       read(conflictingLegacy)!="synthetic divergent legacy metadata")
        return fail("Mixed-version MYBOX publish overwrote a conflicting user record");
    if(!QFile::remove(conflictingLegacy))
        return fail("Cannot clean up isolated MYBOX collision");
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
    // Simulate another PC importing a metadata file that MYBOX delivered.
    QFile synced(luna::portableSidecar(newName));
    if(!synced.open(QIODevice::ReadOnly)) return fail("Cannot open synced MYBOX sidecar");
    const QJsonObject remote=QJsonDocument::fromJson(synced.readAll()).object();
    synced.close();
    if(remote.value("bibliography").toObject().value("title").toString() !=
           QStringLiteral("Synthetic metadata remains attached") ||
       remote.value("memo").toString()!=QStringLiteral("synced memo") ||
       !remote.value("favorite").toBool())
        return fail("Portable MYBOX record lost citation, memo or favorite");
    const QString otherDevice=QDir(root).filePath(QStringLiteral("remote-device.pdf"));
    if(!write(otherDevice,document) ||
       !luna::applyPortableRecord(otherDevice,remote,bibliography) ||
       bibliography.record(luna::DocumentClaim::identity(otherDevice)).value("title").toString()
           != QStringLiteral("Synthetic metadata remains attached"))
        return fail("MYBOX remote device could not import verified portable metadata");
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
    // Atomic multi-PDF deletion: all PDFs and sidecars disappear together,
    // and one Ctrl+Z restores the entire Shift selection byte for byte.
    const QString batchA=QDir(root).filePath(QStringLiteral("batch-first.pdf"));
    const QString batchB=QDir(root).filePath(QStringLiteral("batch-second.pdf"));
    const QString batchThumb=QDir(root).filePath(QStringLiteral("batch-second.luna-thumb.png"));
    const QString batchMeta=QDir(root).filePath(QStringLiteral("batch-first.luna-meta.json"));
    if(!write(batchA,"batch-first-bytes") || !write(batchB,"batch-second-bytes") ||
       !write(batchThumb,"batch-thumbnail") || !write(batchMeta,"batch-metadata"))
        return fail("Cannot prepare multi-delete files");
    QStringList batch{batchA,batchB};
    if(!undo.trashBatch(batch,&renameError,recycle) ||
       QFileInfo::exists(batchA) || QFileInfo::exists(batchB) ||
       QFileInfo::exists(batchThumb) || QFileInfo::exists(batchMeta) ||
       undo.pdfPaths().size()!=2 || !undo.available())
        return fail("Multi-PDF delete did not remove the complete selected group");
    if(!undo.undo(&renameError) || read(batchA)!="batch-first-bytes" ||
       read(batchB)!="batch-second-bytes" || read(batchThumb)!="batch-thumbnail" ||
       read(batchMeta)!="batch-metadata")
        return fail("Ctrl+Z did not restore all selected PDFs and sidecars");
    // If recycling any member fails, already-removed members must be
    // restored. The former successfully undone batch must not be lost.
    int recycled=0;
    const auto failAfterOne=[&recycled](const QString& item) {
        if(++recycled>1) return false;
        return QFile::remove(item);
    };
    if(undo.trashBatch(batch,&renameError,failAfterOne) ||
       read(batchA)!="batch-first-bytes" || read(batchB)!="batch-second-bytes" ||
       read(batchThumb)!="batch-thumbnail" || read(batchMeta)!="batch-metadata")
        return fail("Failed multi-PDF delete left an unexpected partially deleted group");
    // End-to-end regression: the previous tests called reconcileExplorerRename
    // directly and could not catch a missing folder watcher. Reproduce a real
    // Explorer rename in a nested PDF folder and wait for the live callback.
    const QString watchedRoot=QDir(root).filePath(QStringLiteral("watched-library"));
    const QString watchedFolder=QDir(watchedRoot).filePath(QStringLiteral("nested"));
    if(!QDir().mkpath(watchedFolder)) return fail("Cannot create watched nested folder");
    const QString watchedBefore=QDir(watchedFolder).filePath(QStringLiteral("watched-original.pdf"));
    const QString watchedAfter=QDir(watchedFolder).filePath(QStringLiteral("watched-renamed.pdf"));
    const QString oldPng=QDir(watchedFolder).filePath(QStringLiteral("watched-original.luna-thumb.png"));
    const QString newPng=luna::SidecarStorage::thumbnail(watchedAfter,QStringLiteral(".png"));
    const QString oldJpg=QDir(watchedFolder).filePath(QStringLiteral("watched-original.luna-thumb.jpg"));
    const QString newJpg=luna::SidecarStorage::thumbnail(watchedAfter,QStringLiteral(".jpg"));
    if(!write(watchedBefore,document) || !write(oldPng,thumbnail) ||
       !write(oldJpg,QByteArray("separate JPG crop")))
        return fail("Cannot create nested Explorer thumbnail fixtures");
    bool watcherRenamed=false;
    {
        luna::LocalFolderSync watcher;
        watcher.localSnapshot=[&](const QString& item){
            return luna::capturePortableRecord(item,bibliography);
        };
        watcher.renamed=[&](const QString& before,const QString& after){
            QString error;
            watcherRenamed=luna::reconcileExplorerRename(before,after,bibliography,&error);
            if(!watcherRenamed) qCritical().noquote()<<error;
        };
        watcher.configure(watchedRoot,true);
        const QString key=QStringLiteral("library/fileIds/")+luna::lunaPathKey(watchedRoot);
        const QString stableId=luna::fileObjectId(watchedBefore);
#ifdef Q_OS_WIN
        if(stableId.isEmpty()) return fail("Native watcher fixture has no stable Windows file ID");
#endif
        auto pumpUntil=[&](const std::function<bool()>& predicate) {
            QElapsedTimer timer; timer.start();
            while(timer.elapsed()<7500) {
                QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                if(predicate()) return true;
                QThread::msleep(15);
            }
            return predicate();
        };
        if(!pumpUntil([&] {
            const QVariantMap stored=luna::lunaLocalStore().value(key).toMap();
            const QString storedPath=stored.value(stableId).toString();
            return !storedPath.isEmpty() &&
                QFileInfo(storedPath).canonicalFilePath().compare(
                    QFileInfo(watchedBefore).canonicalFilePath(),Qt::CaseInsensitive)==0;
        })) return fail("Native nested watcher did not index original PDF file identity");
        if(!luna::renameWithoutReplacement(watchedBefore,watchedAfter))
            return fail("Cannot simulate nested Explorer PDF filename rename");
        if(!pumpUntil([&] { return watcherRenamed; }))
            return fail("Native nested watcher missed Explorer rename");
    }
    if(QFileInfo::exists(oldPng) || QFileInfo::exists(oldJpg) ||
       read(newPng)!=thumbnail || read(newJpg)!="separate JPG crop")
        return fail("Live Explorer rename did not preserve original PNG/JPG thumbnails");
    qInfo() << "[PASS] Undo and MYBOX metadata/Explorer rename contracts, plus Unicode/long-path paired moves";
    return 0;
}
