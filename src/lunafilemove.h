// SPDX-License-Identifier: MIT
#pragma once
#include "lunasidecarpaths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <functional>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#elif defined(Q_OS_LINUX)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace luna {
struct FileMoveResult {
    bool success = false;
    QString documentPath;
    QString error;
};

// Unlike QFile::rename, these operations never fall back to copy/delete and
// never replace an existing destination. Cross-volume moves fail safely.
inline bool renameWithoutReplacement(const QString& from, const QString& to) {
#ifdef Q_OS_WIN
    auto native = [](const QString& path) {
        QString p = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
        if (p.startsWith(QStringLiteral("\\\\?\\"))) return p;
        if (p.startsWith(QStringLiteral("\\\\"))) return QStringLiteral("\\\\?\\UNC\\") + p.mid(2);
        return QStringLiteral("\\\\?\\") + p;
    };
    const QString source = native(from), destination = native(to);
    return MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()),
                       reinterpret_cast<LPCWSTR>(destination.utf16()), 0) != 0;
#elif defined(Q_OS_LINUX)
    return syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(from).constData(),
                   AT_FDCWD, QFile::encodeName(to).constData(), 1 /* RENAME_NOREPLACE */) == 0;
#else
    Q_UNUSED(from); Q_UNUSED(to);
    return false;
#endif
}

inline FileMoveResult movePdfWithThumbnails(const QString& path, const QString& directory,
    const std::function<bool(const QString&, const QString&)>& rename = renameWithoutReplacement,
    const QString& requestedFileName = {}) {
    FileMoveResult result;
    const QFileInfo source(path), folder(directory);
    result.documentPath = source.absoluteFilePath();
    if (!source.isFile() || source.isSymLink() || source.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) ||
        !folder.isDir() || folder.isSymLink()) {
        result.error = QStringLiteral("PDF 또는 대상 폴더를 확인할 수 없습니다. 연결 파일은 이동하지 않습니다.");
        return result;
    }
    const QString targetRoot = folder.canonicalFilePath();
    const QString originalRoot = source.canonicalPath();
    // Reuse the same atomic paired move for a title-based rename in one folder.
    // The default, ordinary folder move keeps the original basename.
    if (!requestedFileName.isEmpty() &&
        (QFileInfo(requestedFileName).fileName() != requestedFileName ||
         QFileInfo(requestedFileName).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0 ||
         requestedFileName == QStringLiteral(".") || requestedFileName == QStringLiteral(".."))) {
        result.error = QStringLiteral("잘못된 PDF 파일명입니다.");
        return result;
    }
    const QString targetPdf = QDir(targetRoot).filePath(
        requestedFileName.isEmpty() ? source.fileName() : requestedFileName);
    if (targetPdf.compare(source.absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        result.success = true;
        return result;
    }
    if (!SidecarStorage::safeManagedDirectory(path) ||
        !SidecarStorage::safeManagedDirectory(targetPdf)) {
        result.error = QStringLiteral("LUNA 관리 폴더가 연결 폴더이므로 안전한 이동을 중단했습니다.");
        return result;
    }
    QList<QPair<QString, QString>> moves;
    // Check BOTH the old visible and new hidden sidecar layouts. Any
    // destination collision prevents all moves, even for a source with no
    // companion, so an orphan cannot be attached to another PDF.
    for (const auto& pair : SidecarStorage::companions(path, targetPdf)) {
        const QFileInfo sidecar(pair.first), targetSidecar(pair.second);
        if (sidecar.isSymLink() || (sidecar.exists() && !sidecar.isFile())) {
            result.error = QStringLiteral("메타데이터·썸네일이 일반 파일이 아니므로 이동하지 않았습니다.");
            return result;
        }
        if (targetSidecar.exists() || targetSidecar.isSymLink()) {
            result.error = QStringLiteral("대상 폴더에 같은 이름의 관리 파일이 있어 이동하지 않았습니다.\\n%1")
                .arg(pair.second);
            return result;
        }
        if (sidecar.isFile()) moves.append(pair);
    }
    moves.append({source.canonicalFilePath(), targetPdf});
    for (const auto& pair : moves) {
        const QFileInfo target(pair.second);
        if (target.exists() || target.isSymLink()) {
            result.error = QStringLiteral("같은 이름의 대상이 있어 이동하지 않았습니다.\\n%1").arg(pair.second);
            return result;
        }
    }
    for (const auto& pair : moves) {
        if (!SidecarStorage::prepareTarget(targetPdf, pair.second)) {
            result.error = QStringLiteral("대상 LUNA 숨김 관리 폴더를 안전하게 준비하지 못했습니다.");
            return result;
        }
    }
    int moved = 0;
    for (const auto& pair : moves) {
        if (!rename(pair.first, pair.second)) {
            QStringList remaining;
            while (moved > 0) {
                const auto& previous = moves.at(--moved);
                if (!rename(previous.second, previous.first)) remaining.append(previous.second);
            }
            result.error = QStringLiteral("이동하지 못했습니다. 사용 중인 파일, 쓰기 권한 또는 다른 드라이브인지 확인해 주세요. 이 버전은 같은 드라이브 내 이동을 지원합니다.");
            if (!remaining.isEmpty()) result.error += QStringLiteral("\n일부 썸네일은 아래 위치에 보존되었습니다. 원본 PDF는 이동하지 않았습니다.\n") + remaining.join(QLatin1Char('\n'));
            return result;
        }
        ++moved;
    }
    result.success = true;
    result.documentPath = moves.last().second;
    return result;
}

