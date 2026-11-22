# SPDX-License-Identifier: MIT
param(
    [Parameter(Mandatory)][string]$Candidate,
    [Parameter(Mandatory)][string]$Work
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$assetPrefix = 'https://github.com/MRYoonn/LUNA-Library-V2/releases/download/'
$baselineZip = Join-Path $Work 'v306.zip'
$install = Join-Path $Work 'v306'
New-Item -ItemType Directory -Path $Work | Out-Null
Microsoft.PowerShell.Utility\Invoke-WebRequest -Uri "${assetPrefix}v0.9.0.306/LUNA-Windows-Portable.zip" -OutFile $baselineZip
if ((Get-FileHash $baselineZip).Hash.ToLowerInvariant() -cne '03fd2c9c1863ce2f3a7eaca2195833af7bc56f4894d7c463d250067d24d6ad58') {
    throw 'v306 baseline ZIP checksum mismatch'
}
Expand-Archive -LiteralPath $baselineZip -DestinationPath $install
$helper = Join-Path $install 'LUNA-Update.ps1'
$manifest = Get-Content -LiteralPath (Join-Path $Candidate 'LUNA-Release.json') -Raw | ConvertFrom-Json
$target = $manifest.tag
$localInfo = Join-Path $install 'build-info.json'
if ((Get-Content -LiteralPath $localInfo -Raw | ConvertFrom-Json).version -cne 'v0.9.0.306') {
    throw 'Wrong installed baseline version'
}

# Stub only GitHub responses so the published v306 updater sees the unpublished
# candidate. Check, Stage, Apply, startup acknowledgement and hashing are real.
$releaseApi = 'https://api.github.com/repos/MRYoonn/LUNA-Library-V2/releases/latest'
$fixtureUrls = @{}
foreach ($name in @('LUNA-Release.json', 'LUNA-Windows-Portable.zip')) {
    $fixtureUrls["$assetPrefix$target/$name"] = Get-Item -LiteralPath (Join-Path $Candidate $name)
}
function Invoke-RestMethod($Uri, $Headers, $TimeoutSec) {
    if ($Uri -cne $releaseApi) { throw 'Unexpected updater API request' }
    @{
        tag_name = $target
        draft = $false
        prerelease = $false
        assets = @($fixtureUrls.GetEnumerator() | ForEach-Object {
            @{ name = $_.Value.Name; size = $_.Value.Length; browser_download_url = $_.Key }
        })
    }
}
function Invoke-WebRequest($Uri, $OutFile, $TimeoutSec, [switch]$UseBasicParsing) {
    if (!$fixtureUrls.ContainsKey($Uri)) { throw 'Unexpected updater asset request' }
    Copy-Item -LiteralPath $fixtureUrls[$Uri].FullName -Destination $OutFile
}

$sentinel = Join-Path $install 'legacy-user-settings.json'
Set-Content -LiteralPath $sentinel -Value '{"legacyFixture":true}' -Encoding UTF8
$sentinelHash = (Get-FileHash $sentinel).Hash
$token = ''
$env:LUNA_METADATA_OFFLINE = '1'
$env:LUNA_DISABLE_UPDATES = '1'
try {
    $check = & $helper -Mode Check -InstallDir $install | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or $check.status -cne 'ok' -or !$check.available -or $check.tag -cne $target) {
        throw 'v306 updater could not detect candidate'
    }
    $stage = & $helper -Mode Stage -InstallDir $install | ConvertFrom-Json
    $token = $stage.token
    if ($LASTEXITCODE -ne 0 -or $stage.status -cne 'staged' -or $token -cnotmatch '^[a-f0-9]{32}$') {
        throw 'v306 updater could not stage candidate'
    }
    $applyHelper = Join-Path $Work 'apply-v306.ps1'
    Copy-Item -LiteralPath $helper -Destination $applyHelper
    & $applyHelper -Mode Apply -InstallDir $install -Token $token
    if ($LASTEXITCODE -ne 0) { throw 'v306 updater failed to apply and launch candidate' }
    $installed = Get-Content -LiteralPath $localInfo -Raw | ConvertFrom-Json
    if ($installed.version -cne $target -or $installed.commit -cne $manifest.commit -or
        (Get-FileHash (Join-Path $install 'LUNA.exe')).Hash.ToLowerInvariant() -cne $installed.exeSha256 -or
        (Get-FileHash $sentinel).Hash -cne $sentinelHash) {
        throw 'Installed version, file hashes or legacy data preservation failed'
    }
    $current = & $helper -Mode Check -InstallDir $install | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or $current.status -cne 'ok' -or $current.available) {
        throw 'Installed candidate was incorrectly offered again'
    }
    Write-Host '[PASS] v306 updater detected, staged and applied the candidate; native startup was acknowledged and the legacy file was preserved'
} finally {
    $exe = Join-Path $install 'LUNA.exe'
    Get-CimInstance Win32_Process -Filter "name='LUNA.exe'" |
        Where-Object { $_.ExecutablePath -ieq $exe } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
}
