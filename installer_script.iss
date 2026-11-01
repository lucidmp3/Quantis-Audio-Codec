[Setup]
AppName=Quantis Audio Player
AppVersion=1.3.0
AppPublisher=Lucid Audio
DefaultDirName={autopf}\Quantis
DefaultGroupName=Quantis
OutputDir=C:\Users\simon\CustomAudioCodec\InstallerOutput
OutputBaseFilename=Quantis_Setup_v1.3.0
Compression=lzma
SolidCompression=yes
ChangesAssociations=yes
PrivilegesRequired=admin

[Files]
; This grabs your compiled, lightning-fast Release executable
Source: "C:\Users\simon\CustomAudioCodec\build\Release\codec.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Creates a Desktop shortcut and a Start Menu shortcut
Name: "{autodesktop}\Quantis Audio Player"; Filename: "{app}\codec.exe"
Name: "{group}\Quantis Audio Player"; Filename: "{app}\codec.exe"

[Registry]
; 1. Tell Windows that .qnt files exist and belong to the "Quantis.Audio" class
Root: HKCR; Subkey: ".qnt"; ValueType: string; ValueName: ""; ValueData: "Quantis.Audio"; Flags: uninsdeletevalue

; 2. Describe the file type (This shows up in the "Type" column in File Explorer)
Root: HKCR; Subkey: "Quantis.Audio"; ValueType: string; ValueName: ""; ValueData: "Quantis Audio File"; Flags: uninsdeletekey

; 3. Give the file an icon (We just use the default icon from our own .exe)
Root: HKCR; Subkey: "Quantis.Audio\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\codec.exe,0"

; 4. The Magic Command: When you double-click, pass the file path ("%1") to our app!
Root: HKCR; Subkey: "Quantis.Audio\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\codec.exe"" ""%1"""
