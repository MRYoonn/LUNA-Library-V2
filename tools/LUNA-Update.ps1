# SPDX-License-Identifier: MIT
# LUNA portable update helper. Stages and verifies while LUNA is running;
# replaces ONLY signed-off package file types after every LUNA process exits.
# User PDFs, thumbnails, citation sidecars, QSettings and unknown files are never
# deleted or overwritten. SHA-256 is integrity checking, not publisher signing.
param(
    [ValidateSet('Check','Stage','Apply','Cancel')][string]$Mode = 'Check',
    [Parameter(Mandatory)][string]$InstallDir,
    [int]$WaitPid = 0,
    [string]$Token = ''
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = 'MRYoonn/LUNA-Library-V2'
$api = "https://api.github.com/repos/$repo/releases/latest"
$assetName = 'LUNA-Windows-Portable.zip'
$manifestName = 'LUNA-Release.json'
$install = [System.IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$headers = @{ 'User-Agent'='LUNA-Library-Updater'; 'Accept'='application/vnd.github+json' }

function Read-Json([string]$path) {
    Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
}
function Version-Parts([string]$tag) {
    if ($tag -cnotmatch '^v\d+\.\d+\.\d+(?:\.\d+)?$') { throw 'Invalid semantic version' }
    $parts = @($tag.Substring(1).Split('.') | ForEach-Object { [long]::Parse($_) })
    while ($parts.Count -lt 4) { $parts += [long]0 }
    return ,$parts
}
function Is-Newer([string]$candidate, [string]$current) {
    $next = Version-Parts $candidate
    $old = Version-Parts $current
    for ($i=0; $i -lt 4; $i++) {
        if ($next[$i] -gt $old[$i]) { return $true }
        if ($next[$i] -lt $old[$i]) { return $false }
    }
    return $false
}
function Get-Release {
    $release = Invoke-RestMethod -Uri $api -Headers $headers -TimeoutSec 18
    [void](Version-Parts ([string]$release.tag_name))
    if ($release.draft -or $release.prerelease) { throw 'No approved stable release' }
    $manifest = @($release.assets | Where-Object { $_.name -ceq $manifestName })
    $zip = @($release.assets | Where-Object { $_.name -ceq $assetName })
    if ($manifest.Count -ne 1 -or $zip.Count -ne 1) { throw 'Release assets missing or ambiguous' }
    $prefix = "https://github.com/$repo/releases/download/$($release.tag_name)/"
    foreach ($asset in @($manifest[0],$zip[0])) {
        if (!$asset.browser_download_url.StartsWith($prefix,[StringComparison]::Ordinal) -or
            $asset.size -le 0 -or $asset.size -gt 524288000) { throw 'Unsafe release asset' }
    }
    return @{ Release=$release; ManifestAsset=$manifest[0]; ZipAsset=$zip[0] }
}
function Validate-Relative([string]$name) {
    if (!$name -or $name -match '^[\\/]' -or $name.Contains(':') -or
        $name.Contains('//') -or $name.Contains('\') -or
        $name.Split('/') -contains '..' -or $name.Split('/') -contains '.' -or
        $name -match '(^|/)\.' -or $name -match '[<>|?*"\\]' -or
        $name.EndsWith(' ') -or $name.EndsWith('.')) { throw 'Unsafe package path' }
    # Explicit managed executable / runtime / documentation surface.
    # No PDF, image, database, user annotations or hidden files may enter.
    if ($name -ceq 'LUNA.exe') { return }
    if ($name -match '(?i)\.(exe|pdf|png|jpe?g|webp|db|sqlite3?|log|key|pem|pfx|p12|token|svg|ico)$') {
        throw 'Package attempts to replace user data or an unapproved executable'
    }
    if ($name -notmatch '(?i)\.(dll|qm|txt|md|json|ps1|cmd|dat|pak|ini|conf)$') {
        throw 'Unrecognized program payload type'
    }
    if ($name -match '(?i)\.json$' -and $name -cne 'build-info.json') {
        throw 'Unrecognized program metadata'
    }
    if ($name -match '(?i)\.(ps1|cmd)$' -and
        $name -notmatch '^(LUNA-Update\.ps1|(?:Start-Luna-(?:Diagnostics|Crash-Diagnostics)|Analyze-Luna-Crash)\.(?:ps1|cmd))$') {
        throw 'Unapproved helper script'
    }
}
function Get-Manifest($source, [string]$dest) {
    Invoke-WebRequest -Uri $source.ManifestAsset.browser_download_url -OutFile $dest -TimeoutSec 30 -UseBasicParsing
    $m = Read-Json $dest
    if ([int]$m.schema -ne 2 -or [string]$m.tag -cne [string]$source.Release.tag_name -or
        [string]$m.zip -cne $assetName -or [string]$m.sha256 -cnotmatch '^[a-f0-9]{64}$' -or
        [string]$m.commit -cnotmatch '^[0-9a-f]{40}$' -or
        [string]$m.source_run -notmatch '^\d+$' -or @($m.files).Count -lt 5) {
        throw 'Invalid release manifest'
    }
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in @($m.files)) {
        $name = [string]$file.path
        Validate-Relative $name
        if (!$seen.Add($name) -or [string]$file.sha256 -cnotmatch '^[a-f0-9]{64}$' -or
            [long]$file.size -lt 0 -or [long]$file.size -gt 524288000) {
            throw 'Duplicate or malformed release file'
        }
    }
    if (!$seen.Contains('LUNA.exe') -or !$seen.Contains('build-info.json')) {
        throw 'Required release files missing'
    }
    return $m
}
function Validate-Zip([string]$zipPath, $manifest) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $fileIndex = @{}
        foreach ($f in @($manifest.files)) { $fileIndex[[string]$f.path] = $f }
        $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        [long]$total = 0
        foreach ($entry in $zip.Entries) {
            $name = [string]$entry.FullName
            if (!$name -or $name.EndsWith('/') -or !$seen.Add($name)) { throw 'Duplicate or directory ZIP entry' }
            Validate-Relative $name
            if (!$fileIndex.ContainsKey($name)) { throw 'Unlisted ZIP file' }
            $unixType = ($entry.ExternalAttributes -shr 16) -band 0xF000
            if ($unixType -eq 0xA000) { throw 'ZIP contains symlink' }
            $total += [long]$entry.Length
            if ($total -gt 1073741824 -or $entry.Length -ne [long]$fileIndex[$name].size) {
                throw 'ZIP expansion quota or size mismatch'
            }
        }
        if ($seen.Count -ne $fileIndex.Count) { throw 'ZIP manifest file count mismatch' }
    } finally { $zip.Dispose() }
}
function Verify-Payload([string]$payload, $manifest) {
    foreach ($file in @($manifest.files)) {
        $name = [string]$file.path
        Validate-Relative $name
        $path = Join-Path $payload ($name.Replace('/', '\'))
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw 'Staged file missing' }
        if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Staged link forbidden'
        }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$file.sha256) {
            throw 'Staged file digest mismatch'
        }
    }
    $info = Read-Json (Join-Path $payload 'build-info.json')
    if ([string]$info.version -cne [string]$manifest.tag -or
        [string]$info.run -cne [string]$manifest.source_run -or
        [string]$info.commit -cne [string]$manifest.commit -or
        [string]$info.validation.windowsCpp -cne 'passed' -or
        [string]$info.exeSha256 -cne ([string](@($manifest.files | Where-Object { $_.path -ceq 'LUNA.exe' })[0].sha256))) {
        throw 'Staged release provenance mismatch'
    }
}
function Stage-Path([string]$value) {
    if ($value -cnotmatch '^[a-f0-9]{32}$') { throw 'Invalid stage token' }
    Join-Path ([IO.Path]::GetTempPath()) ("LUNA-Update-Stage-" + $value)
}
function New-Stage {
    $source = Get-Release
    $local = Read-Json (Join-Path $install 'build-info.json')
    if ([string]$local.version -match '^v\d+\.\d+\.\d+(?:\.\d+)?$') {
        $available = Is-Newer ([string]$source.Release.tag_name) ([string]$local.version)
    } else {
        # Existing pre-release CI builds had no semantic version. A release
        # must come from a newer verified Actions run, never a downgrade.
        $available = [long]$source.Release.id -ge 0 -and
                     [long]$local.run -lt 1 -or
                     [long]$local.run -lt [long](Get-Manifest $source (Join-Path ([IO.Path]::GetTempPath()) 'luna-ignored-unverified.json')).source_run
    }
    if (!$available) { return @{ status='current'; available=$false } }
    $token = [guid]::NewGuid().ToString('N')
    $work = Stage-Path $token
    New-Item -ItemType Directory -Path $work -ErrorAction Stop | Out-Null
    try {
        $manifest = Get-Manifest $source (Join-Path $work $manifestName)
        $localVersion = [string]$local.version
        if ($localVersion -match '^v\d+\.\d+\.\d+(?:\.\d+)?$') {
            if (!(Is-Newer ([string]$manifest.tag) $localVersion)) { throw 'Refusing version downgrade' }
        } elseif ([long]$manifest.source_run -le [long]$local.run) { throw 'Refusing source run downgrade' }
        $zipFile = Join-Path $work $assetName
        Invoke-WebRequest -Uri $source.ZipAsset.browser_download_url -OutFile $zipFile -TimeoutSec 240 -UseBasicParsing
        if ((Get-FileHash $zipFile -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$manifest.sha256) {
            throw 'Release ZIP hash mismatch'
        }
        Validate-Zip $zipFile $manifest
        $payload = Join-Path $work 'payload'
        New-Item -ItemType Directory $payload | Out-Null
        Expand-Archive -LiteralPath $zipFile -DestinationPath $payload
        Verify-Payload $payload $manifest
        @{ token=$token; install=$install; tag=$manifest.tag; source_run=$manifest.source_run;
           state='verified' } | ConvertTo-Json -Compress | Set-Content -LiteralPath (Join-Path $work 'state.json') -Encoding UTF8
        @{ status='staged'; token=$token; tag=$manifest.tag; available=$true } | ConvertTo-Json -Compress
    } catch {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
        throw
    }
}
function Check-Release {
    $source = Get-Release
    $local = Read-Json (Join-Path $install 'build-info.json')
    $mPath = Join-Path ([IO.Path]::GetTempPath()) ("LUNA-Manifest-" + [guid]::NewGuid().ToString('N') + ".json")
    try { $m = Get-Manifest $source $mPath }
    finally { Remove-Item -LiteralPath $mPath -Force -ErrorAction SilentlyContinue }
    if ([string]$local.version -match '^v\d+\.\d+\.\d+(?:\.\d+)?$') {
        $available = Is-Newer ([string]$m.tag) ([string]$local.version)
    } else { $available = [long]$m.source_run -gt [long]$local.run }
    @{ status='ok'; available=$available; tag=$m.tag; source_run=$m.source_run } | ConvertTo-Json -Compress
}
function Undo-Copy([string]$backup, [System.Collections.Generic.List[string]]$changed,
                   [System.Collections.Generic.HashSet[string]]$previous) {
    foreach ($name in $changed) {
        $dest = Join-Path $install ($name.Replace('/', '\'))
        if ($previous.Contains($name)) {
            $src = Join-Path $backup ($name.Replace('/', '\'))
            Copy-Item -LiteralPath $src -Destination $dest -Force
        } else {
            Remove-Item -LiteralPath $dest -Force -ErrorAction SilentlyContinue
        }
    }
}
function Apply-Stage {
    $work = Stage-Path $Token
    $state = Read-Json (Join-Path $work 'state.json')
    if ([string]$state.token -cne $Token -or [string]$state.install -cne $install -or
        [string]$state.state -cne 'verified') { throw 'Unknown or already-consumed update stage' }
    $manifest = Read-Json (Join-Path $work $manifestName)
    $zipFile = Join-Path $work $assetName
    if ([int]$manifest.schema -ne 2 -or
        (Get-FileHash -LiteralPath $zipFile -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$manifest.sha256) {
        throw 'Staged ZIP changed'
    }
    Verify-Payload (Join-Path $work 'payload') $manifest
    if ($WaitPid -gt 0) {
        $previousProcess = Get-Process -Id $WaitPid -ErrorAction SilentlyContinue
        if ($previousProcess) { $previousProcess | Wait-Process -Timeout 300 -ErrorAction Stop }
    }
    $busy = @(Get-CimInstance Win32_Process -Filter "name='LUNA.exe'" -ErrorAction Stop |
              Where-Object { $_.ExecutablePath -ieq (Join-Path $install 'LUNA.exe') })
    if ($busy.Count) { throw 'LUNA still running; files untouched' }
    $mutexHash = [Convert]::ToBase64String([Security.Cryptography.SHA256]::Create().ComputeHash(
        [Text.Encoding]::UTF8.GetBytes($install.ToLowerInvariant()))).Replace('/','_').Replace('+','-').TrimEnd('=')
    $mutex = New-Object Threading.Mutex($false, "Local\LunaPortableUpdate-" + $mutexHash)
    $taken = $false
    try {
        $taken = $mutex.WaitOne(0)
        if (!$taken) { throw 'Concurrent updater already running' }
        # Reserve the complete rollback before mutating a single file.
        $backup = Join-Path $work 'backup'
        New-Item -ItemType Directory -Path $backup | Out-Null
        $prior = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($file in @($manifest.files)) {
            $name = [string]$file.path; Validate-Relative $name
            $dst = Join-Path $install ($name.Replace('/', '\'))
            if (Test-Path -LiteralPath $dst) {
                if (!(Test-Path -LiteralPath $dst -PathType Leaf) -or
                    ((Get-Item -LiteralPath $dst).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                    throw 'Existing target is not an ordinary managed file'
                }
                $prior.Add($name) | Out-Null
                $saved = Join-Path $backup ($name.Replace('/', '\'))
                New-Item -ItemType Directory -Force -Path (Split-Path $saved) | Out-Null
                Copy-Item -LiteralPath $dst -Destination $saved -Force
            }
        }
        # The journal survives normal script exceptions, but manual recovery
        # may be required if Windows loses power during file replacement.
        @{ token=$Token; tag=$manifest.tag; state='backup-complete';
           prior=@($prior); files=@($manifest.files | ForEach-Object { $_.path }) } |
            ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $work 'transaction.json') -Encoding UTF8
        $changed = New-Object 'System.Collections.Generic.List[string]'
        try {
            foreach ($file in @($manifest.files)) {
                $name = [string]$file.path
                $dst = Join-Path $install ($name.Replace('/', '\'))
                New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
                $changed.Add($name)
                Copy-Item -LiteralPath (Join-Path $work ('payload\' + $name.Replace('/', '\'))) -Destination $dst -Force
            }
            # The child inherits a single-use acknowledgement token and writes
            # ack only after the native LUNA window becomes visible.
            $env:LUNA_UPDATE_ACK_TOKEN = $Token
            $proc = Start-Process -FilePath (Join-Path $install 'LUNA.exe') -WorkingDirectory $install -PassThru
            Remove-Item Env:\LUNA_UPDATE_ACK_TOKEN -ErrorAction SilentlyContinue
            $confirmed = $false
            for ($i=0; $i -lt 60; $i++) {
                if (Test-Path (Join-Path $work 'startup.ok')) { $confirmed=$true; break }
                if ($proc.HasExited) { break }
                Start-Sleep -Seconds 1
                $proc.Refresh()
            }
            if (!$confirmed) {
                if (!$proc.HasExited) {
                    throw 'New LUNA has not confirmed startup; no rollback while it is running'
                }
                throw 'New LUNA failed startup acknowledgement'
            }
        } catch {
            if ($proc -and !$proc.HasExited) {
                # Do not replace DLLs under a running process. Retain backup
                # and journal for recovery instead of risking user documents.
                throw
            }
            Undo-Copy $backup $changed $prior
            Start-Process -FilePath (Join-Path $install 'LUNA.exe') -WorkingDirectory $install
            throw
        }
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    } finally {
        if ($taken) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
}
try {
    switch ($Mode) {
        'Check' { Check-Release }
        'Stage' { New-Stage }
        'Apply' { Apply-Stage }
        'Cancel' {
            $work = Stage-Path $Token
            if (Test-Path -LiteralPath (Join-Path $work 'state.json')) {
                $state = Read-Json (Join-Path $work 'state.json')
                if ($state.token -ceq $Token -and $state.install -ceq $install -and
                    $state.state -ceq 'verified') {
                    Remove-Item -LiteralPath $work -Recurse -Force
                }
            }
        }
    }
    exit 0
} catch {
    if ($Mode -in @('Check','Stage')) {
        @{ status='unavailable'; available=$false } | ConvertTo-Json -Compress
        exit 0
    }
    Write-Error 'LUNA update not completed; program data was not deliberately removed. Check the recovery journal if present.'
    exit 1
}
