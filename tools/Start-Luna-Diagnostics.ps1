# Local P0 reproduction collector. Does not upload anything or copy PDFs/settings.
param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$package = $PSScriptRoot
if (!(Test-Path (Join-Path $package 'LUNA.exe'))) { throw 'Keep this script next to LUNA.exe.' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path ([Environment]::GetFolderPath('Desktop')) ('LUNA-Diagnostics-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Path $OutputDirectory -ErrorAction Stop | Out-Null
$trace = Join-Path $OutputDirectory 'lifecycle.jsonl'
$start = [DateTime]::UtcNow
Copy-Item (Join-Path $package 'build-info.json') (Join-Path $OutputDirectory 'build-info.json')
Write-Host 'Reproduce Ctrl+W with zero, one and multiple PDFs, including search focus. Then close LUNA normally if it stays open.'
Write-Host 'Logs stay on this PC. No PDF, thumbnail, memo or settings are copied.'
$process = Start-Process -FilePath (Join-Path $package 'LUNA.exe') -ArgumentList @('--luna-lifecycle-trace', ('"' + $trace + '"')) -PassThru
$processHandle = $process.Handle
$process.WaitForExit()
$code = $process.ExitCode
$hex = if ($null -eq $code) { 'unavailable' } else { '{0:X8}' -f ([long]$code -band 0xffffffffL) }
$events = @()
if (Test-Path $trace) { $events = @(Get-Content -LiteralPath $trace -Encoding UTF8 | ForEach-Object { try { $_ | ConvertFrom-Json } catch {} }) }
$approved = @($events | Where-Object { $_.event -eq 'shell-close-approved' }).Count -gt 0
@{ startedUtc=$start.ToString('o'); endedUtc=[DateTime]::UtcNow.ToString('o'); exitCode=$code; exitCodeHex=$hex;
   shellApprovedExit=$approved; lifecycleRecords=$events.Count; osVersion=[Environment]::OSVersion.Version.ToString();
   classification=$(if ($null -eq $code) { 'exit-code-unavailable-unresolved' } elseif ($code -ne 0) { 'abnormal-exit-investigate-code' } elseif (!$approved) { 'exit-without-shell-approval-unresolved' } else { 'shell-approved-exit' });
   scope='No crash dump or private documents collected. Exit code alone does not establish the cause.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'process-exit.json') -Encoding UTF8
Compress-Archive -Path (Join-Path $OutputDirectory '*') -DestinationPath ($OutputDirectory + '.zip')
Write-Host ('Saved locally: ' + $OutputDirectory + '.zip')
Read-Host 'Press Enter to finish'
