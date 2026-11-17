// Updates from GitHub releases: a periodic check, then the installer or portable exe download and a restart.

use crate::*;
use core::sync::atomic::{AtomicI32, Ordering::SeqCst};
use windows_sys::Win32::Networking::WinHttp::*;

const UPD_URL: &str = "https://api.github.com/repos/mel1x/mixlauncher/releases/latest";
const UPD_SETUP_ASSET: &str = "MixLauncher-Setup.exe";
const UPD_PORTABLE_ASSET: &str = "MixLauncher-portable.exe";
const UPD_PORTABLE_V2_ASSET: &str = "MixLauncher-portable-v2.exe";
const UPD_PORTABLE_V3_ASSET: &str = "MixLauncher-portable-v3.exe";
const UPD_CHECK_EVERY_MS: u32 = 6 * 3600 * 1000;
pub const TIMER_UPDATE: usize = 3;

pub const UPD_IDLE: i32 = 0;
pub const UPD_CHECKING: i32 = 1;
pub const UPD_LATEST: i32 = 2;
pub const UPD_AVAILABLE: i32 = 3;
pub const UPD_DOWNLOADING: i32 = 4;
pub const UPD_INSTALLING: i32 = 5;
pub const UPD_ERROR: i32 = 6;

#[repr(C)]
pub struct Upd {
    pub state: AtomicI32,
    pub progress: AtomicI32, // 0..1000 while downloading
    pub busy: AtomicI32,
    pub latest: [WCHAR; 32],
    pub url: [WCHAR; 1024],
    pub file: [WCHAR; MAX_PATH],
    pub cur: [i32; 3],
    pub dry_run: bool,
}

pub static mut UPD: Upd = unsafe { zeroed() };

unsafe fn upd_notify() {
    if !g_hwnd.is_null() {
        PostMessageW(g_hwnd, WM_APP_UPDATE, 0, 0);
    }
}

unsafe fn upd_set(st: i32) {
    UPD.state.store(st, SeqCst);
    upd_notify();
}

pub unsafe fn upd_state() -> i32 {
    UPD.state.load(SeqCst)
}

// Installed by the setup (its uninstaller is next to us), not the portable exe.
unsafe fn upd_installed() -> bool {
    let mut p = [0u16; MAX_PATH];
    wcopy(p.as_mut_ptr(), MAX_PATH as i32, g_exe_path.as_ptr());
    let slash = wcsrchr(p.as_ptr(), '\\' as u16);
    if slash.is_null() {
        return false;
    }
    wcopy(slash.add(1), (MAX_PATH as isize - slash.add(1).offset_from(p.as_ptr())) as i32, w!("unins000.exe"));
    GetFileAttributesW(p.as_ptr()) != INVALID_FILE_ATTRIBUTES
}

fn upd_parse_ver(s: &[u8], v: &mut [i32; 3]) {
    *v = [0; 3];
    let mut s = s;
    if !s.is_empty() && (s[0] == b'v' || s[0] == b'V') {
        s = &s[1..];
    }
    for i in 0..3 {
        if s.is_empty() || s[0] == 0 {
            break;
        }
        let mut n = 0i32;
        let mut k = 0;
        while k < s.len() && s[k].is_ascii_digit() {
            n = n.wrapping_mul(10).wrapping_add((s[k] - b'0') as i32);
            k += 1;
        }
        v[i] = n;
        s = &s[k..];
        if s.is_empty() || s[0] != b'.' {
            break;
        }
        s = &s[1..];
    }
}

fn upd_newer(a: &[i32; 3], b: &[i32; 3]) -> bool {
    for i in 0..3 {
        if a[i] != b[i] {
            return a[i] > b[i];
        }
    }
    false
}

