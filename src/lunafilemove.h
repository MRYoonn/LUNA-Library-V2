// SPDX-License-Identifier: MIT
#pragma once
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
    const std::function<bool(const QString&, const QString&)>& rename = renameWithoutReplacement) {
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
    if (targetRoot == originalRoot) { result.success = true; return result; }
    QList<QPair<QString, QString>> moves;
    // Move optional sidecars first and the PDF last. On error, roll back in
    // reverse order without ever overwriting files created by another process.
    for (const QString& extension : {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")}) {
        const QString name = source.completeBaseName() + QStringLiteral(".luna-thumb") + extension;
        const QFileInfo targetSidecar(QDir(targetRoot).filePath(name));
        if (targetSidecar.exists() || targetSidecar.isSymLink()) {
            result.error = QStringLiteral("대상 폴더에 같은 이름의 썸네일이 있어 이동하지 않았습니다.\n%1").arg(targetSidecar.absoluteFilePath());
            return result;
        }
        const QFileInfo sidecar(QDir(originalRoot).filePath(name));
        if (sidecar.isSymLink() || (sidecar.exists() && !sidecar.isFile())) {
            result.error = QStringLiteral("썸네일이 일반 파일이 아니므로 이동하지 않았습니다.");
            return result;
        }
        if (sidecar.exists()) moves.append({sidecar.absoluteFilePath(), QDir(targetRoot).filePath(name)});
    }
    moves.append({source.canonicalFilePath(), QDir(targetRoot).filePath(source.fileName())});
    for (const auto& pair : moves) {
        const QFileInfo target(pair.second);
        if (target.exists() || target.isSymLink()) {
            result.error = QStringLiteral("같은 이름의 대상이 있어 이동하지 않았습니다.\n%1").arg(pair.second);
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
} // namespace luna
