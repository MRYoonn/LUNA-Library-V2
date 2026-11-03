// SPDX-License-Identifier: MIT
#pragma once
#include "lunabibliography.h"
#include "lunadocumentclaim.h"
#include "lunafilemove.h"
#include "lunasidecarpaths.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QSet>
#include <memory>
#include <functional>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace luna {
inline QString lunaPathKey(const QString& path) {
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}
inline QSettings lunaLocalStore() {
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
}
inline QString portableSidecar(const QString& path) {
    return SidecarStorage::metadata(path);
}
inline QString portableSidecarForRead(const QString& path) {
    return SidecarStorage::metadataForRead(path);
}
inline QString fileObjectId(const QString& path) {
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(path);
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),
                           FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid = GetFileInformationByHandle(h, &info) != 0;
    CloseHandle(h);
    if (!valid) return {};
    return QStringLiteral("%1:%2:%3").arg(info.dwVolumeSerialNumber, 8, 16, QLatin1Char('0'))
        .arg(info.nFileIndexHigh, 8, 16, QLatin1Char('0'))
        .arg(info.nFileIndexLow, 8, 16, QLatin1Char('0'));
#else
    Q_UNUSED(path)
    return {}; // No unreliable name/size heuristic on unsupported systems.
#endif
}
// Called ONLY when Windows's stable file identity confirms that the source
// disappeared and precisely the same file appears with a new name.
inline bool reconcileExplorerRename(const QString& oldPath, const QString& newPath,
                                    Bibliography& bibliography, QString* error = nullptr) {
    if (QFileInfo::exists(oldPath) || !QFileInfo(newPath).isFile()) return false;
    const QFileInfo oldFile(oldPath), newFile(newPath);
    if (oldFile.absolutePath().compare(newFile.absolutePath(), Qt::CaseInsensitive) != 0)
        return false; // External cross-directory moves are not silently inferred.
    QList<QPair<QString,QString>> companions;
    for (const auto& pair : SidecarStorage::companions(oldPath, newPath)) {
        const QFileInfo from(pair.first), to(pair.second);
        if (from.isSymLink() || to.isSymLink() ||
            (from.exists() && !from.isFile())) return false;
        if (from.isFile()) {
            if (to.exists()) {
                if (error) *error = QStringLiteral("동일 이름의 썸네일·메타데이터가 있어 자동 변경을 중단했습니다.");
                return false;
            }
            companions.append(pair);
        }
    }
    for (const auto& item : companions)
        if (!SidecarStorage::prepareTarget(newPath, item.second)) {
            if (error) *error = QStringLiteral("숨김 LUNA 관리 폴더를 안전하게 준비할 수 없습니다.");
            return false;
        }
    QList<QPair<QString,QString>> moved;
    for (const auto& item : companions) {
        if (!renameWithoutReplacement(item.first,item.second)) {
            for (auto it=moved.crbegin();it!=moved.crend();++it)
                renameWithoutReplacement(it->second,it->first);
            if (error) *error = QStringLiteral("연결 파일명 변경에 실패해 원본 이름을 유지했습니다.");
            return false;
        }
        moved.append(item);
    }
    auto settings = lunaLocalStore();
    const QString oldKey = lunaPathKey(oldPath), newKey = lunaPathKey(newPath);
    for (const QString& prefix : {QStringLiteral("library/memos/"),QStringLiteral("library/openedDates/")}) {
        const auto value = settings.value(prefix+oldKey);
        if (value.isValid()) {
            if (!settings.contains(prefix+newKey)) settings.setValue(prefix+newKey,value);
            settings.remove(prefix+oldKey);
        }
    }
    for (const QString& key : {QStringLiteral("library/recent"),QStringLiteral("library/favorites")}) {
        auto paths = settings.value(key).toStringList();
        for (QString& p:paths) if (p.compare(oldPath,Qt::CaseInsensitive)==0) p=newPath;
        paths.removeDuplicates();
        settings.setValue(key,paths);
    }
    if (!bibliography.relocate(DocumentClaim::identity(oldPath),DocumentClaim::identity(newPath))) {
        if (error) *error = QStringLiteral("기존 서지정보가 다른 파일과 충돌했습니다. 새 파일을 확인하세요.");
        return false; // Preserve original data on metadata conflict.
    }
    settings.sync();
    return settings.status()==QSettings::NoError;
}

