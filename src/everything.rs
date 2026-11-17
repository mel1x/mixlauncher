// File search through Everything's IPC (WM_COPYDATA, QUERY2): a worker thread asks, ranks and posts the results
// (WM_APP_FILES_READY), then fetches more pages as the list scrolls.

use crate::*;

const EV_COPYDATA_QUERY2W: usize = 18;
// fields of a QUERY2 reply, in the order they come (only those up to DATE_MODIFIED are parsed)
const EV_REQ_NAME: u32 = 0x00000001;
const EV_REQ_PATH: u32 = 0x00000002;
const EV_REQ_FULL_PATH: u32 = 0x00000004;
const EV_REQ_EXTENSION: u32 = 0x00000008;
const EV_REQ_SIZE: u32 = 0x00000010;
const EV_REQ_DATE_CREATED: u32 = 0x00000020;
const EV_REQ_DATE_MODIFIED: u32 = 0x00000040;
const EV_SORT_DATE_MODIFIED_DESC: u32 = 14;
const EV_ITEM_FOLDER: u32 = 0x1;
const EV_ITEM_DRIVE: u32 = 0x2;
const EV_FIRST_PAGE: u32 = 256;
const EV_PAGE: u32 = 256;

pub const EV_OK: i32 = 0;
pub const EV_NOT_RUNNING: i32 = 1;
pub const EV_ERROR: i32 = 2;
pub const FI_FOLDER: u32 = 1;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FileItem {
    pub name: *mut WCHAR,
    pub dir: *mut WCHAR,
    pub full: *mut WCHAR,
    pub mtime: i64,
    pub flags: u32,
    pub score: f32,
    pub icon_key: u64,
}

#[repr(C)]
pub struct FileResults {
    pub arena: Arena,
    pub id: u32,
    pub status: i32,
    pub total: u32,
    pub fetched: u32,
    pub page: bool,
    pub count: i32,
    pub items: *mut FileItem,
}

pub unsafe fn fileresults_free(r: *mut FileResults) {
    if r.is_null() {
        return;
    }
    arena_release(&raw mut (*r).arena);
    free(r as *mut c_void);
}

// The folder part of a path (its length) and the name after it. A drive root ("C:\") is all name.
pub unsafe fn path_split(full: *const WCHAR, name: &mut *const WCHAR) -> i32 {
    let slash = wcsrchr(full, '\\' as u16);
    if slash.is_null() || *slash.add(1) == 0 {
        *name = full;
        return 0;
    }
    *name = slash.add(1);
    slash.offset_from(full) as i32
}

#[repr(C)]
#[derive(Clone, Copy)]
struct RawFile {
    name: *mut WCHAR,
    path: *mut WCHAR,
    mtime: i64,
    flags: u32,
}

#[repr(C)]
struct Ev {
    lock: SRWLOCK,
    event: HANDLE,
    req_id: u32,
    req_query: [WCHAR; 1024],
    has_req: bool,
    filter: [WCHAR; 512],

    reply: HWND,
    tag: u32,
    waiting_tag: u32,
    got: bool,
    scratch: Arena,
    raw: *mut RawFile,
    raw_count: i32,
    raw_cap: i32,
    raw_total: u32,
    profile_norm: [WCHAR; MAX_PATH],
    profile_len: i32,

    more_id: u32,
    has_more: bool,
    cur_id: u32,
    cur_total: u32,
    cur_fetched: u32,
    cur_search: [WCHAR; 2048],
    cur_qn: [WCHAR; 1024],
    cur_qlen: i32,
    cur_plain: bool,
    seen: *mut u64,
    seen_cap: u32,
    seen_count: u32,
}

static mut E: Ev = unsafe { zeroed() };

#[inline]
unsafe fn ee() -> &'static mut Ev {
    &mut *(&raw mut E)
}

pub unsafe fn ev_request(id: u32, query: *const WCHAR) {
    let e = ee();
    AcquireSRWLockExclusive(&mut e.lock);
    e.req_id = id;
    wcopy(e.req_query.as_mut_ptr(), 1024, query);
    wcopy(e.filter.as_mut_ptr(), 512, g_cfg.everything_filter.as_ptr());
    e.has_req = true;
    ReleaseSRWLockExclusive(&mut e.lock);
    SetEvent(e.event);
}

