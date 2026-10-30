# Local P0 reproduction collector. Does not upload anything or copy PDFs/settings.
param([string]$OutputDirectory = '', [switch]$NoPause)
$ErrorActionPreference = 'Stop'
# A launcher may inherit PowerShell 7's module search path. Restore the native
# Windows PowerShell module roots before using Utility/Archive cmdlets.
$nativeModules = @((Join-Path $PSHOME 'Modules'), (Join-Path $env:ProgramFiles 'WindowsPowerShell/Modules'))
$env:PSModulePath = (($nativeModules + @($env:PSModulePath -split ';')) | Select-Object -Unique) -join ';'
$package = $PSScriptRoot
if (!(Test-Path (Join-Path $package 'LUNA.exe'))) { throw 'Keep this script next to LUNA.exe.' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path ([Environment]::GetFolderPath('Desktop')) ('LUNA-Diagnostics-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Path $OutputDirectory -ErrorAction Stop | Out-Null
$trace = Join-Path $OutputDirectory 'lifecycle.jsonl'
$start = [DateTime]::UtcNow
Copy-Item (Join-Path $package 'build-info.json') (Join-Path $OutputDirectory 'build-info.json')
$hashStream = [IO.File]::OpenRead((Join-Path $package 'LUNA.exe'))
$hasher = [Security.Cryptography.SHA256]::Create()
try { $exeHash = ([BitConverter]::ToString($hasher.ComputeHash($hashStream))).Replace('-','').ToLowerInvariant() }
finally { $hashStream.Dispose(); $hasher.Dispose() }
Write-Host 'Save and close other LUNA windows before reproducing here, so another process does not own the PDFs.'
Write-Host 'Open the slow PDF several times, then close LUNA normally. Per-stage timings and first compiled-page paint are recorded.'
Write-Host 'Logs stay on this PC. No PDF, thumbnail, memo or settings are copied.'
$previousPerformanceLog = $env:LUNA_PERF_LOG
try {
    $env:LUNA_PERF_LOG = Join-Path $OutputDirectory 'performance.jsonl'
    $process = Start-Process -FilePath (Join-Path $package 'LUNA.exe') -ArgumentList @('--luna-lifecycle-trace', ('"' + $trace + '"')) -PassThru
} finally { $env:LUNA_PERF_LOG = $previousPerformanceLog }
$processHandle = $process.Handle
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LunaDiagnosticWindow {
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
}
'@
$lastWindow = [IntPtr]::Zero
while (!$process.WaitForExit(1000)) {
    $process.Refresh()
    if ($process.MainWindowHandle -ne [IntPtr]::Zero) { $lastWindow = $process.MainWindowHandle }
    @{ timeUtc=[DateTime]::UtcNow.ToString('o'); pid=$process.Id; processAlive=$true;
       knownWindowExists=[LunaDiagnosticWindow]::IsWindow($lastWindow);
       windowVisible=[LunaDiagnosticWindow]::IsWindowVisible($lastWindow);
       minimized=[LunaDiagnosticWindow]::IsIconic($lastWindow)
    } | ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $OutputDirectory 'process-state.jsonl') -Encoding UTF8
}
$process.WaitForExit()
$code = $process.ExitCode
$hex = if ($null -eq $code) { 'unavailable' } else { '{0:X8}' -f ([long]$code -band 0xffffffffL) }
$events = @()
if (Test-Path $trace) { $events = @(Get-Content -LiteralPath $trace -Encoding UTF8 | ForEach-Object { try { $_ | ConvertFrom-Json } catch {} }) }
$approved = @($events | Where-Object { $_.event -eq 'shell-close-approved' }).Count -gt 0
$faults = @(); $faultStatus = 'not-found-not-proof-of-no-crash'
try {
    # Only the matching child's safe crash fields; never copy event messages,
    # executable paths, command lines, account names or crash dumps.
    $recentFaults = @(Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000; StartTime=$start.ToLocalTime()} -MaxEvents 20 -ErrorAction Stop)
    foreach ($entry in $recentFaults) {
        [xml]$xml = $entry.ToXml(); $data = @{}
        foreach ($field in $xml.Event.EventData.Data) { $data[$field.Name] = $field.InnerText }
        $eventPid = $data['ProcessId']
        if (!$eventPid -or !$eventPid.StartsWith('0x')) { continue }
        if ([Convert]::ToInt64($eventPid.Substring(2),16) -ne $process.Id) { continue }
        $faults += @{ timeUtc=$entry.TimeCreated.ToUniversalTime().ToString('o'); exceptionCode=$data['ExceptionCode'];
            module=[IO.Path]::GetFileName($data['ModuleName']); faultingOffset=$data['FaultingOffset'] }
    }
    if ($faults.Count) { $faultStatus = 'matching-child-application-error' }
} catch { $faultStatus = 'event-log-unavailable-or-no-matching-events' }
@{ startedUtc=$start.ToString('o'); endedUtc=[DateTime]::UtcNow.ToString('o'); pid=$process.Id; exeSha256=$exeHash; exitCode=$code; exitCodeHex=$hex;
   shellApprovedExit=$approved; lifecycleRecords=$events.Count; osVersion=[Environment]::OSVersion.Version.ToString();
   faultEventStatus=$faultStatus; applicationErrors=$faults;
   classification=$(if ($null -eq $code) { 'exit-code-unavailable-unresolved' } elseif ($code -ne 0) { 'abnormal-exit-investigate-code' } elseif (!$approved) { 'exit-without-shell-approval-unresolved' } else { 'shell-approved-exit' });
   scope='No crash dump or private documents collected. Exit code alone does not establish the cause.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'process-exit.json') -Encoding UTF8
Compress-Archive -Path (Join-Path $OutputDirectory '*') -DestinationPath ($OutputDirectory + '.zip')
Write-Host ('Saved locally: ' + $OutputDirectory + '.zip')
if (!$NoPause) { Read-Host 'Press Enter to finish' }
