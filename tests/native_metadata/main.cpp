// SPDX-License-Identifier: MIT
#include "../../src/lunametadataengine.h"
#include "../../src/lunametadatafetch.h"
#include "../../src/lunabibliography.h"
#include "../../src/lunatitleextractor.h"
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

    checkpoint("si-parent-bibliography");
    const QString originalTitle=QStringLiteral(
        "Visible-Light-Driven C5 Functionalization of Thiouracils by a Photoredox Process");
    const QString siCover=QStringLiteral("Supporting Information\n") + originalTitle +
        QStringLiteral("\nAlice Brown, Clara Green\nDOI: 10.1021/acs.joc.5c01153\nContents");
    const auto si=SIIdentity::evidence(siCover,QStringLiteral("jo5c01153_si_001.pdf"));
    expect(si.value("documentKind")==QStringLiteral("supporting_information") &&
           si.value("siCoverTitle")==originalTitle &&
           si.value("siArticleDoiCandidate")==QStringLiteral("10.1021/acs.joc.5c01153") &&
           si.value("siEvidenceSource")==QStringLiteral("si_cover_explicit_doi"),
           "ACS SI cover DOI/title extracted independently");
    const QString splitCover=QStringLiteral(
        "Supporting Information\nVisible-Light-Driven C5 Functionalization of\n"
        "Thiouracils by a Photoredox Process\nAlice Brown, Clara Green\n"
        "DOI: 10.1021/acs.joc.5c01153");
    expect(SIIdentity::evidence(splitCover).value("siCoverTitle")==originalTitle,
           "two-line SI paper title excludes cover author row");
    expect(SIIdentity::filenameDoiHint(QStringLiteral("jo5c01153_si_001.pdf"))==
           QStringLiteral("10.1021/acs.joc.5c01153"),"ACS JOC SI filename DOI hint");
    expect(SIIdentity::filenameDoiHint(QStringLiteral("jm5c01788_si_002.pdf"))==
           QStringLiteral("10.1021/acs.jmedchem.5c01788"),"ACS medicinal SI filename DOI hint");
    expect(SIIdentity::filenameDoiHint(QStringLiteral("d5ob00864f1_suppl.pdf"))==
           QStringLiteral("10.1039/d5ob00864f"),"RSC ESI filename DOI hint");
    expect(SIIdentity::filenameDoiHint(QStringLiteral("random_notes_si.pdf")).isEmpty(),
           "unknown SI names are not article DOI identities");
    expect(SIIdentity::evidence(QStringLiteral(
           "J. Org. Chem. 2025, 90, 1234\nCite This: J. Org. Chem.\nSupporting Information\nAbstract"),
           QStringLiteral("jo5c01153_si_001.pdf")).isEmpty(),
           "ACS main PDF navbar is not SI even when filename is suspicious");
    const auto onlyHint=SIIdentity::evidence(QStringLiteral(
        "Supporting Information\nContents\nGeneral experimental procedures"),
        QStringLiteral("jo5c01153_si_001.pdf"));
    expect(onlyHint.value("siCoverTitle").toString().isEmpty() &&
           onlyHint.value("siLinkStatus")==QStringLiteral("needs_review") &&
           !onlyHint.contains("doi"),"filename-only SI stays review-only without cover title");

    Bibliography siStore;
    const QString originalIdentity=QStringLiteral("synthetic:si:verified-original");
    const QString siIdentity=QStringLiteral("synthetic:si:acs-cover");
    QVariantMap parentArticle{{"title",originalTitle},
                              {"titleSource",QStringLiteral("Crossref")},
                              {"doi",QStringLiteral("10.1021/acs.joc.5c01153")},
                              {"doiSource",QStringLiteral("Crossref")},
                              {"journal",QStringLiteral("Journal of Organic Chemistry")},
                              {"journalSource",QStringLiteral("Crossref")},
                              {"year",QStringLiteral("2025")},
                              {"authors",QStringLiteral("Alice Brown, Clara Green")},
                              {"authors_structured",QStringList{QStringLiteral("Alice Brown"),QStringLiteral("Clara Green")}},
                              {"authorsSource",QStringLiteral("Crossref")},
                              {"metadata_status",QStringLiteral("complete")}};
    expect(siStore.save(originalIdentity,parentArticle,false),
           "synthetic original article available locally");
    expect(siStore.enrichFromFirstPage(siIdentity,si),"SI first-page document-kind saved");
    expect(siStore.record(siIdentity).value("title").toString().isEmpty(),
           "unverified SI never automatically becomes article title");
    expect(siStore.linkSIFromLocal(siIdentity),
           "strong cover title and exact DOI link local original");
    const QVariantMap linked=siStore.record(siIdentity);
    expect(linked.value("siLinkStatus")==QStringLiteral("verified") &&
           linked.value("siParentDoi")==QStringLiteral("10.1021/acs.joc.5c01153") &&
           linked.value("documentKind")==QStringLiteral("supporting_information") &&
           linked.value("journal")==QStringLiteral("Journal of Organic Chemistry") &&
           linked.value("title")==originalTitle &&
           linked.value("authors").toString()==QStringLiteral("Alice Brown, Clara Green"),
           "SI inherits original paper title, authors, journal and DOI but retains SI kind");
    expect(!siStore.enrichFromFirstPage(siIdentity,si) &&
           siStore.record(siIdentity).value("siLinkStatus")==QStringLiteral("verified"),
           "reopening SI does not clear verified original metadata");

    const QString rscTitle=QStringLiteral(
        "Site-Selective Photocatalytic Activation of Pyrimidine Derivatives under Visible Light");
    const QString rscIdentity=QStringLiteral("synthetic:si:rsc-cover");
    auto rsc=SIIdentity::evidence(QStringLiteral("Electronic Supplementary Material (ESI)\n")+
        rscTitle+QStringLiteral("\nJane Miller, Peter Hill\nDOI: 10.1039/d5ob00864f"),
        QStringLiteral("d5ob00864f1_suppl.pdf"));
    expect(rsc.value("siArticleDoiCandidate")==QStringLiteral("10.1039/d5ob00864f") &&
           rsc.value("siCoverTitle")==rscTitle,"RSC ESI cover parsed");
    expect(siStore.enrichFromFirstPage(rscIdentity,rsc),
           "RSC SI saved without inventing article fields");
    QVariantMap remote=parentArticle;
    remote["title"]=rscTitle;
    remote["doi"]=QStringLiteral("10.1039/d5ob00864f");
    remote["metadata_source"]=QStringLiteral("Crossref");
    expect(!siStore.linkSIToParent(rscIdentity,parentArticle,
                                  QStringLiteral("10.1039/d5ob00864f"),false),
           "SI cannot attach an unrelated article DOI/title");
    auto fakeSupplement=remote;
    fakeSupplement["title"]=QStringLiteral("Supporting Information");
    expect(!siStore.linkSIToParent(rscIdentity,fakeSupplement,
                                  QStringLiteral("10.1039/d5ob00864f"),false),
           "provider SI metadata cannot masquerade as original paper");
    expect(siStore.linkSIToParent(rscIdentity,remote,
                                 QStringLiteral("10.1039/d5ob00864f"),false),
           "verified Crossref article connects to matching SI cover");
    expect(siStore.record(rscIdentity).value("siParentSource")==QStringLiteral("Crossref"),
           "provider provenance saved with SI parent link");

    const QString conflictIdentity=QStringLiteral("synthetic:si:manual-conflict");
    expect(siStore.enrichFromFirstPage(conflictIdentity,rsc),"conflicting SI evidence saved");
    expect(siStore.saveEdits(conflictIdentity,{},
        {{"title",QStringLiteral("Manually Corrected Alternative Publication Title")}}),
        "manual SI edit saved");
    expect(!siStore.linkSIToParent(conflictIdentity,remote,
                                  QStringLiteral("10.1039/d5ob00864f"),false) &&
           siStore.record(conflictIdentity).value("title")==
               QStringLiteral("Manually Corrected Alternative Publication Title"),
           "SI parent enrichment cannot overwrite conflicting manual title");
    auto changedCover=rsc;
    changedCover["siArticleDoiCandidate"]=QStringLiteral("10.1039/d5ob00865g");
    expect(siStore.enrichFromFirstPage(rscIdentity,changedCover) &&
           siStore.record(rscIdentity).value("siLinkStatus")==QStringLiteral("needs_review") &&
           siStore.record(rscIdentity).value("title")==rscTitle,
           "changed DOI on linked SI marks review but preserves bibliography");

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
