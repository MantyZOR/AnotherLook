param([int]$AppProcessId=0,[ValidateRange(10,120)][int]$Seconds=20,[switch]$WaitForRing,[string]$UiProbeDirectory)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if(-not ('GoodViewLiveProbeNative' -as [type])){
 Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class GoodViewLiveProbeNative {
 [StructLayout(LayoutKind.Sequential)] public struct Point {public int X,Y;}
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;}
 public sealed class Window {public long Handle;public uint Thread;public string Class;public bool Visible,Layered;public int X,Y,Width,Height;}
 delegate bool Callback(IntPtr h,IntPtr value);
 [DllImport("user32.dll")] static extern bool EnumWindows(Callback callback,IntPtr value);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out Rect rect);
 [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",EntryPoint="GetWindowLongW")] static extern int GetWindowLong(IntPtr h,int index);
 [DllImport("user32.dll")] static extern bool GetCursorPos(out Point point);
 [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point point);
 [DllImport("kernel32.dll")] public static extern uint GetActiveProcessorCount(ushort group);
 [DllImport("kernel32.dll")] static extern bool GetSystemTimes(out long idle,out long kernel,out long user);
 public static long[] MachineTime(){long idle,kernel,user;GetSystemTimes(out idle,out kernel,out user);return new[]{idle,kernel+user};}
 public static Window[] Windows(uint process) {var list=new List<Window>();EnumWindows((h,v)=>{uint pid;uint thread=GetWindowThreadProcessId(h,out pid);if(pid==process){Rect r;GetWindowRect(h,out r);var name=new StringBuilder(256);GetClassName(h,name,256);list.Add(new Window{Handle=h.ToInt64(),Thread=thread,Class=name.ToString(),Visible=IsWindowVisible(h),Layered=(GetWindowLong(h,-20)&0x80000)!=0,X=r.Left,Y=r.Top,Width=r.Right-r.Left,Height=r.Bottom-r.Top});}return true;},IntPtr.Zero);return list.ToArray();}
 public static object Pointer(){Point p;GetCursorPos(out p);IntPtr h=WindowFromPoint(p);uint pid;GetWindowThreadProcessId(h,out pid);var name=new StringBuilder(256);GetClassName(h,name,256);return new {X=p.X,Y=p.Y,Process=pid,Class=name.ToString(),Handle=h.ToInt64()};}
}
'@
}
$familyMetadata=@(Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name)
if(-not $AppProcessId){
 $candidateIds=@($familyMetadata | Where-Object Name -eq 'WindowsGoodView.exe' | Select-Object -ExpandProperty ProcessId)
 $roots=@($familyMetadata | Where-Object {$_.ProcessId -in $candidateIds -and $_.ParentProcessId -notin $candidateIds})
 if($roots.Count -ne 1){throw 'Pass -AppProcessId with the main WindowsGoodView process ID.'}
 $AppProcessId=[int]$roots[0].ProcessId
}
$mainProcess=Get-Process -Id $AppProcessId
$familyIds=@($AppProcessId)
do{$beforeCount=$familyIds.Count;$familyIds=@($familyIds)+@($familyMetadata | Where-Object {$_.ParentProcessId -in $familyIds -and $_.ProcessId -notin $familyIds} | Select-Object -ExpandProperty ProcessId)}while($familyIds.Count -ne $beforeCount)
$family=@($familyIds | ForEach-Object {Get-Process -Id $_ -ErrorAction SilentlyContinue})+@(Get-Process -Name dwm,explorer -ErrorAction SilentlyContinue)
$logicalProcessors=[GoodViewLiveProbeNative]::GetActiveProcessorCount(65535)
$windows=@([GoodViewLiveProbeNative]::Windows([uint32]$AppProcessId))
$uiThreadIds=@($windows | Where-Object Class -eq 'GoodView.Surface' | Select-Object -ExpandProperty Thread -Unique)
$rings=@($windows | Where-Object {$_.Visible -and $_.Layered -and $_.Class -eq 'GoodView.Surface' -and $_.Width -ge 200 -and $_.Width -eq $_.Height -and $_.Width -le 1000})
if($WaitForRing){
 Write-Output 'WAITING_RING: recording starts when the pointer reaches a visible ring. Timeout: 120 seconds.'
 $waiting=[Diagnostics.Stopwatch]::StartNew();$found=$false
 while($waiting.Elapsed.TotalSeconds -lt 120){$pointer=[GoodViewLiveProbeNative]::Pointer();if($pointer.Handle -in @($rings | Select-Object -ExpandProperty Handle)){$found=$true;break};Start-Sleep -Milliseconds 150}
 if(-not $found){throw 'No pointer entry into a ring was observed; no active-hover measurement was claimed.'}
 Write-Output 'CAPTURE_STARTED: move around the ring until 20 moving-on-ring samples have been collected.'
}
if($UiProbeDirectory){
 $probeDirectory=(Resolve-Path -LiteralPath $UiProbeDirectory).Path
 $command=Start-Process -FilePath $mainProcess.Path -ArgumentList @('--profile-ui','--data',('"'+$probeDirectory+'"')) -WindowStyle Hidden -PassThru -Wait
 if($command.ExitCode -ne 0){throw 'Cannot start the optional in-app probe.'}
}
$samples=[Collections.Generic.List[object]]::new()
$threadStart=@{};foreach($thread in $mainProcess.Threads){try{$threadStart[$thread.Id]=$thread.TotalProcessorTime.TotalMilliseconds}catch{}}
$previousCpu=@{};foreach($entry in $family){try{$entry.Refresh();$previousCpu[$entry.Id]=$entry.TotalProcessorTime.TotalMilliseconds}catch{}}
$previousMachine=[GoodViewLiveProbeNative]::MachineTime()
$clock=[Diagnostics.Stopwatch]::StartNew();$previousMs=0.0;$activeSamples=0;$lastRingPointer=$null
if(-not $WaitForRing){Write-Output "Recording for $Seconds seconds. No input is injected and no windows are switched."}
while(($WaitForRing -and $activeSamples -lt 20 -and $clock.Elapsed.TotalSeconds -lt 120) -or (-not $WaitForRing -and $clock.Elapsed.TotalSeconds -lt $Seconds)){
 Start-Sleep -Milliseconds 500
 $now=$clock.Elapsed.TotalMilliseconds;$period=$now-$previousMs;$rows=@()
 foreach($entry in $family){
  try{$entry.Refresh();if($entry.HasExited){continue};$cpu=$entry.TotalProcessorTime.TotalMilliseconds;$delta=$cpu-$previousCpu[$entry.Id];$previousCpu[$entry.Id]=$cpu
   $rows += [pscustomobject]@{PID=$entry.Id;Name=$entry.ProcessName;Family=($entry.Id -in $familyIds);CpuPercent=[math]::Round(100*$delta/$period/$logicalProcessors,3);CpuMs=[math]::Round($delta,3);PrivateMiB=[math]::Round($entry.PrivateMemorySize64/1MB,2)}
  }catch{}
 }
 $machine=[GoodViewLiveProbeNative]::MachineTime();$machineDelta=$machine[1]-$previousMachine[1];$machinePercent=if($machineDelta -gt 0){100*(1-($machine[0]-$previousMachine[0])/$machineDelta)}else{0};$previousMachine=$machine
 $pointer=[GoodViewLiveProbeNative]::Pointer();$onRing=$pointer.Handle -in @($rings | Select-Object -ExpandProperty Handle);if($onRing -and (!$lastRingPointer -or [math]::Abs($pointer.X-$lastRingPointer.X)+[math]::Abs($pointer.Y-$lastRingPointer.Y) -gt 2)){++$activeSamples;$lastRingPointer=$pointer;if($WaitForRing -and $activeSamples%5 -eq 0){Write-Output "RING_SAMPLES: $activeSamples / 20"}}
 $samples.Add([pscustomobject]@{Seconds=[math]::Round($now/1000,3);MachineCpuPercent=[math]::Round($machinePercent,3);OnRing=$onRing;Pointer=$pointer;Processes=$rows})
 $previousMs=$now
}
$mainProcess.Refresh();$threads=@();foreach($thread in $mainProcess.Threads){try{if($threadStart.ContainsKey($thread.Id)){$used=$thread.TotalProcessorTime.TotalMilliseconds-$threadStart[$thread.Id];$threads += [pscustomobject]@{Thread=$thread.Id;Ui=($thread.Id -in $uiThreadIds);CpuMs=[math]::Round($used,3)}}}catch{}}
$binary=$mainProcess.Path
$binaryInfo=Get-Item -LiteralPath $binary
$result=[pscustomobject]@{Version=$binaryInfo.VersionInfo.FileVersion;Binary=$binary;Sha256=(Get-FileHash -LiteralPath $binary).Hash;ProcessStartedUtc=$mainProcess.StartTime.ToUniversalTime();BinaryModifiedUtc=$binaryInfo.LastWriteTimeUtc;ImageMayPredateFile=($mainProcess.StartTime.ToUniversalTime() -lt $binaryInfo.LastWriteTimeUtc);MainPID=$AppProcessId;LogicalProcessors=$logicalProcessors;DurationSeconds=[math]::Round($clock.Elapsed.TotalSeconds,3);MovingRingSamples=$activeSamples;HoverCoverageSufficient=($activeSamples -ge 20);Windows=$windows;Threads=@($threads | Sort-Object CpuMs -Descending);Samples=$samples.ToArray()}
$outputFolder=Join-Path $projectRoot ('test-artifacts\live-ring-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'));New-Item -ItemType Directory -Force -Path $outputFolder | Out-Null
$outputPath=Join-Path $outputFolder 'live-cpu.json';[IO.File]::WriteAllText($outputPath,($result | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output $outputPath
$ringRows=@($samples | Where-Object OnRing | ForEach-Object {$_.Processes | Where-Object PID -eq $AppProcessId})
if($activeSamples -ge 20 -and $ringRows.Count){
 Write-Output ('Main process on ring: average '+[math]::Round(($ringRows | Measure-Object CpuPercent -Average).Average,2)+'%, peak '+[math]::Round(($ringRows | Measure-Object CpuPercent -Maximum).Maximum,2)+'% (500 ms samples).')
}else{Write-Output 'INCONCLUSIVE: insufficient movement over the native ring. This is not a successful high-CPU reproduction.'}
$threads | Sort-Object CpuMs -Descending | Select-Object -First 6
