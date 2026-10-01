; MixLauncher installer (Inno Setup 7). release.bat builds it:
;   build\mixlauncher.exe + third_party\everything  ->  build\MixLauncher-Setup.exe
; The version comes from the exe (res\mixlauncher.rc, ProductVersion).

#ifndef SourceExe
  #define SourceExe "..\build\mixlauncher.exe"
#endif
#define AppVersion GetStringFileInfo(SourceExe, "ProductVersion")

[Setup]
AppId={{6A00689B-7DEA-4DFA-82BA-037DE1EEC7E7}
AppName=MixLauncher
AppVersion={#AppVersion}
AppVerName=MixLauncher {#AppVersion}
AppPublisher=mel1x
AppPublisherURL=https://github.com/mel1x/mixlauncher
AppSupportURL=https://github.com/mel1x/mixlauncher/issues
AppUpdatesURL=https://github.com/mel1x/mixlauncher/releases
DefaultDirName={autopf}\MixLauncher
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=..\build
OutputBaseFilename=MixLauncher-Setup
SetupIconFile=..\res\mixlauncher.ico
UninstallDisplayIcon={app}\mixlauncher.exe
UninstallDisplayName=MixLauncher
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; We close the launcher and our Everything ourselves (PrepareToInstall, --uninstall).
CloseApplications=no
; The launcher's data lives in the installing user's %LOCALAPPDATA%.
UsedUserAreasWarning=no
VersionInfoVersion={#AppVersion}
VersionInfoProductName=MixLauncher

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
ru.AutostartTask=Запускать MixLauncher при входе в Windows
en.AutostartTask=Start MixLauncher with Windows
ru.SettingsIcon=Настройки MixLauncher
en.SettingsIcon=MixLauncher Settings
ru.LaunchNow=Запустить MixLauncher
en.LaunchNow=Launch MixLauncher

[Tasks]
Name: "autostart"; Description: "{cm:AutostartTask}"

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; DestName: "mixlauncher.exe"; Flags: ignoreversion
Source: "..\third_party\everything\Everything.exe"; DestDir: "{app}\Everything"; Flags: ignoreversion
Source: "..\third_party\everything\License.txt"; DestDir: "{app}\Everything"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MixLauncher"; Filename: "{app}\mixlauncher.exe"
Name: "{autoprograms}\{cm:SettingsIcon}"; Filename: "{app}\mixlauncher.exe"; Parameters: "--settings"

[Run]
Filename: "{app}\mixlauncher.exe"; Parameters: "--autostart on"; Flags: waituntilterminated; Tasks: autostart
Filename: "{app}\mixlauncher.exe"; Parameters: "--autostart off"; Flags: waituntilterminated; Tasks: not autostart
Filename: "{app}\mixlauncher.exe"; Description: "{cm:LaunchNow}"; Flags: postinstall nowait skipifsilent

[UninstallRun]
; Closes the launcher and our Everything, removes the autostart task.
Filename: "{app}\mixlauncher.exe"; Parameters: "--uninstall"; Flags: waituntilterminated; RunOnceId: "MixLauncherUninstall"

[UninstallDelete]
; The index of the built-in Everything. Settings and launch history stay.
Type: filesandordirs; Name: "{localappdata}\MixLauncher\Everything"

[Code]
// An update: close the running launcher and its Everything so their files can be replaced.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Exe: String;
  Code: Integer;
begin
  Result := '';
  Exe := ExpandConstant('{app}\mixlauncher.exe');
  if FileExists(Exe) then
    Exec(Exe, '--shutdown', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;
