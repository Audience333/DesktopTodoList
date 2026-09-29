#define AppVersion "@AppVersion@"
#define Architecture "@Architecture@"
#define ExePath "@ExePath@"
#define ReadmePath "@ReadmePath@"
#define PrivacyPath "@PrivacyPath@"
#define LicensePath "@LicensePath@"
#define OutputDirectory "@OutputDirectory@"
#define IconFilePath "@IconFilePath@"
#define MessagesFilePath "@MessagesFilePath@"
#define InstallGuidePath "@InstallGuidePath@"
#define MigrationGuidePath "@MigrationGuidePath@"
#define TroubleshootingGuidePath "@TroubleshootingGuidePath@"
#define TestDataDirectory "@TestDataDirectory@"
#define TestAutostartName "@TestAutostartName@"
#define TestDesktopDirectory "@TestDesktopDirectory@"

#if TestAutostartName == ""
  #define InstallerAppId "DesktopTodoList.Native"
  #define ProgramGroup "DesktopTodoList"
#else
  #define InstallerAppId "DesktopTodoList.Native.Test." + TestAutostartName
  #define ProgramGroup "DesktopTodoListTest"
#endif

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
AppId={#InstallerAppId}
AppName=DesktopTodoList
AppVersion={#AppVersion}
AppPublisher=DesktopTodoList Project
AppPublisherURL=
AppSupportURL=
AppUpdatesURL=
DefaultDirName={localappdata}\Programs\DesktopTodoList
DefaultGroupName={#ProgramGroup}
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
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "compiler:Default.isl,{#MessagesFilePath}"

[CustomMessages]
english.RemoveUserDataPrompt=Also remove DesktopTodoList tasks, settings, and backups from this Windows user?
english.CreateDesktopShortcut=Create a desktop shortcut

[Files]
Source: "{#ExePath}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReadmePath}"; DestDir: "{app}"; DestName: "README.md"; Flags: ignoreversion
Source: "{#PrivacyPath}"; DestDir: "{app}"; DestName: "PRIVACY.md"; Flags: ignoreversion
Source: "{#LicensePath}"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#InstallGuidePath}"; DestDir: "{app}\docs"; DestName: "install.md"; Flags: ignoreversion
Source: "{#MigrationGuidePath}"; DestDir: "{app}\docs"; DestName: "migrate-from-web-version.md"; Flags: ignoreversion
Source: "{#TroubleshootingGuidePath}"; DestDir: "{app}\docs"; DestName: "troubleshooting.md"; Flags: ignoreversion

[Icons]
Name: "{group}\DesktopTodoList"; Filename: "{app}\DesktopTodoList.exe"; WorkingDir: "{app}"; AppUserModelID: "DesktopTodoList.Native"
Name: "{group}\{cm:UninstallProgram,DesktopTodoList}"; Filename: "{uninstallexe}"
Name: "{code:GetDesktopShortcutDirectory}\DesktopTodoList"; Filename: "{app}\DesktopTodoList.exe"; WorkingDir: "{app}"; Tasks: desktopicon; AppUserModelID: "DesktopTodoList.Native"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopShortcut}"; Flags: unchecked

[Run]
Filename: "{app}\DesktopTodoList.exe"; Description: "{cm:LaunchProgram,DesktopTodoList}"; Flags: nowait postinstall skipifsilent

[Code]
var
  DeleteUserData: Boolean;

function GetDataDirectory: String;
#if TestDataDirectory != ""
var
  Index: Integer;
  ProfileId: String;
#endif
begin
#if TestDataDirectory == ""
  Result := ExpandConstant('{localappdata}\DesktopTodoList');
#else
  Result := GetEnv('DESKTOP_TODO_TEST_DATA_ROOT');
  ProfileId := ExtractFileName(ExtractFileDir(ExtractFileDir(Result)));
  if (ExtractFileName(Result) <> 'DesktopTodoList') or
     (ExtractFileName(ExtractFileDir(Result)) <> 'LocalAppData') or
     (Length(ProfileId) <> 32) or
     (ExtractFileName(ExtractFileDir(ExtractFileDir(ExtractFileDir(Result)))) <> 'DesktopTodoListTestProfiles') or
     (CompareText(ExtractFileDir(ExtractFileDir(ExtractFileDir(ExtractFileDir(Result)))), GetEnv('TEMP')) <> 0) then
    Result := '';
  for Index := 1 to Length(ProfileId) do
    if Pos(Lowercase(Copy(ProfileId, Index, 1)), '0123456789abcdef') = 0 then
      Result := '';
#endif
end;

function GetDesktopShortcutDirectory(Param: String): String;
#if TestDesktopDirectory == ""
begin
  Result := ExpandConstant('{autodesktop}');
#else
begin
  Result := '{#TestDesktopDirectory}';
#endif
end;

function GetAutostartValueName: String;
#if TestAutostartName == ""
begin
  Result := 'DesktopTodoList';
#else
var
  Index: Integer;
begin
  Result := GetEnv('DESKTOP_TODO_TEST_AUTOSTART_NAME');
  if (Length(Result) <> 52) or (Copy(Result, 1, 20) <> 'DesktopTodoListTest_') then
    Result := '';
  for Index := 21 to Length(Result) do
    if Pos(Lowercase(Copy(Result, Index, 1)), '0123456789abcdef') = 0 then
      Result := '';
#endif
end;

function InitializeUninstall: Boolean;
var
  Index: Integer;
  ExplicitRemoval: Boolean;
begin
  Result := True;
  DeleteUserData := False;
  ExplicitRemoval := False;
  for Index := 1 to ParamCount do
    if CompareText(ParamStr(Index), '/REMOVEUSERDATA') = 0 then
      ExplicitRemoval := True;
  if ExplicitRemoval then
    DeleteUserData := True
  else if (not UninstallSilent) and DirExists(GetDataDirectory()) then
    DeleteUserData := MsgBox(CustomMessage('RemoveUserDataPrompt'),
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDirectory: String;
begin
  if CurUninstallStep = usPostUninstall then begin
    RegDeleteValue(HKEY_CURRENT_USER,
      'Software\Microsoft\Windows\CurrentVersion\Run', GetAutostartValueName());
    if DeleteUserData then begin
      DataDirectory := GetDataDirectory();
      if (ExtractFileName(DataDirectory) = 'DesktopTodoList') and DirExists(DataDirectory) then
        if not DelTree(DataDirectory, True, True, True) then
          MsgBox('Could not remove user data directory: ' + DataDirectory, mbError, MB_OK);
    end;
  end;
end;
