// Launching runs on its own STA thread: the shell may take its time, and the launcher must not wait for it.

use crate::*;

pub const ACT_OPEN: i32 = 0;
pub const ACT_REVEAL: i32 = 1;
pub const ACT_RUNAS: i32 = 2;
pub const ACT_PROPERTIES: i32 = 3;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LaunchJob {
    pub act: i32,
    pub is_app: bool,
    pub uri: bool, // target goes to the shell as is: ms-settings:, shell:Downloads, explorer.exe
    pub cmd: u8,
    pub target: *mut WCHAR,
    pub path: *mut WCHAR,
    pub dir: *mut WCHAR,
    pub hist_type: u8, // a file that turns out to be gone is dropped from the history
    pub hist_key: *mut WCHAR,
}

#[repr(C)]
struct LaunchQueue {
    lock: SRWLOCK,
    event: HANDLE,
    jobs: *mut LaunchJob,
    count: i32,
    cap: i32,
}

static mut LQ: LaunchQueue = unsafe { zeroed() };

unsafe fn job_free(j: &LaunchJob) {
    free(j.target as *mut c_void);
    free(j.path as *mut c_void);
    free(j.dir as *mut c_void);
    free(j.hist_key as *mut c_void);
}

unsafe fn enable_shutdown_privilege() {
    let mut tok: HANDLE = null_mut();
    if OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &mut tok) == 0 {
        return;
    }
    let mut tp: TOKEN_PRIVILEGES = zeroed();
    tp.PrivilegeCount = 1;
    if LookupPrivilegeValueW(null(), SE_SHUTDOWN_NAME, &mut tp.Privileges[0].Luid) != 0 {
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, null_mut(), null_mut());
    }
    CloseHandle(tok);
}

pub unsafe fn shell_run(verb: *const WCHAR, file: *const WCHAR, args: *const WCHAR, dir: *const WCHAR, mask: u32, err: *mut u32) -> bool {
    let mut sei: SHELLEXECUTEINFOW = zeroed();
    sei.cbSize = size_of::<SHELLEXECUTEINFOW>() as u32;
    sei.fMask = SEE_MASK_NOASYNC | mask;
    sei.lpVerb = verb;
    sei.lpFile = file;
    sei.lpParameters = args;
    sei.lpDirectory = if !dir.is_null() && *dir != 0 { dir } else { null() };
    sei.nShow = SW_SHOWNORMAL;
    let ok = ShellExecuteExW(&mut sei);
    if !err.is_null() {
        *err = if ok != 0 { 0 } else { GetLastError() };
    }
    ok != 0
}

pub unsafe fn shell_exec(verb: *const WCHAR, file: *const WCHAR, dir: *const WCHAR, err: *mut u32) -> bool {
    shell_run(verb, file, null(), dir, SEE_MASK_FLAG_LOG_USAGE, err)
}

// Through Explorer while we are elevated, so it runs at the user's level; directly otherwise or when that fails.
pub unsafe fn open_as_user(target: *const WCHAR, dir: *const WCHAR, err: *mut u32) {
    if !g_elevated || !shell_exec_unelevated(target, null(), dir, null()) {
        shell_exec(null(), target, dir, err);
    }
}

// An app by its AUMID; its target file when that fails (unless the user cancelled, a UAC prompt for one).
unsafe fn app_exec(verb: *const WCHAR, app_uri: *const WCHAR, path: *const WCHAR) {
    let mut err = 0u32;
    if shell_run(verb, app_uri, null(), null(), SEE_MASK_FLAG_LOG_USAGE | SEE_MASK_FLAG_NO_UI, &mut err) {
        return;
    }
    if err == ERROR_CANCELLED {
        return;
    }
    shell_exec(verb, if path.is_null() { app_uri } else { path }, null(), &mut err);
}

pub unsafe fn reveal_in_explorer(path: *const WCHAR) {
    let pidl = ILCreateFromPathW(path);
    if !pidl.is_null() {
        SHOpenFolderAndSelectItems(pidl, 0, null(), 0);
        ILFree(pidl);
    }
}

unsafe fn run_command(cmd: u8) {
    match cmd {
        CMD_LOCK => {
            LockWorkStation();
        }
        CMD_SLEEP | CMD_HIBERNATE => {
            enable_shutdown_privilege();
            type Pfn = unsafe extern "system" fn(u8, u8, u8) -> u8;
            let m = LoadLibraryW(w!("powrprof.dll"));
            let sss: Option<Pfn> = if m.is_null() { None } else { core::mem::transmute(GetProcAddress(m, s!("SetSuspendState"))) };
            if let Some(f) = sss {
                f((cmd == CMD_HIBERNATE) as u8, 0, 0);
            }
        }
        CMD_RESTART => {
            enable_shutdown_privilege();
            ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
        }
        CMD_SHUTDOWN => {
            enable_shutdown_privilege();
            ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF | 0x00400000 /*EWX_HYBRID_SHUTDOWN*/, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
        }
        CMD_SIGNOUT => {
            ExitWindowsEx(EWX_LOGOFF, 0);
        }
        CMD_RECYCLE => open_as_user(w!("shell:RecycleBinFolder"), null(), null_mut()),
        CMD_UNINSTALL | CMD_ADDREMOVE => open_as_user(w!("ms-settings:appsfeatures"), null(), null_mut()),
        _ => {}
    }
}

