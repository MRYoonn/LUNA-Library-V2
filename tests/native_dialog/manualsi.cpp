// SPDX-License-Identifier: MIT
#include "lunamanualsi.h"
#include "lunabibliography.h"
#include "lunadocumentclaim.h"
#include "lunalocalsync.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QSettings>
#include <QDebug>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;
    auto fail=[](const char* s){qCritical()<<s;return 1;};
    if(!temp.isValid())return fail("Cannot allocate private Windows SI test temp dir");
    const QString root=temp.path();
    const QString parent=QDir(root).filePath(QStringLiteral("original paper.pdf"));
    const QString input=QDir(root).filePath(QStringLiteral("selected SI.pdf"));
    const QByteArray bytes("synthetic PDF: manual attachment fixture");
    auto write=[](const QString& path,const QByteArray& payload){
        QFile f(path);return f.open(QIODevice::WriteOnly|QIODevice::NewOnly) &&
            f.write(payload)==payload.size();};
    auto read=[](const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)
        ?f.readAll():QByteArray();};
    if(!write(parent,"synthetic original paper")||!write(input,bytes))
        return fail("Failed to create original and SI PDF fixtures");
    const QString title=QStringLiteral("C5: Photoredox / Chemical? Study* of <Thiouracils>");
    const QString safe=luna::ManualSI::safeTitle(title);
    if(safe.contains(QLatin1Char('/'))||safe.contains(QLatin1Char('*'))||
       safe.contains(QLatin1Char('?'))||safe.contains(QLatin1Char('<'))||
       safe.contains(QLatin1Char('>'))||safe.isEmpty())
        return fail("Windows SI filename sanitization failed");
    auto first=luna::ManualSI::copyNextToParent(input,parent,title);
    if(!first.success || !QFileInfo::exists(first.path) ||
       read(first.path)!=bytes || read(input)!=bytes ||
       !QFileInfo(first.path).fileName().startsWith(QStringLiteral("SI · ")))
        return fail("SI selection failed to copy into original folder without changing source");
    auto next=luna::ManualSI::copyNextToParent(input,parent,title);
    if(!next.success || next.path==first.path || read(next.path)!=bytes ||
       read(first.path)!=bytes || !QFileInfo(next.path).fileName().contains(QStringLiteral("(2)")))
        return fail("Second SI did not generate a collision-free indexed name");
    if(luna::ManualSI::copyNextToParent(parent,parent,title).success ||
       !QFileInfo::exists(parent))
        return fail("Original PDF must not be selectable as its own SI");
    if(luna::ManualSI::validParentFileName(QStringLiteral("../outside.pdf")) ||
       luna::ManualSI::validParentFileName(QStringLiteral("C:\\evil.pdf")) ||
       !luna::ManualSI::validParentFileName(QFileInfo(parent).fileName()))
        return fail("SI parent name validation accepts directory traversal");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);
    luna::Bibliography bibliography;
    const QString id=luna::DocumentClaim::identity(first.path);
    if(!bibliography.attachManualSI(id,QFileInfo(parent).fileName()))
        return fail("Cannot save manual SI association");
    const QVariantMap record=bibliography.record(id);
    if(!record.value("manualSI").toBool() ||
       record.value("manualSiParentFile").toString()!=QFileInfo(parent).fileName() ||
       record.value("manualSiRelationSource").toString()!=QStringLiteral("user_selected_pdf") ||
       record.value("documentKind").toString()!=QStringLiteral("supporting_information"))
        return fail("SI parent relation missing after settings reload");
    if(!bibliography.detachManualSI(id)||!QFileInfo::exists(first.path)||
       bibliography.record(id).value("manualSI").toBool())
        return fail("Unlink must preserve copied SI PDF");
    // A stale orphan .luna sidecar must block the target name, not be silently
    // adopted or overwritten by a newly copied SI document.
    const QString orphan=QDir(root).filePath(
        QStringLiteral("SI · ")+safe+QStringLiteral(" (3).pdf"));
    const QString meta=luna::SidecarStorage::legacyMetadata(orphan);
    if(!write(meta,"orphan user metadata"))return fail("Cannot make orphan sidecar fixture");
    auto third=luna::ManualSI::copyNextToParent(input,parent,title);
    if(!third.success || QFileInfo(third.path).fileName().contains(QStringLiteral("(3)")) ||
       read(meta)!="orphan user metadata")
        return fail("SI copy overwrote or adopted stale unrelated sidecar");
    // Same-folder user selection must reuse the existing PDF rather than
    // creating another copy, while all existing annotations/metadata follow
    // a different sanitized filename through Explorer's rename reconciliation.
    const QString oldId=luna::DocumentClaim::identity(input);
    const QString oldMeta=luna::SidecarStorage::metadata(input);
    const QString oldThumb=luna::SidecarStorage::thumbnail(input,QStringLiteral(".png"));
    const QByteArray metaBytes("{\"original\":\"research-metadata\",\"memo\":\"keep\"}");
    const QByteArray thumbnailBytes("synthetic saved crop");
    if(!luna::SidecarStorage::ensureArea(input,true) ||
       !luna::SidecarStorage::ensureArea(input,false) ||
       !write(oldMeta,metaBytes) || !write(oldThumb,thumbnailBytes))
        return fail("Cannot prepare existing same-folder SI user data");
    if(!bibliography.save(oldId,QVariantMap{
        {"title",QStringLiteral("SI user-edited NMR description")},
        {"titleSource",QStringLiteral("manual")}},true))
        return fail("Cannot prepare SI manual bibliography");
    const QString noteKey=QStringLiteral("library/memos/")+luna::lunaPathKey(input);
    luna::lunaLocalStore().setValue(noteKey,QStringLiteral("Original SI user memo"));
    const int beforeCount=QDir(root).entryList(
        QStringList{QStringLiteral("*.pdf")},QDir::Files).size();
    const auto renamed=luna::ManualSI::placeNextToParent(input,parent,title);
    if(!renamed.success || !renamed.renamedExisting || renamed.reusedExisting ||
       renamed.previousPath!=QFileInfo(input).absoluteFilePath() ||
       QFileInfo::exists(input) || read(renamed.path)!=bytes ||
       QDir(root).entryList(QStringList{QStringLiteral("*.pdf")},QDir::Files).size()!=beforeCount)
        return fail("Same-folder manual SI created a duplicate or lost original PDF");
    QString renameError;
    if(!luna::reconcileExplorerRename(input,renamed.path,bibliography,&renameError)) {
        qCritical().noquote()<<renameError;
        return fail("Existing SI rename could not reconcile user data");
    }
    if(QFileInfo::exists(oldMeta) || QFileInfo::exists(oldThumb) ||
       read(luna::SidecarStorage::metadata(renamed.path))!=metaBytes ||
       read(luna::SidecarStorage::thumbnail(renamed.path,QStringLiteral(".png")))!=thumbnailBytes ||
       bibliography.record(luna::DocumentClaim::identity(renamed.path))
          .value("title").toString()!=QStringLiteral("SI user-edited NMR description") ||
       luna::lunaLocalStore().value(
          QStringLiteral("library/memos/")+luna::lunaPathKey(renamed.path)).toString()!=
          QStringLiteral("Original SI user memo"))
        return fail("Same-folder rename lost crop, manual bibliography or memo");
    const auto reused=luna::ManualSI::placeNextToParent(renamed.path,parent,title);
    if(!reused.success || !reused.reusedExisting || reused.renamedExisting ||
       reused.path!=renamed.path || QFileInfo::exists(input) ||
       QDir(root).entryList(QStringList{QStringLiteral("*.pdf")},QDir::Files).size()!=beforeCount)
        return fail("Selecting already named same-folder SI must reuse without another rename/copy");
    // Different folder always imports a COPY. The source and its path must
    // stay untouched; same-folder reuse must not leak into this branch.
    const QString externalDir=QDir(root).filePath(QStringLiteral("external-source"));
    if(!QDir().mkpath(externalDir))return fail("Cannot create external SI folder");
    const QString external=QDir(externalDir).filePath(QStringLiteral("outside SI.pdf"));
    if(!write(external,"external SI user PDF"))return fail("Cannot write external SI fixture");
    const auto imported=luna::ManualSI::placeNextToParent(external,parent,title);
    if(!imported.success || imported.renamedExisting || imported.reusedExisting ||
       imported.path==external || read(external)!="external SI user PDF" ||
       read(imported.path)!="external SI user PDF" ||
       QFileInfo(imported.path).absolutePath()!=root)
        return fail("Outside-folder SI must be copied into article folder without moving source");
    qInfo()<<"[PASS] manual SI: same-folder rename/reuse, legacy/hidden metadata and cropped thumbnail, manual edits, memo, outside-folder copy";
    return 0;
}
