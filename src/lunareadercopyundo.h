// SPDX-License-Identifier: MIT
#pragma once
// One-shot Ctrl+Z for a newly duplicated reader PDF. A copy is undoable only
// while its on-disk PDF and every managed sidecar are byte-for-byte unchanged.
// The owning LibraryPanel manages UI, open-tab guards and QSettings cleanup.
#include "lunasidecarpaths.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QPair>
#include <QString>

namespace luna {
class ReaderCopyUndo final {
public:
    bool available() const { return !path_.isEmpty(); }
    QString path() const { return path_; }
    void clear() { path_.clear(); fingerprints_.clear(); }

    bool remember(const QString& pdf) {
        clear();
        const QFileInfo source(pdf);
        if (!source.isFile() || source.isSymLink() || source.canonicalFilePath().isEmpty() ||
            !SidecarStorage::safeManagedDirectory(pdf)) return false;
        path_ = source.canonicalFilePath();
        if (!append(path_)) { clear(); return false; }
        for (const auto& companion : SidecarStorage::allCompanions(path_)) {
            const QFileInfo file(companion);
            if (file.isSymLink() || (file.exists() && !file.isFile())) { clear(); return false; }
            if (file.isFile() && !append(file.canonicalFilePath())) { clear(); return false; }
        }
        return true;
    }

    bool unchanged(QString* reason) const {
        if (!available()) return fail(reason, QStringLiteral("취소할 PDF 복사 기록이 없습니다."));
        if (!SidecarStorage::safeManagedDirectory(path_))
            return fail(reason, QStringLiteral("복사본 관리 폴더가 변경됐습니다."));
        QList<QString> present;
        present.append(path_);
        for (const auto& companion : SidecarStorage::allCompanions(path_)) {
            const QFileInfo file(companion);
            if (file.isSymLink() || (file.exists() && !file.isFile()))
                return fail(reason, QStringLiteral("복사본 관리 파일이 변경됐습니다."));
            if (file.isFile()) present.append(file.canonicalFilePath());
        }
        if (present.size() != fingerprints_.size())
            return fail(reason, QStringLiteral("복사본에 새 썸네일이나 메타데이터가 생겼습니다."));
        for (int i = 0; i < present.size(); ++i) {
            if (present.at(i) != fingerprints_.at(i).first)
                return fail(reason, QStringLiteral("복사본 파일 구성이 변경됐습니다."));
            QFile file(present.at(i));
            if (!file.open(QIODevice::ReadOnly) ||
                QCryptographicHash::hash(&file, QCryptographicHash::Sha256) != fingerprints_.at(i).second)
                return fail(reason, QStringLiteral("복사본 파일 내용이 수정됐습니다."));
        }
        return true;
    }
private:
    bool append(const QString& name) {
        QFile file(name);
        if (!file.open(QIODevice::ReadOnly)) return false;
        fingerprints_.append(qMakePair(name,
            QCryptographicHash::hash(&file, QCryptographicHash::Sha256)));
        return true;
    }
    static bool fail(QString* reason, const QString& message) {
        if (reason) *reason = message;
        return false;
    }
    QString path_;
    QList<QPair<QString, QByteArray>> fingerprints_;
};
} // namespace luna
