// SPDX-License-Identifier: MIT
#pragma once
// PDF association is a USER-OPT-IN ProgID registration, never a forced default.
// Keep LUNA.exe's existing BLUE icon; only Explorer PDF file-type association
// uses the separate RED LUNA-PDF.ico in the SAME portable installation folder.
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QString>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#endif

namespace luna::PdfAssociation {
inline constexpr const char* kProgId="LUNA.Library.PDF";
inline constexpr const char* kAppName="LUNA Library PDF";
inline QString pdfIconPath() {
    return QCoreApplication::applicationDirPath()+QStringLiteral("/LUNA-PDF.ico");
}
inline QString executablePath() {return QCoreApplication::applicationFilePath();}
#ifdef Q_OS_WIN
inline std::wstring wide(const QString& s) {return s.toStdWString();}

inline bool writeValue(const QString& key,const QString& name,
                       const QString& value,QString* error=nullptr) {
    HKEY opened=nullptr;
    const auto keyW=wide(key),nameW=wide(name),valueW=wide(value);
    auto code=RegCreateKeyExW(HKEY_CURRENT_USER,keyW.c_str(),0,nullptr,
                             REG_OPTION_NON_VOLATILE,KEY_SET_VALUE,nullptr,&opened,nullptr);
    if(code!=ERROR_SUCCESS) {
        if(error)*error=QStringLiteral("레지스트리 키 생성 실패 (%1)").arg(code);
        return false;
    }
    code=RegSetValueExW(opened,nameW.empty()?nullptr:nameW.c_str(),0,REG_SZ,
         reinterpret_cast<const BYTE*>(valueW.c_str()),
         DWORD((valueW.size()+1)*sizeof(wchar_t)));
    RegCloseKey(opened);
    if(code!=ERROR_SUCCESS && error)
        *error=QStringLiteral("레지스트리 값 기록 실패 (%1)").arg(code);
    return code==ERROR_SUCCESS;
}
inline bool addOpenWith(QString* error=nullptr) {
    HKEY key=nullptr;
    const auto path=wide(QStringLiteral("Software\\Classes\\.pdf\\OpenWithProgids"));
    LONG code=RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,
                              REG_OPTION_NON_VOLATILE,KEY_SET_VALUE,nullptr,&key,nullptr);
    if(code==ERROR_SUCCESS) {
        const auto name=wide(QString::fromLatin1(kProgId));
        code=RegSetValueExW(key,name.c_str(),0,REG_NONE,nullptr,0);
        RegCloseKey(key);
    }
    if(code!=ERROR_SUCCESS && error)
        *error=QStringLiteral("PDF 연결 후보 등록 실패 (%1)").arg(code);
    return code==ERROR_SUCCESS;
}
inline QString readValue(const QString& key,const QString& name) {
    HKEY opened=nullptr;
    const auto path=wide(key),valueName=wide(name);
    if(RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_QUERY_VALUE,&opened)!=ERROR_SUCCESS)
        return {};
    DWORD type=0,size=0;
    LONG code=RegQueryValueExW(opened,valueName.empty()?nullptr:valueName.c_str(),
                               nullptr,&type,nullptr,&size);
    if(code!=ERROR_SUCCESS || type!=REG_SZ || size<sizeof(wchar_t) || size>65536) {
        RegCloseKey(opened);return {};
    }
    std::wstring value(size/sizeof(wchar_t),L'\0');
    code=RegQueryValueExW(opened,valueName.empty()?nullptr:valueName.c_str(),
              nullptr,&type,reinterpret_cast<BYTE*>(value.data()),&size);
    RegCloseKey(opened);
    if(code!=ERROR_SUCCESS)return {};
    value.resize(wcsnlen_s(value.c_str(),value.size()));
    return QString::fromStdWString(value);
}
inline bool removeKey(const QString& key,QString* error=nullptr) {
    const auto path=wide(key);
    LONG code=RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());
    if(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND)return true;
    if(code!=ERROR_SUCCESS && error)
        *error=QStringLiteral("LUNA 연결 제거 실패 (%1)").arg(code);
    return code==ERROR_SUCCESS;
}
inline bool removeValue(const QString& key,const QString& name,QString* error=nullptr) {
    HKEY opened=nullptr;
    const auto path=wide(key),valueName=wide(name);
    LONG code=RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_SET_VALUE,&opened);
    if(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND)return true;
    if(code!=ERROR_SUCCESS)return false;
    code=RegDeleteValueW(opened,valueName.c_str());
    RegCloseKey(opened);
    if(code==ERROR_FILE_NOT_FOUND)return true;
    if(code!=ERROR_SUCCESS && error)
        *error=QStringLiteral("LUNA 연결 값 제거 실패 (%1)").arg(code);
    return code==ERROR_SUCCESS;
}
inline void notifyExplorer() {
    SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
}

