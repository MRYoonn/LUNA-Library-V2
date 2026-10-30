// SPDX-License-Identifier: MIT
#include "lunabibliography.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());

    const QString firstPage = QStringLiteral(
        "Research Article\nJ. Am. Chem. Soc. 2025, 147, 10012–10019\n"
        "Visible title\nAuthor One, Author Two\n");
    const QVariantMap parsed = luna::Bibliography::firstPageCitation(firstPage);
    if (parsed.value("journal") != QStringLiteral("J. Am. Chem. Soc.") ||
        parsed.value("year") != QStringLiteral("2025") ||
        parsed.value("volume") != QStringLiteral("147") ||
        parsed.value("page") != QStringLiteral("10012-10019")) return 19;
    const QVariantMap ange = luna::Bibliography::firstPageCitation(
        QStringLiteral("Angew. Chem. Int. Ed. 2024, 63, e202410123"));
    if (ange.value("page") != QStringLiteral("e202410123")) return 20;
    const QVariantMap wrapped = luna::Bibliography::firstPageCitation(
        QStringLiteral("Article\nJ. Am. Chem. Soc.\n2026, 148, 12050–12058"));
    if (wrapped.value("page") != QStringLiteral("12050-12058")) return 23;
    if (!luna::Bibliography::firstPageCitation(
            QStringLiteral("Supporting Information\nTable of Contents\nNo volume data")).isEmpty()) return 21;
    const QVariantMap si = luna::Bibliography::firstPageIdentity(
        QStringLiteral("Supporting Information\nCatalytic Ketyl-Olefin Cyclizations Enabled by Proton Coupled-Electron\n"
                       "Transfer\nKyle T. Tarantino, Peng Liu, Robert R. Knowles*\n"
                       "Department of Chemistry, Princeton University"));
    if (!si.isEmpty()) return 22; // SI without authoritative publication data: leave blank.
    const QVariantMap proof = luna::Bibliography::firstPageCitation(
        QStringLiteral("Nature Communications\nArticle in Press\nPhoto-enabled chemistry"));
    if (proof.value("journal") != QStringLiteral("Nature Communications") ||
        !proof.value("year").toString().isEmpty()) return 24;
    const QVariantMap proofIdentity = luna::Bibliography::firstPageIdentity(
        QStringLiteral("Nature Communications\nArticle in Press\n"
            "Photo-enabled and thioamide-directed alpha-C(sp3)-H\n"
            "carboxylation of alpha-substituted benzylamines with\n"
            "CO2 towards alpha-tertiary amino acids\n"
            "Received: 16 October 2025\nAccepted: 14 April 2026\n"
            "Jie Xu, Chang Liu, Chao-Yi Liu, Miao Peng, Yan Zhang, Ding-Hai Wang & Heng Jiang\n"));
    if (!proofIdentity.value("title").toString().contains(
            QStringLiteral("Photo-enabled and thioamide-directed")) ||
        !proofIdentity.value("authors").toString().contains(QStringLiteral("Heng Jiang")))
        return 25;
    const QString rscAuthors = QStringLiteral(
        "Hannah M. Kortman, Hao Fang, Kane A. C. Bastick, Charlotte Völkel, "
        "Dominik Oberthür, Peter H. Seeberger, Markus Perbandt and John J. Molloy");
    if (luna::Bibliography::lastListedAuthor(rscAuthors) != QStringLiteral("John J. Molloy"))
        return 26;
    if (luna::Bibliography::lastListedAuthor(
            QStringLiteral("A. Researcher, B. Contributor, C. Professor")) !=
        QStringLiteral("C. Professor")) return 27;
    if (luna::Bibliography::firstPageIdentity(
            QStringLiteral("Chemical Science\nCite this: Chem. Sci., 2026, 17, 247\n"
                           "Fused 3D boron heterocycles via EnT catalysis:\n"
                           "synthesis, modification and validation as beta-lactamase inhibitors\n"
                           "Hannah M. Kortman, Hao Fang, Kane A. C. Bastick, Charlotte Völkel,\n"
                           "Dominik Oberthür, Peter H. Seeberger, Markus Perbandt and John J. Molloy"))
        .value("authors") != QStringLiteral("John J. Molloy")) {
        // A wrapped author list is a supported common RSC case.
        return 28;
    }
    // Regression from a real user report: the PDF metadata and first extracted
    // author fragment said "Hao Fang" while the cover ends in John J. Molloy.
    const QString rscCover = QStringLiteral(
        "Chemical Science\nCite this: Chem. Sci., 2026, 17, 247\n"
        "Fused 3D boron heterocycles via EnT catalysis:\n"
        "synthesis, modification and validation as beta-lactamase inhibitors\n"
        "Hannah M. Kortman, Hao Fang, Kane A. C. Bastick, Charlotte Völkel,\n"
        "Dominik Oberthür, Peter H. Seeberger, Markus Perbandt\n"
        "and John J. Molloy*a\n");
    const QString last = luna::Bibliography::firstPageLastAuthor(rscCover);
    if (last != QStringLiteral("John J. Molloy")) return 38;
    if (luna::Bibliography::firstPageLastAuthor(QStringLiteral(
            "Chemical Science\nCite this: Chem. Sci., 2026, 17, 247\n"
            "Hannah M. Kortman, Hao Fang")).size() != 0) return 39;
    // Simulate a pre-fix version that had persisted the PDF Info author Hao Fang.
    if (!store.save("auto-rsc", {{"title", QStringLiteral("Fused 3D boron heterocycles via EnT catalysis")},
                                 {"authors", QStringLiteral("Hao Fang")},
                                 {"source", QStringLiteral("PDF metadata")}}, false)) return 40;
    if (!store.record("auto-rsc").value("authors").toString().isEmpty()) return 41;
    const QVariantMap verifiedLast{{"authors", last},
                                   {"authorEvidence", QStringLiteral("first-page-terminal-and")}};
    if (!store.enrichFromFirstPage("auto-rsc", verifiedLast) ||
        store.record("auto-rsc").value("authors") != QStringLiteral("John J. Molloy")) return 42;
    if (!store.save("manual-rsc", {{"authors", QStringLiteral("Manual Correct Author")}}, true))
        return 43;
    store.enrichFromFirstPage("manual-rsc", verifiedLast);
    if (store.record("manual-rsc").value("authors") != QStringLiteral("Manual Correct Author"))
        return 44;
    if (!store.save("new-pdf-info", {}, false)) return 45;
    store.ingest("new-pdf-info", QStringLiteral("Fused 3D boron heterocycles via EnT catalysis"),
                 QStringLiteral("Hao Fang"));
    if (!store.record("new-pdf-info").value("authors").toString().isEmpty()) return 46;
    if (!store.save("untrusted-proposal", {}, false)) return 47;
    if (store.enrichFromFirstPage("untrusted-proposal", {{"authors", QStringLiteral("Hao Fang")}}) ||
        !store.record("untrusted-proposal").value("authors").toString().isEmpty())
        return 48;
    const QVariantMap jpcb = luna::Bibliography::firstPageCitation(
        QStringLiteral("J. Phys. Chem. B 2004, 108, 5472–5476\nLow Energy Electron Interactions with Uracil"));
    if (jpcb.value("journal") != QStringLiteral("J. Phys. Chem. B") ||
        jpcb.value("year") != QStringLiteral("2004") ||
        jpcb.value("page") != QStringLiteral("5472-5476")) return 29;
    if (!luna::Bibliography::firstPageCitation(
        QStringLiteral("Supporting Information\nJ. Org. Chem. 2026, 91, 124–129")).isEmpty()) return 30;
    if (luna::Bibliography::credibleTitle(QStringLiteral("No Job Name")) ||
        luna::Bibliography::credibleTitle(QStringLiteral("Microsoft Word - SI-JOC.docx")) ||
        luna::Bibliography::credibleAuthor(QStringLiteral("as well as the energetics involved"))) return 31;
    const QVariantMap structured{
        {"title", QStringLiteral("Synthetic Paper")},
        {"authors", QStringLiteral("Author A")},
        {"journal", QStringLiteral("J. Am. Chem. Soc.")},
        {"year", QStringLiteral("2026")},
        {"volume", QStringLiteral("148")},
        {"page", QStringLiteral("15354–15363")}
    };
    const QString formatted = QStringLiteral("J. Am. Chem. Soc. 2026, 148, 15354–15363");
    if (luna::Bibliography::citation(structured) != formatted) return 2;
    if (!luna::Bibliography::citation({}).isEmpty()) return 3;

    const QString pasted = QStringLiteral("Author A, J. Am. Chem. Soc. 2026, 148, 15354–15363.");
    QVariantMap complete = structured;
    complete["citation"] = pasted;
    if (luna::Bibliography::citation(complete) != pasted) return 4;

    luna::Bibliography store;
    if (!store.save("before", complete)) return 5;
    store.ingest("before", QStringLiteral("Wrong Title"), QStringLiteral("Wrong Author"));
    if (store.record("before").value("title") != QStringLiteral("Synthetic Paper")) return 6;
    if (store.record("before").value("citation") != pasted) return 7;
    if (!store.relocate("before", "after") || store.record("before").value("citation") != pasted) return 8;
    if (store.record("after").value("citation") != pasted) return 9;
    if (!store.relocate("after", "before")) return 10;
    { luna::Bibliography reopened;
      if (reopened.record("before").value("authors") != QStringLiteral("Author A")) return 11;
    }
    // Local metadata import must only fill missing fields, never erase a citation.
    store.ingest("auto", QStringLiteral("Photocatalytic Coupling of Uracil Derivatives"), QStringLiteral("Alice B. Example"),
        {{"journal", QStringLiteral("J. Org. Chem.")}, {"year", QStringLiteral("2025")},
         {"volume", QStringLiteral("90")}, {"page", QStringLiteral("100–108")}});
    if (store.record("auto").value("title") != QStringLiteral("Photocatalytic Coupling of Uracil Derivatives")) return 12;
    if (luna::Bibliography::citation(store.record("auto")) !=
        QStringLiteral("J. Org. Chem. 2025, 90, 100–108")) return 13;
    if (!store.save("garbage", {{"title", QStringLiteral("No Job Name")},
                                {"authors", QStringLiteral("as well as the energetics involved")}}, false))
        return 32;
    if (!store.record("garbage").value("title").toString().isEmpty() ||
        !store.record("garbage").value("authors").toString().isEmpty()) return 33;
    if (!store.save("old-fragment", {{"title", QStringLiteral("Fused 3D boron heterocycles via EnT")},
                                      {"authors", QStringLiteral("validation as beta-lactamase inhibitors")}}, true))
        return 36;
    if (!store.record("old-fragment").value("authors").toString().isEmpty()) return 37;
    if (!store.enrichFromFirstPage("garbage", jpcb)) return 34;
    if (store.record("garbage").value("journal") != QStringLiteral("J. Phys. Chem. B") ||
        !store.record("garbage").value("title").toString().isEmpty()) return 35;
    store.ingest("invalid-year", {}, {}, {{"year", QStringLiteral("unknown")}});
    if (!store.record("invalid-year").value("year").toString().isEmpty()) return 18;
    if (!store.save("conflict", {{"citation", QStringLiteral("Keep existing")}})) return 14;
    if (store.relocate("auto", "conflict")) return 15;
    if (store.record("conflict").value("citation") != QStringLiteral("Keep existing")) return 16;
    if (store.save(QString(), structured)) return 17;

    qInfo() << "[PASS] Local-only citation paste/format, override, move/restart, collision and PDF info";
    return 0;
}
