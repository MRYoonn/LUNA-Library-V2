; SPDX-License-Identifier: MIT
; LUNA development installer. It packages only the already-verified portable
; directory, so the installed EXE and the portable EXE have identical bytes.
#define LunaProduct "LUNA Library"
#ifndef LunaVersion
  #define LunaVersion "0.1.0-beta"
#endif

[Setup]
; Script lives in tools/; compiled sources, icon and output live at repository root.
SourceDir=..
AppId={{D921C772-E1D2-47B0-AF0E-13E81945C2B4}
AppName={#LunaProduct}
AppVersion={#LunaVersion}
AppVerName={#LunaProduct} {#LunaVersion}
DefaultDirName={localappdata}\Programs\LUNA Library
DefaultGroupName=LUNA Library
PrivilegesRequired=lowest
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
OutputDir=LUNA-Windows-Installer
OutputBaseFilename=LUNA-Library-Setup
SetupIconFile=assets\icons\LUNA-Moon.ico
UninstallDisplayIcon={app}\LUNA.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
ChangesAssociations=no
DisableWelcomePage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "LUNA-Windows-Portable\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\LUNA Library"; Filename: "{app}\LUNA.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\LUNA Library"; Filename: "{app}\LUNA.exe"; Tasks: desktopicon; WorkingDir: "{app}"

[Run]
Filename: "{app}\LUNA.exe"; Description: "Launch LUNA Library"; Flags: nowait postinstall skipifsilent
