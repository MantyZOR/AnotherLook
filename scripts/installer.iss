#ifndef AppVersion
  #define AppVersion "0.8.16"
#endif
#ifndef AppBinary
  #define AppBinary "..\build\native\WindowsGoodView.exe"
#endif
#ifndef OutputPath
  #define OutputPath "..\dist"
#endif

[Setup]
AppId={{96CFAB47-450B-46BA-A701-C58178D17A1E}
AppName=WindowsGoodView
AppVersion={#AppVersion}
AppVerName=WindowsGoodView {#AppVersion}
DefaultDirName={autopf}\WindowsGoodView
DefaultGroupName=WindowsGoodView
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputPath}
OutputBaseFilename=WindowsGoodView-{#AppVersion}-Setup
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\WindowsGoodView.exe
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern dark includetitlebar
WizardSizePercent=110
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
VersionInfoVersion={#AppVersion}
VersionInfoDescription=WindowsGoodView Setup

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "webview2"; Description: "Установить Microsoft WebView2 для встроенных сайтов (нужен интернет)"; Check: NeedsWebView2

[Files]
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#AppBinary}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\installed.mode"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\Lucide-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\Rubik-OFL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\Onest-OFL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\ManagedShell-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\WebView2-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\WebView2-NOTICE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\MicrosoftEdgeWebview2Setup.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Tasks: webview2; Check: NeedsWebView2

[Icons]
Name: "{group}\WindowsGoodView"; Filename: "{app}\WindowsGoodView.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\WindowsGoodView"; Filename: "{app}\WindowsGoodView.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\MicrosoftEdgeWebview2Setup.exe"; Parameters: "/silent /install"; StatusMsg: "Установка Microsoft WebView2…"; Tasks: webview2; Check: NeedsWebView2; Flags: waituntilterminated; AfterInstall: CheckRuntimeInstall
Filename: "{app}\WindowsGoodView.exe"; Description: "Открыть WindowsGoodView"; Flags: postinstall nowait skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{app}\WindowsGoodView.exe"; Parameters: "--uninstall-cleanup"; Flags: waituntilterminated; RunOnceId: "QuitAndRestoreWindows"

[Code]
function HasRuntimeAt(Root: Integer): Boolean;
var Version: String;
begin
  Result := RegQueryStringValue(Root, 'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) and (Version <> '') and (Version <> '0.0.0.0');
end;

function NeedsWebView2: Boolean;
begin
  Result := not (HasRuntimeAt(HKLM32) or HasRuntimeAt(HKCU32) or HasRuntimeAt(HKCU64));
end;

procedure CheckRuntimeInstall;
begin
  if NeedsWebView2 then
    SuppressibleMsgBox('WebView2 пока недоступен. Оболочка установлена, но для встроенных сайтов нужен Microsoft WebView2 Runtime. Проверь подключение к интернету и повтори установку компонента.', mbInformation, MB_OK, IDOK);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Code: Integer;
begin
  Result := '';
  if FileExists(ExpandConstant('{app}\WindowsGoodView.exe')) then
    Exec(ExpandConstant('{app}\WindowsGoodView.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;
