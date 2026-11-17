; MixLauncher installer (Inno Setup 7). release.bat builds it:
;   the three builds (generic, v2, v3) + third_party\everything  ->  build\MixLauncher-Setup.exe
; Without SourceExeV2/V3 it packs SourceExe alone (build\mixlauncher.exe by default).
; The version comes from the exe (res\version.h, ProductVersion).

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
#ifdef SourceExeV3
; The highest x86-64 level the processor runs: v3 (AVX2), v2 (SSE4.2), the generic build elsewhere
Source: "{#SourceExeV3}"; DestDir: "{app}"; DestName: "mixlauncher.exe"; Flags: ignoreversion; Check: CpuLevel(3)
Source: "{#SourceExeV2}"; DestDir: "{app}"; DestName: "mixlauncher.exe"; Flags: ignoreversion; Check: CpuLevel(2)
Source: "{#SourceExe}"; DestDir: "{app}"; DestName: "mixlauncher.exe"; Flags: ignoreversion; Check: CpuLevel(1)
#else
Source: "{#SourceExe}"; DestDir: "{app}"; DestName: "mixlauncher.exe"; Flags: ignoreversion
#endif
Source: "..\third_party\everything\Everything.exe"; DestDir: "{app}\Everything"; Flags: ignoreversion
Source: "..\third_party\everything\License.txt"; DestDir: "{app}\Everything"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MixLauncher"; Filename: "{app}\mixlauncher.exe"
Name: "{autoprograms}\{cm:SettingsIcon}"; Filename: "{app}\mixlauncher.exe"; Parameters: "--settings"

[Run]
Filename: "{app}\mixlauncher.exe"; Parameters: "--autostart on"; Flags: waituntilterminated; Tasks: autostart; Check: not IsUpdate
Filename: "{app}\mixlauncher.exe"; Parameters: "--autostart off"; Flags: waituntilterminated; Tasks: not autostart; Check: not IsUpdate
; postinstall entries run as the original, non-elevated user by default; the launcher needs administrator rights.
Filename: "{app}\mixlauncher.exe"; Description: "{cm:LaunchNow}"; Flags: postinstall nowait skipifsilent runascurrentuser
; An update from the launcher (/VERYSILENT /update=1): start the new version again.
Filename: "{app}\mixlauncher.exe"; Flags: nowait runascurrentuser; Check: IsUpdate

[UninstallRun]
; Closes the launcher and our Everything, removes the autostart task.
Filename: "{app}\mixlauncher.exe"; Parameters: "--uninstall"; Flags: waituntilterminated; RunOnceId: "MixLauncherUninstall"

[UninstallDelete]
; The index of the built-in Everything. Settings and launch history stay.
Type: filesandordirs; Name: "{localappdata}\MixLauncher\Everything"

[Code]
function IsUpdate: Boolean;
begin
  Result := ExpandConstant('{param:update|0}') = '1';
end;

function IsProcessorFeaturePresent(Feature: DWORD): BOOL;
  external 'IsProcessorFeaturePresent@kernel32.dll stdcall';

// The x86-64 level by the flags Windows knows: AVX2 (PF 40) for v3, SSE4.2 (PF 38) for v2. Every processor with
// one has the rest of its level (FMA, BMI1/2, MOVBE; POPCNT, CMPXCHG16B). A Windows that does not know a flag
// answers no, and a lower build goes in.
function CpuLevel(Level: Integer): Boolean;
var
  Have: Integer;
begin
  Have := 1;
  if IsProcessorFeaturePresent(40) then
    Have := 3
  else if IsProcessorFeaturePresent(38) then
    Have := 2;
  Result := Have = Level;
end;

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