inline bool isRegisteredHere() {
    const QString command=QStringLiteral("\"")+executablePath()+QStringLiteral("\" \"%1\"");
    return readValue(QStringLiteral("Software\\Classes\\LUNA.Library.PDF\\shell\\open\\command"),
                     {})==command;
}
inline bool registerPdf(QString* error=nullptr) {
    const QFileInfo exe(executablePath()),icon(pdfIconPath());
    if(!exe.isFile() || exe.fileName().compare(QStringLiteral("LUNA.exe"),
            Qt::CaseInsensitive)!=0 || !icon.isFile() || icon.isSymLink() ||
       icon.size()<1000) {
        if(error)*error=QStringLiteral(
            "LUNA.exe 또는 LUNA-PDF.ico가 없습니다. ZIP 전체를 압축 해제하세요.");
        return false;
    }
    // Check ICO signature. This is intentionally separate from the EXE icon.
    QFile test(icon.filePath());
    if(!test.open(QIODevice::ReadOnly) || test.read(4)!=QByteArray::fromHex("00000100")) {
        if(error)*error=QStringLiteral("PDF 연결 전용 아이콘 파일이 올바르지 않습니다.");
        return false;
    }
    const QString prog=QStringLiteral("Software\\Classes\\LUNA.Library.PDF");
    const QString capabilities=QStringLiteral("Software\\LUNA Library\\Capabilities");
    const QString quotedExe=QStringLiteral("\"")+exe.absoluteFilePath()+QStringLiteral("\"");
    const QString quotedIcon=QStringLiteral("\"")+icon.absoluteFilePath()+QStringLiteral("\"");
    const QString command=quotedExe+QStringLiteral(" \"%1\"");
    if(!writeValue(prog,{},QStringLiteral("LUNA Library PDF Document"),error) ||
       !writeValue(prog+QStringLiteral("\\DefaultIcon"),{},quotedIcon+QStringLiteral(",0"),error) ||
       !writeValue(prog+QStringLiteral("\\shell\\open\\command"),{},command,error) ||
       !writeValue(capabilities,QStringLiteral("ApplicationName"),QStringLiteral("LUNA Library"),error) ||
       !writeValue(capabilities,QStringLiteral("ApplicationDescription"),
                  QStringLiteral("LUNA Library PDF viewer and research paper manager"),error) ||
       !writeValue(capabilities+QStringLiteral("\\FileAssociations"),
                   QStringLiteral(".pdf"),QString::fromLatin1(kProgId),error) ||
       !writeValue(QStringLiteral("Software\\RegisteredApplications"),
                   QString::fromLatin1(kAppName),capabilities,error) ||
       !addOpenWith(error))return false;
    notifyExplorer();
    return true;
}
inline bool unregisterPdf(QString* error=nullptr) {
    const QString prog=QStringLiteral("Software\\Classes\\LUNA.Library.PDF");
    const QString command=readValue(prog+QStringLiteral("\\shell\\open\\command"),{});
    if(!command.isEmpty() && !isRegisteredHere()) {
        if(error)*error=QStringLiteral(
          "PDF 연결은 다른 위치의 LUNA 실행 파일을 가리킵니다. 해당 복사본에서 해제하세요.");
        return false;
    }
    const QString capabilities=QStringLiteral("Software\\LUNA Library\\Capabilities");
    const QString registered=QStringLiteral("Software\\RegisteredApplications");
    const QString openWith=QStringLiteral("Software\\Classes\\.pdf\\OpenWithProgids");
    // Never delete the .pdf extension key or Windows' UserChoice hash, and
    // never delete another PDF handler's registration or user-owned settings.
    bool ok=removeKey(prog,error);
    ok=removeValue(openWith,QString::fromLatin1(kProgId),error) && ok;
    if(readValue(registered,QString::fromLatin1(kAppName))==capabilities) {
        ok=removeValue(registered,QString::fromLatin1(kAppName),error) && ok;
        ok=removeKey(capabilities,error) && ok;
    }
    notifyExplorer();
    return ok;
}
inline bool openDefaultAppsSettings() {
    const HINSTANCE opened=ShellExecuteW(nullptr,L"open",L"ms-settings:defaultapps",
                                         nullptr,nullptr,SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(opened)>32;
}
#else
inline bool isRegisteredHere(){return false;}
inline bool registerPdf(QString* error=nullptr) {
    if(error)*error=QStringLiteral("Windows 전용 기능입니다.");return false;
}
inline bool unregisterPdf(QString* error=nullptr) {
    if(error)*error=QStringLiteral("Windows 전용 기능입니다.");return false;
}
inline bool openDefaultAppsSettings(){return false;}
#endif
} // namespace luna::PdfAssociation
