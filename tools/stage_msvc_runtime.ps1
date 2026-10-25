param([Parameter(Mandatory = $true)][string]$Destination)
$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio locator absent' }
$vs = (& $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio 2022 C++ installation absent' }
$redistRoot = Join-Path $vs.Trim() 'VC\Redist\MSVC'
$required = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
# VS images contain both numeric directories and a v143 toolset alias.
# Prefer the alias when present; validate the actual files instead of casting names.
$crt = Get-ChildItem -LiteralPath $redistRoot -Directory |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
    Where-Object {
        $candidate = $_
        (Test-Path -LiteralPath $candidate) -and
        (@($required | Where-Object { !(Test-Path -LiteralPath (Join-Path $candidate $_)) }).Count -eq 0)
    } | Select-Object -First 1
if (!$crt) { throw 'Complete Microsoft x64 app-local CRT absent' }
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Copy-Item -Path (Join-Path $crt '*.dll') -Destination $Destination
Write-Host 'Staged official Visual Studio 2022 x64 app-local CRT DLLs.'
