#define MyAppExecName "lektra.exe"
#define MyAppName "Lektra"

; Version comes from CMakeLists.txt's project(... VERSION x.y.z ...), via
; build/version.txt written by CMake at configure time. Run configure.bat
; (or `cmake -S . -B build`) before compiling this script.
#define VersionFile SourcePath + "build\version.txt"
#if FileExists(VersionFile)
  #define VersionHandle FileOpen(VersionFile)
  #define MyAppVersion Trim(FileRead(VersionHandle))
  #expr FileClose(VersionHandle)
#else
  #error "build\version.txt not found. Run configure.bat (or cmake -S . -B build) before compiling lektra.iss."
#endif

[Setup]
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=installer
OutputBaseFilename=lektra-setup
ChangesAssociations=yes
SetupIconFile=.\resources\lektra.ico

[Files]
; Icon is embedded in the exe via lektra.rc — no separate .ico needed here.
Source: ".\build\release\*"; DestDir: "{app}";              Flags: recursesubdirs
Source: ".\tutorial.pdf";    DestDir: "{userappdata}\{#MyAppName}"; Flags: ignoreversion

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Icons]
Name: "{group}\Lektra";                Filename: "{app}\{#MyAppExecName}"
Name: "{commondesktop}\{#MyAppName}";  Filename: "{app}\{#MyAppExecName}"
Name: "{autodesktop}\{#MyAppName}";    Filename: "{app}\{#MyAppExecName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExecName}"; Description: "Launch {#MyAppName}"; Flags: postinstall

[Registry]
; PDF
Root: HKCR; Subkey: ".pdf\OpenWithProgids";             ValueType: string; ValueName: "Lektra.pdf";  ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.pdf";                       ValueType: string; ValueName: "";             ValueData: "PDF Document";          Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.pdf\DefaultIcon";           ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.pdf\shell\open\command";    ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

; DjVu
Root: HKCR; Subkey: ".djvu\OpenWithProgids";            ValueType: string; ValueName: "Lektra.djvu"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.djvu";                      ValueType: string; ValueName: "";             ValueData: "DjVu Document";         Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.djvu\DefaultIcon";          ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.djvu\shell\open\command";   ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

; XPS / OXPS
Root: HKCR; Subkey: ".xps\OpenWithProgids";             ValueType: string; ValueName: "Lektra.xps";  ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.xps";                       ValueType: string; ValueName: "";             ValueData: "XPS Document";          Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.xps\DefaultIcon";           ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.xps\shell\open\command";    ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

Root: HKCR; Subkey: ".oxps\OpenWithProgids";            ValueType: string; ValueName: "Lektra.oxps"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.oxps";                      ValueType: string; ValueName: "";             ValueData: "OXPS Document";         Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.oxps\DefaultIcon";          ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.oxps\shell\open\command";   ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

; CBZ / CBT
Root: HKCR; Subkey: ".cbz\OpenWithProgids";             ValueType: string; ValueName: "Lektra.cbz";  ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.cbz";                       ValueType: string; ValueName: "";             ValueData: "CBZ Archive";           Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.cbz\DefaultIcon";           ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.cbz\shell\open\command";    ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

Root: HKCR; Subkey: ".cbt\OpenWithProgids";             ValueType: string; ValueName: "Lektra.cbt";  ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.cbt";                       ValueType: string; ValueName: "";             ValueData: "CBT Archive";           Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.cbt\DefaultIcon";           ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.cbt\shell\open\command";    ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

; EPUB
Root: HKCR; Subkey: ".epub\OpenWithProgids";            ValueType: string; ValueName: "Lektra.epub"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.epub";                      ValueType: string; ValueName: "";             ValueData: "EPUB Document";         Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.epub\DefaultIcon";          ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.epub\shell\open\command";   ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

; FB2
Root: HKCR; Subkey: ".fb2\OpenWithProgids";             ValueType: string; ValueName: "Lektra.fb2";  ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.fb2";                       ValueType: string; ValueName: "";             ValueData: "FictionBook Document";  Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.fb2\DefaultIcon";           ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.fb2\shell\open\command";    ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

; MOBI
Root: HKCR; Subkey: ".mobi\OpenWithProgids";            ValueType: string; ValueName: "Lektra.mobi"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.mobi";                      ValueType: string; ValueName: "";             ValueData: "Mobipocket eBook";      Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.mobi\DefaultIcon";          ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.mobi\shell\open\command";   ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

; JPEG
Root: HKCR; Subkey: ".jpg\OpenWithProgids";             ValueType: string; ValueName: "Lektra.jpg";  ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.jpg";                       ValueType: string; ValueName: "";             ValueData: "JPEG Image";            Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.jpg\DefaultIcon";           ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.jpg\shell\open\command";    ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

Root: HKCR; Subkey: ".jpeg\OpenWithProgids";            ValueType: string; ValueName: "Lektra.jpeg"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.jpeg";                      ValueType: string; ValueName: "";             ValueData: "JPEG Image";            Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.jpeg\DefaultIcon";          ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.jpeg\shell\open\command";   ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

; PNG
Root: HKCR; Subkey: ".png\OpenWithProgids";             ValueType: string; ValueName: "Lektra.png";  ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.png";                       ValueType: string; ValueName: "";             ValueData: "PNG Image";             Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.png\DefaultIcon";           ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.png\shell\open\command";    ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

; TIFF
Root: HKCR; Subkey: ".tif\OpenWithProgids";             ValueType: string; ValueName: "Lektra.tif";  ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.tif";                       ValueType: string; ValueName: "";             ValueData: "TIFF Image";            Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.tif\DefaultIcon";           ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.tif\shell\open\command";    ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

Root: HKCR; Subkey: ".tiff\OpenWithProgids";            ValueType: string; ValueName: "Lektra.tiff"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.tiff";                      ValueType: string; ValueName: "";             ValueData: "TIFF Image";            Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.tiff\DefaultIcon";          ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.tiff\shell\open\command";   ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""

; SVG
Root: HKCR; Subkey: ".svg\OpenWithProgids";             ValueType: string; ValueName: "Lektra.svg";  ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.svg";                       ValueType: string; ValueName: "";             ValueData: "SVG Image";             Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.svg\DefaultIcon";           ValueType: string; ValueName: "";             ValueData: "{app}\lektra.exe,0"
Root: HKCR; Subkey: "{#MyAppName}.svg\shell\open\command";    ValueType: string; ValueName: "";             ValueData: """{app}\lektra.exe"" ""%1"""
