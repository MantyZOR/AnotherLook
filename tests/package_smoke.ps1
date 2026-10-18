param([Parameter(Mandatory)][string]$Installer,[Parameter(Mandatory)][string]$TestDirectory,[string]$IndexDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$expectedVersion = [regex]::Match([IO.File]::ReadAllText((Join-Path $projectRoot 'src\version.h')), '#define GOODVIEW_VERSION "([^"]+)"').Groups[1].Value
if (-not $expectedVersion) { throw 'Cannot read the expected application version.' }
$testRoot = [IO.Path]::GetFullPath($TestDirectory)
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'test-artifacts'))
if (-not $testRoot.StartsWith($allowedRoot + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Package checks must use a fresh directory under test-artifacts.' }
if (Test-Path -LiteralPath $testRoot) { throw 'Package test directory must be new.' }
$appId = '{96CFAB47-450B-46BA-A701-C58178D17A1E}_is1'
$registryKeys = @("Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\$appId", "Registry::HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Uninstall\$appId")
foreach ($key in $registryKeys) { if (Test-Path -LiteralPath $key) { throw 'An installed copy already exists; the test will not replace it.' } }
New-Item -ItemType Directory -Path $testRoot | Out-Null
$installFolder = Join-Path $testRoot 'application'
$userDataFolder = Join-Path $testRoot 'user-data'
$groupName = 'WindowsGoodView'
$groupPath = Join-Path ([Environment]::GetFolderPath('Programs')) $groupName
if (Test-Path -LiteralPath $groupPath) { throw 'A Start Menu folder already exists; the test will not touch its shortcuts.' }
$shortcutPath = Join-Path $groupPath 'WindowsGoodView.lnk'
$fixturePath = Join-Path $groupPath 'GoodView catalogue watch fixture.lnk'
$installedExe = Join-Path $installFolder 'WindowsGoodView.exe'
$uninstaller = Join-Path $installFolder 'unins000.exe'
$checks = [Collections.Generic.List[object]]::new()
$installed = $false
$fixtureCreated = $false
$completed = $false
$previewApp = $null
function Check-Result([bool]$ok,[string]$name) {
    $checks.Add([pscustomobject]@{name=$name;passed=$ok})
    if (-not $ok) { throw $name }
}
function Wait-Condition([scriptblock]$predicate,[int]$timeout=8000) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do { if (& $predicate) { return $true }; Start-Sleep -Milliseconds 100 } while ($timer.ElapsedMilliseconds -lt $timeout)
    return [bool](& $predicate)
}
function Run-Setup([string]$logName) {
    $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/CURRENTUSER','/TASKS=""',('/DIR="' + $installFolder + '"'),('/GROUP="' + $groupName + '"'),('/LOG="' + (Join-Path $testRoot $logName) + '"'))
    $setup = Start-Process -FilePath $Installer -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait
    Check-Result ($setup.ExitCode -eq 0) ('installer exits successfully: ' + $logName)
}
function Index-ContainsFixture {
    $path = if ($IndexDirectory) { Join-Path $IndexDirectory 'program-index.json' } else { Join-Path $projectRoot 'dist\data\program-index.json' }
    if (-not (Test-Path -LiteralPath $path)) { return $false }
    $index = [IO.File]::ReadAllText($path) | ConvertFrom-Json
    return @($index.catalogue | Where-Object { $_.name -eq 'GoodView catalogue watch fixture' -and $_.target -eq $fixturePath }).Count -eq 1
}
try {
    Run-Setup 'install.log'; $installed=$true
    Check-Result ((Test-Path -LiteralPath $installedExe) -and (Test-Path -LiteralPath (Join-Path $installFolder 'installed.mode'))) 'installer deploys executable and installed-profile marker'
    Check-Result ((Get-FileHash -LiteralPath $installedExe).Hash -eq (Get-FileHash -LiteralPath (Join-Path $projectRoot 'build\native\WindowsGoodView.exe')).Hash) 'installed payload matches the tested native build'
    Check-Result ((Get-Item -LiteralPath $installedExe).VersionInfo.FileVersion -eq $expectedVersion) 'executable has the expected file version'
    Check-Result (Test-Path -LiteralPath $shortcutPath) 'installer creates a Start Menu shortcut'
    $wsh = New-Object -ComObject WScript.Shell
    $shortcut = $wsh.CreateShortcut($shortcutPath)
    Check-Result ($shortcut.TargetPath -eq $installedExe) 'Start Menu shortcut points to the installed executable'
    Check-Result (@($registryKeys | Where-Object { Test-Path -LiteralPath $_ }).Count -eq 1) 'installer registers one Windows uninstall entry'
    $watchFixture = $wsh.CreateShortcut($fixturePath);$watchFixture.TargetPath=$installedExe;$watchFixture.Save();$fixtureCreated=$true
    Check-Result (Wait-Condition { Index-ContainsFixture }) 'running shell learns a newly created Start Menu shortcut from change notifications'
    Remove-Item -LiteralPath $fixturePath; $fixtureCreated=$false
    Check-Result (Wait-Condition { -not (Index-ContainsFixture) }) 'running shell removes a deleted shortcut from the persistent index'
    New-Item -ItemType Directory -Path $userDataFolder | Out-Null
    $configuration = @{version=1;activeScene='test';enabled=$false;wallpaperEnabled=$false;autoStart=$false;scenes=@(@{id='test';name='Installer profile fixture';cards=@(@{id='note';type='note';title='Keep this note';content='User data survives update and uninstall';x=0;y=0;width=360;height=220})})}
    [IO.File]::WriteAllText((Join-Path $userDataFolder 'settings.json'),($configuration | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    $previewApp = Start-Process -FilePath $installedExe -ArgumentList @('--preview','--background','--data',('"' + $userDataFolder + '"')) -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 750; $previewApp.Refresh()
    Check-Result (-not $previewApp.HasExited -and $previewApp.Responding) 'installed app starts normally with a writable user profile'
    $quit = Start-Process -FilePath $installedExe -ArgumentList @('--quit','--data',('"' + $userDataFolder + '"')) -WindowStyle Hidden -PassThru -Wait
    Check-Result ($previewApp.WaitForExit(8000) -and $previewApp.ExitCode -eq 0 -and $quit.ExitCode -eq 0) 'installed app shuts down gracefully'
    $configuration = [IO.File]::ReadAllText((Join-Path $userDataFolder 'settings.json')) | ConvertFrom-Json
    Check-Result ($configuration.scenes[0].name -eq 'Installer profile fixture' -and $configuration.scenes[0].cards[0].title -eq 'Keep this note') 'installed app preserves user settings'
    $dataHash = (Get-FileHash -LiteralPath (Join-Path $userDataFolder 'settings.json')).Hash
    $fileCache = Join-Path $userDataFolder 'file-index.sqlite'
    [IO.File]::WriteAllBytes($fileCache,[byte[]](1,3,5,7,9))
    $fileCacheHash = (Get-FileHash -LiteralPath $fileCache).Hash
    Run-Setup 'update.log'
    Check-Result ((Get-FileHash -LiteralPath (Join-Path $userDataFolder 'settings.json')).Hash -eq $dataHash) 'reinstall leaves user data unchanged'
    Check-Result ((Get-FileHash -LiteralPath $fileCache).Hash -eq $fileCacheHash) 'reinstall preserves the independent file index cache'
    $remove = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="' + (Join-Path $testRoot 'uninstall.log') + '"')) -WindowStyle Hidden -PassThru -Wait
    Check-Result ($remove.ExitCode -eq 0) 'standard uninstaller exits successfully'
    Check-Result (Wait-Condition { -not (Test-Path -LiteralPath $installedExe) }) 'uninstaller removes the application executable'
    Check-Result (-not (Test-Path -LiteralPath $shortcutPath)) 'uninstaller removes its Start Menu shortcut'
    Check-Result (@($registryKeys | Where-Object { Test-Path -LiteralPath $_ }).Count -eq 0) 'uninstaller removes its Windows uninstall entry'
    Check-Result ((Get-FileHash -LiteralPath (Join-Path $userDataFolder 'settings.json')).Hash -eq $dataHash) 'uninstaller preserves user settings for a future install'
    Check-Result ((Get-FileHash -LiteralPath $fileCache).Hash -eq $fileCacheHash) 'uninstaller preserves the independent file index cache'
    $installed=$false; $completed=$true
} finally {
    if ($fixtureCreated -and (Test-Path -LiteralPath $fixturePath)) { Remove-Item -LiteralPath $fixturePath }
    if ($previewApp -and -not $previewApp.HasExited -and (Test-Path -LiteralPath $installedExe)) {
        Start-Process -FilePath $installedExe -ArgumentList @('--quit','--data',('"' + $userDataFolder + '"')) -WindowStyle Hidden -Wait
        $previewApp.WaitForExit(8000) | Out-Null
    }
    if ($installed -and (Test-Path -LiteralPath $uninstaller)) { Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait }
    [pscustomobject]@{passed=$completed -and @($checks | Where-Object { -not $_.passed }).Count -eq 0;checks=$checks} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $testRoot 'package-smoke.json') -Encoding utf8
}
$checks | Format-Table -AutoSize
