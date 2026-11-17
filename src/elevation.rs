// Elevation: starting things at the user's level from our elevated process, and autostart.

use crate::*;
use core::mem::ManuallyDrop;
use windows::Win32::System::Com as wcom;
use windows::Win32::System::Variant as wvar;
use windows::Win32::UI::Shell as wsh;
use windows::core::{BSTR, Interface};

pub static mut g_elevated: bool = false;

unsafe fn process_is_elevated() -> bool {
    let mut tok: HANDLE = null_mut();
    let mut e: TOKEN_ELEVATION = zeroed();
    let mut sz = 0u32;
    if OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut tok) == 0 {
        return false;
    }
    let r = GetTokenInformation(tok, TokenElevation, &mut e as *mut _ as *mut c_void, size_of::<TOKEN_ELEVATION>() as u32, &mut sz) != 0 && e.TokenIsElevated != 0;
    CloseHandle(tok);
    r
}

pub unsafe fn uipi_allow(h: HWND, msg: u32) {
    if !h.is_null() && msg != 0 {
        ChangeWindowMessageFilterEx(h, msg, MSGFLT_ALLOW, null_mut());
    }
}

fn variant_i4(v: i32) -> wvar::VARIANT {
    let mut r = wvar::VARIANT::default();
    unsafe {
        (*r.Anonymous.Anonymous).vt = wvar::VT_I4;
        (*r.Anonymous.Anonymous).Anonymous.lVal = v;
    }
    r
}

// A BSTR variant, or an empty one for no text (the shell then uses its default).
unsafe fn variant_str(s: *const WCHAR) -> wvar::VARIANT {
    let mut r = wvar::VARIANT::default();
    if !s.is_null() && *s != 0 {
        (*r.Anonymous.Anonymous).vt = wvar::VT_BSTR;
        (*r.Anonymous.Anonymous).Anonymous.bstrVal = ManuallyDrop::new(BSTR::from_wide(core::slice::from_raw_parts(s, wlen(s) as usize)));
    }
    r
}

const SID_STopLevelBrowser: windows::core::GUID = windows::core::GUID::from_u128(0x4c96be40_915c_11cf_99d3_00aa004ae837);

// ShellExecute through Explorer's own Shell.Application, so the program starts at the user's (not our
// elevated) level.
pub unsafe fn shell_exec_unelevated_ex(file: *const WCHAR, args: *const WCHAR, dir: *const WCHAR, verb: *const WCHAR, show: i32) -> bool {
    let r = (|| -> windows::core::Result<bool> {
        let sw: wsh::IShellWindows = wcom::CoCreateInstance(&wsh::ShellWindows, None, wcom::CLSCTX_LOCAL_SERVER)?;
        let loc = variant_i4(0 /*CSIDL_DESKTOP*/);
        let root = wvar::VARIANT::default();
        let mut hwnd = 0i32;
        let desk = sw.FindWindowSW(&loc, &root, wsh::SWC_DESKTOP, &mut hwnd, wsh::SWFO_NEEDDISPATCH)?;
        let sp: wcom::IServiceProvider = desk.cast()?;
        let sb: wsh::IShellBrowser = sp.QueryService(&SID_STopLevelBrowser)?;
        let sv = sb.QueryActiveShellView()?;
        let bg: wcom::IDispatch = sv.GetItemObject(wsh::SVGIO_BACKGROUND)?;
        let fvd: wsh::IShellFolderViewDual = bg.cast()?;
        let app = fvd.Application()?;
        let sd: wsh::IShellDispatch2 = app.cast()?;
        let mut va = variant_str(args);
        let mut vd = variant_str(dir);
        let mut vo = variant_str(verb);
        let vs = variant_i4(show);
        let bfile = BSTR::from_wide(core::slice::from_raw_parts(file, wlen(file) as usize));
        let ok = sd.ShellExecute(&bfile, &va, &vd, &vo, &vs).is_ok();
        let _ = wvar::VariantClear(&mut va);
        let _ = wvar::VariantClear(&mut vd);
        let _ = wvar::VariantClear(&mut vo);
        Ok(ok)
    })();
    r.unwrap_or(false)
}

pub unsafe fn shell_exec_unelevated(file: *const WCHAR, args: *const WCHAR, dir: *const WCHAR, verb: *const WCHAR) -> bool {
    shell_exec_unelevated_ex(file, args, dir, verb, SW_SHOWNORMAL)
}