unsafe fn upd_session() -> *mut c_void {
    let agent = wstr(&format!("MixLauncher/{}", ML_VER_STR));
    let mut s = WinHttpOpen(agent.as_ptr(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, null(), null(), 0);
    if s.is_null() {
        s = WinHttpOpen(agent.as_ptr(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, null(), null(), 0);
    }
    if !s.is_null() {
        let redirect: u32 = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &redirect as *const u32 as *const c_void, 4);
        WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    }
    s
}

// GET https://host/path; the body goes to out (memory) or to the file (with progress). Follows redirects.
unsafe fn upd_get(s: *mut c_void, url: *const WCHAR, out: Option<&mut Buf>, file: HANDLE, progress: bool) -> bool {
    let mut uc: URL_COMPONENTS = zeroed();
    uc.dwStructSize = size_of::<URL_COMPONENTS>() as u32;
    let mut host = [0u16; 256];
    let mut path = vec![0u16; 2048];
    uc.lpszHostName = host.as_mut_ptr();
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path.as_mut_ptr();
    uc.dwUrlPathLength = 2048;
    if WinHttpCrackUrl(url, 0, 0, &mut uc) == 0 || uc.nScheme != 2 /*INTERNET_SCHEME_HTTPS*/ {
        return false;
    }
    let c = WinHttpConnect(s, host.as_ptr(), uc.nPort, 0);
    if c.is_null() {
        return false;
    }
    let r = WinHttpOpenRequest(c, w!("GET"), path.as_ptr(), null(), null(), null(), WINHTTP_FLAG_SECURE);
    let mut ok = false;
    let mut out = out;
    let hdr = if out.is_some() { w!("Accept: application/vnd.github+json\r\n") } else { null() };
    if !r.is_null() && WinHttpSendRequest(r, hdr, if out.is_some() { u32::MAX } else { 0 }, null(), 0, 0, 0) != 0 && WinHttpReceiveResponse(r, null_mut()) != 0 {
        let mut status = 0u32;
        let mut sz = 4u32;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, null(), &mut status as *mut u32 as *mut c_void, &mut sz, null_mut());
        let mut total = 0u32;
        sz = 4;
        if WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, null(), &mut total as *mut u32 as *mut c_void, &mut sz, null_mut()) == 0 {
            total = 0;
        }
        if status == 200 {
            ok = true;
            let mut got = 0u64;
            let mut chunk = vec![0u8; 64 * 1024];
            loop {
                let mut n = 0u32;
                if WinHttpReadData(r, chunk.as_mut_ptr() as *mut c_void, chunk.len() as u32, &mut n) == 0 {
                    ok = false;
                    break;
                }
                if n == 0 {
                    break;
                }
                got += n as u64;
                if let Some(o) = out.as_mut() {
                    if got > 4 * 1024 * 1024 {
                        ok = false;
                        break;
                    }
                    o.put(&chunk[..n as usize]);
                } else {
                    let mut w = 0u32;
                    if WriteFile(file, chunk.as_ptr(), n, &mut w, null_mut()) == 0 || w != n {
                        ok = false;
                        break;
                    }
                }
                if progress && total != 0 {
                    UPD.progress.store(min(1000, got * 1000 / total as u64) as i32, SeqCst);
                    upd_notify();
                }
            }
            if ok && total != 0 && got != total as u64 {
                ok = false;
            }
        }
    }
    if !r.is_null() {
        WinHttpCloseHandle(r);
    }
    WinHttpCloseHandle(c);
    ok
}

// Minimal field lookup in the release JSON: "key":"value" with no escapes we care about.
fn json_str(js: &[u8], key: &str, from: usize, cap: usize) -> Option<String> {
    let pat = format!("\"{}\"", key);
    let hay = &js[from..];
    let at = hay.windows(pat.len()).position(|w| w == pat.as_bytes())?;
    let mut p = from + at + pat.len();
    while p < js.len() && matches!(js[p], b' ' | b':' | b'\t' | b'\r' | b'\n') {
        p += 1;
    }
    if p >= js.len() || js[p] != b'"' {
        return None;
    }
    p += 1;
    let start = p;
    while p < js.len() && js[p] != 0 && js[p] != b'"' && p - start < cap - 1 {
        p += 1;
    }
    let v = String::from_utf8_lossy(&js[start..p]).into_owned();
    if p < js.len() && js[p] == b'"' { Some(v) } else { None }
}

