// SPDX-License-Identifier: MIT
#pragma once
// Manual-only Supporting Information attachment. Copy a selected PDF beside
// the original article; NEVER move or modify the selected source PDF.
#include "lunasidecarpaths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>

namespace luna::ManualSI {
struct CopyResult {
    bool success = false;
    QString path;
    QString error;
};

inline QString safeTitle(QString title) {
    title=title.normalized(QString::NormalizationForm_KC).simplified();
    title.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])")),QStringLiteral(" "));
    title=title.simplified();
    while(title.endsWith(QLatin1Char('.')) || title.endsWith(QLatin1Char(' ')))title.chop(1);
    if(title.size()>94) title=title.left(94).trimmed();
    while(title.endsWith(QLatin1Char('.')) || title.endsWith(QLatin1Char(' ')))title.chop(1);
    return title.isEmpty()?QStringLiteral("Supporting Information"):title;
}

inline bool validParentFileName(const QString& name) {
    if(name.isEmpty() || name==QStringLiteral(".") || name==QStringLiteral("..") ||
       name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
       name.contains(QLatin1Char(':')))return false;
    return QFileInfo(name).fileName()==name &&
        QFileInfo(name).suffix().compare(QStringLiteral("pdf"),Qt::CaseInsensitive)==0;
}

inline CopyResult copyNextToParent(const QString& chosenPdf, const QString& articlePdf,
                                    const QString& articleTitle) {
    CopyResult result;
    const QFileInfo input(chosenPdf), parent(articlePdf);
    if(!input.isFile() || input.isSymLink() ||
       input.suffix().compare(QStringLiteral("pdf"),Qt::CaseInsensitive)!=0 ||
       !parent.isFile() || parent.isSymLink() ||
       parent.suffix().compare(QStringLiteral("pdf"),Qt::CaseInsensitive)!=0) {
        result.error=QStringLiteral("원논문과 SI는 일반 PDF 파일이어야 합니다.");
        return result;
    }
    const QString root=parent.canonicalPath();
    const QString source=input.canonicalFilePath();
    if(root.isEmpty() || source.isEmpty() || !QFileInfo(root).isWritable()) {
        result.error=QStringLiteral("선택한 PDF를 읽거나 원논문 폴더에 쓸 수 없습니다.");
        return result;
    }
    const QString stem=QStringLiteral("SI · ")+safeTitle(articleTitle);
    const QDir targetDir(root);
    for(int index=1;index<=999;++index) {
        const QString name=stem+(index==1?QString():QStringLiteral(" (%1)").arg(index))+
            QStringLiteral(".pdf");
        const QString target=targetDir.filePath(name);
        if(QFileInfo(target).exists() || QFileInfo(target).isSymLink())continue;
        // Do not touch any stale metadata/thumbnail belonging to a previously
        // removed PDF with the same name.
        if(!SidecarStorage::safeManagedDirectory(target) ||
           QFileInfo(SidecarStorage::metadata(target)).exists() ||
           QFileInfo(SidecarStorage::legacyMetadata(target)).exists() ||
           QFileInfo(SidecarStorage::thumbnail(target,QStringLiteral(".png"))).exists() ||
           QFileInfo(SidecarStorage::legacyThumbnail(target,QStringLiteral(".png"))).exists())continue;
        if(!QFile::copy(source,target)) {
            result.error=QStringLiteral("SI PDF 복사 실패: ")+target;
            return result;
        }
        result.success=true;
        result.path=target;
        return result;
    }
    result.error=QStringLiteral("SI 파일 이름을 정할 수 없습니다 (동일 이름 또는 메타데이터 충돌).");
    return result;
}
} // namespace luna::ManualSI
