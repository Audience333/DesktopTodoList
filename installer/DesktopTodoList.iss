#define AppVersion "@AppVersion@"
#define Architecture "@Architecture@"
#define ExePath "@ExePath@"
#define ReadmePath "@ReadmePath@"
#define PrivacyPath "@PrivacyPath@"
#define LicensePath "@LicensePath@"
#define OutputDirectory "@OutputDirectory@"
#define IconFilePath "@IconFilePath@"
#define MessagesFilePath "@MessagesFilePath@"

#if Architecture == "x64"
  #define AllowedArchitectures "x64compatible and not arm64"
  #define InstallArchitectures "x64compatible"
#elif Architecture == "arm64"
  #define AllowedArchitectures "arm64"
  #define InstallArchitectures "arm64"
#else
  #error Unsupported installer architecture
#endif

[Setup]
AppId=DesktopTodoList.Native
AppName=DesktopTodoList
AppVersion={#AppVersion}
AppPublisher=DesktopTodoList Project
AppPublisherURL=
AppSupportURL=
AppUpdatesURL=
DefaultDirName={localappdata}\Programs\DesktopTodoList
DefaultGroupName=DesktopTodoList
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed={#AllowedArchitectures}
ArchitecturesInstallIn64BitMode={#InstallArchitectures}
OutputDir={#OutputDirectory}
OutputBaseFilename=DesktopTodoList-{#Architecture}-Setup
SetupIconFile={#IconFilePath}
UninstallDisplayIcon={app}\DesktopTodoList.exe
LicenseFile={#LicensePath}
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany=DesktopTodoList Project
VersionInfoDescription=DesktopTodoList per-user installer
VersionInfoOriginalFileName=DesktopTodoList-{#Architecture}-Setup.exe
VersionInfoProductName=DesktopTodoList

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Default.isl,{#MessagesFilePath}"

[Files]
Source: "{#ExePath}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReadmePath}"; DestDir: "{app}"; DestName: "README.md"; Flags: ignoreversion
Source: "{#PrivacyPath}"; DestDir: "{app}"; DestName: "PRIVACY.md"; Flags: ignoreversion
Source: "{#LicensePath}"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\DesktopTodoList"; Filename: "{app}\DesktopTodoList.exe"; WorkingDir: "{app}"; AppUserModelID: "DesktopTodoList.Native"
Name: "{group}\{cm:UninstallProgram,DesktopTodoList}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\DesktopTodoList.exe"; Description: "{cm:LaunchProgram,DesktopTodoList}"; Flags: nowait postinstall skipifsilent
