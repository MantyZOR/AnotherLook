param([ValidateRange(10,1800)][int]$Seconds=30,[switch]$Hardware,[switch]$Motion,[string]$BinaryPath)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if(-not $BinaryPath){$BinaryPath=Join-Path $projectRoot 'dist\WindowsGoodView.exe'}
$BinaryPath=(Resolve-Path -LiteralPath $BinaryPath).Path
$measurementDir=Join-Path $projectRoot ('test-artifacts\idle-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $measurementDir -Force|Out-Null
# Do not copy user roots, applications or websites into a measurement profile.
$config=@{
 version=1;activeScene='perf';enabled=$true;wallpaperEnabled=$false;autoStart=$false;softwareRendering=(-not $Hardware)
 fileSearch=@{initialized=$true;enabled=$false;paused=$true;allFixedDrives=$false;fastNtfs=$false;roots=@()}
 interface=@{railOrbit=$true}
 scenes=@(@{id='perf';name='Наблюдение';network='links';networkMotion=[bool]$Motion;networkDensity=1;networkSpeed=1
  appearance='dark';accent='A99AEB';backdrop='18283F';autoTile=$false;windows=@()
  cards=@(@{id='clock';type='clock';title='Часы';x=350;y=150;width=300;height=200;autoLaunch=$false})})
}
$config|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $measurementDir 'settings.json') -Encoding utf8NoBOM
$processName=[IO.Path]::GetFileNameWithoutExtension($BinaryPath)
$existingIds=@(Get-Process -Name $processName -ErrorAction SilentlyContinue|Select-Object -ExpandProperty Id)
$measurementApp=Start-Process -FilePath $BinaryPath -ArgumentList @('--background','--data',('"'+$measurementDir+'"')) -WindowStyle Hidden -PassThru
$ownedProcesses=@()
try{
 Start-Sleep -Seconds 5
 $ownedProcesses=@(Get-Process -Name $processName -ErrorAction SilentlyContinue|Where-Object {$_.Id -notin $existingIds})
 if($ownedProcesses.Count -ne 2){throw "Expected app and recovery helper; found $($ownedProcesses.Count)."}
 foreach($owned in $ownedProcesses){$owned.Refresh()}
 $initialCpu=($ownedProcesses|ForEach-Object {$_.TotalProcessorTime.TotalSeconds}|Measure-Object -Sum).Sum
 $clock=[Diagnostics.Stopwatch]::StartNew();$samples=[Collections.Generic.List[object]]::new()
 while($clock.Elapsed.TotalSeconds -lt $Seconds){
  Start-Sleep -Milliseconds ([math]::Min(1000,[math]::Max(1,1000*($Seconds-$clock.Elapsed.TotalSeconds))))
  $private=0L;$working=0L;$cpu=0.0
  foreach($owned in $ownedProcesses){$owned.Refresh();if($owned.HasExited){throw 'A measured process exited unexpectedly.'};$private+=$owned.PrivateMemorySize64;$working+=$owned.WorkingSet64;$cpu+=$owned.TotalProcessorTime.TotalSeconds}
  $samples.Add([pscustomobject]@{Seconds=[math]::Round($clock.Elapsed.TotalSeconds,2);PrivateBytes=$private;WorkingSetBytes=$working;CpuSeconds=$cpu})
 }
 $report=[pscustomobject]@{
  Version=(Get-Item -LiteralPath $BinaryPath).VersionInfo.FileVersion;Binary=$BinaryPath;Sha256=(Get-FileHash -LiteralPath $BinaryPath).Hash
  Scenario='Isolated shell and guardian, one clock, ring rail, no live sites or file indexing'
  SoftwareRendering=(-not $Hardware);Motion=[bool]$Motion;DurationSeconds=[math]::Round($clock.Elapsed.TotalSeconds,2)
  LogicalProcessors=[Environment]::ProcessorCount;ProcessCount=$ownedProcesses.Count
  AverageCpuPercentOfMachine=[math]::Round(100*($samples[-1].CpuSeconds-$initialCpu)/$clock.Elapsed.TotalSeconds/[Environment]::ProcessorCount,4)
  AveragePrivateMiB=[math]::Round(($samples|Measure-Object PrivateBytes -Average).Average/1MB,2)
  PeakPrivateMiB=[math]::Round(($samples|Measure-Object PrivateBytes -Maximum).Maximum/1MB,2)
  AverageWorkingSetMiB=[math]::Round(($samples|Measure-Object WorkingSetBytes -Average).Average/1MB,2);Samples=$samples.ToArray()
 }
 $report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $measurementDir 'result.json') -Encoding utf8NoBOM
 $report|Select-Object -ExcludeProperty Samples|ConvertTo-Json
 Write-Output (Join-Path $measurementDir 'result.json')
}finally{
 Start-Process -FilePath $BinaryPath -ArgumentList @('--quit','--data',('"'+$measurementDir+'"')) -WindowStyle Hidden -Wait
 if(-not $measurementApp.WaitForExit(5000)){$measurementApp.Kill()}
}