unsafe fn launch_exec(j: &LaunchJob) {
    if j.cmd != 0 {
        run_command(j.cmd);
        return;
    }
    let mut app_uri = [0u16; 2048];
    if j.is_app {
        wjoin(app_uri.as_mut_ptr(), 2048, &[(w!("shell:AppsFolder\\"), -1), (j.target, -1)]);
    }
    let file: *const WCHAR = if j.is_app { j.path } else { j.target }; // what Reveal and Properties act on
    match j.act {
        ACT_OPEN => {
            if j.is_app {
                let what = if !wcsstr(j.target, w!("://")).is_null() { j.target as *const WCHAR } else { app_uri.as_ptr() };
                if !g_elevated || !shell_exec_unelevated(what, null(), null(), null()) {
                    app_exec(null(), app_uri.as_ptr(), j.path);
                }
            } else if j.uri || !wcsstr(j.target, w!("://")).is_null() {
                open_as_user(j.target, null(), null_mut());
            } else if GetFileAttributesW(j.target) == INVALID_FILE_ATTRIBUTES {
                let e = GetLastError();
                if e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND {
                    let k = wdup_heap(j.hist_key);
                    if PostMessageW(g_hwnd, WM_APP_LAUNCH_FAILED, j.hist_type as WPARAM, k as LPARAM) == 0 {
                        free(k as *mut c_void);
                    }
                }
            } else {
                open_as_user(j.target, j.dir, null_mut());
            }
        }
        ACT_RUNAS => {
            if j.is_app {
                app_exec(w!("runas"), app_uri.as_ptr(), j.path);
            } else {
                shell_exec(w!("runas"), j.target, j.dir, null_mut());
            }
        }
        ACT_REVEAL => {
            if !file.is_null() {
                reveal_in_explorer(file);
            }
        }
        ACT_PROPERTIES => {
            if !file.is_null() {
                shell_run(w!("properties"), file, null(), null(), SEE_MASK_INVOKEIDLIST, null_mut());
            }
        }
        _ => {}
    }
}

unsafe extern "system" fn launch_thread(_param: *mut c_void) -> u32 {
    CoInitializeEx(null(), (COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) as _);
    let q = &raw mut LQ;
    loop {
        // STA thread: keep pumping so shell components that post to us work.
        MsgWaitForMultipleObjects(1, &(*q).event, FALSE, INFINITE, QS_ALLINPUT);
        pump_messages();
        loop {
            AcquireSRWLockExclusive(&raw mut (*q).lock);
            let has = (*q).count > 0;
            let mut j: LaunchJob = zeroed();
            if has {
                j = *(*q).jobs;
                memmove((*q).jobs, (*q).jobs.add(1), size_of::<LaunchJob>() * ((*q).count - 1) as usize);
                (*q).count -= 1;
            }
            ReleaseSRWLockExclusive(&raw mut (*q).lock);
            if !has {
                break;
            }
            launch_exec(&j);
            job_free(&j);
        }
    }
}

pub unsafe fn launch_start() {
    InitializeSRWLock(&raw mut LQ.lock);
    LQ.event = CreateEventW(null(), FALSE, FALSE, null());
    thread_start(launch_thread, null_mut());
}

// Queues a job; the launch thread frees its strings.
pub unsafe fn launch_submit(j: &LaunchJob) {
    let q = &raw mut LQ;
    AcquireSRWLockExclusive(&raw mut (*q).lock);
    if grow(&raw mut (*q).jobs, &raw mut (*q).cap, (*q).count + 1, 16) {
        *(*q).jobs.add((*q).count as usize) = *j;
        (*q).count += 1;
    }
    ReleaseSRWLockExclusive(&raw mut (*q).lock);
    SetEvent((*q).event);
}

pub const OPEN_PATH: i32 = 0;
pub const OPEN_URI: i32 = 1;
pub const OPEN_APP: i32 = 2;

pub unsafe fn launch_open(target: *const WCHAR, kind: i32) {
    let mut j: LaunchJob = zeroed();
    j.act = ACT_OPEN;
    j.uri = kind == OPEN_URI;
    j.is_app = kind == OPEN_APP;
    j.target = wdup_heap(target);
    launch_submit(&j);
}

pub unsafe fn launch_command(cmd: u8) {
    let mut j: LaunchJob = zeroed();
    j.cmd = cmd;
    launch_submit(&j);
}
