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
#define Tr4131Name "TR4131 Viewer"
#define Tr4131Exe "TR4131-Viewer.exe"

[Setup]
AppId={{FA245A97-D3EE-4F2A-85DF-BED99669CF11}
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
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
; Inno 5.3.5 can see modern Windows as 6.2 due to its legacy manifest.
SetupIconFile=assets\{#Tr4131Icon}
UninstallDisplayIcon={app}\{#Tr4131Exe}
OutputDir=installer
OutputBaseFilename=TR4131-Viewer-Setup-{#Tr4131Version}-x64
Compression=lzma
SolidCompression=yes

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "build-spectrogram\{#Tr4131Exe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "build-spectrogram\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build-spectrogram\generic\*.dll"; DestDir: "{app}\generic"; Flags: ignoreversion
Source: "build-spectrogram\iconengines\*.dll"; DestDir: "{app}\iconengines"; Flags: ignoreversion
Source: "build-spectrogram\imageformats\*.dll"; DestDir: "{app}\imageformats"; Flags: ignoreversion
Source: "build-spectrogram\networkinformation\*.dll"; DestDir: "{app}\networkinformation"; Flags: ignoreversion
Source: "build-spectrogram\platforms\*.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion
Source: "build-spectrogram\styles\*.dll"; DestDir: "{app}\styles"; Flags: ignoreversion
Source: "build-spectrogram\tls\*.dll"; DestDir: "{app}\tls"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "licenses\*.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\{#Tr4131Name}"; Filename: "{app}\{#Tr4131Exe}"; WorkingDir: "{app}"
Name: "{commondesktop}\{#Tr4131Name}"; Filename: "{app}\{#Tr4131Exe}"; WorkingDir: "{app}"; Tasks: desktopicon
