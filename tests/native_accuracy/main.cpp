// SPDX-License-Identifier: MIT
#include "snapshot_adapter.h"
#include "../../src/lunabibliography.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

static void check(bool yes, const char* message) {
    if (yes) return;
    std::fprintf(stderr, "LUNA_ACCURACY_SNAPSHOT_FAIL: %s\n", message);
    std::fflush(stderr);
    std::abort();
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    check(root.isValid(), "isolated synthetic settings");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
    luna::Bibliography store;
    QVariantMap paper;
    paper["doi"] = QStringLiteral("10.1000/synthetic");
    paper["title"] = QStringLiteral("Synthetic Chemistry Paper");
    paper["authors"] = QStringLiteral("A. First, B. Last");
    paper["authors_structured"] = QStringList{QStringLiteral("A. First"), QStringLiteral("B. Last")};
    paper["authorsSource"] = QStringLiteral("Crossref");
    paper["documentKind"] = QStringLiteral("main_article");
    paper["metadata_status"] = QStringLiteral("complete");
    check(store.save(QStringLiteral("synthetic:one"), paper, false), "seed synthetic record");
    const auto one = luna::Accuracy::captureCase(QStringLiteral("syn-one"), store.record(QStringLiteral("synthetic:one")));
    const auto observed = one.value(QStringLiteral("observed")).toObject();
    check(observed.value(QStringLiteral("terminal_author")).toString() == QStringLiteral("B. Last"), "last listed author");
    check(!one.contains(QStringLiteral("path")) && !one.contains(QStringLiteral("identity")), "no identity/path export");
    check(one.value(QStringLiteral("audit")).toObject().value(QStringLiteral("authorsSource")).toString() == QStringLiteral("Crossref"), "author provenance");

    // Simulate the legacy ambiguous record-wide manual=true setting without
    // marking the author field as an explicit human edit.
    const QString legacy = QStringLiteral("synthetic:legacy");
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(legacy.toUtf8(), QCryptographicHash::Sha256).toHex());
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
    settings.setValue(QStringLiteral("library/bibliography/") + digest,
                      QVariantMap{{QStringLiteral("authors"), QStringLiteral("Hao Fang")},
                                  {QStringLiteral("manual"), true}});
    settings.sync();
    check(settings.status() == QSettings::NoError, "legacy synthetic fixture saved");
    const auto hidden = luna::Accuracy::captureCase(QStringLiteral("syn-legacy"), store.record(legacy));
    check(!hidden.value(QStringLiteral("observed")).toObject().contains(QStringLiteral("terminal_author")),
          "suppressed legacy author must not reappear");

    paper["authors"] = QStringLiteral("Corrected, User Last");
    paper["authorsSource"] = QStringLiteral("manual");
    paper["senior_author_override"] = QStringLiteral("A. First");
    check(store.save(QStringLiteral("synthetic:manual"), paper, false), "seed manual author");
    const auto manual = luna::Accuracy::captureCase(QStringLiteral("syn-manual"), store.record(QStringLiteral("synthetic:manual")));
    check(manual.value(QStringLiteral("observed")).toObject().value(QStringLiteral("terminal_author")).toString() == QStringLiteral("User Last"),
          "manual last author beats stale structured list and senior override");
    check(manual.value(QStringLiteral("audit")).toObject().value(QStringLiteral("senior_author_override_active")).toBool(),
          "senior override disclosed separately");

    const QVariantMap si{{QStringLiteral("documentKind"), QStringLiteral("supporting_information")}};
    const auto supporting = luna::Accuracy::captureCase(QStringLiteral("syn-si"), si);
    check(supporting.value(QStringLiteral("observed")).toObject().value(QStringLiteral("document_kind")).toString() == QStringLiteral("supporting_information"),
          "SI classified without invented DOI");
    check(!supporting.value(QStringLiteral("observed")).toObject().contains(QStringLiteral("doi")),
          "SI has no invented DOI");
    QJsonArray cases{one, hidden, manual, supporting};
    const auto doc = luna::Accuracy::predictionDocument(QStringLiteral("luna-synthetic-native"),
                    QString(40, QLatin1Char('a')), cases);
    check(doc.value(QStringLiteral("cases")).toArray().size() == 4, "predictions envelope");
    check(luna::Accuracy::predictionDocument(QStringLiteral("luna-synthetic-native"),
                    QStringLiteral("unknown"), cases).isEmpty(), "reject unverified commit identity");
    check(luna::Accuracy::captureCase(QStringLiteral("/sensitive/path"), paper).isEmpty(),
          "opaque case ID required");
    std::fprintf(stderr, "LUNA_ACCURACY_SNAPSHOT_PASS: 4 synthetic cases\n");
    return 0;
}