pub unsafe fn ev_request_more(id: u32) {
    let e = ee();
    AcquireSRWLockExclusive(&mut e.lock);
    e.more_id = id;
    e.has_more = true;
    ReleaseSRWLockExclusive(&mut e.lock);
    SetEvent(e.event);
}

unsafe fn ev_superseded() -> bool {
    let e = ee();
    AcquireSRWLockShared(&mut e.lock);
    let r = e.has_req;
    ReleaseSRWLockShared(&mut e.lock);
    r
}

// A string of a reply: its length, the characters and a terminator. The length is what is trusted: the reply
// comes through WM_COPYDATA from a process below our level.
unsafe fn ev_read_str(p: *const u8, end: *const u8, out: Option<(&mut *const WCHAR, &mut i32)>) -> *const u8 {
    if p.is_null() || p.add(4) > end {
        return null();
    }
    let mut len = 0u32;
    memcpy(&mut len, p as *const u32, 4);
    let p = p.add(4);
    if (end.offset_from(p) as usize) < (len as usize + 1) * 2 {
        return null();
    }
    if let Some((o, ol)) = out {
        *o = p as *const WCHAR;
        *ol = len as i32;
    }
    p.add((len as usize + 1) * 2)
}

unsafe fn ev_parse(data: *const u8, size: u32) {
    if data.is_null() || size < 20 {
        return;
    }
    let mut hdr = [0u32; 5];
    memcpy(hdr.as_mut_ptr(), data as *const u32, 20);
    let (total, num, req) = (hdr[0], hdr[1], hdr[3]);
    let e = ee();
    e.raw_total = max(e.raw_total, total);
    if 20u64 + num as u64 * 8 > size as u64 {
        return;
    }
    let end = data.add(size as usize);
    for i in 0..num as usize {
        let mut it = [0u32; 2];
        memcpy(it.as_mut_ptr(), data.add(20 + i * 8) as *const u32, 8);
        if it[1] >= size {
            continue;
        }
        let mut p = data.add(it[1] as usize);
        let mut name: *const WCHAR = null();
        let mut path: *const WCHAR = null();
        let mut mtime: i64 = 0;
        let mut name_len = 0;
        let mut path_len = 0;
        if req & EV_REQ_NAME != 0 {
            p = ev_read_str(p, end, Some((&mut name, &mut name_len)));
        }
        if req & EV_REQ_PATH != 0 {
            p = ev_read_str(p, end, Some((&mut path, &mut path_len)));
        }
        if req & EV_REQ_FULL_PATH != 0 {
            p = ev_read_str(p, end, None);
        }
        if req & EV_REQ_EXTENSION != 0 {
            p = ev_read_str(p, end, None);
        }
        if !p.is_null() && (req & EV_REQ_SIZE) != 0 {
            p = if p.add(8) <= end { p.add(8) } else { null() };
        }
        if !p.is_null() && (req & EV_REQ_DATE_CREATED) != 0 {
            p = if p.add(8) <= end { p.add(8) } else { null() };
        }
        if !p.is_null() && (req & EV_REQ_DATE_MODIFIED) != 0 {
            if p.add(8) <= end {
                memcpy(&mut mtime, p as *const i64, 8);
                p = p.add(8);
            } else {
                p = null();
            }
        }
        if p.is_null() || name.is_null() {
            continue;
        }
        if !grow(&mut e.raw, &mut e.raw_cap, e.raw_count + 1, 512) {
            return;
        }
        let r = &mut *e.raw.add(e.raw_count as usize);
        e.raw_count += 1;
        r.name = wdup(&mut e.scratch, name, name_len);
        r.path = wdup(&mut e.scratch, if path.is_null() { w!("") } else { path }, path_len);
        r.mtime = if mtime < 0 { 0 } else { mtime };
        r.flags = it[0];
    }
}