struct FileCopyResult {
    bool success = false;
    QString documentPath;
    QString error;
};

// Copy the actual PDF and its user-cropped thumbnail sidecars. The destination
// must be empty; on any copy failure remove only files created by this attempt.
inline FileCopyResult copyPdfWithThumbnails(const QString& path, const QString& directory) {
    FileCopyResult result;
    const QFileInfo source(path), folder(directory);
    if (!source.isFile() || source.isSymLink() ||
        source.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0 ||
        !folder.isDir() || folder.isSymLink()) {
        result.error = QStringLiteral("일반 PDF 파일과 대상 폴더가 필요합니다.");
        return result;
    }
    const QString targetRoot = folder.canonicalFilePath();
    const QString originalRoot = source.canonicalPath();
    if (targetRoot.isEmpty() || originalRoot.isEmpty() || targetRoot == originalRoot) {
        result.error = QStringLiteral("같은 폴더로 복사할 수 없습니다.");
        return result;
    }
    const QString targetPdf = QDir(targetRoot).filePath(source.fileName());
    if (!SidecarStorage::safeManagedDirectory(path) ||
        !SidecarStorage::safeManagedDirectory(targetPdf)) {
        result.error = QStringLiteral("관리 폴더 링크를 통해 복사하지 않습니다.");
        return result;
    }
    QList<QPair<QString, QString>> copies;
    copies.append(qMakePair(source.canonicalFilePath(), targetPdf));
    // Copies retain user-cropped thumbnails from either format, but do NOT
    // adopt the original metadata UUID; the new PDF gets its own identity.
    for (const auto& pair : SidecarStorage::companions(path, targetPdf, false)) {
        const QFileInfo sidecar(pair.first), dest(pair.second);
        if (sidecar.isSymLink() || (sidecar.exists() && !sidecar.isFile())) {
            result.error = QStringLiteral("썸네일이 일반 파일이 아닙니다.");
            return result;
        }
        if (dest.exists() || dest.isSymLink()) {
            result.error = QStringLiteral("대상 폴더에 이미 썸네일이 있습니다: ") + pair.second;
            return result;
        }
        if (sidecar.isFile()) copies.append(pair);
    }
    for (const QString& stale : {SidecarStorage::metadata(targetPdf),
                                 SidecarStorage::legacyMetadata(targetPdf)}) {
        if (QFileInfo(stale).exists() || QFileInfo(stale).isSymLink()) {
            result.error=QStringLiteral("대상 폴더에 같은 이름의 서지정보 파일이 있습니다.");
            return result;
        }
    }
    for (const auto& pair : copies) {
        const QFileInfo destination(pair.second);
        if (destination.exists() || destination.isSymLink()) {
            result.error = QStringLiteral("대상 폴더에 이미 같은 이름이 있습니다: ") + pair.second;
            return result;
        }
        if (!SidecarStorage::prepareTarget(targetPdf, pair.second)) {
            result.error = QStringLiteral("대상 LUNA 썸네일 숨김 폴더를 준비할 수 없습니다.");
            return result;
        }
    }
    QStringList created;
    for (const auto& pair : copies) {
        if (!QFile::copy(pair.first, pair.second)) {
            for (const QString& item : created) QFile::remove(item);
            result.error = QStringLiteral("파일 복사 실패 (원본 보존): ") + pair.second;
            return result;
        }
        created.append(pair.second);
    }
    result.success = true;
    result.documentPath = copies.first().second;
    return result;
}

} // namespace luna
