// SPDX-License-Identifier: MIT
#pragma once
#include "lunabibliography.h"
#include "lunametadataengine.h"
#include "pdfdocument.h"
#include "pdftextlayoutgenerator.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfoptionalcontent.h"
#include <algorithm>

namespace luna {
// Worker-owned PDF4QT snapshot; process only the first three pages. Neither PDF
// bytes nor page text leave the computer. Return an evidence-ranked DOI only
// when strong unambiguous front matter supports that paper's identity.
inline QVariantMap extractFirstPage(pdf::PDFDocument& document, QString* diagnostic = nullptr) {
    const auto* catalog=document.getCatalog();
    if(!catalog || catalog->getPageCount()<1) return {};
    pdf::PDFFontCache fonts(128,128);
    fonts.setDocument(pdf::PDFModifiedDocument(&document,nullptr));
    pdf::PDFCMSManager manager(nullptr);
    manager.setDocument(&document);
    const auto cms=manager.getCurrentCMS();
    pdf::PDFICCProfileCache profiles;
    pdf::PDFOptionalContentActivity activity(&document,pdf::OCUsage::View,nullptr);
    QStringList pages;
    for (int i=0; i<qMin(3,catalog->getPageCount()); ++i) {
        const auto* page=catalog->getPage(i);
        if(!page) {pages.append({});continue;}
        pdf::PDFTextLayoutGenerator generator(pdf::PDFRenderer::getDefaultFeatures(),page,
            &document,&fonts,&profiles,cms.data(),&activity,QTransform(),
            pdf::PDFMeshQualitySettings());
        generator.processContents();
        const auto layout=generator.createTextLayout();
        struct Line {double x,y;QString text;};
        std::vector<Line> lines;
        const QTransform visual(1,0,0,-1,0,page->getMediaBox().height());
        for(const auto& block:layout.getTextBlocks())
            for(const auto& line:block.getLines()) {
                QString text;
                const auto& characters=line.getCharacters();
                for(size_t j=0;j<characters.size();++j){
                    const auto& current=characters[j];
                    if(j && !current.character.isSpace()){
                        const auto& previous=characters[j-1];
                        if(!previous.character.isSpace() &&
                           QLineF(previous.position,current.position).length()>previous.advance*1.2)
                            text+=QLatin1Char(' ');
                    }
                    text+=current.character;
                }
                const QPointF position=visual.map(line.getTopLeft());
                lines.push_back({position.x(),position.y(),text});
            }
        std::stable_sort(lines.begin(),lines.end(),[](const Line& a,const Line& b){
            return a.y != b.y ? a.y < b.y : a.x < b.x;
        });
        QString text;
        for(const auto& row:lines){
            text+=row.text+QLatin1Char('\n');
            if(text.size()>=32000)break;
        }
        pages.append(text);
    }
    const QString first=pages.isEmpty()?QString():pages.first();
    if(diagnostic)*diagnostic=first; // Diagnostics stay local.
    QString infoText;
    if(const auto* info=document.getInfo()) {
        infoText=info->title + QLatin1Char(' ');
        for(const auto& [name,value]:info->extra) {
            const QString key=QString::fromLatin1(name).toLower();
            if(key.contains(QStringLiteral("doi")) ||
               key.contains(QStringLiteral("identifier")) ||
               key.contains(QStringLiteral("keyword")))
                infoText+=value.toString()+QLatin1Char(' ');
        }
    }
    const auto candidates=Metadata::collectCandidates(pages,infoText);
    const auto headings=first.split(QLatin1Char('\n'),Qt::SkipEmptyParts);
    for(qsizetype i=0;i<headings.size() && i<16;++i)
        if(headings[i].contains(QStringLiteral("Supporting Information"),Qt::CaseInsensitive) ||
           headings[i].contains(QStringLiteral("Supplementary Information"),Qt::CaseInsensitive))
            return {{"documentKind",QStringLiteral("supporting_information")},
                    {"doiStatus",QStringLiteral("needs_review")}};
    QVariantMap evidence=Bibliography::firstPageCitation(first);
    if(const auto* info=document.getInfo(); info && Bibliography::credibleTitle(info->title)) {
        evidence["title"]=info->title.simplified();
        evidence["titleSource"]=QStringLiteral("pdf_metadata");
    }
    const auto author=Bibliography::firstPageLastAuthor(first);
    if(!author.isEmpty()) {
        evidence["authors"]=author;
        evidence["authorEvidence"]=QStringLiteral("first-page-terminal-and");
    }
    if(Metadata::acceptableIdentity(candidates)) {
        evidence["doi"]=candidates.first().doi;
        evidence["doiSource"]=candidates.first().location;
        evidence["doiScore"]=candidates.first().score;
    } else if(!candidates.isEmpty()) {
        evidence["doiStatus"]=QStringLiteral("needs_review");
        evidence["doiCandidates"]=QStringList{candidates.first().doi};
    }
    return evidence;
}
}