unsafe extern "system" fn ev_wndproc(h: HWND, m: u32, w: WPARAM, l: LPARAM) -> LRESULT {
    if m == WM_COPYDATA {
        let cds = l as *const COPYDATASTRUCT;
        if !cds.is_null() && (*cds).dwData == E.waiting_tag as usize && !E.got {
            ev_parse((*cds).lpData as *const u8, (*cds).cbData);
            E.got = true;
        }
        return TRUE as LRESULT;
    }
    DefWindowProcW(h, m, w, l)
}

// Built-in Everything (Everything\Everything.exe next to ours): a hidden instance with its index in our data folder,
// run only when the user has no Everything of their own.
macro_rules! ev_own_class {
    () => {
        w!("EVERYTHING_TASKBAR_NOTIFICATION_(MixLauncher)")
    };
}

pub const EVS_NONE: i32 = 0;
pub const EVS_USER: i32 = 1;
pub const EVS_OWN: i32 = 2;

pub static mut g_ev_own_allowed: bool = false;
static mut g_ev_own_proc: HANDLE = null_mut();
static mut g_ev_own_started: f64 = 0.0;

unsafe fn ev_user_window() -> HWND {
    let mut h = FindWindowW(w!("EVERYTHING_TASKBAR_NOTIFICATION"), null());
    if h.is_null() {
        h = FindWindowW(w!("EVERYTHING_TASKBAR_NOTIFICATION_(1.5a)"), null());
    }
    h
}

pub unsafe fn ev_state() -> i32 {
    if !ev_user_window().is_null() {
        EVS_USER
    } else if !FindWindowW(ev_own_class!(), null()).is_null() {
        EVS_OWN
    } else {
        EVS_NONE
    }
}

pub unsafe fn ev_own_exe(out: *mut WCHAR) -> bool {
    let mut dir = [0u16; MAX_PATH];
    wcopy(dir.as_mut_ptr(), MAX_PATH as i32, g_exe_path.as_ptr());
    PathRemoveFileSpecW(dir.as_mut_ptr());
    wfmt!(out, MAX_PATH, "{}\\Everything\\Everything.exe", Ws(dir.as_ptr()));
    GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES
}

unsafe fn ev_own_run(args: &str) -> HANDLE {
    let mut exe = [0u16; MAX_PATH];
    let mut cmd = [0u16; 3 * MAX_PATH + 128];
    if !ev_own_exe(exe.as_mut_ptr()) {
        return null_mut();
    }
    wfmt!(cmd.as_mut_ptr(), cmd.len(), "\"{}\" -instance MixLauncher {}", Ws(exe.as_ptr()), args);
    let mut si: STARTUPINFOW = zeroed();
    let mut pi: PROCESS_INFORMATION = zeroed();
    si.cb = size_of::<STARTUPINFOW>() as u32;
    if CreateProcessW(exe.as_ptr(), cmd.as_mut_ptr(), null(), null(), FALSE, 0, null(), null(), &si, &mut pi) == 0 {
        log_msg!("everything: could not run our copy ({})", GetLastError());
        return null_mut();
    }
    CloseHandle(pi.hThread);
    pi.hProcess
}

unsafe fn ev_own_start() {
    if !g_ev_own_allowed {
        return;
    }
    if !g_ev_own_proc.is_null() && WaitForSingleObject(g_ev_own_proc, 0) == WAIT_TIMEOUT {
        return;
    }
    if g_ev_own_started != 0.0 && time_now() - g_ev_own_started < 30.0 {
        return;
    }
    let mut exe = [0u16; MAX_PATH];
    if !ev_own_exe(exe.as_mut_ptr()) {
        return;
    }
    let mut dir = [0u16; MAX_PATH];
    let mut ini = [0u16; MAX_PATH];
    let mut db = [0u16; MAX_PATH];
    data_path(dir.as_mut_ptr(), w!("Everything"));
    CreateDirectoryW(dir.as_ptr(), null());
    wfmt!(ini.as_mut_ptr(), MAX_PATH, "{}\\Everything.ini", Ws(dir.as_ptr()));
    wfmt!(db.as_mut_ptr(), MAX_PATH, "{}\\Everything.db", Ws(dir.as_ptr()));
    if GetFileAttributesW(ini.as_ptr()) == INVALID_FILE_ATTRIBUTES {
        let k_ini = b"[Everything]\r\nshow_tray_icon=0\r\nrun_in_background=1\r\ncheck_for_updates_on_startup=0\r\nauto_include_fixed_volumes=1\r\n";
        write_file_atomic(ini.as_ptr(), k_ini.as_ptr() as *const c_void, k_ini.len() as u32);
    }
    let args = format!("-startup -config \"{}\" -db \"{}\"", Ws(ini.as_ptr()), Ws(db.as_ptr()));
    if !g_ev_own_proc.is_null() {
        CloseHandle(g_ev_own_proc);
    }
    g_ev_own_proc = ev_own_run(&args);
    g_ev_own_started = time_now();
    if !g_ev_own_proc.is_null() {
        log_msg!("everything: started our own copy");
    }
}

