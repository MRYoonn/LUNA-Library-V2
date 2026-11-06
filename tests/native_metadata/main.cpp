// SPDX-License-Identifier: MIT
#include "../../src/lunametadataengine.h"
#include "../../src/lunametadatafetch.h"
#include "../../src/lunabibliography.h"
#include "../../src/lunatitleextractor.h"
#include "../../src/lunafirstpageevidence.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDebug>
#include <cstdio>
#include <cstdlib>
using namespace luna;
using namespace luna::Metadata;
// Windows Qt qFatal normally routes diagnostics to OutputDebugString rather
// than GitHub Actions' captured stderr, leaving only 0xC0000409 in CTest.
// Print a stable failure identifier and flush BEFORE aborting so failures
// become reproducible without guessing about code or disabling assertions.
static void diagnosticMessage(QtMsgType type, const QMessageLogContext&, const QString& text) {
    const QByteArray bytes=text.toUtf8();
    std::fprintf(stderr, "LUNA_QT[%d]: %s\n",int(type),bytes.constData());
    std::fflush(stderr);
}
static void expect(bool yes,const char* what) {
    if (yes) return;
    std::fprintf(stderr,"LUNA_METADATA_TEST_FAILED: %s\n",what);
    std::fflush(stderr);
    std::abort();
}
static void checkpoint(const char* name) {
    std::fprintf(stderr,"LUNA_METADATA_CHECKPOINT: %s\n",name);
    std::fflush(stderr);
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    qInstallMessageHandler(diagnosticMessage);
    checkpoint("setup");
    QTemporaryDir root;
    expect(root.isValid(),"isolated settings root");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());

    checkpoint("layout-title-no-doi");
    const QList<TitleExtraction::Line> paperCover = {
        {QStringLiteral("Journal of the American Chemical Society"),45,37,9},
        {QStringLiteral("Research Article"),52,67,9},
        {QStringLiteral("Visible-Light-Driven C5 Functionalization of"),86,140,18},
        {QStringLiteral("Thiouracils through Radical Addition"),86,165,17},
        {QStringLiteral("Alice Chen, Bob White and Daniel Green"),84,205,9},
        {QStringLiteral("Department of Chemistry"),85,235,9},
        {QStringLiteral("Abstract"),85,291,9},
        {QStringLiteral("We studied radical reaction pathways."),85,315,9}
    };
    const auto titleEvidence = TitleExtraction::infer(paperCover,792);
    expect(titleEvidence.value("title").toString() ==
           QStringLiteral("Visible-Light-Driven C5 Functionalization of Thiouracils through Radical Addition"),
           "DOI-less scientific cover title captured");
    expect(titleEvidence.value("titleSource").toString()==QStringLiteral("first_page_layout"),
           "first-page title source is explicit and not fake Crossref");
    expect(titleEvidence.value("titleConfidence").toInt()>=86,
           "auto title requires strong relative typography");
    expect(TitleExtraction::infer({
        {QStringLiteral("Supporting Information"),20,30,11},
        {QStringLiteral("A Photocatalytic Synthesis of Heterocycles"),40,150,19},
        {QStringLiteral("Supplementary experiments and characterization"),40,220,9},
        {QStringLiteral("Department of Chemistry"),40,245,9}
    },792).isEmpty(), "SI parent article title cannot be assigned to the SI PDF");
    expect(TitleExtraction::infer({
        {QStringLiteral("Introduction to Radical Addition Chemistry"),60,100,9},
        {QStringLiteral("We discuss experiments and the product distribution."),60,135,9},
        {QStringLiteral("The synthetic methods were studied."),60,171,9},
        {QStringLiteral("The reaction was performed at room temperature."),60,205,9}
    },792).isEmpty(), "body/Introduction cannot be promoted to a title");
    const auto ambiguous = TitleExtraction::infer({
        {QStringLiteral("Publishing header"),40,40,9},
        {QStringLiteral("Photoredox Functionalization of Heteroarenes"),82,140,18},
        {QStringLiteral("Chemoselective Catalytic Radical Transformations"),82,300,18},
        {QStringLiteral("Authors and affiliations"),40,390,9},
        {QStringLiteral("Summary of experimental procedures"),40,425,9}
    },792);
    expect(ambiguous.value("title").toString().isEmpty() &&
           ambiguous.value("titleStatus").toString()==QStringLiteral("needs_review"),
           "competing title headings must not autosave");
    const auto weak = TitleExtraction::infer({
        {QStringLiteral("Publisher header text"),40,25,9},
        {QStringLiteral("Potentially Important Study of Heteroarenes"),62,140,12.5},
        {QStringLiteral("A Smith, B Brown and C Green"),62,200,9},
        {QStringLiteral("Department of Chemistry"),62,249,9},
        {QStringLiteral("Abstract"),62,285,9}
    },792);
    expect(weak.value("title").toString().isEmpty() &&
           weak.value("titleStatus").toString()==QStringLiteral("needs_review"),
           "weak title typography must require review");
    const auto chemistry = TitleExtraction::infer({
        {QStringLiteral("Journal header text"),20,30,9},
        {QStringLiteral("β-C–H and α-C–C Activation in Thiouracils"),60,140,19},
        {QStringLiteral("Alice Brown and Alice Green"),60,200,9},
        {QStringLiteral("Department of Chemistry"),60,250,9},
        {QStringLiteral("Abstract"),60,300,9}
    },792);
    expect(chemistry.value("title").toString().contains(QStringLiteral("β-C–H")) &&
           chemistry.value("title").toString().contains(QStringLiteral("α-C–C")),
           "chemistry glyphs in printed title must be preserved");
    Bibliography titleStore;
    const QString titleIdentity=QStringLiteral("synthetic:title:no-doi");
    expect(titleStore.enrichFromFirstPage(titleIdentity,titleEvidence),
           "verified geometry title saved without DOI");
    expect(titleStore.record(titleIdentity).value("title").toString()==titleEvidence.value("title").toString(),
           "saved geometry title is readable offline");
    expect(titleStore.record(titleIdentity).value("titleSource").toString()==QStringLiteral("first_page_layout"),
           "geometry title provenance survives persistence");
    const QString manualIdentity=QStringLiteral("synthetic:title:manual");
    expect(titleStore.saveEdits(manualIdentity,{},{{"title",QStringLiteral("My corrected article title")}}),
           "manual title before automatic scan");
    expect(!titleStore.enrichFromFirstPage(manualIdentity,titleEvidence) &&
           titleStore.record(manualIdentity).value("title").toString()==QStringLiteral("My corrected article title"),
           "manual title must never be overwritten by new extractor");
    const QString staleIdentity=QStringLiteral("synthetic:title:stale-info");
    expect(titleStore.enrichFromFirstPage(staleIdentity,
        {{"title",QStringLiteral("Publisher PDF export title")},{"titleSource",QStringLiteral("pdf_metadata")}}),
        "legacy embedded title saved");
    expect(titleStore.enrichFromFirstPage(staleIdentity,titleEvidence) &&
           titleStore.record(staleIdentity).value("title").toString()==titleEvidence.value("title").toString(),
           "strong printed title corrects a conflicting machine-owned PDF metadata title");
    checkpoint("v262-real-pdf-failure-patterns");
    const QString corruptTitle=QStringLiteral("Effectiveness of Animal Models in Human Diseasesâx80x94A Review of Models");
    const QString printedTitle=QStringLiteral("Effectiveness of Animal Models in Human Diseases—A Review of Models");
    expect(titleStore.enrichFromFirstPage(QStringLiteral("synthetic:v262:mojibake"),
        {{"title",corruptTitle},{"titleSource",QStringLiteral("pdf_metadata")}}),
        "legacy embedded title fixture persisted");
    expect(titleStore.enrichFromFirstPage(QStringLiteral("synthetic:v262:mojibake"),
        {{"title",printedTitle},{"titleSource",QStringLiteral("first_page_layout")}}) &&
        titleStore.record(QStringLiteral("synthetic:v262:mojibake")).value("title").toString()==printedTitle,
        "strong printed title corrects similar-prefix mojibake instead of treating it as compatible");
    expect(titleStore.enrichFromFirstPage(QStringLiteral("synthetic:v262:range"),{{"page",QStringLiteral("3408")}}),
        "legacy start page fixture persisted");
    expect(titleStore.enrichFromFirstPage(QStringLiteral("synthetic:v262:range"),{{"page",QStringLiteral("3408-3427")}}) &&
        titleStore.record(QStringLiteral("synthetic:v262:range")).value("page").toString()==QStringLiteral("3408-3427"),
        "complete printed range upgrades same-start machine-owned single page");
    expect(FrontMatter::isSupplementCover(QStringLiteral("Supplementary Information for:\nA printed title")),
        "Nature SI colon heading is recognized");
    expect(FrontMatter::isSupplementCover(QStringLiteral("Supplement to: A printed title\nAlice Green")),
        "Nature explicit supplement-to heading is recognized");
    expect(!FrontMatter::isSupplementCover(QStringLiteral("Article\nSupplementary\nWe discuss information in Table 1")),
        "body supplement mention is not a cover");
    expect(Bibliography::firstPageLastAuthor(QStringLiteral(
        "Article\nAlice Green1,2, Brian White3,\nReceived: 1 January 2024\n"
        "Carol Smith4 & Daniel Brown\n1,2\nAbstract\n"))==QStringLiteral("Daniel Brown"),
        "numeric affiliation markers cannot hide the preceding Nature author list");
    const auto sideColumn = TitleExtraction::infer({
        {QStringLiteral("Chemical Science"),40,35,10},
        {QStringLiteral("Organelle-Targeting Ratiometric Fluorescent Probes"),172,130,16},
        {QStringLiteral("Design Principles and Detection Mechanisms"),172,149,16},
        {QStringLiteral("Cite this: Chem. Sci., 2024, 15, 3408"),42,165,7},
        {QStringLiteral("Bio-Applications and Challenges"),172,167,16},
        {QStringLiteral("Alice Green and Brian White"),172,205,10},
        {QStringLiteral("Department of Chemistry"),172,245,10},
        {QStringLiteral("Abstract"),172,280,10}
    },792);
    expect(sideColumn.value("title").toString().endsWith(QStringLiteral("Bio-Applications and Challenges")),
        "RSC citation sidebar must not truncate the three-line title in the other column");
    const auto edgeArticle = TitleExtraction::infer({
        {QStringLiteral("Chemical"),41,32,20.92},
        {QStringLiteral("Science"),41,53,20.92},
        {QStringLiteral("EDGE ARTICLE"),51,92,15.94},
        {QStringLiteral("Fused 3D boron heterocycles via EnT catalysis:"),173,131,15.94},
        {QString::fromUtf8("synthesis, modiﬁcation and validation as beta-"),173,149,15.94},
        {QStringLiteral("Cite this: Chem. Sci., 2026, 17, 247"),42,164,7.22},
        {QStringLiteral("lactamase inhibitors"),173,167,15.94},
        {QStringLiteral("All publication charges for this article"),50,182,7.22},
        {QStringLiteral("Alice Green, Brian White, and John J. Molloy"),173,194,9.96},
        {QStringLiteral("Department of Chemistry"),173,220,7.97},
        {QStringLiteral("Abstract"),173,245,7.97}
    },779.528);
    expect(edgeArticle.value("title").toString()==QStringLiteral(
        "Fused 3D boron heterocycles via EnT catalysis: synthesis, modification and validation as beta-lactamase inhibitors"),
        "RSC EDGE ARTICLE is a header; mapped fi ligatures preserve exact printed words");
    expect(Bibliography::firstPageLastAuthor(QStringLiteral(
        "Enzyme Technologies and Industrial Applications\n"
        "Alice Green, Brian White, and Hassan Mohamed El-Said Azzazy*\n"
        "ABSTRACT: A study of materials\n"))==QStringLiteral("Hassan Mohamed El-Said Azzazy"),
        "capitalized title ending in and-phrase cannot compete with actual final author");
    const auto serialCandidates=collectCandidates({QStringLiteral("https://doi.org/10.1002/anie.202412622")},
        QStringLiteral("doi:10.1002/(ISSN)1521-3773"));
    expect(acceptableIdentity(serialCandidates) && serialCandidates.first().doi==QStringLiteral("10.1002/anie.202412622"),
        "journal ISSN DOI cannot displace explicit printed article DOI");
    const QList<TitleExtraction::Line> damagedLines={
        {QStringLiteral("Article"),40,80,9},
        {QStringLiteral("Effectiveness of Zebra sh Models in Human Diseases"),40,130,18,true},
        {QStringLiteral("Alice Green and Brian White"),40,205,9},
        {QStringLiteral("Department of Chemistry"),40,250,9},
        {QStringLiteral("Abstract"),40,290,9}
    };
    expect(TitleExtraction::infer(damagedLines,792).value("title").toString().isEmpty(),
        "visible unmapped glyph cannot become an automatically trusted heading");
    const auto damagedEvidence=firstPageEvidence({QStringLiteral("Article\nhttps://doi.org/10.1016/j.heliyon.2024.e28357")},damagedLines,792,{},QString());
    expect(damagedEvidence.value("title").toString().isEmpty() &&
        damagedEvidence.value("titleGlyphStatus").toString()==QStringLiteral("unmapped"),
        "damaged title suppresses every publisher or metadata fallback");
    const QString damagedIdentity=QStringLiteral("synthetic:v262:unmapped");
    titleStore.enrichFromFirstPage(damagedIdentity,{{"title",QStringLiteral("Effectiveness of Zebra sh Models in Human Diseases")},
        {"titleSource",QStringLiteral("first_page_layout")}});
    titleStore.enrichFromFirstPage(damagedIdentity,damagedEvidence);
    expect(titleStore.record(damagedIdentity).value("title").toString().isEmpty() &&
        !titleStore.record(damagedIdentity).value("legacyTitle").toString().isEmpty(),
        "old machine title with known glyph loss is archived for review");
    auto conflictLines=damagedLines;
    conflictLines[1].unreliable=false;
    const auto conflict=firstPageEvidence({QStringLiteral("Article\nhttps://doi.org/10.1016/j.heliyon.2023.e14557")},
        conflictLines,792,{},QStringLiteral("Effectiveness of Zebrafish Models in Understanding Human Diseases—A Review of Models"));
    expect(conflict.value("title").toString().isEmpty() &&
        conflict.value("titleStatus").toString()==QStringLiteral("needs_review") &&
        conflict.value("titleConflict").toString()==QStringLiteral("embedded_metadata"),
        "conflicting printed and metadata headlines must not return a trusted title alongside needs_review");
    expect(Bibliography::firstPageLastAuthor(QStringLiteral(
        "Article\nAlice Green 1,2, Brian White 3,\n"
        "Carol Smith 4 & Daniel Brown 1,2\nAbstract\n"))==QStringLiteral("Daniel Brown"),
        "PDF4QT spaces and comma-separated final affiliation numbers do not hide the last name");
    expect(Bibliography::firstPageLastAuthor(QStringLiteral(
        "Article\nAlice Green, Brian White and Daniel Brown,\nCarol Smith\nAbstract\n")).isEmpty(),
        "a dangling comma after a candidate is not a completed final author");
    expect(Bibliography::firstPageLastAuthor(QStringLiteral(
        "Article\nAlice Green 1,2, Brian White 3,\n& Eleonora Aronica 11,12\n"
        "Rob C. I. Wüst 3,4,15\nAbstract\n")).isEmpty(),
        "floating Nature ampersand cannot establish the wrong neighboring author as last");
    const auto shortFinal = TitleExtraction::infer({
        {QStringLiteral("Article"),40,90,10},
        {QStringLiteral("Analysis of DIA Proteomics Data Using"),40,113,26},
        {QStringLiteral("MSFragger-DIA and FragPipe Computational"),40,141,26},
        {QStringLiteral("Platform"),40,169,26},
        {QStringLiteral("Alice Green and Bob White"),40,230,9},
        {QStringLiteral("Department of Chemistry"),40,270,9},
        {QStringLiteral("Abstract"),40,300,9}
    },792);
    expect(shortFinal.value("title").toString().endsWith(QStringLiteral("Platform")),
        "short last title line cannot be silently dropped");
    const QString hel = QStringLiteral("Heliyon 10 (2024) e28357\nContents lists available at ScienceDirect\n"
        "Heliyon\njournal homepage: www.cell.com/heliyon\nReview article\n"
        "https://doi.org/10.1016/j.heliyon.2024.e28357\n");
    const QString helAuthors=hel + QStringLiteral(
        "Printed Title for a Public Study\nAlice Green a,*, Brian White b,c,\n"
        "Carol Smith a, Daniel Brown b\na Department of Chemistry\nA B S T R A C T\n");
    expect(Bibliography::heliyonLastAuthor(helAuthors)==QStringLiteral("Daniel Brown"),
        "Heliyon comma-delimited last author confirmed by affiliation boundary");
    expect(Bibliography::heliyonLastAuthor(hel+QStringLiteral(
        "A Printed Title\nAlice Green a, Brian White b,\nCarol Smith a")).isEmpty(),
        "No affiliation boundary: incomplete Elsevier author list must abstain");
    expect(Bibliography::heliyonLastAuthor(QStringLiteral(
        "Other article\nAlice Green, Brian White\nDepartment of Chemistry")).isEmpty(),
        "No Heliyon publisher header: do not infer last author from arbitrary prose");
    const auto helInfo = Bibliography::firstPageCitation(hel);
    auto helRecord=helInfo;
    helRecord["authors"]=Bibliography::heliyonLastAuthor(helAuthors);
    helRecord["authorEvidence"]=QStringLiteral("publisher-bounded-author-block");
    expect(titleStore.enrichFromFirstPage(QStringLiteral("synthetic:hel:stored"),helRecord),
        "bounded Elsevier fields accepted by actual persistence path");
    Bibliography reopened;
    const auto storedHel=reopened.record(QStringLiteral("synthetic:hel:stored"));
    expect(storedHel.value("authors").toString()==QStringLiteral("Daniel Brown") &&
        storedHel.value("article_number").toString()==QStringLiteral("e28357") &&
        Bibliography::citation(storedHel).contains(QStringLiteral("e28357")),
        "Elsevier last author and e-article number persist and appear in offline citation");
    expect(titleStore.saveEdits(QStringLiteral("synthetic:hel:stored"),storedHel,
        {{"authors",QStringLiteral("Manual Final Author")},{"article_number",QStringLiteral("e99999")}}),
        "manual Elsevier author and article correction saved");
    titleStore.enrichFromFirstPage(QStringLiteral("synthetic:hel:stored"),helRecord);
    expect(reopened.record(QStringLiteral("synthetic:hel:stored")).value("authors").toString()==QStringLiteral("Manual Final Author") &&
        reopened.record(QStringLiteral("synthetic:hel:stored")).value("article_number").toString()==QStringLiteral("e99999"),
        "new publisher evidence cannot overwrite user author or article-number edits");
    expect(helInfo.value("article_number").toString()==QStringLiteral("e28357") &&
        helInfo.value("volume").toString()==QStringLiteral("10"),
        "Heliyon printed header retains distinct article number");
    expect(Bibliography::firstPageCitation(QStringLiteral(
        "Unrelated Article\nReferences\nHeliyon 10 (2024) e28357")).isEmpty(),
        "Heliyon reference without publisher cover is rejected");
    const auto acsOmega = Bibliography::firstPageCitation(QStringLiteral(
        "http://pubs.acs.org/journal/acsodf\nReview\nCite This: ACS Omega 2023, 8, 5184−5196"));
    expect(acsOmega.value("page").toString()==QStringLiteral("5184-5196"),
        "ACS Omega complete printed range recovered");
    const auto incomplete = Bibliography::firstPageCitation(QStringLiteral("Cite this: Chem. Sci., 2024, 15, 3408"));
    expect(incomplete.value("year").toString()==QStringLiteral("2024") && !incomplete.contains("page"),
        "RSC starting page must not masquerade as full page range");
    const auto stalePdf = firstPageEvidence({hel}, {}, 792, QString(),
        QStringLiteral("A Completely Unrelated Exporter Document Title"));
    expect(stalePdf.value("title").toString().isEmpty() &&
        stalePdf.value("titleCandidate").toString().contains(QStringLiteral("Unrelated")),
        "uncorroborated embedded metadata stays review-only");
    checkpoint("v261-evidence-ranked-bibliography");
    const QString natureArticle=QStringLiteral(
        "Nature Synthesis\nResearch Article\n"
        "Photoredox Catalysis Enables Chemoselective Heterocycle\n"
        "Functionalization under Mild Conditions\n"
        "Alice Q. Smith, Brian R. Chen and Carol J. Green\n"
        "https://doi.org/10.1038/s44160-026-00001-x\nAbstract\n");
    expect(FrontMatter::natureArticleTitle(natureArticle)==QStringLiteral(
        "Photoredox Catalysis Enables Chemoselective Heterocycle Functionalization under Mild Conditions"),
        "Nature cover font-neutral title verified by publisher + article label + author boundary");
    expect(FrontMatter::natureArticleTitle(QStringLiteral(
        "Research Article\nPhotoredox Catalysis Enables Chemoselective Heterocycle\n"
        "Functionalization under Mild Conditions\n"
        "Alice Q. Smith, Brian R. Chen and Carol J. Green\n")).isEmpty(),
        "No publisher masthead: do not infer article header title");
    expect(FrontMatter::natureArticleTitle(QStringLiteral(
        "Nature Synthesis\nResearch Article\n"
        "Photoredox Catalysis Enables Chemoselective Heterocycle\n"
        "Abstract\nChemistry details follow\n")).isEmpty(),
        "Missing author boundary: do not promote abstract prose");
    expect(FrontMatter::natureArticleTitle(QStringLiteral(
        "Nature Synthesis\nSupporting Information\n"
        "Research Article\nPhotoredox Catalysis Enables Chemoselective Heterocycle\n"
        "Alice Q. Smith, Brian R. Chen and Carol J. Green\n")).isEmpty(),
        "Supporting Information cover never inherits proposed parent title");
    const QString splitTerminal=QStringLiteral(
        "Chem. Sci. 2026, 17, 247\nPhotoredox Radical Transformations\n"
        "Alice Q. Smith, Brian R. Chen,\nand\nJohn J. Molloy*ab\n"
        "Department of Chemistry\nAbstract\n");
    expect(Bibliography::firstPageLastAuthor(splitTerminal)==QStringLiteral("John J. Molloy"),
        "RSC split explicit terminal-and recovers true last-listed author");
    expect(Bibliography::firstPageLastAuthor(QStringLiteral(
        "Chem. Sci. 2026, 17, 247\nAlice Q. Smith, Brian R. Chen\n"
        "Department of Chemistry\nAbstract\n")).isEmpty(),
        "Missing explicit final author cannot be guessed from first names");
    const QString reviewIdentity=QStringLiteral("synthetic:v261:review");
    expect(weak.value("titleStatus").toString()==QStringLiteral("needs_review") &&
           !weak.value("titleCandidate").toString().isEmpty(),
           "Low typography confidence returns only candidate");
    expect(titleStore.enrichFromFirstPage(reviewIdentity, weak),
           "Ambiguous headline can be stored as review-only");
    const auto reviewRecord=titleStore.record(reviewIdentity);
    expect(reviewRecord.value("title").toString().isEmpty() &&
           reviewRecord.value("titleCandidate")==weak.value("titleCandidate") &&
           reviewRecord.value("titleCandidateSource")==QStringLiteral("first_page_layout_review"),
           "Review-only headline never becomes public title or automatic filename");
    QVariantMap selected=reviewRecord;
    selected["title"]=reviewRecord.value("titleCandidate");
    expect(titleStore.saveEdits(reviewIdentity,reviewRecord,selected),
           "Explicit human acceptance saves proposed title");
    expect(titleStore.record(reviewIdentity).value("titleSource")==QStringLiteral("manual") &&
           titleStore.record(reviewIdentity).value("title")==reviewRecord.value("titleCandidate"),
           "Accepted title becomes a protected manual field");
    expect(!titleStore.enrichFromFirstPage(reviewIdentity,titleEvidence) &&
           titleStore.record(reviewIdentity).value("title")==reviewRecord.value("titleCandidate"),
           "Automatic first-page extraction never overwrites manually accepted title");
    const QString manualAuthorIdentity=QStringLiteral("synthetic:v261:manual-author");
    expect(titleStore.saveEdits(manualAuthorIdentity,{},{
           {"authors",QStringLiteral("Verified User Edited Author")}}),
           "Explicit human author override");
    expect(titleStore.enrichFromFirstPage(manualAuthorIdentity,{
           {"authors",QStringLiteral("John J. Molloy")},
           {"authorEvidence",QStringLiteral("first-page-terminal-and")}})==false &&
           titleStore.record(manualAuthorIdentity).value("authors")==QStringLiteral("Verified User Edited Author"),
           "Strong printed author evidence never overwrites explicit manual author");

    checkpoint("doi-evidence");
    const auto docs=collectCandidates({QStringLiteral(
        "Title of paper\nDOI: https://doi.org/10.1021/jacs.4c05881\nAuthors")},
        QString(),QStringLiteral("another-10.1111_fake.pdf"));
    expect(acceptableIdentity(docs) && docs.first().doi==QStringLiteral("10.1021/jacs.4c05881"),
           "first page explicit DOI");
    expect(!acceptableIdentity(collectCandidates({QStringLiteral(
        "References\nDOI: 10.1021/jo.1c00001")})),
           "reference-only DOI must not be accepted");
    expect(!acceptableIdentity(collectCandidates({}, {}, QStringLiteral("10.1021_jacs.4c05881.pdf"))),
           "filename-only DOI must not be accepted");
    expect(!acceptableIdentity(collectCandidates({QStringLiteral(
        "doi: 10.1021/jacs.4c05881 and doi: 10.1021/jacs.4c05882")})),
           "multiple DOI ambiguity");
    expect(normalizeDoi(QStringLiteral("https://doi.org/10.1021/JACS.4C05881."))==
           QStringLiteral("10.1021/jacs.4c05881"),"normalization");

    checkpoint("crossref-citation");
    const QString doi=QStringLiteral("10.1021/jacs.4c05881");
    // Match Crossref's actual HTTP JSON structure, not nested C++ list
    // constructors. They previously produced a different date-parts shape on
    // MSVC while all other synthetic source tests appeared to pass.
    const QByteArray crossrefJson = R"json({
        "DOI": "10.1021/jacs.4c05881",
        "title": ["Synthetic N-H Activation Study"],
        "container-title": ["Journal of the American Chemical Society"],
        "short-container-title": ["J. Am. Chem. Soc."],
        "published": {"date-parts": [[2024, 7, 1]]},
        "volume": "146",
        "page": "20349-20356",
        "author": [
            {"given": "Robert R.", "family": "Knowles"},
            {"given": "Abigail G.", "family": "Doyle"}
        ]
    })json";
    QJsonParseError crossrefParse;
    const QJsonDocument crossrefDocument = QJsonDocument::fromJson(crossrefJson, &crossrefParse);
    expect(crossrefParse.error==QJsonParseError::NoError && crossrefDocument.isObject(),
           "canonical Crossref JSON fixture parses as object");
    const QJsonObject cr = crossrefDocument.object();
    if(cr.value(QStringLiteral("published")).toObject()
         .value(QStringLiteral("date-parts")).toArray().isEmpty()) {
        const QByteArray dump=QJsonDocument(cr).toJson(QJsonDocument::Compact);
        std::fprintf(stderr,"LUNA_CROSSREF_FIXTURE: %s\n",dump.constData());
        std::fflush(stderr);
    }
    const QJsonArray publishedParts = cr.value(QStringLiteral("published")).toObject()
        .value(QStringLiteral("date-parts")).toArray();
    expect(!publishedParts.isEmpty() && !publishedParts.first().toArray().isEmpty(),
           "synthetic Crossref publication date-parts shape");
    expect(publishedParts.first().toArray().first().toVariant().toString()==QStringLiteral("2024"),
           "synthetic Crossref year must be present and numeric");
    expect(yearFromCrossref(cr)==QStringLiteral("2024"),
           "Crossref nested date-parts publication year");
    const auto readFixture=[](const char* json) -> QJsonObject {
        QJsonParseError parse;
        const auto doc=QJsonDocument::fromJson(QByteArray(json),&parse);
        expect(parse.error==QJsonParseError::NoError && doc.isObject(),
               "date fixture must be valid JSON");
        return doc.object();
    };
    expect(yearFromCrossref(readFixture(R"json({"issued":{"date-parts":[[2023]]}})json")) ==
           QStringLiteral("2023"), "Crossref issued year-only date-parts");
    expect(yearFromCrossref(readFixture(R"json({"published-online":{"date-time":"2022-07-11T00:00:00Z"}})json")) ==
           QStringLiteral("2022"), "Crossref ISO date-time year");
    expect(yearFromCrossref(readFixture(R"json({"created":{"date-parts":[[2021]]}})json")).isEmpty(),
           "Crossref created timestamp is not a publication year");
    expect(yearFromCrossref(readFixture(R"json({"published":{"date-parts":[["2024"]]}})json")) ==
           QStringLiteral("2024"), "Crossref string-valued publication year");
    expect(yearFromCrossref(readFixture(R"json({"issued":{"date-parts":[[0,1,1]]}})json")).isEmpty(),
           "invalid publication year must remain empty");

    expect(fromCrossref(cr,QStringLiteral("10.1000/wrong")).isEmpty(),"Crossref DOI mismatch rejected");
    auto online=fromCrossref(cr,doi);
    expect(online.value("authors_structured").toStringList().size()==2,"author list preserved");
    const QString expectedCitation = QStringLiteral(
        "Abigail G. Doyle, J. Am. Chem. Soc. 2024, 146, 20349") +
        QChar(0x2013) + QStringLiteral("20356.");
    const QString actualCitation = labCitation(online);
    if (actualCitation != expectedCitation) {
        const QByteArray actualUtf8 = actualCitation.toUtf8();
        const QByteArray expectedUtf8 = expectedCitation.toUtf8();
        std::fprintf(stderr, "LUNA_CITATION_ACTUAL: %s\n", actualUtf8.constData());
        std::fprintf(stderr, "LUNA_CITATION_EXPECTED: %s\n", expectedUtf8.constData());
        for(const char* key : {"journal", "journal_abbreviation", "year", "volume",
                               "page", "authors", "doi"}) {
            const QByteArray value = online.value(QString::fromLatin1(key)).toString().toUtf8();
            std::fprintf(stderr, "LUNA_CITATION_FIELD[%s]: %s\n", key, value.constData());
        }
        std::fflush(stderr);
    }
    expect(actualCitation == expectedCitation, "required lab citation string");
    expect(officialAbbreviation(QStringLiteral("Unknown Chemistry Review"))==
           QStringLiteral("Unknown Chemistry Review"),"unknown journal never invented");
    online["senior_author_override"]=QStringLiteral("Robert R. Knowles");
    expect(labCitation(online).startsWith(QStringLiteral("Robert R. Knowles, ")),
           "senior author manual override");
    online.remove("senior_author_override");
    online["senior_author_index"]=0;
    expect(labCitation(online).startsWith(QStringLiteral("Robert R. Knowles, ")),
           "senior author explicit index");
    online.remove("senior_author_index");

    checkpoint("openalex-parser");
    // OpenAlex is received over HTTP as ordinary JSON, so test the real
    // nested source/authorships/biblio shape rather than Qt brace coercions.
    const QJsonObject alex = readFixture(R"json({
        "doi": "https://doi.org/10.1021/jacs.4c05881",
        "title": "Synthetic N-H Activation Study",
        "publication_year": 2024,
        "biblio": {"volume": "146", "first_page": "20349", "last_page": "20356"},
        "primary_location": {"source": {
            "display_name": "Journal of the American Chemical Society"
        }},
        "authorships": [{"author": {"display_name": "Abigail G. Doyle"}}]
    })json");
    expect(alex.value(QStringLiteral("biblio")).toObject()
               .value(QStringLiteral("first_page")).toString()==QStringLiteral("20349"),
           "OpenAlex HTTP fixture biblio hierarchy");
    expect(alex.value(QStringLiteral("authorships")).toArray().size()==1,
           "OpenAlex HTTP fixture authorships hierarchy");
    expect(fromOpenAlex(alex,doi).value("page").toString()==(QStringLiteral("20349") + QChar(0x2013) + QStringLiteral("20356")),
           "OpenAlex verified fallback page range");
    expect(fromOpenAlex(alex,QStringLiteral("10.1000/else")).isEmpty(),
           "OpenAlex mismatch rejected");
    expect(articleNumberValid(QStringLiteral("e20591"),doi),"article number usable");
    expect(!articleNumberValid(doi,doi),"DOI is not an article number");

    checkpoint("qsettings-merge");
    Bibliography store;
    const QString identity=QStringLiteral("synthetic:test:one");
    expect(store.mergeVerifiedOnline(identity,doi,online),"persist verified record");
    expect(Bibliography::citation(store.record(identity))==expectedCitation,
        "saved citation remains formatted");
    QVariantMap existing=store.record(identity);
    existing["journal"]=QStringLiteral("Manually Fixed Journal");
    existing["journalSource"]=QStringLiteral("manual");
    existing["senior_author_override"]=QStringLiteral("Robert R. Knowles");
    expect(store.save(identity,existing,false),"persist manual fields");
    expect(store.mergeVerifiedOnline(identity,doi,online),"rerun without overwrite");
    auto after=store.record(identity);
    expect(after.value("journal")==QStringLiteral("Manually Fixed Journal"),
           "manual journal never overwritten");
    expect(Bibliography::citation(after).contains(QStringLiteral("Manually Fixed Journal")),
           "manual journal must outrank stale provider abbreviation on displayed card");
    expect(after.value("senior_author_override")==QStringLiteral("Robert R. Knowles"),
           "senior override remains");
    expect(store.recordLookupFailure(identity,QStringLiteral("offline")),
           "record offline error without data loss");
    expect(!Bibliography::citation(store.record(identity)).isEmpty(),"offline cached citation");

    checkpoint("explicit-original-refresh");
    auto edited = store.record(identity);
    edited["title"] = QStringLiteral("User modified title");
    edited["titleSource"] = QStringLiteral("manual");
    edited["citation"] = QStringLiteral("User pasted reference");
    edited["citationSource"] = QStringLiteral("manual");
    expect(store.save(identity, edited, false), "save manually edited title and citation");
    expect(!store.restoreVerifiedOnline(identity, QStringLiteral("10.1000/mismatch"), online),
           "reject non-matching DOI refresh");
    expect(store.record(identity).value("title").toString() == QStringLiteral("User modified title"),
           "mismatched DOI must never erase user title");
    expect(store.restoreVerifiedOnline(identity, doi, online), "explicit DOI refresh persisted");
    const auto restored = store.record(identity);
    expect(restored.value("title") == online.value("title"),
           "explicit refresh restores verified original title");
    expect(restored.value("journal") == online.value("journal"),
           "explicit refresh restores provider journal");
    expect(restored.value("citation").toString().isEmpty(),
           "pasted citation cannot shadow refreshed fields");
    expect(restored.value("senior_author_override").toString().isEmpty(),
           "explicit refresh clears manually chosen citation override");
    const QString copiedIdentity = QStringLiteral("synthetic:test:copied");
    expect(store.cloneTo(identity, copiedIdentity), "clone bibliography for physical PDF copy");
    expect(store.record(copiedIdentity).value("title") == online.value("title"),
           "cloned bibliography retains verified title");
    expect(!store.cloneTo(identity, copiedIdentity), "cloning cannot overwrite destination");
    const QString localIdentity = QStringLiteral("synthetic:test:firstpage");
    expect(store.saveEdits(localIdentity, {}, {
               {"title", QStringLiteral("Manually typed title")}}), "local edit prior to recovery");
    expect(store.restoreFromFirstPage(localIdentity,
               {{"title", QStringLiteral("Recovered from printed first page of the paper")}}),
           "local first-page recovery without DOI");
    expect(store.record(localIdentity).value("title").toString() ==
           QStringLiteral("Recovered from printed first page of the paper"),
           "manual title is restored from trusted first-page title");

    checkpoint("offline-fallback");
    MetadataFetcher offline;
    qputenv("LUNA_METADATA_OFFLINE","1");
    bool called=false;
    offline.lookup(doi,[&](QVariantMap data,QString error){
        called=true;expect(data.isEmpty()&&error==QStringLiteral("offline"),"offline avoids network");
    });
    expect(called,"offline callback");
    qunsetenv("LUNA_METADATA_OFFLINE");
    checkpoint("all-native-contracts-passed");
    qInfo()<<"LUNA metadata native contracts passed";
    return 0;
}
