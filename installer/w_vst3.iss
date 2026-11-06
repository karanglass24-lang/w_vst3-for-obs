; Inno Setup script. Build: ISCC /DAppVersion=0.1.0 /DBuildDir=..\build\Release installer\w_vst3.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\Release"
#endif

[Setup]
AppId={{6F0D6A52-3C1B-4E0B-9B7E-5C1F3E2A7D41}
AppName=w_vst3 (OBS VST3 host)
AppVersion={#AppVersion}
AppPublisher=w_vst3
DefaultDirName={commonappdata}\obs-studio\plugins\w_vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
OutputBaseFilename=w_vst3-{#AppVersion}-windows-x64-setup
OutputDir=..\dist
Compression=lzma2
SolidCompression=yes

[Files]
Source: "{#BuildDir}\w_vst3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\w_vst3_scanner.exe"; DestDir: "{app}"; Flags: ignoreversion

[Code]
// OBS locks the DLL while running; ask the user to close it first.
function InitializeSetup(): Boolean;
begin
  Result := True;
  if MsgBox('Please close OBS Studio before installing. Continue?', mbConfirmation, MB_YESNO) = IDNO then
    Result := False;
end;

[UninstallDelete]
Type: files; Name: "{userappdata}\obs-studio\plugin_config\w_vst3\scan-cache.json"
