param([string]$Compiler, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -BuildOnly }
if (-not $Compiler) {
    $candidates = @((Join-Path $projectRoot 'third_party\inno\ISCC.exe'), (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'), (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe'))
    $Compiler = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $Compiler) { throw 'Inno Setup 6.7+ is required. Pass -Compiler with the path to ISCC.exe.' }
$bootstrapper = Join-Path $projectRoot 'third_party\MicrosoftEdgeWebview2Setup.exe'
if (-not (Test-Path -LiteralPath $bootstrapper)) {
    Invoke-WebRequest -Uri 'https://go.microsoft.com/fwlink/p/?LinkId=2124703' -OutFile $bootstrapper
}
$signature = Get-AuthenticodeSignature -LiteralPath $bootstrapper
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') { throw 'Invalid Microsoft WebView2 bootstrapper signature.' }
$versionHeader = [System.IO.File]::ReadAllText((Join-Path $projectRoot 'src\version.h'))
$version = [regex]::Match($versionHeader, '#define GOODVIEW_VERSION "([^"]+)"').Groups[1].Value
if (-not $version) { throw 'Application version is missing.' }
& $Compiler "/DAppVersion=$version" (Join-Path $PSScriptRoot 'installer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
$installer = Join-Path $projectRoot "dist\WindowsGoodView-$version-Setup.exe"
Get-FileHash -LiteralPath $installer -Algorithm SHA256
