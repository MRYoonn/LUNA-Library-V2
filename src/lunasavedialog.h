// SPDX-License-Identifier: MIT
#pragma once
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <filesystem>
#include <system_error>

namespace luna {
inline bool sameExistingFile(const QString& left, const QString& right) {
    auto path = [](const QString& value) {
#ifdef Q_OS_WIN
        return std::filesystem::path(value.toStdWString());
#else
        return std::filesystem::path(value.toStdString());
#endif
    };
    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(path(left), path(right), error);
    return !error && equivalent;
}

// CI only. Compare existing directory identities and a new leaf name: the new
// PDF cannot have a canonicalFilePath yet. Windows dialogs may expand 8.3 paths
// or change directory casing relative to Python's temporary-directory path.
inline bool selectSyntheticCopy(QFileDialog& dialog, const QString& target, QString& error) {
    auto* filename = dialog.findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
    if (!filename) {
        error = QStringLiteral("Qt Save As filename widget missing");
        return false;
    }
    const QFileInfo expected(target);
    const QString expectedParent = QFileInfo(expected.absolutePath()).canonicalFilePath();
    if (expectedParent.isEmpty() || expected.fileName().isEmpty() || expected.exists()) {
        error = QStringLiteral("Temporary copy target is not a new file in an existing directory");
        return false;
    }
    dialog.setDirectory(expected.absolutePath());
    filename->setText(expected.fileName());
    const QStringList selected = dialog.selectedFiles();
    if (selected.size() != 1) {
        error = QStringLiteral("Qt Save As selected-file count: %1").arg(selected.size());
        return false;
    }
    const QFileInfo actual(selected.front());
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    const bool sameName = actual.fileName().compare(expected.fileName(), sensitivity) == 0;
    const bool sameParent = sameExistingFile(actual.absolutePath(), expected.absolutePath());
    if (!sameName || !sameParent) {
        // Flags only: no personal or machine-specific path is written to logs.
        error = QStringLiteral("Qt Save As target mismatch: filename=%1 directory=%2")
            .arg(sameName).arg(sameParent);
        return false;
    }
    return true;
}
}
