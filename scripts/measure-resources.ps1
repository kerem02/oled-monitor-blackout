param(
    [ValidateRange(60,3600)][int]$Seconds = 60,
    [ValidateSet('OnOLED','Waiting','BlackedOut','Disabled','Unavailable','LifecycleCycles')][string]$State = 'OnOLED'
)
$ErrorActionPreference = 'Stop'
$apps = @(Get-Process -Name OLEDBlackout -ErrorAction Stop)
if ($apps.Count -ne 1) { throw 'Run exactly one OLED Blackout instance.' }
if (-not ('OLEDResourceProbe' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OLEDResourceProbe {
    [DllImport("user32.dll")]
    public static extern uint GetGuiResources(IntPtr process, uint flags);
}
'@
}
$app = $apps[0]
$app.Refresh()
$startCpu = $app.TotalProcessorTime.TotalMilliseconds
$watch = [Diagnostics.Stopwatch]::StartNew()
$working = @(); $private = @(); $handles = @(); $userObjects = @(); $gdiObjects = @()
for ($i = 0; $i -lt $Seconds; $i++) {
    Start-Sleep -Seconds 1
    $app.Refresh()
    if ($app.HasExited) { throw 'App exited during measurement.' }
    $working += $app.WorkingSet64
    $private += $app.PrivateMemorySize64
    $handles += $app.HandleCount
    $gdiObjects += [OLEDResourceProbe]::GetGuiResources($app.Handle, 0)
    $userObjects += [OLEDResourceProbe]::GetGuiResources($app.Handle, 1)
}
$elapsed = $watch.Elapsed.TotalMilliseconds
$cpu = $app.TotalProcessorTime.TotalMilliseconds - $startCpu
[pscustomobject]@{
    State = $State # Operator label; this script does not change or infer the app state.
    ProcessId = $app.Id
    ElapsedSeconds = [math]::Round($elapsed / 1000, 2)
    CpuMilliseconds = [math]::Round($cpu, 2)
    PercentOfOneLogicalCpu = [math]::Round(100 * $cpu / $elapsed, 4)
    MeanWorkingSetMiB = [math]::Round(($working | Measure-Object -Average).Average / 1MB, 2)
    PeakWorkingSetMiB = [math]::Round(($working | Measure-Object -Maximum).Maximum / 1MB, 2)
    MeanPrivateMiB = [math]::Round(($private | Measure-Object -Average).Average / 1MB, 2)
    PeakPrivateMiB = [math]::Round(($private | Measure-Object -Maximum).Maximum / 1MB, 2)
    FirstHandleCount = $handles[0]
    LastHandleCount = $handles[-1]
    PeakHandleCount = ($handles | Measure-Object -Maximum).Maximum
    FirstUSER = $userObjects[0]
    LastUSER = $userObjects[-1]
    PeakUSER = ($userObjects | Measure-Object -Maximum).Maximum
    FirstGDI = $gdiObjects[0]
    LastGDI = $gdiObjects[-1]
    PeakGDI = ($gdiObjects | Measure-Object -Maximum).Maximum
}
# External diagnostic inspects only OLED Blackout. It is not part of the application.
# Repeat samples before/after cycles; first/last counts alone do not establish leak freedom.
