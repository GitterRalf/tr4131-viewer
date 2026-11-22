; Build TR4131-Viewer.exe and deploy its Qt runtime with build.ps1 before compiling.
; Install NI-488.2 or the Elektor virtual COM-port driver separately.
#define ReadQuoted(Text) Copy(Text, Pos('"', Text) + 1, Pos('"', Copy(Text, Pos('"', Text) + 1, Len(Text))) - 1)
#define VersionHeader FileOpen(SourcePath + "version.h")
#expr FileRead(VersionHeader)
#expr FileRead(VersionHeader)
#define Tr4131Version ReadQuoted(FileRead(VersionHeader))
#expr FileClose(VersionHeader)
#define IconResource FileOpen(SourcePath + "assets\viewer.rc")
#define Tr4131Icon ReadQuoted(FileRead(IconResource))
#expr FileClose(IconResource)
#ifndef BuildArch
  #define BuildArch "x64"
#endif
#if BuildArch == "x86"
  #define Tr4131Name "TR4131 Viewer (32 Bit)"
  #define Tr4131AppId "{{9AC5F55A-D8CE-4FA6-83AD-423D6930C78D}"
  #define Tr4131BuildDir "build-x86"
#elif BuildArch == "x64"
  #define Tr4131Name "TR4131 Viewer"
  #define Tr4131AppId "{{FA245A97-D3EE-4F2A-85DF-BED99669CF11}"
  #define Tr4131BuildDir "build-spectrogram"
#else
  #error BuildArch must be x86 or x64.
#endif
#define Tr4131Exe "TR4131-Viewer.exe"

[Setup]
AppId={#Tr4131AppId}
AppName={#Tr4131Name}
AppVersion={#Tr4131Version}
AppVerName={#Tr4131Name} {#Tr4131Version}
AppPublisher=Ralf Tralow
VersionInfoVersion={#Tr4131Version}
VersionInfoDescription={#Tr4131Name} Setup
DefaultDirName={pf}\{#Tr4131Name}
DefaultGroupName={#Tr4131Name}
AllowNoIcons=yes
PrivilegesRequired=admin
#if BuildArch == "x64"
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
#else
ArchitecturesAllowed=x86 x64
#endif
; Inno 5.3.5 can see modern Windows as 6.2 due to its legacy manifest.
SetupIconFile=assets\{#Tr4131Icon}
UninstallDisplayIcon={app}\{#Tr4131Exe}
OutputDir=installer
OutputBaseFilename=TR4131-Viewer-Setup-{#Tr4131Version}-{#BuildArch}
Compression=lzma
SolidCompression=yes

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
#if BuildArch == "x86"
Source: "{#Tr4131BuildDir}\qt.conf"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Tr4131BuildDir}\bearer\*.dll"; DestDir: "{app}\bearer"; Flags: ignoreversion skipifsourcedoesntexist
#endif
Source: "{#Tr4131BuildDir}\{#Tr4131Exe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Tr4131BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Tr4131BuildDir}\generic\*.dll"; DestDir: "{app}\generic"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Tr4131BuildDir}\iconengines\*.dll"; DestDir: "{app}\iconengines"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Tr4131BuildDir}\imageformats\*.dll"; DestDir: "{app}\imageformats"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Tr4131BuildDir}\networkinformation\*.dll"; DestDir: "{app}\networkinformation"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Tr4131BuildDir}\platforms\*.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion
Source: "{#Tr4131BuildDir}\styles\*.dll"; DestDir: "{app}\styles"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Tr4131BuildDir}\tls\*.dll"; DestDir: "{app}\tls"; Flags: ignoreversion skipifsourcedoesntexist
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "licenses\*.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\{#Tr4131Name}"; Filename: "{app}\{#Tr4131Exe}"; WorkingDir: "{app}"
Name: "{commondesktop}\{#Tr4131Name}"; Filename: "{app}\{#Tr4131Exe}"; WorkingDir: "{app}"; Tasks: desktopicon