pub unsafe fn ev_own_stop(wait_ms: u32) {
    if FindWindowW(ev_own_class!(), null()).is_null() {
        return;
    }
    let p = ev_own_run(if wait_ms != 0 { "-exit -wait" } else { "-exit" });
    if !p.is_null() {
        if wait_ms != 0 {
            WaitForSingleObject(p, wait_ms);
        }
        CloseHandle(p);
    }
}

unsafe fn ev_find_window() -> HWND {
    let mut h = ev_user_window();
    if !h.is_null() {
        if g_ev_own_allowed && !FindWindowW(ev_own_class!(), null()).is_null() {
            ev_own_stop(0);
        }
        return h;
    }
    h = FindWindowW(ev_own_class!(), null());
    if !h.is_null() {
        return h;
    }
    ev_own_start();
    let mut i = 0;
    while h.is_null() && i < 40 && !g_ev_own_proc.is_null() && WaitForSingleObject(g_ev_own_proc, 50) == WAIT_TIMEOUT {
        h = FindWindowW(ev_own_class!(), null());
        i += 1;
    }
    h
}

// Sends a query and pumps messages until the reply comes. 1 = ok, 0 = error, -1 = cancelled by a newer request.
unsafe fn ev_send_query(ev: HWND, search: *const WCHAR, offset: u32, max_results: u32, sort: u32) -> i32 {
    let e = ee();
    let len = wlen(search);
    let size = 28 + (len as u32 + 1) * 2;
    let buf = arena_push(&mut e.scratch, size as usize) as *mut u8;
    e.tag = (e.tag + 1) & 0xFFFF;
    e.waiting_tag = 0x4D4C0000 | e.tag;
    let q: [u32; 7] = [e.reply as usize as u32, e.waiting_tag, 0, offset, max_results, EV_REQ_NAME | EV_REQ_PATH | EV_REQ_DATE_MODIFIED, sort];
    memcpy(buf, q.as_ptr() as *const u8, 28);
    memcpy(buf.add(28), search as *const u8, (len as usize + 1) * 2);
    let cds = COPYDATASTRUCT { dwData: EV_COPYDATA_QUERY2W, cbData: size, lpData: buf as *mut c_void };
    e.got = false;
    let mut res: usize = 0;
    if SendMessageTimeoutW(ev, WM_COPYDATA, e.reply as WPARAM, &cds as *const COPYDATASTRUCT as LPARAM, SMTO_ABORTIFHUNG | SMTO_NORMAL, 3000, &mut res) == 0
        || res == 0
    {
        return 0;
    }
    let t0 = time_now();
    // Our copy builds its index on the very first run and answers once it is done.
    let limit = if g_ev_own_started != 0.0 && t0 - g_ev_own_started < 60.0 { 30.0 } else { 5.0 };
    let mut result = 1;
    while !(*(&raw const E.got)) {
        if ev_superseded() {
            result = -1;
            break;
        }
        if time_now() - t0 > limit {
            result = 0;
            break;
        }
        MsgWaitForMultipleObjects(0, null(), FALSE, 15, QS_ALLINPUT);
        pump_messages();
    }
    E.waiting_tag = 0;
    result
}

// No Everything syntax (paths, wildcards, operators): ranked as a plain name.
unsafe fn query_is_simple(mut q: *const WCHAR) -> bool {
    let special = w!(":\"<>|*?!\\/");
    while *q != 0 {
        if !wcschr(special, *q).is_null() {
            return false;
        }
        q = q.add(1);
    }
    true
}

