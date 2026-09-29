# Asks a running MixLauncher to exit so the build can overwrite its exe. Works for an elevated
# instance too (it lets WM_APP_EXIT through UIPI), without starting the exe (which would ask UAC).
param([string]$name = "mixlauncher")
Add-Type -Namespace ML -Name Win -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindowExW(System.IntPtr parent, System.IntPtr after, string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint msg, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
'@
$h = [IntPtr]::Zero
while (($h = [ML.Win]::FindWindowExW([IntPtr]::Zero, $h, 'MixLauncherWindow', [IntPtr]::Zero)) -ne [IntPtr]::Zero) {
    $procId = 0
    [ML.Win]::GetWindowThreadProcessId($h, [ref]$procId) | Out-Null
    $p = Get-Process -Id $procId -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -eq $name) {
        [ML.Win]::PostMessageW($h, 0x8007, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_APP_EXIT
        for ($i = 0; $i -lt 40 -and -not $p.HasExited; $i++) { Start-Sleep -Milliseconds 50; $p.Refresh() }
    }
}
