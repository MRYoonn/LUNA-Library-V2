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
    if (si.value("title") != QStringLiteral(
            "Catalytic Ketyl-Olefin Cyclizations Enabled by Proton Coupled-Electron Transfer") ||
        !si.value("authors").toString().contains(QStringLiteral("Robert R. Knowles"))) return 22;
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
            QStringLiteral("Fused 3D boron heterocycles via EnT catalysis:\n"
                           "synthesis, modification and validation as beta-lactamase inhibitors\n"
                           "Hannah M. Kortman, Hao Fang, Kane A. C. Bastick, Charlotte Völkel,\n"
                           "Dominik Oberthür, Peter H. Seeberger, Markus Perbandt and John J. Molloy"))
        .value("authors") != QStringLiteral("John J. Molloy")) {
        // A wrapped author list is a supported common RSC case.
        return 28;
    }
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
    store.ingest("auto", QStringLiteral("PDF title"), QStringLiteral("PDF author"),
        {{"journal", QStringLiteral("J. Org. Chem.")}, {"year", QStringLiteral("2025")},
         {"volume", QStringLiteral("90")}, {"page", QStringLiteral("100–108")}});
    if (store.record("auto").value("title") != QStringLiteral("PDF title")) return 12;
    if (luna::Bibliography::citation(store.record("auto")) !=
        QStringLiteral("J. Org. Chem. 2025, 90, 100–108")) return 13;
    store.ingest("invalid-year", {}, {}, {{"year", QStringLiteral("unknown")}});
    if (!store.record("invalid-year").value("year").toString().isEmpty()) return 18;
    if (!store.save("conflict", {{"citation", QStringLiteral("Keep existing")}})) return 14;
    if (store.relocate("auto", "conflict")) return 15;
    if (store.record("conflict").value("citation") != QStringLiteral("Keep existing")) return 16;
    if (store.save(QString(), structured)) return 17;

    qInfo() << "[PASS] Local-only citation paste/format, override, move/restart, collision and PDF info";
    return 0;
}