struct Penalty {
    needle: *const WCHAR,
    score: i32,
}

unsafe fn path_penalties() -> [Penalty; 16] {
    [
        Penalty { needle: w!("\\$recycle.bin"), score: -6000 },
        Penalty { needle: w!("\\system volume information"), score: -6000 },
        Penalty { needle: w!("\\winsxs\\"), score: -6000 },
        Penalty { needle: w!("\\windowsapps\\"), score: -4000 },
        Penalty { needle: w!("\\node_modules\\"), score: -3500 },
        Penalty { needle: w!("\\__pycache__"), score: -3000 },
        Penalty { needle: w!("\\appdata\\"), score: -2000 },
        Penalty { needle: w!("\\site-packages\\"), score: -2000 },
        Penalty { needle: w!("\\programdata\\"), score: -1500 },
        Penalty { needle: w!("\\temp\\"), score: -1500 },
        Penalty { needle: w!("\\cache"), score: -1200 },
        Penalty { needle: w!("\\msys64\\"), score: -1500 },
        Penalty { needle: w!("\\steamapps\\"), score: -1500 },
        Penalty { needle: w!("\\tmp\\"), score: -1000 },
        Penalty { needle: w!("\\obj\\"), score: -800 },
        Penalty { needle: w!("\\build\\"), score: -300 },
    ]
}

fn internal_ext() -> [*const WCHAR; 42] {
    [
        w!(".dll"), w!(".sys"), w!(".mui"), w!(".xbf"), w!(".pri"), w!(".pak"), w!(".dat"), w!(".bin"), w!(".pdb"), w!(".obj"), w!(".o"), w!(".lib"),
        w!(".a"), w!(".class"), w!(".pyc"), w!(".pyd"), w!(".pom"), w!(".cat"), w!(".manifest"), w!(".etl"), w!(".tmp"), w!(".cache"), w!(".idx"),
        w!(".lock"), w!(".map"), w!(".d"), w!(".tlog"), w!(".ilk"), w!(".exp"), w!(".nls"), w!(".db-journal"), w!(".db-wal"), w!(".db-shm"), w!(".ldb"),
        w!(".sha1"), w!(".sha256"), w!(".mo"), w!(".qm"), w!(".winmd"), w!(".node"), w!(".so"), w!(".jsonl"),
    ]
}
// (".sig" is the 43rd; kept separately so the array type stays readable)
const EXT_SIG: &str = ".sig";

