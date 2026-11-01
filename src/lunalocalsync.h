// SPDX-License-Identifier: MIT
#pragma once
#include "lunabibliography.h"
#include "lunadocumentclaim.h"
#include "lunafilemove.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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
    const QFileInfo f(path);
    return QDir(f.absolutePath()).filePath(f.completeBaseName() + QStringLiteral(".luna-meta.json"));
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
    for (const QString& ext : {QStringLiteral(".luna-meta.json"), QStringLiteral(".luna-thumb.png"),
                               QStringLiteral(".luna-thumb.jpg"), QStringLiteral(".luna-thumb.jpeg")}) {
        const QString from = QDir(oldFile.absolutePath()).filePath(oldFile.completeBaseName() + ext);
        const QString to = QDir(newFile.absolutePath()).filePath(newFile.completeBaseName() + ext);
        if (QFileInfo(from).isSymLink() || QFileInfo(to).isSymLink()) return false;
        if (QFileInfo::exists(from)) {
            if (QFileInfo::exists(to)) {
                if (error) *error = QStringLiteral("동일 이름의 썸네일·메타데이터가 있어 자동 변경을 중단했습니다.");
                return false;
            }
            companions.append(qMakePair(from,to));
        }
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

// MYBOX support intentionally uses the user's installed desktop sync folder.
// No cloud tokens, API calls, credentials, background uploads or download code.
class LocalFolderSync final : public QObject {
public:
    explicit LocalFolderSync(QObject* parent=nullptr) : QObject(parent) {
        timer_.setInterval(30000);
        connect(&timer_,&QTimer::timeout,this,[this]{scan();});
        timer_.start();
    }
    std::function<void(const QString&,const QString&)> renamed;
    std::function<void(const QString&,const QJsonObject&)> remoteChanged;
    std::function<QJsonObject(const QString&)> localSnapshot;
    std::function<void()> filesChanged;
    void configure(const QString& root, bool myboxEnabled) {
        root_ = QFileInfo(root).canonicalFilePath();
        mybox_ = myboxEnabled && !root_.isEmpty();
        ++generation_;
        seen_.clear();
        known_.clear();
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
        const QString output=portableSidecar(pdf);
        if (QFileInfo(output).isSymLink()) return false;
        QString id;
        if (QFileInfo(output).isFile()) {
            QFile previous(output);
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
            if (error) *error=QStringLiteral("MYBOX 로컬 메타데이터 저장 실패");
            return false;
        }
        seen_[output]=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
        lunaLocalStore().setValue(QStringLiteral("library/mybox/seen/")+lunaPathKey(pdf),
                                  QString::fromLatin1(seen_.value(output).toHex()));
        return true;
    }
    void scan() {
        if (busy_ || root_.isEmpty() || !QFileInfo(root_).isDir()) return;
        busy_=true;
        const QString root=root_;
        const int generation=generation_;
        struct Scan {
            QStringList paths;
            QHash<QString,QString> ids;
            QSet<QString> ambiguous;
        };
        auto result=std::make_shared<Scan>();
        auto* worker=QThread::create([root,result] {
            QDirIterator it(root,{QStringLiteral("*.pdf"),QStringLiteral("*.PDF")},
                QDir::Files|QDir::Readable|QDir::NoDotAndDotDot|QDir::NoSymLinks,
                QDirIterator::Subdirectories);
            while(it.hasNext() && result->paths.size()<30000) {
                const QFileInfo f(it.next());
                if (!f.isFile() || f.isSymLink()) continue;
                const QString path=f.canonicalFilePath();
                if (path.isEmpty()) continue;
                result->paths.append(path);
                const QString id=fileObjectId(path);
                if (!id.isEmpty()) {
                    if (result->ids.contains(id)) result->ambiguous.insert(id);
                    else result->ids.insert(id,path);
                }
            }
        });
        connect(worker,&QThread::finished,this,[this,result,generation] {
            busy_=false;
            if (generation!=generation_) return;
            bool changed=false;
            for(auto it=result->ids.cbegin();it!=result->ids.cend();++it) {
                if(result->ambiguous.contains(it.key())) continue;
                const QString before=known_.value(it.key());
                if (!before.isEmpty() && before!=it.value() &&
                    !QFileInfo::exists(before) && renamed) {
                    renamed(before,it.value());
                    changed=true;
                }
            }
            // Replace the map only after callbacks have reconciled old paths.
            QHash<QString,QString> fresh;
            QVariantMap stored;
            for(auto it=result->ids.cbegin();it!=result->ids.cend();++it)
                if(!result->ambiguous.contains(it.key())) {
                    fresh.insert(it.key(),it.value());
                    stored.insert(it.key(),it.value());
                }
            known_=std::move(fresh);
            lunaLocalStore().setValue(QStringLiteral("library/fileIds/")+lunaPathKey(root_),stored);
            const QSet<QString> paths(result->paths.cbegin(),result->paths.cend());
            if (paths!=lastPaths_) { changed=true; lastPaths_=paths; }
            if(mybox_ && localSnapshot) {
                int read=0;
                for(const QString& pdf:result->paths) {
                    if(++read>30000) break;
                    const QString sidecar=portableSidecar(pdf);
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
                       document.object().value("schemaVersion").toInt()!=1) continue;
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
                        qWarning("LUNA MYBOX metadata conflict: preserving local edits");
                        continue;
                    }
                    seen_[sidecar]=digest;
                    if(remoteChanged) remoteChanged(pdf,remote);
                    lunaLocalStore().setValue(
                        QStringLiteral("library/mybox/seen/")+lunaPathKey(pdf),
                        QString::fromLatin1(digest.toHex()));
                }
            }
            if(changed && filesChanged) filesChanged();
        });
        connect(worker,&QThread::finished,worker,&QObject::deleteLater);
        connect(qApp,&QCoreApplication::aboutToQuit,worker,[worker]{worker->wait();});
        worker->start(QThread::LowPriority);
    }
private:
    QString root_;
    bool mybox_=false, busy_=false;
    int generation_=0;
    QTimer timer_;
    QHash<QString,QString> known_;
    QSet<QString> lastPaths_;
    QHash<QString,QByteArray> seen_;
};
} // namespace luna
