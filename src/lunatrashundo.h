// SPDX-License-Identifier: MIT
#pragma once
#include "lunasidecarpaths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QSet>
#include <QTemporaryDir>
#include <memory>
#include <functional>

namespace luna {
// Only the MOST RECENT completed LUNA deletion batch can be undone in this process.
// Originals go to the Windows Recycle Bin; a private temporary byte-for-byte
// backup supports immediate Ctrl+Z without guessing Shell recycle-bin IDs.
// Backups disappear on app exit and are never placed in a MYBOX library.
class RecentTrashUndo final {
public:
    bool available() const { return !copies_.isEmpty() && temporary_ && temporary_->isValid(); }
    QString path() const { return pdfPath_; }
    QStringList pdfPaths() const { return pdfPaths_; }
    QStringList originals() const {
        QStringList result;
        for (const auto& copy : copies_) result.append(copy.first);
        return result;
    }
    bool trash(const QString& pdf, QString* error,
               const std::function<bool(const QString&)>& recycler =
                   [](const QString& item) { return QFile::moveToTrash(item); }) {
        return trashBatch(QStringList{pdf}, error, recycler);
    }
    // Every PDF and its matching thumbnail/metadata files are one undoable
    // operation. Backup ALL files before moving ANY of them to the Recycle
    // Bin; reject overlaps and restore every already-recycled item on failure.
    bool trashBatch(const QStringList& pdfs, QString* error,
                    const std::function<bool(const QString&)>& recycler =
                        [](const QString& item) { return QFile::moveToTrash(item); }) {
        if (pdfs.isEmpty()) return reject(error, QStringLiteral("삭제할 PDF가 없습니다."));
        auto backup = std::make_unique<QTemporaryDir>();
        if (!backup->isValid()) return reject(error, QStringLiteral("로컬 복원 공간을 만들 수 없습니다."));
        QList<QPair<QString,QString>> saved;
        QStringList pdfPaths;
        QSet<QString> sourcePaths;
        for (const QString& pdf : pdfs) {
            const QFileInfo source(pdf);
            if (!source.isFile() || source.isSymLink() || source.canonicalFilePath().isEmpty())
                return reject(error, QStringLiteral("실제 PDF 파일이 아닙니다."));
            const QString canonical = source.canonicalFilePath();
            if (pdfPaths.contains(source.absoluteFilePath())) continue;
            pdfPaths.append(source.absoluteFilePath());
            QStringList paths{canonical};
            // Back up both legacy visible and new hidden management files.
            // Ignore absent companions, never follow symlinks or overwrite.
            if (!SidecarStorage::safeManagedDirectory(pdf))
                return reject(error, QStringLiteral("관리 폴더가 연결되어 있어 삭제하지 않습니다."));
            for (const QString& sidecar : SidecarStorage::allCompanions(pdf)) {
                const QFileInfo item(sidecar);
                if (item.isSymLink() || (item.exists() && !item.isFile()))
                    return reject(error, QStringLiteral("연결된 메타데이터·썸네일은 삭제하지 않습니다."));
                if (item.isFile()) paths.append(item.canonicalFilePath());
            }
            for (const QString& item : paths) {
                if (sourcePaths.contains(item))
                    return reject(error, QStringLiteral("삭제 대상 사이에 중복 파일이 있습니다."));
                sourcePaths.insert(item);
                const QString backupPath = QDir(backup->path()).filePath(QString::number(saved.size()));
                if (!QFile::copy(item, backupPath))
                    return reject(error, QStringLiteral("삭제 취소용 백업을 만들 수 없어 원본을 유지했습니다."));
                saved.append(qMakePair(item, backupPath));
            }
        }
        if (saved.isEmpty()) return reject(error, QStringLiteral("삭제할 PDF가 없습니다."));
        QStringList removed;
        for (const auto& item : saved) {
            if (!recycler(item.first)) {
                bool recovered = true;
                for (const auto& prior : saved) {
                    if (removed.contains(prior.first) && !QFileInfo::exists(prior.first))
                        if (!QFile::copy(prior.second, prior.first)) recovered = false;
                }
                if (!recovered) {
                    backup->setAutoRemove(false);
                    return reject(error, QStringLiteral("일부 파일 복원 실패. 보존된 로컬 복구 폴더: ")
                        + backup->path());
                }
                return reject(error, QStringLiteral("휴지통 이동이 완료되지 않아 원본을 복구했습니다."));
            }
            removed.append(item.first);
        }
        clear();
        pdfPath_ = pdfPaths.constFirst();
        pdfPaths_ = pdfPaths;
        copies_ = saved;
        temporary_ = std::move(backup);
        return true;
    }
    bool undo(QString* error) {
        if (!available()) return reject(error, QStringLiteral("실행 취소할 LUNA 삭제가 없습니다."));
        for (const auto& item : copies_) {
            const QFileInfo destination(item.first);
            if (destination.exists() || destination.isSymLink())
                return reject(error, QStringLiteral("원래 위치에 같은 이름의 파일이 있어 덮어쓰지 않습니다."));
            if (!QFileInfo(destination.absolutePath()).isDir())
                return reject(error, QStringLiteral("원래 폴더가 사라져 복원할 수 없습니다."));
        }
        QStringList restored;
        for (const auto& item : copies_) {
            if (!QFile::copy(item.second, item.first)) {
                for (const auto& path : restored) QFile::remove(path);
                return reject(error, QStringLiteral("복원이 완료되지 않았습니다. Ctrl+Z를 다시 시도하세요."));
            }
            restored.append(item.first);
        }
        clear();
        return true;
    }
    void clear() { copies_.clear(); temporary_.reset(); pdfPath_.clear(); pdfPaths_.clear(); }
private:
    static bool reject(QString* error, const QString& reason) {
        if (error) *error = reason;
        return false;
    }
    QString pdfPath_;
    QStringList pdfPaths_;
    QList<QPair<QString,QString>> copies_;
    std::unique_ptr<QTemporaryDir> temporary_;
};
} // namespace luna
