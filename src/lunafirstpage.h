// SPDX-License-Identifier: MIT
#pragma once
#include "lunafirstpageevidence.h"
#include "lunametadataengine.h"
#include "lunatitleextractor.h"
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
    return firstPageEvidence(pages, firstPageLines, firstPageHeight, infoText,
        document.getInfo() ? document.getInfo()->title : QString());
}
}
