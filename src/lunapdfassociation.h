// SPDX-License-Identifier: MIT
#pragma once
// Register LUNA as a PER-USER .pdf candidate. Windows 10/11 owns the actual
// UserChoice association: never write UserChoice or hijack the default app.
#include <QDir>
#include <QFileInfo>
#include <QString>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shellapi.h>
#endif

namespace luna::PdfAssociation {
#ifdef Q_OS_WIN
inline bool writeValue(const QString& keyPath, const QString& name,
                       const QString& value, QString* failure) {
    HKEY handle=nullptr;
    const LSTATUS opened=RegCreateKeyExW(HKEY_CURRENT_USER,
        reinterpret_cast<const wchar_t*>(keyPath.utf16()),0,nullptr,
        REG_OPTION_NON_VOLATILE,KEY_SET_VALUE,nullptr,&handle,nullptr);
    if(opened!=ERROR_SUCCESS) {
        if(failure)*failure=QStringLiteral("Registry key %1: Win32 %2")
            .arg(keyPath).arg(opened);
        return false;
    }
    const auto chars=reinterpret_cast<const BYTE*>(value.utf16());
    const LSTATUS status=RegSetValueExW(handle,
        name.isEmpty()?nullptr:reinterpret_cast<const wchar_t*>(name.utf16()),
        0,REG_SZ,chars,DWORD((value.size()+1)*sizeof(wchar_t)));
    RegCloseKey(handle);
    if(status!=ERROR_SUCCESS && failure)
        *failure=QStringLiteral("Registry value %1: Win32 %2").arg(keyPath).arg(status);
    return status==ERROR_SUCCESS;
}
inline bool writeEmptyValue(const QString& keyPath,const QString& name,QString* failure) {
    HKEY handle=nullptr;
    const LSTATUS opened=RegCreateKeyExW(HKEY_CURRENT_USER,
        reinterpret_cast<const wchar_t*>(keyPath.utf16()),0,nullptr,
        REG_OPTION_NON_VOLATILE,KEY_SET_VALUE,nullptr,&handle,nullptr);
    if(opened!=ERROR_SUCCESS) {
        if(failure)*failure=QStringLiteral("Registry key %1: Win32 %2")
            .arg(keyPath).arg(opened);
        return false;
    }
    const LSTATUS status=RegSetValueExW(handle,
        reinterpret_cast<const wchar_t*>(name.utf16()),0,REG_NONE,nullptr,0);
    RegCloseKey(handle);
    if(status!=ERROR_SUCCESS && failure)
        *failure=QStringLiteral("OpenWithProgids: Win32 %1").arg(status);
    return status==ERROR_SUCCESS;
}
inline bool registerCurrentUser(const QString& exePath, const QString& pdfIconPath,
                                QString* failure=nullptr,
                                const QString& prefix=QStringLiteral("Software\\")) {
    const QFileInfo executable(exePath),icon(pdfIconPath);
    // A private test prefix is allowed for synthetic HKCU registry checks;
    // the user-visible action can only use the real HKCU\Software prefix.
    if(prefix!=QStringLiteral("Software\\") &&
       !prefix.startsWith(QStringLiteral("Software\\LUNA-Association-Test\\"))) {
        if(failure)*failure=QStringLiteral("Invalid registry prefix");
        return false;
    }
    if(!executable.isFile() || executable.isSymLink() ||
       executable.fileName().compare(QStringLiteral("LUNA.exe"),Qt::CaseInsensitive)!=0 ||
       !icon.isFile() || icon.isSymLink() ||
       icon.fileName().compare(QStringLiteral("LUNA-PDF.ico"),Qt::CaseInsensitive)!=0 ||
       executable.canonicalPath().compare(icon.canonicalPath(),Qt::CaseInsensitive)!=0) {
        if(failure)*failure=QStringLiteral("LUNA.exe와 LUNA-PDF.ico를 같은 폴더에 두세요.");
        return false;
    }
    const QString exe=QDir::toNativeSeparators(executable.canonicalFilePath());
    const QString ico=QDir::toNativeSeparators(icon.canonicalFilePath());
    const QString association=prefix+QStringLiteral("Classes\\LUNA.Library.PDF");
    const QString executableKey=prefix+QStringLiteral("Classes\\Applications\\LUNA.exe");
    const QString capabilities=prefix+QStringLiteral("LUNA Library\\Capabilities");
    const QString open=QStringLiteral("\"")+exe+QStringLiteral("\" \"%1\"");
    const QString iconValue=QStringLiteral("\"")+ico+QStringLiteral("\",0");
    const auto set=[&](const QString& key,const QString& name,const QString& value){
        return writeValue(key,name,value,failure);
    };
    if(!set(association,QString(),QStringLiteral("LUNA PDF Document")) ||
       !set(association+QStringLiteral("\\DefaultIcon"),QString(),iconValue) ||
       !set(association+QStringLiteral("\\shell\\open\\command"),QString(),open) ||
       !set(executableKey,QStringLiteral("FriendlyAppName"),QStringLiteral("LUNA Library")) ||
       !set(executableKey+QStringLiteral("\\shell\\open\\command"),QString(),open) ||
       !writeEmptyValue(executableKey+QStringLiteral("\\SupportedTypes"),
                        QStringLiteral(".pdf"),failure) ||
       !writeEmptyValue(prefix+QStringLiteral("Classes\\.pdf\\OpenWithProgids"),
                        QStringLiteral("LUNA.Library.PDF"),failure) ||
       !set(capabilities,QStringLiteral("ApplicationName"),QStringLiteral("LUNA Library")) ||
       !set(capabilities,QStringLiteral("ApplicationDescription"),
            QStringLiteral("LUNA Library PDF 문서 뷰어")) ||
       !set(capabilities+QStringLiteral("\\FileAssociations"),
            QStringLiteral(".pdf"),QStringLiteral("LUNA.Library.PDF")) ||
       !set(prefix+QStringLiteral("RegisteredApplications"),
            QStringLiteral("LUNA Library"),
            QStringLiteral("Software\\LUNA Library\\Capabilities")))return false;
    // Notify Explorer about the *candidate* only. Windows still prompts the
    // user to choose LUNA in Default Apps; never overwrite .pdf UserChoice.
    if(prefix==QStringLiteral("Software\\"))
        SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
    return true;
}
#else
inline bool registerCurrentUser(const QString&,const QString&,QString* failure=nullptr,
                                const QString& ={}) {
    if(failure)*failure=QStringLiteral("Windows 전용 기능");
    return false;
}
#endif
} // namespace luna::PdfAssociation
