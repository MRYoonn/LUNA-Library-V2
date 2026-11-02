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
    paper["field_sources"] = QVariantMap{{QStringLiteral("title"), QStringLiteral("Crossref")},
                                         {QStringLiteral("authors"), QStringLiteral("Crossref")},
                                         {QStringLiteral("private_note"), QStringLiteral("should never export")}};
    check(store.save(QStringLiteral("synthetic:one"), paper, false), "seed synthetic record");
    const auto one = luna::Accuracy::captureCase(QStringLiteral("syn-one"), store.record(QStringLiteral("synthetic:one")));
    const auto observed = one.value(QStringLiteral("observed")).toObject();
    check(observed.value(QStringLiteral("terminal_author")).toString() == QStringLiteral("B. Last"), "last listed author");
    check(!one.contains(QStringLiteral("path")) && !one.contains(QStringLiteral("identity")), "no identity/path export");
    check(one.value(QStringLiteral("audit")).toObject().value(QStringLiteral("authorsSource")).toString() == QStringLiteral("Crossref"), "author provenance");
    const auto sources = one.value(QStringLiteral("audit")).toObject()
        .value(QStringLiteral("field_sources")).toObject();
    check(sources.value(QStringLiteral("title")).toString() == QStringLiteral("Crossref"),
          "per-field provenance");
    check(!sources.contains(QStringLiteral("private_note")), "reject arbitrary source fields");

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
    // Exercise the actual Qt bibliography persistence layer, not a fabricated
    // JSON before/after pair. All identities and settings are synthetic and
    // isolated under QTemporaryDir, never the user's installed QSettings.
    const QString moveSource = QStringLiteral("synthetic:move-source");
    const QString moveTarget = QStringLiteral("synthetic:move-target");
    QVariantMap movable{{QStringLiteral("doi"), QStringLiteral("10.1000/synthetic-move")},
                        {QStringLiteral("title"), QStringLiteral("Initial Synthetic Title")},
                        {QStringLiteral("documentKind"), QStringLiteral("main_article")}};
    check(store.save(moveSource, movable, false), "seed synthetic move source");
    const QVariantMap editBefore = store.record(moveSource);
    QVariantMap editAfter = editBefore;
    editAfter[QStringLiteral("title")] = QStringLiteral("Human Reviewed Chemistry Title");
    check(store.saveEdits(moveSource, editBefore, editAfter), "explicit human title edit");
    check(store.record(moveSource).value(QStringLiteral("titleSource")).toString()
          == QStringLiteral("manual"), "manual title provenance persisted");

    const QVariantMap conflictingProvider{
        {QStringLiteral("doi"), QStringLiteral("10.1000/synthetic-move")},
        {QStringLiteral("title"), QStringLiteral("Unrelated Automated Title")},
        {QStringLiteral("metadata_source"), QStringLiteral("Crossref")}};
    check(!store.mergeVerifiedOnline(moveSource, QStringLiteral("10.1000/synthetic-move"),
                                     conflictingProvider), "provider conflict must request review");
    check(store.record(moveSource).value(QStringLiteral("title")).toString()
          == QStringLiteral("Human Reviewed Chemistry Title"), "manual title not overwritten");
    check(store.record(moveSource).value(QStringLiteral("metadata_status")).toString()
          == QStringLiteral("needs_review"), "provider conflict disclosed");

    for (const QString& failure : {QStringLiteral("offline"),
                                   QStringLiteral("rate_limited"),
                                   QStringLiteral("not_found")}) {
        const QString identity = QStringLiteral("synthetic:failure:") + failure;
        check(store.save(identity, movable, false), "seed synthetic failure record");
        check(store.recordLookupFailure(identity, failure), "record provider failure");
        const QVariantMap failed = store.record(identity);
        check(failed.value(QStringLiteral("metadata_last_error")).toString() == failure,
              "provider failure diagnostic retained");
        check(failed.value(QStringLiteral("metadata_status")).toString()
              == (failure == QStringLiteral("offline") ? QStringLiteral("offline")
                                                       : QStringLiteral("partial")),
              "provider failure cannot masquerade as complete");
        check(failed.value(QStringLiteral("title")).toString()
              == QStringLiteral("Initial Synthetic Title"), "failure preserves bibliography");
    }

    check(store.relocate(moveSource, moveTarget), "relocate synthetic bibliography");
    check(store.record(moveSource).value(QStringLiteral("title")).toString()
          == QStringLiteral("Human Reviewed Chemistry Title"), "in-process relocation alias");
    luna::Bibliography reopened;
    const QVariantMap restored = reopened.record(moveTarget);
    check(restored.value(QStringLiteral("title")).toString()
          == QStringLiteral("Human Reviewed Chemistry Title"), "restart preserves human title");
    check(restored.value(QStringLiteral("titleSource")).toString()
          == QStringLiteral("manual"), "restart preserves field provenance");
    check(restored.value(QStringLiteral("metadata_status")).toString()
          == QStringLiteral("needs_review"), "restart preserves conflict status");
    check(reopened.record(moveSource).isEmpty(), "restart does not resurrect old path identity");

    const QString occupied = QStringLiteral("synthetic:occupied-target");
    check(store.save(occupied, movable, false), "seed collision target");
    check(!store.relocate(moveTarget, occupied), "move must not overwrite existing record");
    check(reopened.record(moveTarget).value(QStringLiteral("title")).toString()
          == QStringLiteral("Human Reviewed Chemistry Title"), "collision leaves source intact");
    check(reopened.record(occupied).value(QStringLiteral("title")).toString()
          == QStringLiteral("Initial Synthetic Title"), "collision leaves destination intact");

    std::fprintf(stderr, "LUNA_ACCURACY_SNAPSHOT_PASS: 4 synthetic cases\n");
    return 0;
}
