# Local postmortem only. Does not upload dumps or analysis output.
param([string]$DumpFile = '', [switch]$NoPause)
$ErrorActionPreference = 'Stop'
$nativeModules = @((Join-Path $PSHOME 'Modules'), (Join-Path $env:ProgramFiles 'WindowsPowerShell/Modules'))
$env:PSModulePath = (($nativeModules + @($env:PSModulePath -split ';')) | Select-Object -Unique) -join ';'
. (Join-Path $PSScriptRoot 'Luna-Crash-Tools.ps1')
if (!$DumpFile) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    $latest = Get-ChildItem -LiteralPath $desktop -Directory -Filter 'LUNA-Diagnostics-*-PRIVATE-CRASH' | ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -File -Filter 'LUNA-private.dmp' } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (!$latest) { throw 'No local LUNA dump found. Run Start-Luna-Crash-Diagnostics.cmd first.' }
    $DumpFile = $latest.FullName
}
$dump = Get-Item -LiteralPath $DumpFile
$cdb = Get-LunaCdb
$log = Join-Path $dump.DirectoryName 'analysis-PRIVATE.txt'
$symbols = Join-Path (Get-LunaCrashToolCache) 'Symbols'
$commands = '.symfix "' + $symbols + '"; !analyze -v; .exr -1; .ecxr; kv; lm; q'
Write-Host 'Analyzing locally. Microsoft symbols may be downloaded; no dump or PDF data is uploaded.'
& $cdb -z $dump.FullName -logo $log -c $commands
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $log)) { throw 'Debugger analysis failed; private files were preserved.' }
$summary = @('P0 remains unresolved until source diagnosis and actual-PC recheck.', 'Review this text before sharing. Full dump/analysis remain private.')
$stack = $false
foreach ($line in Get-Content -LiteralPath $log) {
    if ($line -match '^\s*(EXCEPTION_CODE_STR|MODULE_NAME|IMAGE_NAME|SYMBOL_NAME|FAILURE_BUCKET_ID):\s*(\S+)') { $summary += ($matches[1]+': '+$matches[2]) }
    if ($line -match '^STACK_TEXT:') { $stack = $true; continue }
    if ($stack -and $line -match '^\s*$') { $stack = $false }
    if ($stack -and $line -match '\b([A-Za-z0-9_.-]+![^\s]+)') { $summary += $matches[1] }
}
$output = Join-Path $dump.DirectoryName 'stack-summary.txt'
$summary | Set-Content -LiteralPath $output -Encoding UTF8
Write-Host ('Review and share only if appropriate: ' + $output)
Write-Host 'Never publish the .dmp or analysis-PRIVATE.txt to GitHub.'
if (!$NoPause) { Read-Host 'Press Enter to finish' }
