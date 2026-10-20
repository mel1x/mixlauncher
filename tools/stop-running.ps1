# Asks a running MixLauncher to exit so the build can overwrite its exe. Works for an elevated
# instance too (it lets WM_APP_EXIT through UIPI), without starting the exe (which would ask UAC).
# Only the exe in build\ is closed: an installed MixLauncher keeps running.
param([string]$name = "mixlauncher")
Add-Type -Namespace ML -Name Win -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindowExW(System.IntPtr parent, System.IntPtr after, string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint msg, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, uint pid);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr h);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern bool QueryFullProcessImageNameW(System.IntPtr h, uint flags, System.Text.StringBuilder path, ref uint size);
'@
$exe = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\build\$name.exe"))
function ImagePath([uint32]$procId) {
    $ph = [ML.Win]::OpenProcess(0x1000, $false, $procId)   # PROCESS_QUERY_LIMITED_INFORMATION: works on elevated ones
    if ($ph -eq [IntPtr]::Zero) { return $null }
    $sb = New-Object System.Text.StringBuilder 1024
    $size = [uint32]1024
    $ok = [ML.Win]::QueryFullProcessImageNameW($ph, 0, $sb, [ref]$size)
    [ML.Win]::CloseHandle($ph) | Out-Null
    if ($ok) { return $sb.ToString() } else { return $null }
}
$h = [IntPtr]::Zero
while (($h = [ML.Win]::FindWindowExW([IntPtr]::Zero, $h, 'MixLauncherWindow', [IntPtr]::Zero)) -ne [IntPtr]::Zero) {
    $procId = 0
    [ML.Win]::GetWindowThreadProcessId($h, [ref]$procId) | Out-Null
    $p = Get-Process -Id $procId -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -eq $name -and (ImagePath $procId) -eq $exe) {
        [ML.Win]::PostMessageW($h, 0x8007, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_APP_EXIT
        for ($i = 0; $i -lt 40 -and -not $p.HasExited; $i++) { Start-Sleep -Milliseconds 50; $p.Refresh() }
    }
}
