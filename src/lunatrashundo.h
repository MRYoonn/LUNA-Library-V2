// SPDX-License-Identifier: MIT
#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <memory>
#include <functional>

namespace luna {
// Only the MOST RECENT completed LUNA deletion can be undone in this process.
// Originals go to the Windows Recycle Bin; a private temporary byte-for-byte
// backup supports immediate Ctrl+Z without guessing Shell recycle-bin IDs.
// Backups disappear on app exit and are never placed in a MYBOX library.
class RecentTrashUndo final {
public:
    bool available() const { return !copies_.isEmpty() && temporary_ && temporary_->isValid(); }
    QString path() const { return pdfPath_; }
    QStringList originals() const {
        QStringList result;
        for (const auto& copy : copies_) result.append(copy.first);
        return result;
    }
    bool trash(const QString& pdf, QString* error,
               const std::function<bool(const QString&)>& recycler = QFile::moveToTrash) {
        const QFileInfo source(pdf);
        if (!source.isFile() || source.isSymLink() || source.canonicalFilePath().isEmpty())
            return reject(error, QStringLiteral("실제 PDF 파일이 아닙니다."));
        auto backup = std::make_unique<QTemporaryDir>();
        if (!backup->isValid()) return reject(error, QStringLiteral("로컬 복원 공간을 만들 수 없습니다."));
        QList<QPair<QString,QString>> saved;
        QStringList paths{source.canonicalFilePath()};
        for (const QString& extension : {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")}) {
            const QString sidecar = QDir(source.absolutePath()).filePath(
                source.completeBaseName() + QStringLiteral(".luna-thumb") + extension);
            const QFileInfo item(sidecar);
            if (item.isSymLink()) return reject(error, QStringLiteral("연결된 썸네일은 삭제하지 않습니다."));
            if (item.exists() && item.isFile()) paths.append(item.canonicalFilePath());
        }
        // Metadata sidecar is synced by MYBOX; trash/restore it with the PDF.
        const QString portable=QDir(source.absolutePath()).filePath(
            source.completeBaseName()+QStringLiteral(".luna-meta.json"));
        if (QFileInfo(portable).isSymLink())
            return reject(error, QStringLiteral("연결된 메타데이터 파일은 삭제하지 않습니다."));
        if (QFileInfo(portable).isFile()) paths.append(QFileInfo(portable).canonicalFilePath());
        // Copy every byte before any source is moved to the Recycle Bin.
        // If the disk is full, abort without deleting a single file.
        for (int n = 0; n < paths.size(); ++n) {
            const QString backupPath = QDir(backup->path()).filePath(QString::number(n));
            if (!QFile::copy(paths[n], backupPath))
                return reject(error, QStringLiteral("삭제 취소용 백업을 만들 수 없어 원본을 유지했습니다."));
            saved.append(qMakePair(paths[n], backupPath));
        }
        QList<QString> removed;
        for (const auto& item : saved) {
            if (!recycler(item.first)) {
                // Recover already trashed paths from their verified backup.
                for (const auto& prior : saved) {
                    if (removed.contains(prior.first) && !QFileInfo::exists(prior.first))
                        QFile::copy(prior.second, prior.first);
                }
                return reject(error, QStringLiteral("휴지통 이동이 완료되지 않았습니다. 원본 경로를 확인하세요."));
            }
            removed.append(item.first);
        }
        clear();
        pdfPath_ = source.absoluteFilePath();
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
    void clear() { copies_.clear(); temporary_.reset(); pdfPath_.clear(); }
private:
    static bool reject(QString* error, const QString& reason) {
        if (error) *error = reason;
        return false;
    }
    QString pdfPath_;
    QList<QPair<QString,QString>> copies_;
    std::unique_ptr<QTemporaryDir> temporary_;
};
} // namespace luna
