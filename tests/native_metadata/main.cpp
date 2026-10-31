// SPDX-License-Identifier: MIT
#include "../../src/lunametadataengine.h"
#include "../../src/lunametadatafetch.h"
#include "../../src/lunabibliography.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
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
    QJsonObject cr {
        {"DOI",doi},
        {"title",QJsonArray{QStringLiteral("Synthetic N–H Activation Study")}},
        {"container-title",QJsonArray{QStringLiteral("Journal of the American Chemical Society")}},
        {"short-container-title",QJsonArray{QStringLiteral("J. Am. Chem. Soc.")}},
        {"published",QJsonObject{{"date-parts",QJsonArray{QJsonArray{2024,7,1}}}}},
        {"volume",QStringLiteral("146")},
        {"page",QStringLiteral("20349-20356")},
        {"author",QJsonArray{
            QJsonObject{{"given",QStringLiteral("Robert R.")},{"family",QStringLiteral("Knowles")}},
            QJsonObject{{"given",QStringLiteral("Abigail G.")},{"family",QStringLiteral("Doyle")}}
        }}
    };
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
    QJsonObject alex {
        {"doi",QStringLiteral("https://doi.org/")+doi},
        {"title",QStringLiteral("Synthetic N–H Activation Study")},
        {"publication_year",2024},
        {"biblio",QJsonObject{{"volume",QStringLiteral("146")},
            {"first_page",QStringLiteral("20349")},{"last_page",QStringLiteral("20356")}}},
        {"primary_location",QJsonObject{{"source",
            QJsonObject{{"display_name",QStringLiteral("Journal of the American Chemical Society")}}}}},
        {"authorships",QJsonArray{QJsonObject{{"author",
            QJsonObject{{"display_name",QStringLiteral("Abigail G. Doyle")}}}}}}
    };
    expect(fromOpenAlex(alex,doi).value("page").toString()==QStringLiteral("20349–20356"),
           "OpenAlex verified fallback page range");
    expect(fromOpenAlex(alex,QStringLiteral("10.1000/else")).isEmpty(),
           "OpenAlex mismatch rejected");
    expect(articleNumberValid(QStringLiteral("e20591"),doi),"article number usable");
    expect(!articleNumberValid(doi,doi),"DOI is not an article number");

    checkpoint("qsettings-merge");
    Bibliography store;
    const QString identity=QStringLiteral("synthetic:test:one");
    expect(store.mergeVerifiedOnline(identity,doi,online),"persist verified record");
    expect(Bibliography::citation(store.record(identity))==QStringLiteral(
        "Abigail G. Doyle, J. Am. Chem. Soc. 2024, 146, 20349–20356."),
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
