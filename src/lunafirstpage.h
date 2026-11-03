// SPDX-License-Identifier: MIT
#pragma once
#include "lunabibliography.h"
#include "lunametadataengine.h"
#include "lunatitleextractor.h"
#include "lunasiidentity.h"
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
inline QVariantMap extractFirstPage(pdf::PDFDocument& document, QString* diagnostic = nullptr,
                                    const QString& fileName = {}) {
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
    QList<TitleExtraction::Line> firstPageLines;
    double firstPageHeight = 0;
    for (int i=0; i<std::min<int>(3, int(catalog->getPageCount())); ++i) {
        const auto* page=catalog->getPage(i);
        if(!page) {pages.append(QString());continue;}
        pdf::PDFTextLayoutGenerator generator(pdf::PDFRenderer::getDefaultFeatures(),page,
            &document,&fonts,&profiles,cms.data(),&activity,QTransform(),
            pdf::PDFMeshQualitySettings());
        generator.processContents();
        const auto layout=generator.createTextLayout();
        struct Line {double x,y;QString text;double fontSize;};
        std::vector<Line> lines;
        const QTransform visual(1,0,0,-1,0,page->getMediaBox().height());
        for(const auto& block:layout.getTextBlocks())
            for(const auto& line:block.getLines()) {
                QString text;
                QList<double> characterSizes;
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
                    if (!current.character.isSpace() && current.fontSize>=5 && current.fontSize<=72)
                        characterSizes.append(current.fontSize);
                }
                std::sort(characterSizes.begin(),characterSizes.end());
                const double textSize=characterSizes.isEmpty()?0.0:characterSizes[characterSizes.size()/2];
                const QPointF position=visual.map(line.getTopLeft());
                lines.push_back({position.x(),position.y(),text,textSize});
            }
        std::stable_sort(lines.begin(),lines.end(),[](const Line& a,const Line& b){
            return a.y != b.y ? a.y < b.y : a.x < b.x;
        });
        QString text;
        for(const auto& row:lines){
            if (i==0) firstPageLines.append({row.text,row.x,row.y,row.fontSize});
            text+=row.text+QLatin1Char('\n');
            if(text.size()>=32000)break;
        }
        if (i==0) firstPageHeight=page->getMediaBox().height();
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
    // Do not classify an ACS main-paper navbar hyperlink as an SI cover.
    if (FrontMatter::isSupplementCover(first))
        return SIIdentity::evidence(first,fileName);
    QVariantMap evidence=Bibliography::firstPageCitation(first);
    const QVariantMap titleLayout=TitleExtraction::infer(firstPageLines,firstPageHeight);
    const QVariantMap textualTitle=Bibliography::firstPageIdentity(first);
    // Publisher-specific text title fallback from scheduled accuracy module:
    // require ACS or RSC masthead, author boundary and strong citation cues.
    const QString acs=FrontMatter::acsTitle(first);
    const QString publisherTitle=acs.isEmpty() ? FrontMatter::rscTitle(first) : acs;
    // A prominent geometric title stays first; conservative publisher titles
    // rescue PDFs whose PDF4QT font/layout extraction has a weak title.
    const QString title=titleLayout.value("title").toString();
    if (!title.isEmpty() && Bibliography::credibleTitle(title)) {
        evidence["title"]=title;
        evidence["titleSource"]=QStringLiteral("first_page_layout");
    } else if (Bibliography::credibleTitle(publisherTitle)) {
        evidence["title"]=publisherTitle;
        evidence["titleSource"]=QStringLiteral("publisher_cover_verified");
    } else if (!textualTitle.value("title").toString().isEmpty()) {
        evidence["title"]=textualTitle.value("title");
        evidence["titleSource"]=QStringLiteral("first_page_verified");
    }
    if (evidence.isEmpty() && !Metadata::acceptableIdentity(candidates)) return {};
    if(const auto* info=document.getInfo(); info && Bibliography::credibleTitle(info->title)) {
        const QString pdfTitle=info->title.simplified();
        // PDF Info /Title can be a stale Word or publisher export name.
        // Keep a strongly identified printed title if they disagree.
        // Do not demote an independently verified publisher-cover title to
        // stale PDF Info, even if their normalized words happen to agree.
        if (evidence.value("titleSource").toString() == QStringLiteral("publisher_cover_verified")) {
            if (!Metadata::compatibleTitle(evidence.value("title").toString(),pdfTitle))
                evidence["titleStatus"]=QStringLiteral("needs_review");
        } else if (evidence.value("title").toString().isEmpty() ||
                   Metadata::compatibleTitle(evidence.value("title").toString(),pdfTitle)) {
            evidence["title"]=pdfTitle;
            evidence["titleSource"]=QStringLiteral("pdf_metadata");
        } else {
            evidence["titleStatus"]=QStringLiteral("needs_review");
        }
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
