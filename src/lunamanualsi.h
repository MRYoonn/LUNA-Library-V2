// SPDX-License-Identifier: MIT
#pragma once
// Manual-only Supporting Information attachment. PDFs selected from the same
// folder are renamed in place; PDFs from elsewhere are copied, never moved.
#include "lunasidecarpaths.h"
#include "lunafilemove.h"
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
    QString previousPath; // Set only for a same-folder in-place rename.
    bool renamedExisting = false;
    bool reusedExisting = false;
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

inline bool targetIsClean(const QString& target) {
    if(!SidecarStorage::safeManagedDirectory(target))return false;
    const QFileInfo pdf(target);
    if(pdf.exists() || pdf.isSymLink())return false;
    // Check BOTH managed and legacy formats, and all supported thumbnails.
    // A stale sidecar must never be silently assigned to a different PDF.
    for(const QString& p:SidecarStorage::allCompanions(target)) {
        const QFileInfo companion(p);
        if(companion.exists() || companion.isSymLink())return false;
    }
    return true;
}

inline QString chooseTarget(const QString& parentFolder,const QString& title) {
    const QString stem=QStringLiteral("SI · ")+safeTitle(title);
    const QDir dir(parentFolder);
    for(int index=1;index<=999;++index) {
        const QString name=stem+(index==1?QString():QStringLiteral(" (%1)").arg(index))+
            QStringLiteral(".pdf");
        const QString target=dir.filePath(name);
        if(targetIsClean(target))return target;
    }
    return {};
}

// For an already-present local SI, keep its stable location when it already
// follows the desired naming scheme. Never rename the same document twice.
inline bool alreadyNamed(const QString& name,const QString& title) {
    const QString stem=QStringLiteral("SI · ")+safeTitle(title);
    const QRegularExpression pattern(QStringLiteral("^")+
        QRegularExpression::escape(stem)+
        QStringLiteral(R"((?: \(\d+\))?\.pdf$)"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(name).hasMatch();
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
    if(!source.isEmpty() && source==parent.canonicalFilePath()) {
        result.error=QStringLiteral("원논문 자신을 SI로 추가할 수 없습니다.");
        return result;
    }
    if(root.isEmpty() || source.isEmpty() || !QFileInfo(root).isWritable()) {
        result.error=QStringLiteral("선택한 PDF를 읽거나 원논문 폴더에 쓸 수 없습니다.");
        return result;
    }
    const QString target=chooseTarget(root,articleTitle);
    if(target.isEmpty()) {
        result.error=QStringLiteral("SI 파일 이름을 정할 수 없습니다 (동일 이름 또는 메타데이터 충돌).");
        return result;
    }
    if(!QFile::copy(source,target)) {
        result.error=QStringLiteral("SI PDF 복사 실패: ")+target;
        return result;
    }
    result.success=true;
    result.path=target;
    return result;
}


// The UI should use this function, not copyNextToParent directly.
// Same folder: NO duplicate. Rename only the actual PDF first; the caller
// immediately invokes reconcileExplorerRename(old,new,bibliography) to relocate
// its existing .luna metadata/thumbnail, memos, favorites and QSettings ID.
// Different folder: use the original non-overwriting copy operation.
inline CopyResult placeNextToParent(const QString& chosenPdf,
                                    const QString& articlePdf,
                                    const QString& articleTitle) {
    CopyResult result;
    const QFileInfo input(chosenPdf),parent(articlePdf);
    if(!input.isFile() || input.isSymLink() ||
       input.suffix().compare(QStringLiteral("pdf"),Qt::CaseInsensitive)!=0 ||
       !parent.isFile() || parent.isSymLink() ||
       parent.suffix().compare(QStringLiteral("pdf"),Qt::CaseInsensitive)!=0) {
        result.error=QStringLiteral("원논문과 SI는 일반 PDF 파일이어야 합니다.");
        return result;
    }
    const QString source=input.canonicalFilePath();
    const QString article=parent.canonicalFilePath();
    const QString sourceFolder=input.canonicalPath();
    const QString parentFolder=parent.canonicalPath();
    if(source.isEmpty() || article.isEmpty() || source==article ||
       parentFolder.isEmpty()) {
        result.error=QStringLiteral("원논문 자신을 SI로 추가할 수 없습니다.");
        return result;
    }
    if(sourceFolder.compare(parentFolder,Qt::CaseInsensitive)!=0)
        return copyNextToParent(chosenPdf,articlePdf,articleTitle);
    // This file already lives beside its selected original paper.
    if(!SidecarStorage::safeManagedDirectory(source) ||
       !QFileInfo(parentFolder).isWritable()) {
        result.error=QStringLiteral("원논문 폴더 또는 LUNA 관리 파일을 안전하게 수정할 수 없습니다.");
        return result;
    }
    if(alreadyNamed(input.fileName(),articleTitle)) {
        result.success=true;
        result.path=source;
        result.reusedExisting=true;
        return result;
    }
    const QString target=chooseTarget(parentFolder,articleTitle);
    if(target.isEmpty()) {
        result.error=QStringLiteral("SI 파일 이름 또는 숨김 관리 파일이 충돌합니다.");
        return result;
    }
    // Validate existing sidecars before even renaming the source PDF. We
    // intentionally leave them at their old paths until the caller performs
    // the same existing rename-reconciliation used by Windows Explorer.
    for(const QString& p:SidecarStorage::allCompanions(source)) {
        const QFileInfo sidecar(p);
        if(sidecar.isSymLink() || (sidecar.exists() && !sidecar.isFile())) {
            result.error=QStringLiteral("SI의 관리 파일이 일반 파일이 아니므로 이름을 바꾸지 않았습니다.");
            return result;
        }
    }
    if(!renameWithoutReplacement(source,target)) {
        result.error=QStringLiteral("원래 SI 파일의 이름 변경에 실패했습니다. 파일을 닫고 다시 시도하세요.");
        return result;
    }
    result.success=true;
    result.path=target;
    result.previousPath=source;
    result.renamedExisting=true;
    return result;
}
} // namespace luna::ManualSI
