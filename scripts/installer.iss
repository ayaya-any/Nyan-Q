; Clash-Q Inno Setup script.
; Build with:
;   ISCC.exe /DAppVersion=0.1.0 /DPayloadDir=... /DOutputDir=... /DVCRedistPath=... installer.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef PayloadDir
  #define PayloadDir "..\dist\Clash-Q"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
#ifndef VCRedistPath
  #define VCRedistPath "..\third_party\vcredist\vc_redist.x64.exe"
#endif

[Setup]
AppId={{8E2D1F2A-0C7A-4C55-9E69-6C2D6C2A4F71}
AppName=Clash-Q
AppVersion={#AppVersion}
AppVerName=Clash-Q {#AppVersion}
AppPublisher=Clash-Q contributors
DefaultDirName={localappdata}\Programs\Clash-Q
DefaultGroupName=Clash-Q
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
LicenseFile=..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=Clash-Q-{#AppVersion}-win64-setup
SetupIconFile=..\resources\icons\app.ico
UninstallDisplayIcon={app}\Clash-Q.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
AllowNoIcons=yes

[Languages]
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Messages]
FileExists=文件已存在。%n%n是否要覆盖？
ExistingFileNewer=现有文件比安装程序要安装的文件新。建议保留现有文件。%n%n是否仍要覆盖？

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式:"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "docs\*,package-check.png,*.log,*.pdb"

; Extracted only when the VC++ 2015-2022 x64 runtime is missing or outdated.
Source: "{#VCRedistPath}"; DestDir: "{tmp}"; Flags: dontcopy

[Icons]
Name: "{group}\Clash-Q"; Filename: "{app}\Clash-Q.exe"
Name: "{autodesktop}\Clash-Q"; Filename: "{app}\Clash-Q.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Clash-Q.exe"; Description: "启动 Clash-Q"; \
    Flags: nowait postinstall skipifsilent

[Code]
const
  VCRedistMinimumBuild = 33810;

function VCRedistInstalled(): Boolean;
var
  Installed: Cardinal;
  Build: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
    'Installed', Installed) and (Installed = 1);

  if Result and RegQueryDWordValue(HKLM64,
      'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
      'Bld', Build) then
    Result := Build >= VCRedistMinimumBuild;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
  RedistPath: String;
begin
  Result := '';
  if VCRedistInstalled() then
    Exit;

  ExtractTemporaryFile('vc_redist.x64.exe');
  RedistPath := ExpandConstant('{tmp}\vc_redist.x64.exe');
  if not Exec(RedistPath, '/install /quiet /norestart', '', SW_SHOW,
      ewWaitUntilTerminated, ResultCode) then
  begin
    Result := '无法启动 Microsoft Visual C++ 运行库安装程序。';
    Exit;
  end;

  if (ResultCode <> 0) and (ResultCode <> 1638) and (ResultCode <> 3010) then
  begin
    Result := Format('Microsoft Visual C++ 运行库安装失败（错误码 %d）。', [ResultCode]);
    Exit;
  end;

  if ResultCode = 3010 then
    NeedsRestart := True;

  if not VCRedistInstalled() then
    Result := 'Microsoft Visual C++ 运行库安装后仍未检测到可用的 x64 运行库。';
end;
