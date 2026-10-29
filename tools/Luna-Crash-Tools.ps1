# Microsoft-signed local debugger tools. No uploads or document access.
function Assert-LunaMicrosoftTool([string]$Path) {
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw 'Microsoft Authenticode verification failed.' }
}
function Get-LunaCrashToolCache {
    $path = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'LUNA/DiagnosticTools'
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
}
function Get-LunaProcDump {
    $cache = Get-LunaCrashToolCache
    $directory = Join-Path $cache 'ProcDump'
    $exe = Join-Path $directory 'procdump64.exe'
    if (!(Test-Path -LiteralPath $exe)) {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $zip = Join-Path $cache 'Procdump.zip'
        Write-Host 'Downloading Microsoft Sysinternals ProcDump. No administrator rights are needed for our own LUNA process.'
        Invoke-WebRequest -UseBasicParsing -Uri 'https://download.sysinternals.com/files/Procdump.zip' -OutFile $zip
        Expand-Archive -LiteralPath $zip -DestinationPath $directory -Force
    }
    Assert-LunaMicrosoftTool $exe
    return $exe
}
function Get-LunaCdb {
    $exe = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Debuggers/x64/cdb.exe'
    if (!(Test-Path -LiteralPath $exe)) {
        $cache = Get-LunaCrashToolCache
        $installer = Join-Path $cache 'winsdksetup.exe'
        Write-Host 'Installing Microsoft Debugging Tools only. Windows may request UAC approval for the SDK component.'
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri 'https://go.microsoft.com/fwlink/?linkid=2120843' -OutFile $installer
        Assert-LunaMicrosoftTool $installer
        $options = @{ FilePath=$installer; ArgumentList=@('/quiet','/norestart','/ceip','off','/features','OptionId.WindowsDesktopDebuggers'); Wait=$true; PassThru=$true }
        $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
        if (!$admin) { $options.Verb = 'RunAs' }
        $installed = Start-Process @options
        if ($installed.ExitCode -notin @(0,3010) -or !(Test-Path -LiteralPath $exe)) { throw 'Microsoft Debugging Tools installation did not finish successfully.' }
    }
    Assert-LunaMicrosoftTool $exe
    return $exe
}
