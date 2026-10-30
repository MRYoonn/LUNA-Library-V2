# SPDX-License-Identifier: MIT
# LUNA portable updater. Trusted source: approved, published GitHub Releases only.
# Never delete the install folder: the user may have local PDF files next to LUNA.exe.
param(
    [ValidateSet('Check','Apply')][string]$Mode = 'Check',
    [Parameter(Mandatory)][string]$InstallDir,
    [int]$WaitPid = 0
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = 'MRYoonn/LUNA-Library-V2'
$api = "https://api.github.com/repos/$repo/releases/latest"
$assetName = 'LUNA-Windows-Portable.zip'
$manifestName = 'LUNA-Release.json'
function Read-Json($path) {
    return (Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json)
}
function Get-Release {
    $release = Invoke-RestMethod -Uri $api -Headers @{ 'User-Agent'='LUNA-Library-Updater'; 'Accept'='application/vnd.github+json' } -TimeoutSec 18
    if ($release.draft -or $release.prerelease -or !$release.tag_name -or $release.tag_name -notmatch '^v\d+\.\d+\.\d+(?:\.\d+)?$') { throw 'No approved stable release' }
    $manifestAsset = @($release.assets | Where-Object { $_.name -ceq $manifestName })
    $zipAsset = @($release.assets | Where-Object { $_.name -ceq $assetName })
    if ($manifestAsset.Count -ne 1 -or $zipAsset.Count -ne 1) { throw 'Release assets missing or ambiguous' }
    $prefix = "https://github.com/$repo/releases/download/$($release.tag_name)/"
    foreach ($asset in @($manifestAsset[0],$zipAsset[0])) {
        if (!$asset.browser_download_url.StartsWith($prefix,[System.StringComparison]::Ordinal) -or
            $asset.size -le 0 -or $asset.size -gt 524288000) { throw 'Unsafe release asset origin or size' }
    }
    return @{ Release=$release; ManifestAsset=$manifestAsset[0]; ZipAsset=$zipAsset[0] }
}
function Get-Manifest($source) {
    $temp = Join-Path ([System.IO.Path]::GetTempPath()) ("luna-manifest-" + [Guid]::NewGuid().ToString('N') + '.json')
    try {
        Invoke-WebRequest -Uri $source.ManifestAsset.browser_download_url -OutFile $temp -TimeoutSec 30 -UseBasicParsing
        $manifest = Read-Json $temp
        if ($manifest.schema -ne 1 -or $manifest.tag -cne $source.Release.tag_name -or
            $manifest.zip -cne $assetName -or $manifest.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            $manifest.source_run -notmatch '^\d+$' -or $manifest.commit -cnotmatch '^[0-9a-f]{40}$') {
            throw 'Invalid update manifest'
        }
        return $manifest
    } finally { Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue }
}
function Validate-Archive($archive, $destination) {
    Add-Type -AssemblyName System.IO.Compression
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $names = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
        [long]$expanded = 0
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('\','/')
            if (!$name -or $name.StartsWith('/') -or $name.Contains(':') -or
                $name.Split('/') -contains '..' -or $name.Split('/') -contains '.' -or
                $name.Contains('//') -or !$names.Add($name)) { throw 'Unsafe archive path' }
            $unixType = ($entry.ExternalAttributes -shr 16) -band 0xF000
            if ($unixType -eq 0xA000) { throw 'Archive contains symlink' }
            $expanded += $entry.Length
            if ($expanded -gt 1073741824) { throw 'Archive uncompressed size too large' }
            $target = [System.IO.Path]::GetFullPath((Join-Path $destination $name))
            $prefix = [System.IO.Path]::GetFullPath($destination).TrimEnd('\') + '\'
            if (!$target.StartsWith($prefix,[System.StringComparison]::OrdinalIgnoreCase)) { throw 'Archive traversal blocked' }
        }
    } finally { $zip.Dispose() }
}
function Apply-Package($source, $manifest) {
    $install = [System.IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
    $app = Join-Path $install 'LUNA.exe'
    if (!(Test-Path -LiteralPath $app -PathType Leaf)) { throw 'LUNA executable absent in target' }
    $work = Join-Path ([System.IO.Path]::GetTempPath()) ('LUNA-Update-' + [Guid]::NewGuid().ToString('N'))
    $stage = Join-Path $work 'stage'
    $backup = Join-Path $work 'backup'
    New-Item -ItemType Directory -Path $stage, $backup -Force | Out-Null
    $replaced = [System.Collections.Generic.List[string]]::new()
    $created = [System.Collections.Generic.List[string]]::new()
    $zipFile = Join-Path $work $assetName
    try {
        Invoke-WebRequest -Uri $source.ZipAsset.browser_download_url -OutFile $zipFile -TimeoutSec 240 -UseBasicParsing
        $digest = (Get-FileHash -LiteralPath $zipFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($digest -cne $manifest.sha256) { throw 'Download SHA256 mismatch' }
        Validate-Archive $zipFile $stage
        Expand-Archive -LiteralPath $zipFile -DestinationPath $stage -Force
        $metaPath = Join-Path $stage 'build-info.json'
        if (!(Test-Path -LiteralPath (Join-Path $stage 'LUNA.exe')) -or !(Test-Path -LiteralPath $metaPath)) { throw 'Incomplete update package' }
        $metadata = Read-Json $metaPath
        if ([string]$metadata.run -cne [string]$manifest.source_run -or
            [string]$metadata.commit -cne [string]$manifest.commit -or
            [string]$metadata.validation.windowsCpp -ne 'passed' -or
            ((Get-FileHash -LiteralPath (Join-Path $stage 'LUNA.exe') -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$metadata.exeSha256)) {
            throw 'Update package identity mismatch'
        }
        if ($WaitPid -gt 0) {
            try { Wait-Process -Id $WaitPid -Timeout 300 -ErrorAction Stop }
            catch { throw 'LUNA did not exit. Update cancelled without changes.' }
        }
        if (Get-CimInstance Win32_Process -Filter "name='LUNA.exe'" | Where-Object { $_.ExecutablePath -eq $app }) {
            throw 'LUNA still running in install folder'
        }
        $files = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
        # Make backups before changing anything. The existing folder (including
        # user PDFs, thumbnail sidecars and local files) is never erased.
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($stage.Length + 1)
            $destination = Join-Path $install $relative
            if (Test-Path -LiteralPath $destination) {
                $save = Join-Path $backup $relative
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $save) | Out-Null
                Copy-Item -LiteralPath $destination -Destination $save -Force
            }
        }
        try {
            foreach ($file in $files) {
                $relative = $file.FullName.Substring($stage.Length + 1)
                $destination = Join-Path $install $relative
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
                if (Test-Path -LiteralPath $destination) { $replaced.Add($relative) }
                else { $created.Add($relative) }
                Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
            }
        } catch {
            foreach ($relative in $created) {
                Remove-Item -LiteralPath (Join-Path $install $relative) -Force -ErrorAction SilentlyContinue
            }
            foreach ($relative in $replaced) {
                Copy-Item -LiteralPath (Join-Path $backup $relative) -Destination (Join-Path $install $relative) -Force -ErrorAction SilentlyContinue
            }
            throw
        }
        Start-Process -FilePath $app -WorkingDirectory $install
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
try {
    $local = Read-Json (Join-Path $InstallDir 'build-info.json')
    $source = Get-Release
    $manifest = Get-Manifest $source
    $available = [long]$manifest.source_run -gt [long]$local.run
    if ($Mode -eq 'Check') {
        @{ status='ok'; available=$available; tag=$source.Release.tag_name; source_run=$manifest.source_run } |
          ConvertTo-Json -Compress
        exit 0
    }
    if (!$available) { exit 0 }
    Apply-Package $source $manifest
    exit 0
} catch {
    if ($Mode -eq 'Check') {
        @{ status='unavailable'; available=$false } | ConvertTo-Json -Compress
        exit 0
    }
    # On network/digest/install errors, return to the existing portable
    # rather than leaving the user with a closed application.
    $existing = Join-Path $InstallDir 'LUNA.exe'
    if (Test-Path -LiteralPath $existing -PathType Leaf) {
        try { Start-Process -FilePath $existing -WorkingDirectory $InstallDir } catch {}
    }
    Write-Error 'LUNA update was not applied; existing installation was kept or rollback was attempted.'
    exit 1
}
