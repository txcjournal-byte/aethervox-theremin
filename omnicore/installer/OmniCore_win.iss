#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Art "..\build\OmniCore_artefacts\Release"

[Setup]
AppName=OmniCore 3000
AppVersion={#AppVersion}
AppPublisher=TrapWin
AppPublisherURL=https://trapwin.com
DefaultDirName={commonpf64}\TrapWin\OmniCore 3000
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=OmniCore_3000_{#AppVersion}_Windows
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
CloseApplications=force
CloseApplicationsFilter=*.exe,*.dll,*.vst3
RestartApplications=no

[Components]
Name: "vst3"; Description: "VST3 plugin (FL Studio, Ableton, Cubase, Studio One...)"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone app"; Types: full

[Files]
Source: "{#Art}\VST3\OmniCore 3000.vst3\*"; DestDir: "{commoncf64}\VST3\OmniCore 3000.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace uninsrestartdelete
Source: "{#Art}\Standalone\OmniCore 3000.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion restartreplace

[Icons]
Name: "{commonprograms}\OmniCore 3000"; Filename: "{app}\OmniCore 3000.exe"; Components: standalone
