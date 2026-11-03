// SPDX-License-Identifier: MIT
// Isolated HKCU-only Windows association tests: never touch a real .pdf UserChoice.
#include "lunapdfassociation.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QDebug>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <vector>

static QString readRegistry(const QString& path,const QString& name={}) {
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,reinterpret_cast<const wchar_t*>(path.utf16()),
                    0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return {};
    DWORD type=0,size=0;
    const LONG result=RegQueryValueExW(key,
        name.isEmpty()?nullptr:reinterpret_cast<const wchar_t*>(name.utf16()),
        nullptr,&type,nullptr,&size);
    if(result!=ERROR_SUCCESS || type!=REG_SZ || !size) {RegCloseKey(key);return {};}
    std::vector<wchar_t> value(size/sizeof(wchar_t)+1,0);
    DWORD capacity=size;
    const LONG read=RegQueryValueExW(key,
        name.isEmpty()?nullptr:reinterpret_cast<const wchar_t*>(name.utf16()),
        nullptr,&type,reinterpret_cast<BYTE*>(value.data()),&capacity);
    RegCloseKey(key);
    return read==ERROR_SUCCESS?QString::fromWCharArray(value.data()):QString();
}
static bool emptyExists(const QString& path,const QString& name) {
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,reinterpret_cast<const wchar_t*>(path.utf16()),
                     0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return false;
    DWORD type=0,len=0;
    const LONG res=RegQueryValueExW(key,reinterpret_cast<const wchar_t*>(name.utf16()),
                                   nullptr,&type,nullptr,&len);
    RegCloseKey(key);
    return res==ERROR_SUCCESS && type==REG_NONE && len==0;
}
struct RegistryCleanup {
    QString key;
    ~RegistryCleanup() {
        RegDeleteTreeW(HKEY_CURRENT_USER,reinterpret_cast<const wchar_t*>(key.utf16()));
    }
};
#endif

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    auto fail=[](const QString& s){qCritical().noquote()<<s;return 1;};
#ifndef Q_OS_WIN
    return 0;
#else
    QTemporaryDir dir;
    if(!dir.isValid())return fail(QStringLiteral("No temporary registry fixture"));
    const QString exe=QDir(dir.path()).filePath(QStringLiteral("LUNA.exe"));
    QFile source(exe);
    if(!source.open(QIODevice::WriteOnly) || source.write("test exe")!=8)
        return fail(QStringLiteral("Could not create synthetic LUNA.exe"));
    source.close();
    const QString isolated=QStringLiteral("Software\\LUNA-Association-Test\\QT-%1")
        .arg(QCoreApplication::applicationPid());
    RegistryCleanup cleanup{isolated};
    const QString prefix=isolated+QStringLiteral("\\");
    QString error;
    if(luna::PdfAssociation::registerCurrentUser(
           exe,&error,QStringLiteral("Software\\NotAllowed\\")))
        return fail(QStringLiteral("Arbitrary registry prefix was not blocked"));
    if(luna::PdfAssociation::registerCurrentUser(
           QDir(dir.path()).filePath(QStringLiteral("missing.exe")),&error,prefix))
        return fail(QStringLiteral("Missing executable was registered"));
    if(!luna::PdfAssociation::registerCurrentUser(exe,&error,prefix))
        return fail(QStringLiteral("Isolated registration failed: ")+error);
    if(!luna::PdfAssociation::registerCurrentUser(exe,&error,prefix))
        return fail(QStringLiteral("Idempotent registration failed: ")+error);
    const QString registered=QDir::toNativeSeparators(QFileInfo(exe).canonicalFilePath());
    const QString progid=prefix+QStringLiteral("Classes\\LUNA.Library.PDF");
    if(readRegistry(progid)!=QStringLiteral("LUNA PDF Document"))
        return fail(QStringLiteral("Document ProgID missing"));
    if(readRegistry(progid+QStringLiteral("\\DefaultIcon")) !=
       QStringLiteral("\"")+registered+QStringLiteral("\",-2"))
        return fail(QStringLiteral("PDF DefaultIcon must point to embedded red ID 2"));
    if(readRegistry(progid+QStringLiteral("\\shell\\open\\command")) !=
       QStringLiteral("\"")+registered+QStringLiteral("\" \"%1\""))
        return fail(QStringLiteral("Launch command quoting invalid"));
    if(!emptyExists(prefix+QStringLiteral("Classes\\.pdf\\OpenWithProgids"),
                    QStringLiteral("LUNA.Library.PDF")))
        return fail(QStringLiteral("Explorer Open With registration absent"));
    if(readRegistry(prefix+QStringLiteral("LUNA Library\\Capabilities\\FileAssociations"),
                    QStringLiteral(".pdf"))!=QStringLiteral("LUNA.Library.PDF"))
        return fail(QStringLiteral("Default Apps capability not advertised"));
    if(readRegistry(prefix+QStringLiteral("RegisteredApplications"),
                    QStringLiteral("LUNA Library"))!=
       QStringLiteral("Software\\LUNA Library\\Capabilities"))
        return fail(QStringLiteral("RegisteredApplications mapping missing"));
    HKEY forbidden=nullptr;
    const QString userChoice=prefix+QStringLiteral("Classes\\.pdf\\UserChoice");
    const LONG bad=RegOpenKeyExW(HKEY_CURRENT_USER,
        reinterpret_cast<const wchar_t*>(userChoice.utf16()),0,KEY_READ,&forbidden);
    if(bad==ERROR_SUCCESS)RegCloseKey(forbidden);
    if(bad==ERROR_SUCCESS)
        return fail(QStringLiteral("Must not create Windows .pdf UserChoice"));
    qInfo()<<"PASS: HKCU isolated LUNA PDF ProgID, embedded icon -2, secure quoted open command, Windows Default Apps candidate; UserChoice untouched";
    return 0;
#endif
}
