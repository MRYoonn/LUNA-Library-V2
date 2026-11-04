// SPDX-License-Identifier: MIT
// Windows real HKCU registry test with a private non-association key.
// NEVER modifies the runner's .pdf UserChoice, other applications or defaults.
#include "lunafileassociation.h"
#include <QCoreApplication>
#include <QDebug>
#include <QString>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
#ifdef Q_OS_WIN
    using namespace luna::PdfAssociation;
    const QString testKey=QStringLiteral("Software\\LUNA Library\\Tests\\PdfAssociationSynthetic");
    QString error;
    if(!writeValue(testKey,QStringLiteral("SyntheticOpenCommand"),
       QStringLiteral("\"C:\\LUNA test folder\\LUNA.exe\" \"%1\""),&error)) {
        qCritical().noquote()<<error;return 1;
    }
    if(readValue(testKey,QStringLiteral("SyntheticOpenCommand")) !=
       QStringLiteral("\"C:\\LUNA test folder\\LUNA.exe\" \"%1\"")) {
        qCritical()<<"PDF open command quoting was not saved correctly";
        removeKey(testKey);return 2;
    }
    if(!writeValue(testKey,{},QStringLiteral("LUNA synthetic document"),&error) ||
       readValue(testKey,{})!=QStringLiteral("LUNA synthetic document")) {
        qCritical()<<"Windows default Reg_SZ value did not round-trip";
        removeKey(testKey);return 3;
    }
    if(!removeValue(testKey,QStringLiteral("SyntheticOpenCommand"),&error) ||
       !readValue(testKey,QStringLiteral("SyntheticOpenCommand")).isEmpty() ||
       !removeKey(testKey,&error) ||
       !readValue(testKey,{}).isEmpty()) {
        qCritical().noquote()<<error;return 4;
    }
    qInfo()<<"[PASS] Windows HKCU-only PDF association registry round trip, quote escaping, cleanup";
#else
    qInfo()<<"[PASS] PDF file associations are Windows-only";
#endif
    return 0;
}