// Finds the newest release and its asset for this kind of install (setup or portable).
unsafe extern "system" fn upd_check_thread(_arg: *mut c_void) -> u32 {
    let s = upd_session();
    let mut b = Buf::new();
    let url = wstr(UPD_URL);
    let ok = !s.is_null() && upd_get(s, url.as_ptr(), Some(&mut b), null_mut(), false);
    if !s.is_null() {
        WinHttpCloseHandle(s);
    }
    let mut result = UPD_ERROR;
    if ok {
        let js = &b.data;
        // the setup picks the build for the processor itself; the portable exe takes the highest x86-64 level
        // the processor runs and the release has (v3, v2), the generic one otherwise
        let assets: &[&str] = if upd_installed() {
            &[UPD_SETUP_ASSET]
        } else if cpu_v3() {
            &[UPD_PORTABLE_V3_ASSET, UPD_PORTABLE_V2_ASSET, UPD_PORTABLE_ASSET]
        } else if cpu_v2() {
            &[UPD_PORTABLE_V2_ASSET, UPD_PORTABLE_ASSET]
        } else {
            &[UPD_PORTABLE_ASSET]
        };
        let mut found = None;
        if let Some(tag) = json_str(js, "tag_name", 0, 64) {
            let needle = b"\"browser_download_url\"";
            for asset in assets {
                let mut p = 0usize;
                while let Some(at) = js[p..].windows(needle.len()).position(|w| w == needle) {
                    p += at;
                    if let Some(u) = json_str(js, "browser_download_url", p, 1024) {
                        let (n, m) = (u.len(), asset.len());
                        if n > m && u.as_bytes()[n - m - 1] == b'/' && u[n - m..].eq_ignore_ascii_case(asset) && u.starts_with("https://github.com/") {
                            found = Some(u);
                            break;
                        }
                    }
                    p += 1;
                }
                if found.is_some() {
                    break;
                }
            }
            let mut v = [0; 3];
            upd_parse_ver(tag.as_bytes(), &mut v);
            if found.is_some() && upd_newer(&v, &UPD.cur) {
                let t = tag.strip_prefix('v').or_else(|| tag.strip_prefix('V')).unwrap_or(&tag);
                wfmt!(UPD.latest.as_mut_ptr(), 32, "{}", t);
                wfmt!(UPD.url.as_mut_ptr(), 1024, "{}", found.unwrap());
                result = UPD_AVAILABLE;
            } else {
                result = UPD_LATEST;
            }
        }
    }
    if !ok {
        log_msg!("update check failed");
    }
    UPD.busy.store(0, SeqCst);
    upd_set(result);
    0
}

pub unsafe fn upd_check() {
    let st = upd_state();
    if st == UPD_DOWNLOADING || st == UPD_INSTALLING {
        return;
    }
    if UPD.busy.compare_exchange(0, 1, SeqCst, SeqCst).is_err() {
        return;
    }
    if st != UPD_AVAILABLE {
        upd_set(UPD_CHECKING);
    }
    if !thread_start(upd_check_thread, null_mut()) {
        UPD.busy.store(0, SeqCst);
        upd_set(UPD_ERROR);
    }
}

unsafe fn upd_file_is_exe(path: *const WCHAR) -> bool {
    let f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, null(), OPEN_EXISTING, 0, null_mut());
    if f == INVALID_HANDLE_VALUE {
        return false;
    }
    let mut mz = [0u8; 2];
    let mut n = 0u32;
    let mut sz = 0i64;
    let ok = ReadFile(f, mz.as_mut_ptr(), 2, &mut n, null_mut()) != 0 && n == 2 && mz == *b"MZ" && GetFileSizeEx(f, &mut sz) != 0 && sz > 64 * 1024;
    CloseHandle(f);
    ok
}

unsafe extern "system" fn upd_download_thread(_arg: *mut c_void) -> u32 {
    let mut dir = [0u16; MAX_PATH];
    let n = GetTempPathW(MAX_PATH as u32, dir.as_mut_ptr());
    let mut ok = n != 0 && (n as usize) < MAX_PATH - 40;
    if ok {
        wcopy(dir.as_mut_ptr().add(n as usize), MAX_PATH as i32 - n as i32, w!("MixLauncher-update"));
        CreateDirectoryW(dir.as_ptr(), null());
        wfmt!(UPD.file.as_mut_ptr(), MAX_PATH, "{}\\{}", Ws(dir.as_ptr()), if upd_installed() { UPD_SETUP_ASSET } else { UPD_PORTABLE_ASSET });
        let f = CreateFileW(UPD.file.as_ptr(), GENERIC_WRITE, 0, null(), CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, null_mut());
        ok = f != INVALID_HANDLE_VALUE;
        if ok {
            let s = upd_session();
            ok = !s.is_null() && upd_get(s, UPD.url.as_ptr(), None, f, true);
            if !s.is_null() {
                WinHttpCloseHandle(s);
            }
            CloseHandle(f);
            ok = ok && upd_file_is_exe(UPD.file.as_ptr());
            if !ok {
                DeleteFileW(UPD.file.as_ptr());
            }
        }
    }
    if !ok {
        log_msg!("update download failed");
    }
    UPD.busy.store(0, SeqCst);
    upd_set(if ok { UPD_INSTALLING } else { UPD_ERROR });
    0
}

pub unsafe fn upd_download() {
    if upd_state() != UPD_AVAILABLE && upd_state() != UPD_ERROR {
        return;
    }
    if UPD.url[0] == 0 {
        upd_check();
        return;
    }
    if UPD.busy.compare_exchange(0, 1, SeqCst, SeqCst).is_err() {
        return;
    }
    UPD.progress.store(0, SeqCst);
    upd_set(UPD_DOWNLOADING);
    if !thread_start(upd_download_thread, null_mut()) {
        UPD.busy.store(0, SeqCst);
        upd_set(UPD_ERROR);
    }
}