inline QJsonObject capturePortableRecord(const QString& path, Bibliography& bibliography) {
    auto store=lunaLocalStore();
    const auto data=bibliography.record(DocumentClaim::identity(path));
    const QString memo=store.value(QStringLiteral("library/memos/")+lunaPathKey(path)).toString();
    const bool favorite=store.value(QStringLiteral("library/favorites")).toStringList().contains(path);
    if(data.isEmpty() && memo.isEmpty() && !favorite) return {};
    QJsonObject result;
    result.insert(QStringLiteral("bibliography"),QJsonObject::fromVariantMap(data));
    result.insert(QStringLiteral("memo"),memo);
    result.insert(QStringLiteral("favorite"),favorite);
    return result;
}
inline bool applyPortableRecord(const QString& path,const QJsonObject& record,
                                Bibliography& bibliography) {
    if(!QFileInfo(path).isFile() || QFileInfo(path).isSymLink()) return false;
    auto store=lunaLocalStore();
    if(record.value(QStringLiteral("bibliography")).isObject()) {
        const QVariantMap fields=record.value(QStringLiteral("bibliography")).toObject().toVariantMap();
        if(!fields.isEmpty() &&
           !bibliography.save(DocumentClaim::identity(path),fields,fields.value("manual").toBool()))
            return false;
    }
    store.setValue(QStringLiteral("library/memos/")+lunaPathKey(path),record.value("memo").toString());
    auto favorites=store.value(QStringLiteral("library/favorites")).toStringList();
    favorites.removeAll(path);
    if(record.value("favorite").toBool())favorites.append(path);
    favorites.removeDuplicates();
    store.setValue(QStringLiteral("library/favorites"),favorites);
    store.sync();
    return store.status()==QSettings::NoError;
}
// MYBOX support intentionally uses the user's installed desktop sync folder.
// No cloud tokens, API calls, credentials, background uploads or download code.
class LocalFolderSync final : public QObject {
public:
    explicit LocalFolderSync(QObject* parent=nullptr) : QObject(parent) {
        // QFileSystemWatcher makes Explorer renames visible without waiting for
        // the 30-second periodic fallback; debounce rapid MYBOX sync bursts.
        renameDebounce_.setSingleShot(true);
        renameDebounce_.setInterval(300);
        connect(&renameDebounce_, &QTimer::timeout, this, [this]{scan();});
        // Sidecar reconciliation must yield to the PDF list and Qt event loop.
        sidecarTimer_.setSingleShot(true);
        connect(&sidecarTimer_, &QTimer::timeout, this, [this] { processSidecarBatch(); });
        connect(&watcher_, &QFileSystemWatcher::directoryChanged,
                this, [this](const QString& path) {
                    // Temporary/Explorer-deleted directories must not remain
                    // watched while Windows tears down their change handles.
                    if (!QFileInfo(path).isDir()) watcher_.removePath(path);
                    renameDebounce_.start();
                });
        timer_.setInterval(30000);
        connect(&timer_,&QTimer::timeout,this,[this]{scan();});
        timer_.start();
    }
    ~LocalFolderSync() override {
        timer_.stop();
        renameDebounce_.stop();
        sidecarTimer_.stop();
        ++generation_;
        if(worker_) worker_->wait(); // Worker only touches its isolated snapshot.
    }
    std::function<void(const QString&,const QString&)> renamed;
    std::function<bool(const QString&,const QJsonObject&)> remoteChanged;
    std::function<QJsonObject(const QString&)> localSnapshot;
    std::function<void()> filesChanged;
    std::function<void(const QString&)> metadataChanged;
    void configure(const QString& root, bool myboxEnabled) {
        root_ = QFileInfo(root).canonicalFilePath();
        mybox_ = myboxEnabled && !root_.isEmpty();
        renameDebounce_.stop();
        // Cancel stale UI-thread metadata work when switching roots. An active
        // filesystem worker is separately handled by the generation guard.
        sidecarTimer_.stop();
        sidecarPaths_.clear();
        sidecarIndex_ = 0;
        sidecarChanged_ = false;
        if (!worker_) busy_ = false;
        const QStringList existing = watcher_.directories();
        if (!existing.isEmpty()) watcher_.removePaths(existing);
        if (!root_.isEmpty()) watcher_.addPath(root_);
        rescanPending_ = false;
        ++generation_;
        seen_.clear();
        known_.clear();
        pendingPublish_.clear();
        lastPaths_.clear();
        const QVariantMap stored = lunaLocalStore().value(
            QStringLiteral("library/fileIds/") + lunaPathKey(root_)).toMap();
        for (auto it=stored.cbegin();it!=stored.cend();++it)
            known_.insert(it.key(),it.value().toString());
        scan();
    }
    bool myboxEnabled() const { return mybox_; }
    bool publish(const QString& pdf, QString* error=nullptr) {
        if (!mybox_ || !localSnapshot) return false;
        const QFileInfo f(pdf);
        if (!f.isFile() || f.isSymLink() ||
            !(f.canonicalFilePath().startsWith(root_+QLatin1Char('/'),Qt::CaseInsensitive) ||
              f.canonicalFilePath()==root_)) return false;
        QJsonObject data = localSnapshot(pdf);
        if (data.isEmpty()) return false;
        // Relocate old same-folder files without overwriting conflicts.
        SidecarStorage::migrateLegacy(pdf);
        const QString previousOutput=portableSidecarForRead(pdf);
        const QString output=portableSidecar(pdf);
        if (!SidecarStorage::ensureArea(pdf, true) || QFileInfo(output).isSymLink()) {
            if (error) *error=QStringLiteral("LUNA 숨김 메타데이터 폴더 접근 실패");
            return false;
        }
        QString id;
        if (QFileInfo(previousOutput).isFile() && !QFileInfo(previousOutput).isSymLink()) {
            QFile previous(previousOutput);
            if (previous.open(QIODevice::ReadOnly) && previous.size()<=131072)
                id=QJsonDocument::fromJson(previous.readAll()).object().value("documentId").toString();
        }
        if (QUuid(id).isNull()) id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        data.insert("schemaVersion",1);
        data.insert("documentId",id);
        data.insert("pdfName",f.fileName());
        data.insert("updatedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        const QByteArray bytes=QJsonDocument(data).toJson(QJsonDocument::Compact);
        if (bytes.size()>131072) return false;
        QSaveFile file(output);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
            pendingPublish_.insert(pdf);
            if (error) *error=QStringLiteral("MYBOX 로컬 메타데이터 저장 실패");
            return false;
        }
        pendingPublish_.remove(pdf);
        seen_[output]=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
        lunaLocalStore().setValue(QStringLiteral("library/mybox/seen/")+lunaPathKey(pdf),
                                  QString::fromLatin1(seen_.value(output).toHex()));
        return true;
    }
    void scan() {
        if (busy_) { rescanPending_=true; return; }
        if (root_.isEmpty() || !QFileInfo(root_).isDir()) return;
        busy_=true;
        const QString root=root_;
        const int generation=generation_;
        struct Scan {
            QStringList paths;
            QHash<QString,QString> ids;
            QSet<QString> ambiguous;
            QSet<QString> directories;
        };
        auto result=std::make_shared<Scan>();
        auto* worker=QThread::create([root,result] {
            result->directories.insert(root);
            QDirIterator it(root,{QStringLiteral("*.pdf"),QStringLiteral("*.PDF")},
                QDir::Files|QDir::Readable|QDir::NoDotAndDotDot|QDir::NoSymLinks,
                QDirIterator::Subdirectories);
            while(it.hasNext() && result->paths.size()<30000) {
                const QFileInfo f(it.next());
                if (!f.isFile() || f.isSymLink()) continue;
                const QString path=f.canonicalFilePath();
                if (path.isEmpty()) continue;
                if (result->directories.size() < 96)
                    result->directories.insert(f.absolutePath());
                result->paths.append(path);
                const QString id=fileObjectId(path);
                if (!id.isEmpty()) {
                    if (result->ids.contains(id)) result->ambiguous.insert(id);
                    else result->ids.insert(id,path);
                }
            }
        });
        worker_=worker;
        connect(worker,&QThread::finished,this,[this,result,generation] {
            worker_=nullptr;
            if (generation!=generation_) {
                busy_=false;
                QTimer::singleShot(0,this,[this]{scan();});
                return;
            }
            // Keep busy_ true until sidecars have been processed in bounded
            // event-loop turns; watcher feedback then causes one coalesced scan.
            // Track a bounded set of actual PDF parent directories. Watching
            // only the root misses renames inside nested scientific folders.
            const QStringList watchedDirs = watcher_.directories();
            const QSet<QString> watching(watchedDirs.cbegin(), watchedDirs.cend());
            for (const QString& dir : result->directories)
                if (QFileInfo(dir).isDir() && !watching.contains(dir))
                    watcher_.addPath(dir);
            for (const QString& dir : watcher_.directories())
                if (!QFileInfo(dir).isDir() || !result->directories.contains(dir))
                    watcher_.removePath(dir);
            bool changed=false;
            QSet<QString> matched;
            for(auto it=result->ids.cbegin();it!=result->ids.cend();++it) {
                if(result->ambiguous.contains(it.key())) continue;
                const QString before=known_.value(it.key());
                // Explorer filename change must stay in the same directory.
                if (!before.isEmpty() && before!=it.value() &&
                    QFileInfo(before).absolutePath().compare(
                        QFileInfo(it.value()).absolutePath(),Qt::CaseInsensitive)==0 &&
                    !QFileInfo::exists(before) && renamed) {
                    renamed(before,it.value());
                    matched.insert(it.value());
                    changed=true;
                }
            }
            QVariantMap stored;
            for(auto it=known_.cbegin();it!=known_.cend();++it)
                if(!QFileInfo::exists(it.value())) stored.insert(it.key(),it.value());
            for(auto it=result->ids.cbegin();it!=result->ids.cend();++it)
                if(!result->ambiguous.contains(it.key())) stored.insert(it.key(),it.value());
            known_.clear();
            for(auto it=stored.cbegin();it!=stored.cend();++it)
                known_.insert(it.key(),it.value().toString());
            lunaLocalStore().setValue(QStringLiteral("library/fileIds/")+lunaPathKey(root_),stored);
            const QSet<QString> paths(result->paths.cbegin(),result->paths.cend());
            if (!lastPaths_.isEmpty() && paths!=lastPaths_) changed=true;
            lastPaths_=paths;
            // On a second MYBOX PC the NTFS file ID differs. The portable
            // documentId recorded beside the PDF provides the same safe rename
            // detection after sync, even if LUNA was closed when it changed.
            if(mybox_) {
                const QString key=QStringLiteral("library/mybox/documentIds/")+lunaPathKey(root_);
                auto previous=lunaLocalStore().value(key).toMap();
                QVariantMap active;
                QSet<QString> duplicates;
                for(const QString& pdf:result->paths) {
                    const QFileInfo sidecar(portableSidecarForRead(pdf));
                    if(!sidecar.isFile() || sidecar.isSymLink() || sidecar.size()>131072) continue;
                    QFile input(sidecar.absoluteFilePath());
                    if(!input.open(QIODevice::ReadOnly)) continue;
                    const auto doc=QJsonDocument::fromJson(input.readAll());
                    const QString id=doc.object().value("documentId").toString();
                    if(!doc.isObject() || QUuid(id).isNull()) continue;
                    if(active.contains(id)) duplicates.insert(id);
                    else active.insert(id,pdf);
                }
                for(auto it=active.cbegin();it!=active.cend();++it) {
                    const QString after=it.value().toString();
                    // QVariantMap::value() returns QVariant; a QSet<QString>
                    // requires the concrete QString, not the boxed variant.
                    if(duplicates.contains(it.key()) || matched.contains(after)) continue;
                    const QString before=previous.value(it.key()).toString();
                    if(!before.isEmpty() && before!=after && !QFileInfo::exists(before) &&
                       QFileInfo(before).absolutePath().compare(
                           QFileInfo(after).absolutePath(),Qt::CaseInsensitive)==0 && renamed) {
                        renamed(before,after);
                        changed=true;
                    }
                }
                for(auto it=previous.cbegin();it!=previous.cend();++it)
                    if(!active.contains(it.key())) active.insert(it.key(),it.value());
                for(const auto& id:duplicates) active.remove(id);
                lunaLocalStore().setValue(key,active);
            }
            // Also migrate legacy visible files when MYBOX sync is disabled;
            // process at most eight PDFs per UI turn without touching the PDFs.
            if (!result->paths.isEmpty()) {
                sidecarPaths_=result->paths;
                sidecarIndex_=0;
                sidecarChanged_=changed;
                sidecarTimer_.start(0);
                return;
            }
            finishScan(changed);
        });
        connect(worker,&QThread::finished,worker,&QObject::deleteLater);
        connect(qApp,&QCoreApplication::aboutToQuit,worker,[worker]{worker->wait();});
        worker->start(QThread::LowPriority);
    }
private:
    void finishScan(bool changed) {
        sidecarTimer_.stop();
        sidecarPaths_.clear();
        sidecarIndex_=0;
        sidecarChanged_=false;
        busy_=false;
        if (changed && filesChanged) filesChanged();
        if (rescanPending_) {
            rescanPending_=false;
            QTimer::singleShot(0,this,[this]{scan();});
        }
    }
    void processSidecarBatch() {
        // This used to loop over as many as 30,000 PDFs inside one
        // QThread::finished GUI callback. On Windows, creating each
        // .luna-meta.json and updating QSettings could block the reader
        // for longer than the 45s native smoke timeout.
        QElapsedTimer elapsed;
        elapsed.start();
        int processed=0;
        while (sidecarIndex_<sidecarPaths_.size() && processed<8 && elapsed.elapsed()<12) {
            const QString pdf=sidecarPaths_.at(sidecarIndex_++);
            ++processed;
            SidecarStorage::migrateLegacy(pdf);
            if (!mybox_ || !localSnapshot) continue;

            if(pendingPublish_.contains(pdf)) {
                if(publish(pdf)) pendingPublish_.remove(pdf);
                continue; // Never import an older synced copy over local edits.
            }
            const QString sidecar=portableSidecarForRead(pdf);
            const QFileInfo info(sidecar);
            if(info.isSymLink() || (info.exists() && (!info.isFile() || info.size()>131072))) continue;
            if(!info.exists()) { publish(pdf); continue; }
            QFile file(sidecar);
            if(!file.open(QIODevice::ReadOnly)) continue;
            const QByteArray bytes=file.readAll();
            const QByteArray digest=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
            if(seen_.value(sidecar)==digest)continue;
            const QJsonDocument document=QJsonDocument::fromJson(bytes);
            if(!document.isObject() ||
               document.object().value("schemaVersion").toInt()!=1 ||
               QUuid(document.object().value("documentId").toString()).isNull() ||
               !document.object().value("bibliography").isObject() ||
               !document.object().value("memo").isString() ||
               !document.object().value("favorite").isBool()) continue;
            // A newly connected PC with existing divergent local edits
            // keeps its originals; never silently overwrite them.
            const QJsonObject remote=document.object();
            const QJsonObject local=localSnapshot(pdf);
            const QString knownDigest=lunaLocalStore().value(
                QStringLiteral("library/mybox/seen/")+lunaPathKey(pdf)).toString();
            if(knownDigest.isEmpty() && !local.isEmpty() &&
               (local.value("bibliography")!=remote.value("bibliography") ||
                local.value("memo")!=remote.value("memo") ||
                local.value("favorite")!=remote.value("favorite"))) {
                qWarning().noquote() << "LUNA MYBOX metadata conflict: preserving local edits for" << pdf;
                continue;
            }
            if(remoteChanged && !remoteChanged(pdf,remote)) continue;
            seen_[sidecar]=digest;
            if(metadataChanged) metadataChanged(pdf);
            lunaLocalStore().setValue(
                QStringLiteral("library/mybox/seen/")+lunaPathKey(pdf),
                QString::fromLatin1(digest.toHex()));
        }
        if (sidecarIndex_<sidecarPaths_.size()) {
            sidecarTimer_.start(0);
        } else {
            finishScan(sidecarChanged_);
        }
    }
    QString root_;
    bool mybox_=false, busy_=false, rescanPending_=false;
    int generation_=0;
    QTimer timer_;
    QTimer renameDebounce_;
    QTimer sidecarTimer_;
    QStringList sidecarPaths_;
    qsizetype sidecarIndex_=0;
    bool sidecarChanged_=false;
    QFileSystemWatcher watcher_;
    QPointer<QThread> worker_;
    QHash<QString,QString> known_;
    QSet<QString> lastPaths_;
    QHash<QString,QByteArray> seen_;
    QSet<QString> pendingPublish_;
};
} // namespace luna
