param([Parameter(Mandatory=$true)][string]$Package)
$ErrorActionPreference = 'Stop'
$packagePath = (Resolve-Path $Package).Path
$executable = Join-Path $packagePath 'LUNA.exe'
$expected = 'be8607678179d815891a0fa535848a3a2717b7a0891753e8aa0486ac8f54fe90'
if ((Get-FileHash $executable -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Unexpected executable: this probe targets the verified reader build only'
}
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class LunaPrintProbe {
    private delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern IntPtr GetDlgItem(IntPtr window, int item);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] private static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern bool PostMessage(IntPtr window, uint message, IntPtr word, IntPtr parameter);
    public static IntPtr FindPrint(uint process) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((window, unused) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner != process || !IsWindowVisible(window)) return true;
            var name = new StringBuilder(32); var title = new StringBuilder(128);
            GetClassName(window, name, name.Capacity); GetWindowText(window, title, title.Capacity);
            // English hosted Windows image; match only our native Print dialog.
            if (name.ToString() != "#32770" || title.ToString() != "Print") return true;
            IntPtr cancel = GetDlgItem(window, 2);
            if (cancel == IntPtr.Zero || !IsWindowEnabled(cancel)) return true;
            result = window; return false;
        }, IntPtr.Zero);
        return result;
    }
    public static bool Cancel(IntPtr window) {
        return PostMessage(window, 0x111, new IntPtr(2), GetDlgItem(window, 2));
    }
    public static void PrintShortcut() {
        keybd_event(0x11, 0, 0, UIntPtr.Zero);
        keybd_event(0x50, 0, 0, UIntPtr.Zero);
        keybd_event(0x50, 0, 2, UIntPtr.Zero);
        keybd_event(0x11, 0, 2, UIntPtr.Zero);
    }
}
'@
$temporary = Join-Path $env:RUNNER_TEMP ('luna-print-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $temporary | Out-Null
$pdf = Join-Path $temporary 'synthetic-print.pdf'
$process = $null
try {
    python -B -c "import sys; from pathlib import Path; from tools.smoke_windows import synthetic_pdf; Path(sys.argv[1]).write_bytes(synthetic_pdf(2))" $pdf
    if ($LASTEXITCODE -ne 0) { throw 'Synthetic PDF generation failed' }
    $original = (Get-FileHash $pdf -Algorithm SHA256).Hash
    $info = [System.Diagnostics.ProcessStartInfo]::new($executable)
    $info.UseShellExecute = $false
    $info.WorkingDirectory = $temporary
    $info.ArgumentList.Add($pdf)
    $info.Environment['PATH'] = "$packagePath;$env:SystemRoot\System32;$env:SystemRoot"
    $info.Environment['QT_PLUGIN_PATH'] = $packagePath
    $info.Environment['QT_QPA_PLATFORM_PLUGIN_PATH'] = Join-Path $packagePath 'platforms'
    $info.Environment['QT_QPA_PLATFORM'] = 'windows'
    $process = [System.Diagnostics.Process]::Start($info)
    if (-not $process.WaitForInputIdle(15000)) { throw 'LUNA did not initialize its GUI' }
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw 'LUNA exited before opening the PDF' }
        if ($process.MainWindowTitle.Contains('synthetic-print.pdf')) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $process.MainWindowTitle.Contains('synthetic-print.pdf')) { throw 'Synthetic PDF did not open' }
    $window = $process.MainWindowHandle
    [void][LunaPrintProbe]::ShowWindow($window, 9)
    [void][LunaPrintProbe]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 200
    if ([LunaPrintProbe]::GetForegroundWindow() -ne $window) { throw 'Own LUNA window is not foreground; no keys sent' }
    [LunaPrintProbe]::PrintShortcut()
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $print = [IntPtr]::Zero
    do {
        $print = [LunaPrintProbe]::FindPrint([uint32]$process.Id)
        if ($print -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($print -eq [IntPtr]::Zero) { throw 'Ctrl+P did not open the native Windows Print dialog' }
    if (-not [LunaPrintProbe]::Cancel($print)) { throw 'Native Cancel command failed' }
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
        $closed = [LunaPrintProbe]::FindPrint([uint32]$process.Id) -eq [IntPtr]::Zero
    } while (-not $closed -and [DateTime]::UtcNow -lt $deadline)
    if (-not $closed -or -not [LunaPrintProbe]::IsWindowEnabled($window)) { throw 'Print dialog did not close after Cancel' }
    if ((Get-FileHash $pdf -Algorithm SHA256).Hash -ne $original) { throw 'Print probe changed the synthetic PDF' }
    Write-Host '[PASS] Verified portable EXE opened synthetic PDF; foreground Ctrl+P opened own native Windows Print dialog; Cancel closed it; original PDF unchanged. No print job sent.'
} finally {
    if ($process -and -not $process.HasExited) {
        $print = [LunaPrintProbe]::FindPrint([uint32]$process.Id)
        if ($print -ne [IntPtr]::Zero) { [void][LunaPrintProbe]::Cancel($print) }
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
    }
    Remove-Item -Recurse -Force $temporary
}
