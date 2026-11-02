// SPDX-License-Identifier: MIT
#include "snapshot_adapter.h"
#include "local_pdf_capture.h"
#include "../../src/lunabibliography.h"
#include "../../src/lunafilemove.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSettings>
#include <QProcess>
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
    // A second OS process must reload the relocated record from an isolated
    // INI file, not from the parent process's QSettings cache or aliases_.
    const QStringList arguments = app.arguments();
    if (arguments.size() > 1 && arguments.at(1) == QStringLiteral("--verify-fresh-process")) {
        check(arguments.size() == 5, "fresh-process arguments");
        const QString settingsRoot = arguments.at(2);
        const QString movedPdf = arguments.at(3);
        const QString dialogIdentity = arguments.at(4);
        check(QDir(settingsRoot).exists(), "fresh-process isolated settings root");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot);
        luna::Bibliography freshStore;
        const QVariantMap moved = freshStore.record(movedPdf);
        check(moved.value(QStringLiteral("title")).toString() ==
              QStringLiteral("Manually Verified Move Title"),
              "fresh process reads moved PDF bibliography");
        check(moved.value(QStringLiteral("titleSource")).toString() ==
              QStringLiteral("manual"), "fresh process retains manual provenance");
        check(moved.value(QStringLiteral("metadata_status")).toString() ==
              QStringLiteral("needs_review"), "fresh process retains review status");
        const QFileInfo pdfInfo(movedPdf);
        check(pdfInfo.isFile(), "fresh process sees moved PDF");
        check(QFileInfo(QDir(pdfInfo.absolutePath()).filePath(
              pdfInfo.completeBaseName() + QStringLiteral(".luna-thumb.png"))).isFile(),
              "fresh process sees moved thumbnail");
        const QVariantMap dialog = freshStore.record(dialogIdentity);
        check(dialog.value(QStringLiteral("title")).toString() ==
              QStringLiteral("Human Edited Dialog Title"),
              "fresh process reads edited dialog title");
        check(dialog.value(QStringLiteral("journal")).toString() ==
              QStringLiteral("Background Verified Journal"),
              "fresh process preserves background journal");
        check(dialog.value(QStringLiteral("titleSource")).toString() ==
              QStringLiteral("manual"), "fresh process retains dialog provenance");
        // Verify all provider failure classes after an actual process restart.
        // A fresh QSettings reader must not turn cached offline/429/404 records
        // into successful or complete metadata without a new provider result.
        for (const QString& failure : {QStringLiteral("offline"),
                                       QStringLiteral("rate_limited"),
                                       QStringLiteral("not_found")}) {
            const QVariantMap persistedFailure = freshStore.record(
                QStringLiteral("synthetic:failure:") + failure);
            check(persistedFailure.value(QStringLiteral("metadata_last_error")).toString() == failure,
                  "fresh process retains offline/429/404 diagnostic");
            const QString expectedStatus = failure == QStringLiteral("offline")
                ? QStringLiteral("offline") : QStringLiteral("partial");
            check(persistedFailure.value(QStringLiteral("metadata_status")).toString() == expectedStatus,
                  "fresh process does not claim offline/429/404 complete");
            check(persistedFailure.value(QStringLiteral("title")).toString() ==
                  QStringLiteral("Initial Synthetic Title"),
                  "fresh process retains original title after provider failure");
        }
        // The legacy migration matrix is reloaded from disk in this fresh
        // OS process, not merely from a second in-process Qt object.
        for (int variant = 0; variant < 4; ++variant) {
            const QString identity = QStringLiteral("synthetic:legacy-matrix:") + QString::number(variant);
            const QVariantMap record = freshStore.record(identity);
            const auto snapshot = luna::Accuracy::captureCase(
                QStringLiteral("syn-legacy-restart-") + QString::number(variant), record);
            const QString actual = snapshot.value(QStringLiteral("observed")).toObject()
                .value(QStringLiteral("terminal_author")).toString();
            check(variant == 2 ? actual == QStringLiteral("John J. Molloy") : actual.isEmpty(),
                  "fresh process legacy author migration matrix");
        }
        check(freshStore.record(QStringLiteral("synthetic:manual-empty-author"))
                  .value(QStringLiteral("authorsSource")).toString() == QStringLiteral("manual"),
              "fresh process preserves deliberate empty manual author");
        check(freshStore.record(QStringLiteral("synthetic:manual-single-author"))
                  .value(QStringLiteral("authors")).toString() == QStringLiteral("Plato"),
              "fresh process preserves one-word manual author");
        return 0;
    }
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
    check(!one.contains(QStringLiteral("source_sha256")),
          "unbound snapshot does not invent PDF digest");
    const QString captureDigest = QString(64, QLatin1Char('a'));
    const auto boundCase = luna::Accuracy::captureCase(
        QStringLiteral("syn-bound"), paper, captureDigest.toUpper());
    check(boundCase.value(QStringLiteral("source_sha256")).toString() == captureDigest,
          "synthetic capture-time PDF digest is normalized");
    check(luna::Accuracy::captureCase(QStringLiteral("syn-invalid-hash"), paper,
                                     QStringLiteral("invalid")).isEmpty(),
          "invalid capture-time PDF digest rejected");
    check(luna::Accuracy::captureCase(QStringLiteral("syn-zero-hash"), paper,
                                     QString(64, QLatin1Char('0'))).isEmpty(),
          "placeholder capture-time PDF digest rejected");

    // Opt-in local capture must hash actual synthetic PDF bytes, not copy
    // a caller-supplied goldset digest into the native snapshot.
    const QString localPdf = root.filePath(QStringLiteral("synthetic-capture.pdf"));
    const QByteArray pdfBytes("%PDF-1.4\\nsynthetic local fixture only\\n%%EOF\\n");
    {
        QFile fixture(localPdf);
        check(fixture.open(QIODevice::WriteOnly), "create synthetic capture PDF");
        check(fixture.write(pdfBytes) == pdfBytes.size(), "write synthetic capture PDF");
    }
    const QString actualDigest = QString::fromLatin1(
        QCryptographicHash::hash(pdfBytes, QCryptographicHash::Sha256).toHex());
    const auto hashedCase = luna::Accuracy::captureCaseFromLocalPdf(
        QStringLiteral("syn-local-hash"), paper, localPdf);
    check(hashedCase.value(QStringLiteral("source_sha256")).toString() == actualDigest,
          "local PDF bytes bound to snapshot");
    check(!hashedCase.contains(QStringLiteral("path"))
          && !hashedCase.contains(QStringLiteral("filename")),
          "local capture never exports PDF path");
    check(luna::Accuracy::captureCaseFromLocalPdf(
              QStringLiteral("syn-missing"), paper, root.filePath(QStringLiteral("missing.pdf"))).isEmpty(),
          "missing PDF cannot produce a bound snapshot");
    const QString movedPdfFixture = root.filePath(QStringLiteral("synthetic-capture-moved.pdf"));
    check(QFile::rename(localPdf, movedPdfFixture), "rename synthetic PDF for capture");
    check(luna::Accuracy::captureCaseFromLocalPdf(
              QStringLiteral("syn-old-path"), paper, localPdf).isEmpty(),
          "stale PDF path cannot produce a bound snapshot");
    check(luna::Accuracy::captureCaseFromLocalPdf(
              QStringLiteral("syn-new-path"), paper, movedPdfFixture)
              .value(QStringLiteral("source_sha256")).toString() == actualDigest,
          "renamed PDF preserves content binding");
    {
        QFile changed(movedPdfFixture);
        check(changed.open(QIODevice::Append), "open synthetic PDF for modification");
        check(changed.write("modified") == 8, "modify synthetic PDF bytes");
    }
    check(luna::Accuracy::captureCaseFromLocalPdf(
              QStringLiteral("syn-changed"), paper, movedPdfFixture)
              .value(QStringLiteral("source_sha256")).toString() != actualDigest,
          "changed PDF cannot reuse old goldset digest");
    const QString nonPdf = root.filePath(QStringLiteral("not-a-pdf.pdf"));
    {
        QFile invalid(nonPdf);
        check(invalid.open(QIODevice::WriteOnly), "create invalid synthetic PDF");
        check(invalid.write("not a pdf") == 9, "write invalid synthetic PDF");
    }
    check(luna::Accuracy::captureCaseFromLocalPdf(
              QStringLiteral("syn-not-pdf"), paper, nonPdf).isEmpty(),
          "invalid PDF header cannot produce a bound snapshot");

    check(one.value(QStringLiteral("audit")).toObject().value(QStringLiteral("authorsSource")).toString() == QStringLiteral("Crossref"), "author provenance");
    const auto sources = one.value(QStringLiteral("audit")).toObject()
        .value(QStringLiteral("field_sources")).toObject();
    check(sources.value(QStringLiteral("title")).toString() == QStringLiteral("Crossref"),
          "per-field provenance");
    check(!sources.contains(QStringLiteral("private_note")), "reject arbitrary source fields");
    check(!one.value(QStringLiteral("audit")).toObject()
               .value(QStringLiteral("senior_author_override_active")).toBool(),
          "ordinary last author is not a manual card override");
    // A manually chosen author index changes labCitation() without a textual
    // senior_author_override. Never score that card as auto-extracted author.
    QVariantMap selectedAuthor = paper;
    selectedAuthor[QStringLiteral("senior_author_index")] = 0;
    const auto selected = luna::Accuracy::captureCase(QStringLiteral("syn-selected"), selectedAuthor);
    check(selected.value(QStringLiteral("observed")).toObject()
              .value(QStringLiteral("terminal_author")).toString() == QStringLiteral("B. Last"),
          "index selection does not rewrite the last listed author");
    check(selected.value(QStringLiteral("audit")).toObject()
              .value(QStringLiteral("senior_author_override_active")).toBool(),
          "selected senior-author index is a manual card override");
    selectedAuthor[QStringLiteral("senior_author_index")] = 99;
    const auto invalidSelection = luna::Accuracy::captureCase(QStringLiteral("syn-invalid-selection"), selectedAuthor);
    check(!invalidSelection.value(QStringLiteral("audit")).toObject()
               .value(QStringLiteral("senior_author_override_active")).toBool(),
          "out-of-range index does not change the card");

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
    check(hidden.value(QStringLiteral("observed")).toObject().contains(QStringLiteral("terminal_author")) &&
          hidden.value(QStringLiteral("observed")).toObject().value(QStringLiteral("terminal_author")).isNull(),
          "suppressed legacy author must not reappear");

    // PR #13 legacy migration matrix: old record-wide manual/source flags
    // were sometimes written by automation and do NOT prove a human author
    // edit. Preserve a recoverable copy without exporting an invented author.
    for (int variant = 0; variant < 4; ++variant) {
        const QString identity = QStringLiteral("synthetic:legacy-matrix:") + QString::number(variant);
        const QString legacyDigest = QString::fromLatin1(
            QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
        const bool oldManualFlag = (variant % 2) != 0;
        const bool verifiedTerminalEvidence = variant >= 2;
        const QString legacyAuthor = verifiedTerminalEvidence
            ? QStringLiteral("John J. Molloy") : QStringLiteral("Hao Fang");
        QVariantMap entry{{QStringLiteral("authors"), legacyAuthor},
                          {QStringLiteral("manual"), oldManualFlag},
                          {QStringLiteral("source"), QStringLiteral("Manual")}};
        if (verifiedTerminalEvidence)
            entry[QStringLiteral("authorEvidence")] = QStringLiteral("first-page-terminal-and");
        settings.setValue(QStringLiteral("library/bibliography/") + legacyDigest, entry);
    }
    settings.sync();
    check(settings.status() == QSettings::NoError, "legacy migration matrix saved");
    for (int variant = 0; variant < 4; ++variant) {
        const QString identity = QStringLiteral("synthetic:legacy-matrix:") + QString::number(variant);
        const QVariantMap restoredLegacy = store.record(identity);
        const auto capturedLegacy = luna::Accuracy::captureCase(
            QStringLiteral("syn-legacy-matrix-") + QString::number(variant), restoredLegacy);
        const QString actualAuthor = capturedLegacy.value(QStringLiteral("observed")).toObject()
            .value(QStringLiteral("terminal_author")).toString();
        if (variant == 2) {
            check(actualAuthor == QStringLiteral("John J. Molloy"),
                  "verified first-page terminal author survives legacy migration");
            check(restoredLegacy.value(QStringLiteral("authorsSource")).toString() ==
                  QStringLiteral("first_page_verified"), "verified legacy provenance restored");
        } else {
            check(actualAuthor.isEmpty(),
                  "legacy false manual flag still suppresses unverified author");
            check(restoredLegacy.value(QStringLiteral("legacyAuthors")).toString() ==
                  (variant >= 2 ? QStringLiteral("John J. Molloy") : QStringLiteral("Hao Fang")),
                  "unverified legacy author preserved only as recovery data");
        }
    }

    // A human's deliberately empty author and a one-word manual author must
    // remain protected, even if a later first-page worker proposes a name.
    const QString emptyManualId = QStringLiteral("synthetic:manual-empty-author");
    const QVariantMap emptyManual{{QStringLiteral("authors"), QString()},
                                  {QStringLiteral("authorsSource"), QStringLiteral("manual")}};
    check(store.save(emptyManualId, emptyManual, false), "seed deliberate empty manual author");
    check(!store.enrichFromFirstPage(emptyManualId,
          QVariantMap{{QStringLiteral("authors"), QStringLiteral("John J. Molloy")},
                      {QStringLiteral("authorEvidence"), QStringLiteral("first-page-terminal-and")}}),
          "deliberately empty manual author rejects automatic overwrite");
    check(store.record(emptyManualId).value(QStringLiteral("authorsSource")).toString() ==
          QStringLiteral("manual"), "empty manual author provenance retained");
    check(luna::Accuracy::captureCase(QStringLiteral("syn-manual-empty"), store.record(emptyManualId))
              .value(QStringLiteral("observed")).toObject()
              .value(QStringLiteral("terminal_author")).isNull(),
          "empty manual author captured as explicit null");
    const QString singleManualId = QStringLiteral("synthetic:manual-single-author");
    check(store.save(singleManualId,
          QVariantMap{{QStringLiteral("authors"), QStringLiteral("Plato")},
                      {QStringLiteral("authorsSource"), QStringLiteral("manual")}}, false),
          "seed one-word manual author");
    check(luna::Accuracy::captureCase(QStringLiteral("syn-manual-single"), store.record(singleManualId))
              .value(QStringLiteral("observed")).toObject()
              .value(QStringLiteral("terminal_author")).toString() == QStringLiteral("Plato"),
          "one-word human author remains visible");

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
    check(supporting.value(QStringLiteral("observed")).toObject().contains(QStringLiteral("doi")) &&
          supporting.value(QStringLiteral("observed")).toObject().value(QStringLiteral("doi")).isNull(),
          "SI has no invented DOI");
    check(supporting.value(QStringLiteral("observed")).toObject().value(QStringLiteral("title")).isNull() &&
          supporting.value(QStringLiteral("observed")).toObject().value(QStringLiteral("terminal_author")).isNull() &&
          supporting.value(QStringLiteral("observed")).toObject().value(QStringLiteral("journal")).isNull(),
          "SI explicitly captures absent paper fields");
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
    // record() adds an in-memory legacy authorsSource marker even when no
    // stored record exists; inspect the actual persistent key instead.
    const QString oldMoveDigest = QString::fromLatin1(
        QCryptographicHash::hash(moveSource.toUtf8(), QCryptographicHash::Sha256).toHex());
    QSettings persisted(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
    check(!persisted.contains(QStringLiteral("library/bibliography/") + oldMoveDigest),
          "restart does not retain old persistent identity");

    const QString occupied = QStringLiteral("synthetic:occupied-target");
    check(store.save(occupied, movable, false), "seed collision target");
    check(!store.relocate(moveTarget, occupied), "move must not overwrite existing record");
    check(reopened.record(moveTarget).value(QStringLiteral("title")).toString()
          == QStringLiteral("Human Reviewed Chemistry Title"), "collision leaves source intact");
    check(reopened.record(occupied).value(QStringLiteral("title")).toString()
          == QStringLiteral("Initial Synthetic Title"), "collision leaves destination intact");

    // Exercise the production physical PDF+sidecar move helper against disposable
    // on-disk files, then relocate the bibliography and reopen its Qt store.
    // The PDF bytes are fabricated, never parsed, and never leave QTemporaryDir.
    const QDir temporary(root.path());
    check(temporary.mkpath(QStringLiteral("physical-source")) &&
          temporary.mkpath(QStringLiteral("physical-target")),
          "create isolated physical move folders");
    const QString physicalSource = temporary.filePath(QStringLiteral("physical-source"));
    const QString physicalTarget = temporary.filePath(QStringLiteral("physical-target"));
    const QString pdfName = QStringLiteral("synthetic-paper.pdf");
    const QString thumbName = QStringLiteral("synthetic-paper.luna-thumb.png");
    const QString metaName = QStringLiteral("synthetic-paper.luna-meta.json");
    const QString physicalPdf = QDir(physicalSource).filePath(pdfName);
    const QString physicalThumb = QDir(physicalSource).filePath(thumbName);
    const QString physicalMeta = QDir(physicalSource).filePath(metaName);
    const QString targetPdf = QDir(physicalTarget).filePath(pdfName);
    const QString targetThumb = QDir(physicalTarget).filePath(thumbName);
    const QString targetMeta = QDir(physicalTarget).filePath(metaName);
    const QByteArray pdfBytes("%PDF-1.4\n% fabricated fixture, not a publisher PDF\n");
    const QByteArray thumbBytes("fabricated-thumbnail-bytes");
    const QByteArray metaBytes(R"({"synthetic":true})");
    const auto writeFixture = [](const QString& path, const QByteArray& bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
               file.write(bytes) == bytes.size();
    };
    const auto readFixture = [](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    check(writeFixture(physicalPdf, pdfBytes) &&
          writeFixture(physicalThumb, thumbBytes) &&
          writeFixture(physicalMeta, metaBytes), "seed disposable PDF and sidecars");
    QVariantMap physicalRecord{{QStringLiteral("title"), QStringLiteral("Manually Verified Move Title")},
                               {QStringLiteral("titleSource"), QStringLiteral("manual")},
                               {QStringLiteral("doi"), QStringLiteral("10.1000/physical-move")},
                               {QStringLiteral("metadata_status"), QStringLiteral("needs_review")}};
    check(store.save(physicalPdf, physicalRecord, false), "seed physical move bibliography");
    // The current production helper moves PDF + .luna-thumb.* only. A
    // .luna-meta.json is NOT in its move set: test a supported thumbnail
    // collision and separately expose the metadata-sidecar limitation.
    const QByteArray occupiedBytes("unrelated-existing-thumbnail");
    check(writeFixture(targetThumb, occupiedBytes), "seed destination thumbnail collision");
    const auto rejectedMove = luna::movePdfWithThumbnails(physicalPdf, physicalTarget);
    check(!rejectedMove.success, "physical move rejects occupied thumbnail destination");
    check(readFixture(targetThumb) == occupiedBytes &&
          readFixture(physicalPdf) == pdfBytes &&
          readFixture(physicalThumb) == thumbBytes &&
          readFixture(physicalMeta) == metaBytes &&
          !QFileInfo::exists(targetPdf) && !QFileInfo::exists(targetMeta),
          "collision preserves all original files");
    check(QFile::remove(targetThumb), "clear disposable collision fixture");
    const auto movedFiles = luna::movePdfWithThumbnails(physicalPdf, physicalTarget);
    check(movedFiles.success && movedFiles.documentPath == targetPdf,
          "production helper moves physical PDF and sidecars");
    check(!QFileInfo::exists(physicalPdf) &&
          !QFileInfo::exists(physicalThumb), "PDF and thumbnail source files removed after move");
    check(readFixture(targetPdf) == pdfBytes &&
          readFixture(targetThumb) == thumbBytes, "moved PDF and thumbnail bytes preserved");
    check(readFixture(physicalMeta) == metaBytes && !QFileInfo::exists(targetMeta),
          "metadata JSON sidecar remains at source: current helper does not move it");
    check(store.relocate(physicalPdf, movedFiles.documentPath),
          "relocate bibliography only after successful physical move");
    luna::Bibliography afterPhysicalMove;
    const QVariantMap physicalRestored = afterPhysicalMove.record(targetPdf);
    check(physicalRestored.value(QStringLiteral("title")).toString() ==
          QStringLiteral("Manually Verified Move Title"), "physical move title survives new store");
    check(physicalRestored.value(QStringLiteral("titleSource")).toString() ==
          QStringLiteral("manual"), "physical move preserves manual title provenance");
    check(physicalRestored.value(QStringLiteral("metadata_status")).toString() ==
          QStringLiteral("needs_review"), "physical move preserves review status");

    // A stale edit dialog must not erase a newer background metadata refresh.
    // Exercise the real saveEdits three-way merge with disposable QSettings.
    const QString staleIdentity = QStringLiteral("synthetic:stale-dialog");
    QVariantMap staleInitial{{QStringLiteral("doi"), QStringLiteral("10.1000/stale-dialog")},
                             {QStringLiteral("title"), QStringLiteral("Initial Dialog Title")},
                             {QStringLiteral("journal"), QStringLiteral("Initial Journal")},
                             {QStringLiteral("volume"), QStringLiteral("1")}};
    check(store.save(staleIdentity, staleInitial, false), "seed stale dialog record");
    const QVariantMap dialogBefore = store.record(staleIdentity);
    QVariantMap background = dialogBefore;
    background[QStringLiteral("journal")] = QStringLiteral("Background Verified Journal");
    background[QStringLiteral("journalSource")] = QStringLiteral("Crossref");
    background[QStringLiteral("volume")] = QStringLiteral("42");
    background[QStringLiteral("volumeSource")] = QStringLiteral("Crossref");
    check(store.save(staleIdentity, background, false), "simulate background refresh");
    QVariantMap dialogAfter = dialogBefore;
    dialogAfter[QStringLiteral("title")] = QStringLiteral("Human Edited Dialog Title");
    check(store.saveEdits(staleIdentity, dialogBefore, dialogAfter),
          "merge only the explicitly edited dialog field");
    const QVariantMap mergedDialog = store.record(staleIdentity);
    check(mergedDialog.value(QStringLiteral("title")).toString()
          == QStringLiteral("Human Edited Dialog Title"), "human dialog title saved");
    check(mergedDialog.value(QStringLiteral("titleSource")).toString()
          == QStringLiteral("manual"), "human dialog provenance saved");
    check(mergedDialog.value(QStringLiteral("journal")).toString()
          == QStringLiteral("Background Verified Journal"), "stale dialog preserves new journal");
    check(mergedDialog.value(QStringLiteral("journalSource")).toString()
          == QStringLiteral("Crossref"), "stale dialog preserves provider provenance");
    check(mergedDialog.value(QStringLiteral("volume")).toString()
          == QStringLiteral("42"), "stale dialog preserves new volume");

    // An invalid human DOI must reject the entire edit atomically, including
    // unrelated fields changed in the same dialog; no partial QSettings write.
    const QVariantMap beforeInvalid = store.record(staleIdentity);
    QVariantMap invalidEdit = beforeInvalid;
    invalidEdit[QStringLiteral("doi")] = QStringLiteral("not-a-doi");
    invalidEdit[QStringLiteral("title")] = QStringLiteral("Must Not Be Saved");
    check(!store.saveEdits(staleIdentity, beforeInvalid, invalidEdit),
          "reject invalid manual DOI");
    const QVariantMap afterInvalid = store.record(staleIdentity);
    check(afterInvalid.value(QStringLiteral("doi")) == beforeInvalid.value(QStringLiteral("doi")),
          "invalid DOI leaves stored DOI unchanged");
    check(afterInvalid.value(QStringLiteral("title")) == beforeInvalid.value(QStringLiteral("title")),
          "invalid DOI does not partially save title");
    check(afterInvalid.value(QStringLiteral("metadata_status"))
          == beforeInvalid.value(QStringLiteral("metadata_status")),
          "invalid DOI leaves metadata status unchanged");
    luna::Bibliography restartedDialog;
    const QVariantMap durableDialog = restartedDialog.record(staleIdentity);
    check(durableDialog.value(QStringLiteral("title")).toString()
          == QStringLiteral("Human Edited Dialog Title"), "dialog edit survives new store");
    check(durableDialog.value(QStringLiteral("journal")).toString()
          == QStringLiteral("Background Verified Journal"), "background field survives new store");

    // Unlike creating a second Bibliography object, this exits and re-enters
    // the actual application binary in a separate process. Never pass user
    // paths, user settings or real PDFs to this synthetic-only child.
    QProcess freshProcess;
    freshProcess.start(QCoreApplication::applicationFilePath(),
                       {QStringLiteral("--verify-fresh-process"), root.path(),
                        movedFiles.documentPath, staleIdentity});
    check(freshProcess.waitForStarted(10000), "start isolated fresh process");
    check(freshProcess.waitForFinished(15000), "fresh process finished");
    check(freshProcess.exitStatus() == QProcess::NormalExit &&
          freshProcess.exitCode() == 0, "fresh process restart regression");

    std::fprintf(stderr, "LUNA_ACCURACY_SNAPSHOT_PASS: 4 synthetic cases\n");
    return 0;
}