unsafe fn upd_run(file: *const WCHAR, args: *const WCHAR) -> bool {
    shell_run(w!("open"), file, args, null(), 0, null_mut())
}

// UI thread, download ready. The installer closes us itself (--shutdown); the portable exe is swapped in place.
unsafe fn upd_apply() {
    if UPD.dry_run {
        log_msg!("update dry run: downloaded, not applied");
        upd_set(UPD_ERROR);
        return;
    }
    if upd_installed() {
        if !upd_run(UPD.file.as_ptr(), w!("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /update=1")) {
            upd_set(UPD_ERROR);
        }
        return;
    }
    let mut old = [0u16; MAX_PATH + 8];
    wfmt!(old.as_mut_ptr(), old.len(), "{}.old", Ws(g_exe_path.as_ptr()));
    DeleteFileW(old.as_ptr());
    if MoveFileExW(g_exe_path.as_ptr(), old.as_ptr(), MOVEFILE_REPLACE_EXISTING) == 0 {
        upd_set(UPD_ERROR);
        return;
    }
    if MoveFileExW(UPD.file.as_ptr(), g_exe_path.as_ptr(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == 0 {
        MoveFileExW(old.as_ptr(), g_exe_path.as_ptr(), MOVEFILE_REPLACE_EXISTING);
        upd_set(UPD_ERROR);
        return;
    }
    let args = wstr(&format!("--after-update {}", GetCurrentProcessId()));
    if !upd_run(g_exe_path.as_ptr(), args.as_ptr()) {
        MoveFileExW(g_exe_path.as_ptr(), UPD.file.as_ptr(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(old.as_ptr(), g_exe_path.as_ptr(), MOVEFILE_REPLACE_EXISTING);
        upd_set(UPD_ERROR);
        return;
    }
    PostMessageW(g_hwnd, WM_APP_EXIT, 0, 0);
}

pub unsafe fn upd_on_message() {
    if upd_state() == UPD_INSTALLING && UPD.file[0] != 0 {
        upd_apply();
        UPD.file[0] = 0;
    }
}

pub unsafe fn upd_init(pretend_version: *const WCHAR) {
    let v = if pretend_version.is_null() { ML_VER_STR.to_string() } else { wto_string(pretend_version) };
    upd_parse_ver(v.as_bytes(), &mut UPD.cur);
    // A previous portable update leaves the old exe behind.
    let mut old = [0u16; MAX_PATH + 8];
    wfmt!(old.as_mut_ptr(), old.len(), "{}.old", Ws(g_exe_path.as_ptr()));
    DeleteFileW(old.as_ptr());
}

pub unsafe fn upd_schedule(first_ms: u32) {
    if g_cfg.update_check && !g_pinned {
        SetTimer(g_hwnd, TIMER_UPDATE, first_ms, None);
    } else {
        KillTimer(g_hwnd, TIMER_UPDATE);
    }
}

pub unsafe fn upd_on_timer() {
    SetTimer(g_hwnd, TIMER_UPDATE, UPD_CHECK_EVERY_MS, None);
    if g_cfg.update_check {
        upd_check();
    }
}

pub unsafe fn upd_visible() -> bool {
    let st = upd_state();
    st == UPD_AVAILABLE || st == UPD_DOWNLOADING || st == UPD_INSTALLING || (st == UPD_ERROR && UPD.url[0] != 0)
}

pub unsafe fn upd_label(buf: *mut WCHAR, cap: i32) -> *const WCHAR {
    match upd_state() {
        UPD_CHECKING => return tr!("Проверка…", "Checking…"),
        UPD_LATEST => return tr!("Последняя версия", "Up to date"),
        UPD_AVAILABLE => {
            wfmt!(buf, cap, "{} {}", Ws(tr!("Обновить до", "Update to")), Ws(UPD.latest.as_ptr()));
        }
        UPD_DOWNLOADING => {
            wfmt!(buf, cap, "{} {}%", Ws(tr!("Загрузка", "Downloading")), UPD.progress.load(SeqCst) / 10);
        }
        UPD_INSTALLING => return tr!("Установка…", "Installing…"),
        UPD_ERROR => {
            return if UPD.url[0] != 0 { tr!("Ошибка, повторить", "Failed, retry") } else { tr!("Нет связи с GitHub", "GitHub unreachable") };
        }
        _ => return tr!("Проверить", "Check now"),
    }
    *buf.add(cap as usize - 1) = 0;
    buf
}
