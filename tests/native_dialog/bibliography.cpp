// SPDX-License-Identifier: MIT
#include "lunabibliography.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
    const QString doi = "10.1234/synthetic.fixture";
    const QByteArray response = R"({"status":"ok","message":{"DOI":"10.1234/synthetic.fixture","title":["Synthetic metadata fixture"],"container-title":["Synthetic journal"],"volume":"24","issue":"2","page":"10-12","created":{"date-parts":[[1999]]},"published-print":{"date-parts":[[2022,1,1]]},"author":[{"given":"Author","family":"A"}]}})";
    const auto data = luna::Bibliography::crossref(response,doi);
    if (data.value("year").toString() != "2022" || data.value("journal").toString() != "Synthetic journal" || data.value("authors").toString() != "Author A") return 2;
    if (!luna::Bibliography::crossref(response,"10.1234/other").isEmpty() || !luna::Bibliography::crossref("not JSON",doi).isEmpty()) return 3;
    const auto sparse = luna::Bibliography::crossref(R"({"status":"ok","message":{"DOI":"10.1234/synthetic.fixture","created":{"date-parts":[[1999]]}}})",doi);
    if (!sparse.value("authors").toString().isEmpty() || !sparse.value("year").toString().isEmpty()) return 4;
    if (luna::Bibliography::doi("https://doi.org/10.1234/synthetic.fixture.") != doi) return 5;
    if (luna::Bibliography::citation(data) != QStringLiteral("Synthetic journal 2022, 24, 10-12")) return 10;
    if (!luna::Bibliography::citation({{"doi", doi}}).isEmpty()) return 11;
    if (luna::Bibliography::citation({{"year", "2026"}, {"volume", "148"}}) != QStringLiteral("2026, 148")) return 12;
    if (luna::Bibliography::citation(data).contains("10.1234")) return 13;
    luna::Bibliography store(nullptr);
    if (!store.save("before",data)) return 6;
    // The user's correction must never be overwritten by automatic PDF info.
    store.ingest("before", QStringLiteral("Overwritten?"), QStringLiteral("Other"), QString());
    if (store.record("before").value("title") != data.value("title")) return 14;
    if (!store.relocate("before","after") || store.record("after").value("title") != data.value("title")) return 6;
    if (!store.relocate("after","before") || store.record("before").value("authors") != data.value("authors")) return 7;
    if (!store.relocate("before","before") || store.record("before").isEmpty()) return 8;
    { luna::Bibliography reopened(nullptr); if (reopened.record("before").value("doi").toString() != doi) return 9; }
    qInfo() << "[PASS] DOI match/mismatch, malformed/sparse responses, publication-year provenance and local metadata move/restart; no live service query";
    return 0;
}