// Runs a console tool hidden and waits for it. True when it finished in time.
pub unsafe fn run_tool(exe: &str, args: &str, wait_ms: u32, exit_code: *mut u32) -> bool {
    let mut cmd = wstr(&format!("\"{}\" {}", exe, args));
    let mut si: STARTUPINFOW = zeroed();
    si.cb = size_of::<STARTUPINFOW>() as u32;
    let mut pi: PROCESS_INFORMATION = zeroed();
    if CreateProcessW(null(), cmd.as_mut_ptr(), null(), null(), FALSE, CREATE_NO_WINDOW, null(), null(), &si, &mut pi) == 0 {
        return false;
    }
    let done = WaitForSingleObject(pi.hProcess, wait_ms) == WAIT_OBJECT_0;
    let mut code = 1u32;
    GetExitCodeProcess(pi.hProcess, &mut code);
    if !exit_code.is_null() {
        *exit_code = code;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    done
}

const AUTOSTART_TASK: &str = "MixLauncher";
const OLD_HELPER_TASK: &str = "MixLauncher Admin Hook";
const k_run_key: &str = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// Autostart: elevated, a logon task in Task Scheduler (no UAC prompt at sign-in); otherwise the Run key.
unsafe fn task_exists(name: &str) -> bool {
    let mut sys = [0u16; MAX_PATH];
    if GetSystemDirectoryW(sys.as_mut_ptr(), MAX_PATH as u32) == 0 {
        return false;
    }
    let path = wstr(&format!("{}\\Tasks\\{}", Ws(sys.as_ptr()), name));
    GetFileAttributesW(path.as_ptr()) != INVALID_FILE_ATTRIBUTES
}

unsafe fn task_delete(name: &str) {
    run_tool("schtasks.exe", &format!("/delete /tn \"{}\" /f", name), 10000, null_mut());
}

fn xml_escaped(s: &str) -> String {
    let mut o = String::new();
    for c in s.chars() {
        match c {
            '&' => o.push_str("&amp;"),
            '<' => o.push_str("&lt;"),
            '>' => o.push_str("&gt;"),
            '"' => o.push_str("&quot;"),
            _ => o.push(c),
        }
    }
    o
}

unsafe fn task_create_autostart() -> bool {
    let mut dom = [0u16; 128];
    let mut name = [0u16; 128];
    if GetEnvironmentVariableW(w!("USERDOMAIN"), dom.as_mut_ptr(), 128) == 0 {
        dom[0] = 0;
    }
    if GetEnvironmentVariableW(w!("USERNAME"), name.as_mut_ptr(), 128) == 0 {
        return false;
    }
    let user = if dom[0] != 0 { format!("{}\\{}", Ws(dom.as_ptr()), Ws(name.as_ptr())) } else { format!("{}", Ws(name.as_ptr())) };
    let user = xml_escaped(&user);
    let exe = xml_escaped(&wto_string(g_exe_path.as_ptr()));
    let xml = format!(
        "\u{FEFF}<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n\
<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n\
  <RegistrationInfo><Description>MixLauncher</Description></RegistrationInfo>\r\n\
  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>{user}</UserId></LogonTrigger></Triggers>\r\n\
  <Principals><Principal id=\"Author\"><UserId>{user}</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n\
  <Settings>\r\n\
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n\
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n\
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n\
    <AllowHardTerminate>true</AllowHardTerminate>\r\n\
    <StartWhenAvailable>false</StartWhenAvailable>\r\n\
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\r\n\
    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>\r\n\
    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n\
    <Enabled>true</Enabled>\r\n\
    <Hidden>false</Hidden>\r\n\
    <RunOnlyIfIdle>false</RunOnlyIfIdle>\r\n\
    <WakeToRun>false</WakeToRun>\r\n\
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n\
    <Priority>4</Priority>\r\n\
  </Settings>\r\n\
  <Actions Context=\"Author\"><Exec><Command>{exe}</Command></Exec></Actions>\r\n\
</Task>\r\n"
    );
    let data: Vec<u16> = xml.encode_utf16().collect();
    let mut path = [0u16; MAX_PATH];
    data_path(path.as_mut_ptr(), w!("autostart-task.xml"));
    if !write_file_atomic(path.as_ptr(), data.as_ptr() as *const c_void, (data.len() * 2) as u32) {
        return false;
    }
    let mut code = 1u32;
    let ok = run_tool("schtasks.exe", &format!("/create /tn \"{}\" /xml \"{}\" /f", AUTOSTART_TASK, Ws(path.as_ptr())), 10000, &mut code) && code == 0;
    DeleteFileW(path.as_ptr());
    if !ok {
        log_msg!("autostart task creation failed (exit {})", code);
    }
    ok
}

unsafe fn run_key_present() -> bool {
    let mut val = [0u16; MAX_PATH + 8];
    let mut sz = (val.len() * 2) as u32;
    let key = wstr(k_run_key);
    RegGetValueW(HKEY_CURRENT_USER, key.as_ptr(), w!("MixLauncher"), RRF_RT_REG_SZ, null_mut(), val.as_mut_ptr() as *mut c_void, &mut sz) == ERROR_SUCCESS
}

pub unsafe fn run_key_set(on: bool) {
    let mut k: HKEY = null_mut();
    let key = wstr(k_run_key);
    if RegOpenKeyExW(HKEY_CURRENT_USER, key.as_ptr(), 0, KEY_SET_VALUE, &mut k) != ERROR_SUCCESS {
        return;
    }
    if on {
        let val = wstr(&format!("\"{}\"", Ws(g_exe_path.as_ptr())));
        RegSetValueExW(k, w!("MixLauncher"), 0, REG_SZ, val.as_ptr() as *const u8, (val.len() * 2) as u32);
    } else {
        RegDeleteValueW(k, w!("MixLauncher"));
    }
    RegCloseKey(k);
}

pub unsafe fn autostart_get() -> bool {
    if g_elevated { task_exists(AUTOSTART_TASK) } else { run_key_present() }
}

pub unsafe fn autostart_set(on: bool) -> bool {
    if !g_elevated {
        run_key_set(on);
        return true;
    }
    if on {
        return task_create_autostart();
    }
    task_delete(AUTOSTART_TASK);
    !task_exists(AUTOSTART_TASK)
}

// An elevated start cleans up after older versions: their helper task, and a Run key autostart that now
// becomes a task (the Run key cannot start an elevated exe without a UAC prompt).
unsafe extern "system" fn elevation_housekeeping(_param: *mut c_void) -> u32 {
    if !g_elevated {
        return 0;
    }
    if task_exists(OLD_HELPER_TASK) {
        task_delete(OLD_HELPER_TASK);
    }
    if run_key_present() && task_create_autostart() {
        run_key_set(false);
    }
    0
}

pub unsafe fn elevation_init() {
    g_elevated = process_is_elevated();
}

pub unsafe fn elevation_housekeeping_start() {
    thread_start(elevation_housekeeping, null_mut());
}
