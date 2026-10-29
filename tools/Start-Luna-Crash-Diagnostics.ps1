# Opt-in private dump capture, reusing the validated external lifecycle collector.
param([string]$OutputDirectory = '', [switch]$NoPause)
$ErrorActionPreference = 'Stop'
$nativeModules = @((Join-Path $PSHOME 'Modules'), (Join-Path $env:ProgramFiles 'WindowsPowerShell/Modules'))
$env:PSModulePath = (($nativeModules + @($env:PSModulePath -split ';')) | Select-Object -Unique) -join ';'
. (Join-Path $PSScriptRoot 'Luna-Crash-Tools.ps1')
$dumper = Get-LunaProcDump
if (!$OutputDirectory) { $OutputDirectory = Join-Path ([Environment]::GetFolderPath('Desktop')) ('LUNA-Diagnostics-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output already exists; existing files were not changed.' }
$private = $OutputDirectory + '-PRIVATE-CRASH'
New-Item -ItemType Directory -Path $private | Out-Null
Write-Host 'PRIVATE DUMP: may contain PDF text, annotations and personal information. Never upload it to public GitHub.'
Write-Host 'Wait for the attached message before reproducing Ctrl+W. There are no automatic uploads.'
$powershell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$collector = Start-Process -FilePath $powershell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'Start-Luna-Diagnostics.ps1')+'"'),'-NoPause','-OutputDirectory',('"'+$OutputDirectory+'"')) -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds(30)
$target = $null
while ([DateTime]::UtcNow -lt $deadline -and !$collector.HasExited) {
    try {
        $first = Get-Content -LiteralPath (Join-Path $OutputDirectory 'lifecycle.jsonl') -Encoding UTF8 -TotalCount 1 -ErrorAction Stop | ConvertFrom-Json
        $candidate = [Diagnostics.Process]::GetProcessById([int]$first.pid)
        if ([IO.Path]::GetFullPath($candidate.Path) -ne [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'LUNA.exe'))) { throw 'Unexpected process image.' }
        $target = $candidate; $targetHandle = $target.Handle; break
    } catch { Start-Sleep -Milliseconds 100 }
}
if (!$target) { throw 'No matching live LUNA process was found. Existing logs are preserved.' }
$dump = Join-Path $private 'LUNA-private.dmp'
$monitor = Start-Process -FilePath $dumper -ArgumentList @('-accepteula','-e','-mm','-n','1',([string]$target.Id),('"'+$dump+'"')) -RedirectStandardOutput (Join-Path $private 'procdump.log') -RedirectStandardError (Join-Path $private 'procdump-errors.log') -PassThru
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LunaPrivateDebugStatus {
 [DllImport("kernel32.dll", SetLastError=true)] [return: MarshalAs(UnmanagedType.Bool)]
 public static extern bool CheckRemoteDebuggerPresent(IntPtr process, [MarshalAs(UnmanagedType.Bool)] out bool attached);
}
'@
$attached = $false; $deadline = [DateTime]::UtcNow.AddSeconds(20)
while ([DateTime]::UtcNow -lt $deadline -and !$monitor.HasExited) {
    $connected = $false
    if ([LunaPrivateDebugStatus]::CheckRemoteDebuggerPresent($targetHandle,[ref]$connected) -and $connected) { $attached = $true; break }
    Start-Sleep -Milliseconds 100
}
if (!$attached -and !$target.HasExited) { throw 'ProcDump did not confirm attachment. LUNA was not terminated; save and close it normally. Private logs are preserved.' }
@{pid=$target.Id; debuggerAttached=$attached; check='CheckRemoteDebuggerPresent'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $private 'attachment-confirmed.json') -Encoding UTF8
Write-Host ('Attached to LUNA PID ' + $target.Id + '. You can now reproduce Ctrl+W.')
$monitor.WaitForExit()
$collector.WaitForExit()
if ($collector.ExitCode -ne 0) { throw 'The lifecycle collector failed. Private dump and partial logs are preserved.' }
$created = Test-Path -LiteralPath $dump
@{ pid=$target.Id; dumpCreated=$created; privateDumpName='LUNA-private.dmp'; automaticUpload=$false;
   scope='Private local minidump, separate from the four-file diagnostic ZIP; may contain PDF or personal data.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $private 'capture-summary.json') -Encoding UTF8
Write-Host ('Safe lifecycle ZIP: ' + $OutputDirectory + '.zip')
Write-Host ('PRIVATE files, do not publish: ' + $private)
if ($created) { Write-Host 'Run Analyze-Luna-Crash.cmd to analyze the latest private dump locally. Share only reviewed stack-summary.txt text.' }
else { Write-Host 'No dump was produced. This does not establish that the reported crash is fixed.' }
if (!$NoPause) { Read-Host 'Press Enter to finish' }
