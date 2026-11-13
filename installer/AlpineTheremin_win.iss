; Alpine Theremin – Windows installer (Inno Setup 6)
#define AppName "Alpine Theremin"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Art "..\build\AetherVox_artefacts\Release"

[Setup]
AppId={{9C4A1F2E-7B3D-4E8A-A1C5-41A1B1E0A929}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=TrapWin
AppPublisherURL=https://trapwin.com
DefaultDirName={commonpf64}\TrapWin\Alpine Theremin
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputBaseFilename=Alpine_Theremin_{#AppVersion}_Windows
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
; close FL Studio / any host that holds the plugin open, and replace locked files on reboot as a last resort
CloseApplications=force
CloseApplicationsFilter=*.exe,*.dll,*.vst3
RestartApplications=no

[Components]
Name: "vst3"; Description: "VST3 plugin (FL Studio, Ableton, Cubase, Studio One...)"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone app"; Types: full

[Files]
Source: "{#Art}\VST3\Alpine Theremin.vst3\*"; DestDir: "{commoncf64}\VST3\Alpine Theremin.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace uninsrestartdelete
Source: "{#Art}\Standalone\Alpine Theremin.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion restartreplace

[Icons]
Name: "{commonprograms}\Alpine Theremin"; Filename: "{app}\Alpine Theremin.exe"; Components: standalone
