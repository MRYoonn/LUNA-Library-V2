// SPDX-License-Identifier: MIT
#include "lunabibliography.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
    if (app.arguments().contains("--live-crossref")) {
        luna::Bibliography live(nullptr); QEventLoop loop; QTimer timeout; timeout.setSingleShot(true);
        QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
        live.changed=[&](const QString&) { loop.quit(); };
        timeout.start(15000); live.lookup("public-doi-windows-fixture","10.1038/nphys1170"); loop.exec();
        const auto actual=live.record("public-doi-windows-fixture");
        const bool accepted=actual.value("doi").toString()=="10.1038/nphys1170" && actual.value("title").toString()=="Measured measurement" && actual.value("authors").toString()=="Markus Aspelmeyer" && actual.value("year").toString()=="2009" && !actual.value("journal").toString().isEmpty() && actual.value("source").toString()=="Crossref";
        qInfo().noquote()<<QJsonDocument(QJsonObject{{"accepted",accepted},{"scope","native Windows Qt live public DOI query only; no PDF input"},{"actual",QJsonObject::fromVariantMap(actual)}}).toJson(QJsonDocument::Compact);
        return accepted ? 0 : 12;
    }
    const QString doi = "10.1234/synthetic.fixture";
    const QByteArray response = R"({"status":"ok","message":{"DOI":"10.1234/synthetic.fixture","title":["Synthetic metadata fixture"],"container-title":["Synthetic journal"],"volume":"24","issue":"2","page":"10-12","created":{"date-parts":[[1999]]},"published-print":{"date-parts":[[2022,1,1]]},"author":[{"given":"Author","family":"A"}]}})";
    const auto data = luna::Bibliography::crossref(response,doi);
    if (data.value("year").toString() != "2022" || data.value("journal").toString() != "Synthetic journal" || data.value("authors").toString() != "Author A") return 2;
    if (!luna::Bibliography::crossref(response,"10.1234/other").isEmpty() || !luna::Bibliography::crossref("not JSON",doi).isEmpty()) return 3;
    const auto sparse = luna::Bibliography::crossref(R"({"status":"ok","message":{"DOI":"10.1234/synthetic.fixture","created":{"date-parts":[[1999]]}}})",doi);
    if (!sparse.value("authors").toString().isEmpty() || !sparse.value("year").toString().isEmpty()) return 4;
    if (luna::Bibliography::doi("https://doi.org/10.1234/synthetic.fixture.") != doi) return 5;
    if (luna::Bibliography::doi("(10.1234/synthetic(edition))") != "10.1234/synthetic(edition)") return 10;
    luna::Bibliography store(nullptr);
    if (!store.save("before",data) || !store.relocate("before","after") || store.record("after").value("title") != data.value("title")) return 6;
    if (!store.relocate("after","before") || store.record("before").value("authors") != data.value("authors")) return 7;
    if (!store.relocate("before","before") || store.record("before").isEmpty()) return 8;
    { luna::Bibliography reopened(nullptr); if (reopened.record("before").value("doi").toString() != doi) return 9; }
    store.ingest("embedded","Synthetic title","Synthetic author",{},{{"journal","Synthetic journal"},{"year","2022"}});
    if (store.record("embedded").value("year").toString()!="2022" || store.record("embedded").value("authors").toString()!="Synthetic author") return 11;
    qInfo() << "[PASS] DOI match/mismatch, malformed/sparse responses, publication-year provenance and local metadata move/restart; no live service query";
    return 0;
}