unsafe fn ev_rank(r: &RawFile, full: *const WCHAR, full_len: i32, qn: *const WCHAR, qlen: i32, plain: bool, now_ft: i64) -> f32 {
    let mut nn = [0u16; 1024];
    let mut ws = [0u8; 1024];
    let nl = min(wlen(r.name), 1023);
    norm_str(nn.as_mut_ptr(), r.name, nl);
    word_starts(r.name, nl, ws.as_mut_ptr());
    let mut s: f32;
    if plain {
        let mut tier = score_query(nn.as_ptr(), nl, ws.as_ptr(), qn, qlen);
        let mut dot: i32 = -1;
        let mut i = nl - 1;
        while i > 0 {
            if nn[i as usize] == '.' as u16 {
                dot = i;
                break;
            }
            i -= 1;
        }
        if dot >= 0 && dot == qlen && wmem_eq(nn.as_ptr(), qn, qlen) {
            tier = SCORE_EXACT;
        }
        s = if tier != 0 { tier as f32 } else { 1500.0 };
    } else {
        s = 3000.0;
    }
    let age_days = (now_ft - r.mtime) as f64 / 864000000000.0;
    if r.mtime > 0 && age_days >= 0.0 {
        s += (1200.0 / (1.0 + age_days / 3.0)) as f32;
    }

    let mut fnb = [0u16; 2048];
    let fl = min(full_len, 2047);
    norm_str(fnb.as_mut_ptr(), full, fl);
    let fnp = fnb.as_ptr();
    for p in path_penalties() {
        if !wcsstr(fnp, p.needle).is_null() {
            s += p.score as f32;
        }
    }
    if fl >= 11 && wcsncmp(fnp.add(1), w!(":\\windows\\"), 10) == 0 {
        s -= 3000.0;
    }
    let folder = (r.flags & EV_ITEM_FOLDER) != 0;
    let dir_len = fl - nl;
    let mut i = 0;
    while i + 1 < dir_len {
        if fnb[i as usize] == '\\' as u16 && fnb[i as usize + 1] == '.' as u16 {
            s -= 2500.0;
            break;
        }
        i += 1;
    }
    if !wcsstr(fnp, w!("\\program files")).is_null() {
        s -= if folder { 300.0 } else { 1500.0 };
    }
    if !folder {
        let ext = PathFindExtensionW(nn.as_ptr());
        if *ext == 0 {
            s -= 800.0;
        } else {
            let mut hit = false;
            for e in internal_ext() {
                if wcscmp(ext, e) == 0 {
                    hit = true;
                    break;
                }
            }
            if !hit {
                let sig = wstr(EXT_SIG);
                hit = wcscmp(ext, sig.as_ptr()) == 0;
            }
            if hit {
                s -= 1500.0;
            }
        }
    }
    let e = ee();
    if e.profile_len != 0 && wcsncmp(fnp, e.profile_norm.as_ptr(), e.profile_len as usize) == 0 && wcsstr(fnp, w!("\\appdata\\")).is_null() {
        s += 300.0;
        let rest = fnp.add(e.profile_len as usize);
        if wstarts_with_i(rest, w!("\\desktop\\")) || wstarts_with_i(rest, w!("\\documents\\")) || wstarts_with_i(rest, w!("\\downloads\\")) {
            s += 300.0;
        }
    }
    let mut depth = 0;
    for i in 0..fl as usize {
        if fnb[i] == '\\' as u16 {
            depth += 1;
        }
    }
    if depth > 4 {
        s -= ((depth - 4) * 60) as f32;
    }
    if folder {
        s += 150.0;
    }
    s
}

unsafe extern "C" fn cmp_file_score(a: *const c_void, b: *const c_void) -> i32 {
    let x = &*(a as *const FileItem);
    let y = &*(b as *const FileItem);
    if x.score < y.score {
        1
    } else if x.score > y.score {
        -1
    } else {
        0
    }
}

unsafe fn seen_add(h: u64) -> bool {
    let h = if h == 0 { 1 } else { h };
    let e = ee();
    if (e.seen_count + 1) * 2 > e.seen_cap {
        let nc = if e.seen_cap != 0 { e.seen_cap * 2 } else { 1024 };
        let ns = calloc(nc as usize, 8) as *mut u64;
        if ns.is_null() {
            return true;
        }
        for i in 0..e.seen_cap as usize {
            let v = *e.seen.add(i);
            if v != 0 {
                let mut slot = hash_u64(v) as u32 & (nc - 1);
                while *ns.add(slot as usize) != 0 {
                    slot = (slot + 1) & (nc - 1);
                }
                *ns.add(slot as usize) = v;
            }
        }
        free(e.seen as *mut c_void);
        e.seen = ns;
        e.seen_cap = nc;
    }
    let mut slot = hash_u64(h) as u32 & (e.seen_cap - 1);
    while *e.seen.add(slot as usize) != 0 {
        if *e.seen.add(slot as usize) == h {
            return false;
        }
        slot = (slot + 1) & (e.seen_cap - 1);
    }
    *e.seen.add(slot as usize) = h;
    e.seen_count += 1;
    true
}

