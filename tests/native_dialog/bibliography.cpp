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
    store.ingest("auto", QStringLiteral("PDF title"), QStringLiteral("PDF author"));
    if (store.record("auto").value("title") != QStringLiteral("PDF title")) return 12;
    if (!store.record("auto").value("journal").toString().isEmpty()) return 13;
    if (!store.save("conflict", {{"citation", QStringLiteral("Keep existing")}})) return 14;
    if (store.relocate("auto", "conflict")) return 15;
    if (store.record("conflict").value("citation") != QStringLiteral("Keep existing")) return 16;
    if (store.save(QString(), structured)) return 17;

    qInfo() << "[PASS] Local-only citation paste/format, override, move/restart, collision and PDF info";
    return 0;
}
