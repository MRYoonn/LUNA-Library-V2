// SPDX-License-Identifier: MIT
// All identities, names and DOI values below are synthetic and offline.
#include "../../src/lunasirows.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

using namespace luna::SiRows;
static void check(bool ok, const char* label) {
    if (ok) return;
    std::fprintf(stderr, "LUNA_SI_ROWS_FAIL: %s\n", label);
    std::abort();
}
static const QString mainId = QStringLiteral("11111111-1111-4111-8111-111111111111");
static const QString si1 = QStringLiteral("22222222-2222-4222-8222-222222222222");
static const QString si2 = QStringLiteral("33333333-3333-4333-8333-333333333333");
static const QString doi = QStringLiteral("10.1021/jacs.6c01234");
static const QString url = QStringLiteral("https://acs.figshare.com/articles/dataset/Example_SI/12345");

static Public candidate(const QString& name, qint64 fileId) {
    Public p;
    p.articleId = 12345;
    p.fileId = fileId;
    p.resourceDoi = doi;
    p.title = QStringLiteral("Synthetic public SI");
    p.filename = name;
    p.landingUrl = url;
    p.status = QStringLiteral("landing_verified"); // untrusted restored status
    return p;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QList<Local> locals{
        {si1, mainId, QStringLiteral("Private/Supporting1.pdf"), QStringLiteral("user_confirmed")},
        {si2, mainId, QStringLiteral("Private/Supporting2.pdf"), QStringLiteral("user_confirmed")}};
    const auto localOnly = project(mainId, doi, locals);
    check(localOnly.valid && localOnly.localSi.size() == 2 &&
          localOnly.publicSi.isEmpty() &&
          localOnly.lookupStatus == QStringLiteral("not_checked"),
          "two confirmed local SI and no public lookup");
    check(localOnly.localSi.first().value(QStringLiteral("filename")).toString() ==
          QStringLiteral("Supporting1.pdf") &&
          !localOnly.localSi.first().contains(QStringLiteral("relative_path")),
          "local paths never exposed in card projection");
    check(localOnly.fallbackUrl == QStringLiteral("https://doi.org/10.1021/jacs.6c01234"),
          "canonical parent fallback");

    Lookup verified{doi, QStringLiteral("verified"),
                    {candidate(QStringLiteral("Supporting1.pdf"), 123),
                     candidate(QStringLiteral("Supporting2.pdf"), 124)}};
    const auto rows = project(mainId, doi, locals, &verified);
    check(rows.valid && rows.localSi.size() == 2 && rows.publicSi.size() == 2,
          "Main SI1 SI2 independent public projection");
    for (const auto& row : rows.publicSi)
        check(row.value(QStringLiteral("status")).toString() == QStringLiteral("unconfirmed") &&
              row.value(QStringLiteral("local_document_id")).isNull(),
              "restored verified metadata never proves HTTP or local link");

    Lookup offline = verified;
    offline.status = QStringLiteral("offline");
    check(project(mainId, doi, locals, &offline).publicSi.isEmpty(),
          "offline cannot reuse stale verified SI");
    offline.status = QStringLiteral("rate_limited");
    check(project(mainId, doi, locals, &offline).publicSi.isEmpty(),
          "HTTP 429 cannot reuse stale verified SI");
    offline.status = QStringLiteral("not_found");
    check(project(mainId, doi, locals, &offline).publicSi.isEmpty(),
          "HTTP 404 cannot reuse stale verified SI");

    Lookup foreign = verified;
    foreign.doi = QStringLiteral("10.1021/jacs.6c99999");
    const auto mismatch = project(mainId, doi, locals, &foreign);
    check(mismatch.lookupStatus == QStringLiteral("needs_review") &&
          mismatch.publicSi.isEmpty() && mismatch.localSi.size() == 2 &&
          mismatch.fallbackUrl == localOnly.fallbackUrl,
          "foreign DOI cannot forge public rows or fallback");

    Lookup malicious = verified;
    malicious.candidates[0].landingUrl =
        QStringLiteral("https://acs.figshare.com.evil.invalid/articles/dataset/Example_SI/12345");
    const auto rejected = project(mainId, doi, locals, &malicious);
    check(rejected.lookupStatus == QStringLiteral("needs_review") &&
          rejected.publicSi.size() == 1 &&
          rejected.publicSi.first().value(QStringLiteral("status")).toString() ==
              QStringLiteral("unconfirmed"),
          "malicious landing quarantines verified collection");

    malicious = verified;
    malicious.candidates[0].articleId = 999;
    check(project(mainId, doi, locals, &malicious).lookupStatus ==
          QStringLiteral("needs_review"), "wrong Figshare article ID");
    malicious = verified;
    malicious.candidates[0].filename = QStringLiteral("bad／SI.pdf");
    check(project(mainId, doi, locals, &malicious).publicSi.size() == 1,
          "NFKC path separator rejected");
    malicious = verified;
    malicious.candidates[0].title = QStringLiteral("bad\u202eTitle");
    check(project(mainId, doi, locals, &malicious).publicSi.size() == 1,
          "bidi-control publisher title rejected");

    Lookup duplicate{doi, QStringLiteral("verified"),
                     {candidate(QStringLiteral("SI.pdf"), 123),
                      candidate(QStringLiteral("ＳＩ.pdf"), 124)}};
    check(project(mainId, doi, locals, &duplicate).publicSi.isEmpty(),
          "NFKC duplicate public filename suppressed");
    duplicate.candidates = {candidate(QStringLiteral("SI1.pdf"), 123),
                            candidate(QStringLiteral("SI2.pdf"), 123)};
    check(project(mainId, doi, locals, &duplicate).publicSi.isEmpty(),
          "duplicate file IDs suppressed");

    auto invalidLocal = locals;
    invalidLocal[0].source = QStringLiteral("filename_match");
    check(!project(mainId, doi, invalidLocal).valid &&
          project(mainId, doi, invalidLocal).localSi.isEmpty(),
          "filename match is never user confirmation");
    invalidLocal = locals;
    invalidLocal[0].relativePath = QStringLiteral("../Outside.pdf");
    check(!project(mainId, doi, invalidLocal).valid,
          "path traversal local record blocked");
    invalidLocal = locals;
    invalidLocal[1].relativePath = QStringLiteral("Private/Ｓupporting1.pdf");
    invalidLocal[0].relativePath = QStringLiteral("Private/Supporting1.pdf");
    check(!project(mainId, doi, invalidLocal).valid,
          "NFKC-equivalent local path collision blocked");
    check(!project(QStringLiteral("not-a-uuid"), doi, locals).valid &&
          !project(mainId, QStringLiteral("not-a-doi"), locals).valid,
          "invalid main identity fails closed");
    check(!officialLanding(QStringLiteral("https://acs.figshare.com/articles/dataset/SI/012345"),
                           12345), "noncanonical numeric article path blocked");
    std::fprintf(stderr, "LUNA_SI_ROWS_TESTS_PASS: synthetic QtCore display projection\n");
    return 0;
}
