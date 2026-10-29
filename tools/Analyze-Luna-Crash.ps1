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
$env:_NT_SYMBOL_PATH = 'srv*' + $symbols + '*https://msdl.microsoft.com/download/symbols'
$commands = '.echo LUNA_RAW_FAULT_BEGIN;.exr -1;.ecxr;ln @rip;lmva @rip;kv;.echo LUNA_RAW_FAULT_END;!analyze -v;lm;q'
Write-Host 'Analyzing locally. Microsoft symbols may be downloaded; no dump or PDF data is uploaded.'
& $cdb -z $dump.FullName -logo $log -c $commands
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $log)) { throw 'Debugger analysis failed; private files were preserved.' }
$summary = @('P0 remains unresolved until source diagnosis and actual-PC recheck.', 'Review this text before sharing. Full dump/analysis remain private.')
$raw = $false; $moduleRow = $false; $faultAddress = $null; $faultBase = $null
foreach ($line in Get-Content -LiteralPath $log) {
    if ($line.Trim() -eq 'LUNA_RAW_FAULT_BEGIN') { $raw = $true; continue }
    if ($line.Trim() -eq 'LUNA_RAW_FAULT_END') { $raw = $false; continue }
    if (!$raw) { continue }
    if ($line -match '^\s*ExceptionCode:\s*([0-9a-fA-F]{8})') { $summary += ('EXCEPTION_CODE: '+$matches[1]) }
    if ($line -match '^\s*ExceptionAddress:\s*([0-9a-fA-F`]+)') { $faultAddress = [Convert]::ToUInt64($matches[1].Replace('`',''),16) }
    if ($line -match '^\s*start\s+end\s+module name') { $moduleRow = $true; continue }
    if ($moduleRow -and $line -match '^\s*([0-9a-fA-F`]+)\s+[0-9a-fA-F`]+\s+([A-Za-z0-9_.-]+)\s+') { $faultBase=[Convert]::ToUInt64($matches[1].Replace('`',''),16); $summary += ('MODULE_NAME: '+$matches[2]); $moduleRow = $false }
    if ($line -match '^\s*Image name:\s*([^\s]+)') { $summary += ('IMAGE_NAME: '+[IO.Path]::GetFileName($matches[1])) }
    if ($line -match '\b([A-Za-z0-9_.-]+![^\s\)]+)') { $summary += $matches[1] }
}
if ($null -ne $faultAddress -and $null -ne $faultBase -and $faultAddress -ge $faultBase) { $summary += ('FAULT_MODULE_OFFSET: 0x{0:X}' -f ($faultAddress-$faultBase)) }
$output = Join-Path $dump.DirectoryName 'stack-summary.txt'
$summary | Set-Content -LiteralPath $output -Encoding UTF8
Write-Host ('Review and share only if appropriate: ' + $output)
Write-Host 'Never publish the .dmp or analysis-PRIVATE.txt to GitHub.'
if (!$NoPause) { Read-Host 'Press Enter to finish' }
