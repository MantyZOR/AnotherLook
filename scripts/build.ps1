param([switch]$WithoutWeb, [switch]$Test, [switch]$TestDesktop, [switch]$TestPerformance, [switch]$BuildOnly, [switch]$AppOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sdkFolder = Join-Path $projectRoot 'third_party\webview2'
if (-not $WithoutWeb -and -not (Test-Path -LiteralPath (Join-Path $sdkFolder 'build\native\include\WebView2.h'))) {
    $packagePath = Join-Path $projectRoot 'third_party\webview2.zip'
    New-Item -ItemType Directory -Path (Split-Path -Parent $packagePath) -Force | Out-Null
    Invoke-WebRequest -Uri 'https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4258.31/microsoft.web.webview2.1.0.4258.31.nupkg' -OutFile $packagePath
    $expectedHash = '56F7F4B8BF9AEE4B8EFEFBBDD4F67D5F74EBD1B100ED0806DA71BF76AF481AA9'
    if ((Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash -ne $expectedHash) { throw 'WebView2 SDK checksum mismatch.' }
    Expand-Archive -LiteralPath $packagePath -DestinationPath $sdkFolder -Force
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$compiler = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\cl.exe' | Select-Object -First 1
if (-not $compiler) { throw 'Install Visual Studio C++ Build Tools and a Windows SDK.' }
$compilerBin = Split-Path -Parent $compiler
$toolchainRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $compilerBin))
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
$env:PATH = $compilerBin + ';' + (Join-Path $sdkRoot "bin\$sdkVersion\x64") + ';' + $env:PATH
$env:INCLUDE = (Join-Path $toolchainRoot 'include') + ';' + (($('ucrt','shared','um','winrt','cppwinrt') | ForEach-Object { Join-Path $sdkRoot "Include\$sdkVersion\$_" }) -join ';')
$env:LIB = (Join-Path $toolchainRoot 'lib\x64') + ';' + (Join-Path $sdkRoot "Lib\$sdkVersion\ucrt\x64") + ';' + (Join-Path $sdkRoot "Lib\$sdkVersion\um\x64")
$buildFolder = Join-Path $projectRoot 'build\native'
cmake -S $projectRoot -B $buildFolder -G 'NMake Makefiles' "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_MAKE_PROGRAM=$(Join-Path $compilerBin 'nmake.exe')" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
$buildArguments=@('--build',$buildFolder,'--config','Release')
if($AppOnly){$buildArguments+=@('--target','WindowsGoodView')}
cmake @buildArguments
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($Test) {
    ctest --test-dir $buildFolder -C Release -LE 'desktop|performance' --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
if ($TestDesktop) {
    ctest --test-dir $buildFolder -C Release -L desktop --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Desktop integration tests failed.' }
}
if ($TestPerformance) {
    ctest --test-dir $buildFolder -C Release -L performance --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Performance scenario failed.' }
}
$releaseFolder = Join-Path $projectRoot 'dist'
if ($BuildOnly) { Write-Output (Join-Path $buildFolder 'WindowsGoodView.exe'); return }
New-Item -ItemType Directory -Path $releaseFolder -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $buildFolder 'WindowsGoodView.exe') -Destination (Join-Path $releaseFolder 'WindowsGoodView.exe') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'resources\THIRD_PARTY_NOTICES.txt') -Destination $releaseFolder -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'resources\ManagedShell-LICENSE.txt') -Destination $releaseFolder -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'resources\WebView2-LICENSE.txt') -Destination $releaseFolder -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'resources\WebView2-NOTICE.txt') -Destination $releaseFolder -Force
foreach ($fontLicense in @('Rubik-OFL.txt','Onest-OFL.txt','Lucide-LICENSE.txt')) { Copy-Item -LiteralPath (Join-Path $projectRoot ('resources\'+$fontLicense)) -Destination $releaseFolder -Force }
Write-Output (Join-Path $releaseFolder 'WindowsGoodView.exe')