unsafe fn ev_build(res: *mut FileResults, reserve: usize) {
    let e = ee();
    let res = &mut *res;
    arena_init(&mut res.arena, reserve);
    res.items = arena_array::<FileItem>(&mut res.arena, max(e.raw_count, 1));
    let now_ft = filetime_now();
    let mut full = [0u16; 2048];
    for i in 0..e.raw_count as usize {
        let r = *e.raw.add(i);
        if *r.path != 0 {
            wjoin(full.as_mut_ptr(), 2048, &[(r.path, -1), (w!("\\"), 1), (r.name, -1)]);
        } else {
            wjoin(full.as_mut_ptr(), 2048, &[(r.name, -1), (w!("\\"), 1)]);
        }
        let fl = wlen(full.as_ptr());
        if !seen_add(hash_wstr_i(full.as_ptr(), fl)) {
            continue;
        }
        if wends_with_i(full.as_ptr(), fl, w!(".lnk")) && !StrStrIW(full.as_ptr(), w!("\\Start Menu\\Programs\\")).is_null() {
            continue;
        }
        let fi = &mut *res.items.add(res.count as usize);
        res.count += 1;
        fi.full = wdup(&mut res.arena, full.as_ptr(), fl);
        fi.name = fi.full.add(min(if *r.path != 0 { wlen(r.path) + 1 } else { 0 }, fl) as usize); // fl: a path cut to fit ends before its name
        fi.dir = if *r.path != 0 { wdup(&mut res.arena, r.path, -1) } else { wdup(&mut res.arena, w!(""), 0) };
        fi.mtime = r.mtime;
        fi.flags = if (r.flags & (EV_ITEM_FOLDER | EV_ITEM_DRIVE)) != 0 { FI_FOLDER } else { 0 };
        fi.score = ev_rank(&r, full.as_ptr(), fl, e.cur_qn.as_ptr(), e.cur_qlen, e.cur_plain, now_ft);
        fi.icon_key = icon_key_for_file(full.as_ptr(), (fi.flags & FI_FOLDER) != 0);
    }
    qsort(res.items as *mut c_void, res.count as usize, size_of::<FileItem>(), cmp_file_score);
}

// The query with the user's filter: "<query> filter", or "prefix<query> filter".
unsafe fn ev_search_text(out: *mut WCHAR, cap: i32, prefix: *const WCHAR, query: *const WCHAR, filter: *const WCHAR) {
    if *filter != 0 {
        wjoin(out, cap, &[(prefix, -1), (w!("<"), 1), (query, -1), (w!("> "), 2), (filter, -1)]);
    } else {
        wjoin(out, cap, &[(prefix, -1), (query, -1)]);
    }
}

unsafe fn ev_post(res: *mut FileResults) {
    if PostMessageW(g_hwnd, WM_APP_FILES_READY, 0, res as LPARAM) == 0 {
        fileresults_free(res);
    }
}

// A new query: the first page by date, plus (for a plain name) the names that start with it, which a big
// first page by date could miss.
unsafe fn ev_run(id: u32, query: *const WCHAR, filter: *const WCHAR) {
    let res = calloc(1, size_of::<FileResults>()) as *mut FileResults;
    if res.is_null() {
        return;
    }
    (*res).id = id;
    let ev = ev_find_window();
    if ev.is_null() {
        (*res).status = EV_NOT_RUNNING;
        ev_post(res);
        return;
    }
    let e = ee();
    arena_reset(&mut e.scratch);
    e.raw_count = 0;
    e.raw_total = 0;
    e.cur_id = 0;

    let mut search = [0u16; 2048];
    ev_search_text(search.as_mut_ptr(), 2048, w!(""), query, filter);
    let ok = ev_send_query(ev, search.as_ptr(), 0, EV_FIRST_PAGE, EV_SORT_DATE_MODIFIED_DESC);
    if ok < 0 {
        free(res as *mut c_void);
        return;
    }
    let main_count = e.raw_count as u32;
    let plain = query_is_simple(query);
    if ok > 0 && plain && e.raw_total > e.raw_count as u32 && !ev_superseded() {
        let mut starts = [0u16; 2048];
        ev_search_text(starts.as_mut_ptr(), 2048, w!("startwith:"), query, filter);
        let total = e.raw_total;
        let ok2 = ev_send_query(ev, starts.as_ptr(), 0, 128, EV_SORT_DATE_MODIFIED_DESC);
        e.raw_total = total;
        if ok2 < 0 {
            free(res as *mut c_void);
            return;
        }
    }
    if ok == 0 {
        (*res).status = EV_ERROR;
        ev_post(res);
        return;
    }

    e.cur_qlen = min(wlen(query), 1023);
    norm_str(e.cur_qn.as_mut_ptr(), query, e.cur_qlen);
    while e.cur_qlen != 0 && e.cur_qn[e.cur_qlen as usize - 1] == ' ' as u16 {
        e.cur_qlen -= 1;
        e.cur_qn[e.cur_qlen as usize] = 0;
    }
    e.cur_plain = plain;
    wcopy(e.cur_search.as_mut_ptr(), 2048, search.as_ptr());
    if !e.seen.is_null() {
        memset(e.seen, 0, 8 * e.seen_cap as usize);
    }
    e.seen_count = 0;

    (*res).total = e.raw_total;
    ev_build(res, 32 << 20);
    e.cur_id = id;
    e.cur_total = e.raw_total;
    e.cur_fetched = main_count;
    (*res).fetched = main_count;
    ev_post(res);
}

