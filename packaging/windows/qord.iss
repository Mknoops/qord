; Qord's Windows installer (Inno Setup 6). CI builds it from the deployed
; release folder: iscc /DAppVersion=<ver> /DAppFiles=<dir> /DOutputDir=<dir>
; Installs per user by default, so no administrator rights are needed.

#ifndef AppVersion
  #error AppVersion must be defined (/DAppVersion=x.y.z)
#endif
#ifndef AppFiles
  #error AppFiles must be defined (/DAppFiles=path to the deployed files)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{602CCA7E-57B7-47E3-8CBB-1AB7815C1DDB}
AppName=Qord
AppVersion={#AppVersion}
AppVerName=Qord {#AppVersion}
AppPublisher=Qord contributors
AppPublisherURL=https://github.com/mknoops/qord
AppSupportURL=https://github.com/mknoops/qord/issues
AppUpdatesURL=https://github.com/mknoops/qord/releases
DefaultDirName={autopf}\Qord
DefaultGroupName=Qord
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\..\LICENSE.txt
SetupIconFile=..\..\acheron.ico
UninstallDisplayIcon={app}\qord.exe
OutputDir={#OutputDir}
OutputBaseFilename=Qord-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#AppFiles}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Qord"; Filename: "{app}\qord.exe"
Name: "{autodesktop}\Qord"; Filename: "{app}\qord.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\qord.exe"; Description: "{cm:LaunchProgram,Qord}"; Flags: nowait postinstall skipifsilent
