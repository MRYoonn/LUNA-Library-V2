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
// Detect visible glyphs whose Unicode mapping was lost by the pinned parser.
// Keep this inside bibliography extraction; rendering/selection remain unchanged.
class BibliographyTextLayout final : public pdf::PDFTextLayoutGenerator {
public:
    using pdf::PDFTextLayoutGenerator::PDFTextLayoutGenerator;
    void enableAudit(double height) { auditHeight_=height; }
    const QVariantList& auditRuns() const { return auditRuns_; }
    bool unreliable(const QPointF& position, double size, const QTransform& visual) const {
        for (const auto& run : unmapped_) {
            const QPointF baseline=visual.map(run.position);
            if (size>=run.size*0.79 && size<=run.size*1.23 &&
                qAbs(position.y()-baseline.y())<=qMax(4.0,size*1.6) &&
                qAbs(position.x()-baseline.x())<=160) return true;
        }
        return false;
    }
protected:
    void performProcessTextSequence(const pdf::TextSequence& sequence, ProcessOrder order) override {
        pdf::PDFTextLayoutGenerator::performProcessTextSequence(sequence,order);
        if (order!=ProcessOrder::BeforeOperation || isContentSuppressed()) return;
        const auto* state=getGraphicState();
        const auto matrix=state->getTextMatrix()*state->getCurrentTransformationMatrix();
        const QPointF position=matrix.map(QPointF());
        // PDF fonts may use size 1 and carry the real point size in Tm/CTM.
        // Match PDFTextLayout::addCharacter's transformed font-size units.
        const double size=matrix.map(QLineF(QPointF(),QPointF(0,state->getTextFontSize()))).length();
        for (const auto& item : sequence.items) {
            const bool missingText=item.cid>0 && item.character.isNull() &&
                (item.isCharacter() || item.isContentStream() || item.isAdvance());
            const bool visibleWhitespace=item.glyph && !item.glyph->isEmpty() && item.character.isSpace();
            if (missingText || visibleWhitespace) {
                unmapped_.push_back({position,size});
                break;
            }
        }
        // Explicit public-fixture audit only: headline text and codepoints,
        // not page/body dumps, file paths or automatic application logging.
        const double y=auditHeight_-position.y();
        if (auditHeight_>0 && y>=0 && y<auditHeight_*0.62 && size>=12 && auditRuns_.size()<80) {
            QString text;
            QVariantList unicode,cids;
            for (const auto& item : sequence.items) {
                if (unicode.size()>=200) break;
                if (!item.isCharacter() && !item.isContentStream() && item.cid==0) continue;
                text+=item.character.isNull()?QChar(0xfffd):item.character;
                unicode.append(int(item.character.unicode())); cids.append(int(item.cid));
            }
            auditRuns_.append(QVariantMap{{"text",text},{"x",position.x()},{"y",y},
                {"fontSize",size},{"sourceFontSize",state->getTextFontSize()},{"unicode",unicode},{"cids",cids}});
        }
    }
private:
    struct Run { QPointF position; double size; };
    std::vector<Run> unmapped_;
    double auditHeight_=0;
    QVariantList auditRuns_;
};
// Worker-owned PDF4QT snapshot; process only the first three pages. Neither PDF
// bytes nor page text leave the computer. Return an evidence-ranked DOI only
// when strong unambiguous front matter supports that paper's identity.
inline QVariantMap extractFirstPage(pdf::PDFDocument& document, QString* diagnostic = nullptr, QVariantMap* audit = nullptr) {
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
        BibliographyTextLayout generator(pdf::PDFRenderer::getDefaultFeatures(),page,
            &document,&fonts,&profiles,cms.data(),&activity,QTransform(),
            pdf::PDFMeshQualitySettings());
        if (audit && i==0) generator.enableAudit(page->getMediaBox().height());
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
        QVariantList headlineLines;
        for(const auto& row:lines){
            if (audit && i==0 && row.fontSize>=12 && row.y<page->getMediaBox().height()*0.62 && headlineLines.size()<80)
                headlineLines.append(QVariantMap{{"text",row.text},{"x",row.x},{"y",row.y},{"fontSize",row.fontSize}});
            if (i==0) firstPageLines.append({row.text,row.x,row.y,row.fontSize,
                generator.unreliable(QPointF(row.x,row.y),row.fontSize,visual)});
            text+=row.text+QLatin1Char('\n');
            if(text.size()>=32000)break;
        }
        if (i==0) firstPageHeight=page->getMediaBox().height();
        if (audit && i==0) *audit={{"lines",headlineLines},{"runs",generator.auditRuns()},{"height",firstPageHeight}};
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