// The next page of the current query, as the list scrolls toward its end.
unsafe fn ev_run_more(id: u32) {
    let e = ee();
    if id != e.cur_id || e.cur_fetched >= e.cur_total {
        return;
    }
    let ev = ev_find_window();
    arena_reset(&mut e.scratch);
    e.raw_count = 0;
    e.raw_total = 0;
    // Everything gone meanwhile: an error page too, or the list would wait for this one forever
    let ok = if ev.is_null() { 0 } else { ev_send_query(ev, e.cur_search.as_ptr(), e.cur_fetched, EV_PAGE, EV_SORT_DATE_MODIFIED_DESC) };
    if ok < 0 {
        return;
    }
    let res = calloc(1, size_of::<FileResults>()) as *mut FileResults;
    if res.is_null() {
        return;
    }
    (*res).id = id;
    (*res).page = true;
    if ok == 0 {
        (*res).status = EV_ERROR;
        e.cur_fetched = e.cur_total;
    } else {
        if e.raw_total != 0 {
            e.cur_total = e.raw_total;
        }
        e.cur_fetched = if e.raw_count != 0 { e.cur_fetched + e.raw_count as u32 } else { e.cur_total };
        ev_build(res, 8 << 20);
    }
    (*res).total = e.cur_total;
    (*res).fetched = e.cur_fetched;
    ev_post(res);
}

unsafe extern "system" fn ev_thread(_param: *mut c_void) -> u32 {
    let e = ee();
    e.reply = message_window(w!("MixLauncherEverythingReply"), ev_wndproc);
    // We run elevated and Everything does not: its replies must pass UIPI.
    if !e.reply.is_null() {
        ChangeWindowMessageFilterEx(e.reply, WM_COPYDATA, MSGFLT_ALLOW, null_mut());
    }
    arena_init(&mut e.scratch, 64 << 20);

    let mut prof: PWSTR = null_mut();
    if succeeded(SHGetKnownFolderPath(&FOLDERID_Profile, 0, null_mut(), &mut prof)) && !prof.is_null() {
        e.profile_len = min(wlen(prof), MAX_PATH as i32 - 1);
        norm_str(e.profile_norm.as_mut_ptr(), prof, e.profile_len);
    }
    CoTaskMemFree(prof as *const c_void);
    ev_find_window();

    let mut q = [0u16; 1024];
    let mut filter = [0u16; 512];
    loop {
        MsgWaitForMultipleObjects(1, &e.event, FALSE, INFINITE, QS_ALLINPUT);
        pump_messages();
        AcquireSRWLockExclusive(&mut e.lock);
        let has = e.has_req;
        let more = e.has_more;
        let more_id = e.more_id;
        e.has_req = false;
        e.has_more = false;
        let id = e.req_id;
        wcopy(q.as_mut_ptr(), 1024, e.req_query.as_ptr());
        wcopy(filter.as_mut_ptr(), 512, e.filter.as_ptr());
        ReleaseSRWLockExclusive(&mut e.lock);
        if has {
            ev_run(id, q.as_ptr(), filter.as_ptr());
        } else if more {
            ev_run_more(more_id);
        }
    }
}

pub unsafe fn ev_start() {
    InitializeSRWLock(&raw mut E.lock);
    E.event = CreateEventW(null(), FALSE, FALSE, null());
    thread_start(ev_thread, null_mut());
}
