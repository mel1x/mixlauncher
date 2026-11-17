// The app list: Start's "All apps" (the Apps folder), its cache and the indexer thread.

use crate::*;
use windows::Win32::UI::Shell as wsh;
use windows::core::Interface;

pub const APP_SHELL: u8 = 0;
pub const APP_CMD: u8 = 1;
pub const CMD_NONE: u8 = 0;
pub const CMD_LOCK: u8 = 1;
pub const CMD_SLEEP: u8 = 2;
pub const CMD_RESTART: u8 = 3;
pub const CMD_SHUTDOWN: u8 = 4;
pub const CMD_SIGNOUT: u8 = 5;
pub const CMD_RECYCLE: u8 = 6;
pub const CMD_UNINSTALL: u8 = 7;
pub const CMD_ADDREMOVE: u8 = 8;
pub const CMD_HIBERNATE: u8 = 9;

pub const APP_MAX_KW: usize = 12;

#[repr(C)]
pub struct App {
    pub name: *mut WCHAR,
    pub norm: *mut WCHAR, // name for matching (norm_str) and its word starts
    pub ws: *mut u8,
    pub len: i32,
    pub id: *mut WCHAR,   // the Apps folder parsing name (AUMID or path), "cmd:N" for a command
    pub path: *mut WCHAR, // the target file, when there is one
    pub id_hash: u64,
    pub kind: u8,
    pub cmd: u8, // APP_CMD: CMD_*, its glyph, and whether it asks for a second Enter
    pub danger: bool,
    pub glyph: u16,
    pub kw_count: i32, // extra words it is found by: the exe name, the package, aliases, command synonyms
    pub kw: [*mut WCHAR; APP_MAX_KW],
    pub kw_ws: [*mut u8; APP_MAX_KW],
    pub kw_len: [i32; APP_MAX_KW],
    pub frec: f32, // launch frequency bonus, refreshed on show
}

#[repr(C)]
pub struct AppList {
    pub arena: Arena,
    pub apps: *mut App,
    pub count: i32,
    pub shell_count: i32,
    pub signature: u64,
}

pub unsafe fn applist_free(l: *mut AppList) {
    if l.is_null() {
        return;
    }
    arena_release(&raw mut (*l).arena);
    free(l as *mut c_void);
}

// An app as read from the Apps folder or the cache, before applist_build.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct RawApp {
    pub name: *mut WCHAR,
    pub id: *mut WCHAR,
    pub path: *mut WCHAR,
}

#[repr(C)]
pub struct RawApps {
    pub arena: Arena,
    pub items: *mut RawApp,
    pub n: i32,
    pub cap: i32,
}

unsafe fn raw_init(r: *mut RawApps) {
    zero(r);
    arena_init(&raw mut (*r).arena, 64 << 20);
}

unsafe fn raw_free(r: *mut RawApps) {
    free((*r).items as *mut c_void);
    arena_release(&raw mut (*r).arena);
}

unsafe fn raw_add(r: *mut RawApps, name: *const WCHAR, id: *const WCHAR, path: *const WCHAR) -> bool {
    let r = &mut *r;
    if !grow(&mut r.items, &mut r.cap, r.n + 1, 1024) {
        return false;
    }
    let a = &mut *r.items.add(r.n as usize);
    r.n += 1;
    a.name = wdup(&mut r.arena, name, -1);
    a.id = wdup(&mut r.arena, id, -1);
    a.path = if !path.is_null() && *path != 0 { wdup(&mut r.arena, path, -1) } else { null_mut() };
    true
}

unsafe extern "C" fn cmp_raw_name(a: *const c_void, b: *const c_void) -> i32 {
    let x = &*(a as *const RawApp);
    let y = &*(b as *const RawApp);
    let r = CompareStringEx(null(), LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS, x.name, -1, y.name, -1, null(), null(), 0);
    if r != 0 {
        return r - 2;
    }
    _wcsicmp(x.name, y.name)
}

pub unsafe fn app_fill_match(a: *mut Arena, app: *mut App) {
    let app = &mut *app;
    app.len = wlen(app.name);
    app.norm = arena_push(a, (app.len as usize + 1) * 2) as *mut WCHAR;
    norm_str(app.norm, app.name, app.len);
    app.ws = arena_push(a, app.len as usize + 1) as *mut u8;
    word_starts(app.name, app.len, app.ws);
}

unsafe fn looks_like_noise(norm: *const WCHAR, path: *const WCHAR) -> bool {
    let bad_names = [w!("uninstall"), w!("деинсталл"), w!("удалить"), w!("удаление")];
    for b in bad_names {
        if !wcsstr(norm, b).is_null() {
            return true;
        }
    }
    if !path.is_null() {
        if wstarts_with_i(path, w!("http://")) || wstarts_with_i(path, w!("https://")) {
            return true;
        }
        let file = PathFindFileNameW(path);
        if wstarts_with_i(file, w!("unins")) {
            return true;
        }
        let doc_ext = [w!(".chm"), w!(".txt"), w!(".pdf"), w!(".htm"), w!(".html"), w!(".rtf"), w!(".md"), w!(".url"), w!(".hlp")];
        let n = wlen(path);
        for e in doc_ext {
            if wends_with_i(path, n, e) {
                return true;
            }
        }
        if *path.add(1) == ':' as u16 && *PathFindExtensionW(path) == 0 {
            let attr = GetFileAttributesW(path);
            if attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0 {
                return true;
            }
        }
    }
    false
}

// "{GUID}\sub\app.exe" -> "C:\Program Files\sub\app.exe". False when the id is no path.
pub unsafe fn resolve_known_folder_path(id: *const WCHAR, out: *mut WCHAR, cap: i32) -> bool {
    *out = 0;
    if id.is_null() {
        return false;
    }
    let c0 = *id;
    if ((c0 >= 'A' as u16 && c0 <= 'Z' as u16) || (c0 >= 'a' as u16 && c0 <= 'z' as u16)) && *id.add(1) == ':' as u16 && *id.add(2) == '\\' as u16 {
        wcopy(out, cap, id);
        return true;
    }
    let close = if c0 == '{' as u16 { wcschr(id, '}' as u16) } else { null_mut() };
    if close.is_null() || close.offset_from(id) != 37 {
        return false;
    }
    let mut gs = [0u16; 40];
    memcpy(gs.as_mut_ptr(), id, 38 * 2);
    gs[38] = 0;
    let mut g: GUID = zeroed();
    let mut base: PWSTR = null_mut();
    let ok = succeeded(CLSIDFromString(gs.as_ptr(), &mut g)) && succeeded(SHGetKnownFolderPath(&g, KF_FLAG_DONT_VERIFY as _, null_mut(), &mut base)) && !base.is_null();
    if ok {
        wjoin(out, cap, &[(base, -1), (close.add(1), -1)]);
    }
    CoTaskMemFree(base as *const c_void);
    ok
}

type PfnGetPackagesByPackageFamily = unsafe extern "system" fn(*const u16, *mut u32, *mut PWSTR, *mut u32, *mut u16) -> i32;
static mut GET_PACKAGES: Option<PfnGetPackagesByPackageFamily> = None;
static mut GET_PACKAGES_LOADED: bool = false;

// Explorer may hand out a packaged app's name unresolved ("ms-resource:...", e.g. mid-update): resolve it ourselves.
unsafe fn resolve_ms_resource(a: *mut Arena, res: *const WCHAR, aumid: *const WCHAR) -> *mut WCHAR {
    if !GET_PACKAGES_LOADED {
        GET_PACKAGES_LOADED = true;
        GET_PACKAGES = core::mem::transmute(GetProcAddress(GetModuleHandleW(w!("kernel32.dll")), s!("GetPackagesByPackageFamily")));
    }
    let bang = wcschr(aumid, '!' as u16);
    let Some(get_packages) = GET_PACKAGES else { return null_mut() };
    if bang.is_null() || bang.offset_from(aumid) >= 256 {
        return null_mut();
    }
    let mut family = [0u16; 256];
    let fl = bang.offset_from(aumid) as usize;
    memcpy(family.as_mut_ptr(), aumid, fl * 2);
    family[fl] = 0;
    let us = wcsrchr(family.as_ptr(), '_' as u16);
    if us.is_null() {
        return null_mut();
    }

    let mut count = 0u32;
    let mut len = 0u32;
    if get_packages(family.as_ptr(), &mut count, null_mut(), &mut len, null_mut()) != ERROR_INSUFFICIENT_BUFFER as i32 || count == 0 || len == 0 {
        return null_mut();
    }
    let names = malloc(count as usize * size_of::<PWSTR>()) as *mut PWSTR;
    let buf = malloc(len as usize * 2) as *mut WCHAR;
    let mut r: *mut WCHAR = null_mut();
    if !names.is_null() && !buf.is_null() && get_packages(family.as_ptr(), &mut count, names, &mut len, buf) == ERROR_SUCCESS as i32 && count != 0 {
        *us = 0; // family -> package name
        let key = res.add(12); // after "ms-resource:"
        let mut src = [0u16; 1024];
        let mut out = [0u16; 512];
        let mut v = 0;
        while v < 3 && r.is_null() {
            let k;
            if v == 0 {
                if *key == '/' as u16 || !wcschr(key, '/' as u16).is_null() {
                    v += 1;
                    continue;
                }
                k = wfmt!(src.as_mut_ptr(), 1024, "@{{{}?ms-resource://{}/resources/{}}}", Ws(*names), Ws(family.as_ptr()), Ws(key));
            } else if v == 1 {
                if *key == '/' as u16 {
                    v += 1;
                    continue;
                }
                k = wfmt!(src.as_mut_ptr(), 1024, "@{{{}?ms-resource://{}/{}}}", Ws(*names), Ws(family.as_ptr()), Ws(key));
            } else {
                k = wfmt!(src.as_mut_ptr(), 1024, "@{{{}?{}}}", Ws(*names), Ws(res));
            }
            v += 1;
            if k <= 0 || k >= 1024 {
                continue;
            }
            out[0] = 0;
            if succeeded(SHLoadIndirectString(src.as_ptr(), out.as_mut_ptr(), 512, null_mut())) && out[0] != 0 && !wstarts_with_i(out.as_ptr(), w!("ms-resource:")) {
                r = wdup(a, out.as_ptr(), -1);
            }
        }
    }
    free(names as *mut c_void);
    free(buf as *mut c_void);
    r
}

pub struct Command {
    pub cmd: u8,
    pub ru: &'static str,
    pub en: &'static str,
    pub kw: &'static str,
    pub glyph: u16,
    pub danger: bool,
}

pub static k_commands: [Command; 8] = [
    Command { cmd: CMD_LOCK, ru: "Заблокировать", en: "Lock", kw: "lock|lock screen|блокировка|заблокировать компьютер", glyph: 0xE72E, danger: false },
    Command { cmd: CMD_SLEEP, ru: "Спящий режим", en: "Sleep", kw: "sleep|suspend|сон|спящий режим", glyph: 0xE708, danger: true },
    Command { cmd: CMD_RESTART, ru: "Перезагрузка", en: "Restart", kw: "restart|reboot|перезагрузить|перезагрузка|рестарт", glyph: 0xE777, danger: true },
    Command {
        cmd: CMD_SHUTDOWN,
        ru: "Завершение работы",
        en: "Shut down",
        kw: "shutdown|shut down|power off|выключить|выключение|завершение работы",
        glyph: 0xE7E8,
        danger: true,
    },
    Command { cmd: CMD_SIGNOUT, ru: "Выйти из системы", en: "Sign out", kw: "sign out|log off|logout|выйти|выход из системы", glyph: 0xF3B1, danger: true },
    Command { cmd: CMD_RECYCLE, ru: "Корзина", en: "Recycle Bin", kw: "recycle bin|trash|корзина", glyph: 0xE74D, danger: false },
    Command {
        cmd: CMD_UNINSTALL,
        ru: "Установленные приложения",
        en: "Installed apps",
        kw: "installed programs|apps and features|установленные программы|приложения и возможности",
        glyph: 0xE71D,
        danger: false,
    },
    // Start's classic name for the same page: "удаление" / "uninstall" find this one
    Command {
        cmd: CMD_ADDREMOVE,
        ru: "Установка и удаление программ",
        en: "Add or remove programs",
        kw: "uninstall|remove program|programs and features|appwiz|удаление программ|удалить программу|программы и компоненты|деинсталляция",
        glyph: 0xECAA,
        danger: false,
    },
];

unsafe fn app_push_kw(a: *mut Arena, app: *mut App, s: *const WCHAR, len: i32) {
    let app = &mut *app;
    if app.kw_count >= APP_MAX_KW as i32 {
        return;
    }
    let j = app.kw_count as usize;
    app.kw_count += 1;
    app.kw[j] = arena_push(a, (len as usize + 1) * 2) as *mut WCHAR;
    norm_str(app.kw[j], s, len);
    app.kw_len[j] = len;
    app.kw_ws[j] = arena_push(a, len as usize + 1) as *mut u8;
    word_starts(s, len, app.kw_ws[j]);
}

// A hidden keyword, unless it is too short or repeats the name or another keyword.
unsafe fn app_add_kw(a: *mut Arena, app: *mut App, s: *const WCHAR, len: i32) {
    let mut norm = [0u16; 256];
    if len < 3 || len >= 256 {
        return;
    }
    norm_str(norm.as_mut_ptr(), s, len);
    let ap = &*app;
    if len == ap.len && wmem_eq(norm.as_ptr(), ap.norm, len) {
        return;
    }
    for i in 0..ap.kw_count as usize {
        if ap.kw_len[i] == len && wmem_eq(ap.kw[i], norm.as_ptr(), len) {
            return;
        }
    }
    app_push_kw(a, app, s, len);
}

unsafe fn app_add_hidden_keywords(a: *mut Arena, app: *mut App) {
    let ap = &*app;
    if !ap.path.is_null() {
        let file = PathFindFileNameW(ap.path);
        let ext = PathFindExtensionW(file);
        if _wcsicmp(ext, w!(".exe")) == 0 || _wcsicmp(ext, w!(".msc")) == 0 || _wcsicmp(ext, w!(".cpl")) == 0 {
            app_add_kw(a, app, file, ext.offset_from(file) as i32);
        }
    }
    let id = (*app).id;
    let bang = wcschr(id, '!' as u16);
    let us = wcschr(id, '_' as u16);
    let end = if !us.is_null() && !bang.is_null() && us < bang { us } else { bang };
    if !end.is_null() && end > id {
        let mut start = end;
        while start > id && *start.sub(1) != '.' as u16 {
            start = start.sub(1);
        }
        app_add_kw(a, app, start, end.offset_from(start) as i32);
    }
    let aliases: [(*const u16, *const u16); 6] = [
        (w!("windows.immersivecontrolpanel"), w!("Settings")),
        (w!("windows.immersivecontrolpanel"), w!("Параметры")),
        (w!("Microsoft.Windows.Explorer"), w!("Explorer")),
        (w!("Microsoft.Windows.ControlPanel"), w!("Control Panel")),
        (w!("Microsoft.Windows.Shell.RunDialog"), w!("Run")),
        (w!("Microsoft.WindowsTerminal"), w!("Terminal")),
    ];
    for (prefix, kw) in aliases {
        if wstarts_with_i(id, prefix) {
            app_add_kw(a, app, kw, wlen(kw));
        }
    }
}

// Sorted by name, then the commands. The signature tells whether anything changed since the last list.
pub unsafe fn applist_build(r: *mut RawApps) -> *mut AppList {
    let raw = (*r).items;
    let n = (*r).n;
    let l = calloc(1, size_of::<AppList>()) as *mut AppList;
    if l.is_null() {
        return null_mut();
    }
    let lr = &mut *l;
    arena_init(&mut lr.arena, 256 << 20);
    if n > 0 {
        qsort(raw as *mut c_void, n as usize, size_of::<RawApp>(), cmp_raw_name);
    }
    let ncmd = k_commands.len() as i32;
    lr.apps = arena_array::<App>(&mut lr.arena, n + ncmd);
    let mut sig = FNV_BASIS;
    for i in 0..n as usize {
        let app = lr.apps.add(lr.count as usize);
        lr.count += 1;
        let ri = &*raw.add(i);
        (*app).kind = APP_SHELL;
        (*app).name = wdup(&mut lr.arena, ri.name, -1);
        (*app).id = wdup(&mut lr.arena, ri.id, -1);
        (*app).path = if ri.path.is_null() { null_mut() } else { wdup(&mut lr.arena, ri.path, -1) };
        (*app).id_hash = hash_wstr_i((*app).id, -1);
        app_fill_match(&mut lr.arena, app);
        app_add_hidden_keywords(&mut lr.arena, app);
        sig = (sig ^ (*app).id_hash).wrapping_mul(FNV_PRIME);
        sig = (sig ^ hash_wstr((*app).name, -1)).wrapping_mul(FNV_PRIME);
    }
    lr.shell_count = lr.count;
    for c in &k_commands {
        let app = lr.apps.add(lr.count as usize);
        lr.count += 1;
        (*app).kind = APP_CMD;
        (*app).cmd = c.cmd;
        (*app).danger = c.danger;
        (*app).glyph = c.glyph;
        let nm = wstr(if g_lang_ru { c.ru } else { c.en });
        (*app).name = wdup(&mut lr.arena, nm.as_ptr(), -1);
        let mut idbuf = [0u16; 32];
        wfmt!(idbuf.as_mut_ptr(), 32, "cmd:{}", c.cmd as i32);
        (*app).id = wdup(&mut lr.arena, idbuf.as_ptr(), -1);
        (*app).id_hash = hash_wstr_i((*app).id, -1);
        app_fill_match(&mut lr.arena, app);
        for k in c.kw.split('|') {
            let kw = wstr(k);
            app_push_kw(&mut lr.arena, app, kw.as_ptr(), kw.len() as i32 - 1); // all of them: the name changes with the language
        }
    }
    lr.signature = sig;
    l
}

// The UI language changed: rename the commands of a loaded list.
pub unsafe fn apps_relocalize(l: *mut AppList) {
    if l.is_null() {
        return;
    }
    let l = &mut *l;
    for i in l.shell_count..l.count {
        let app = l.apps.add(i as usize);
        for c in &k_commands {
            if c.cmd == (*app).cmd {
                let nm = wstr(if g_lang_ru { c.ru } else { c.en });
                (*app).name = wdup(&mut l.arena, nm.as_ptr(), -1);
                app_fill_match(&mut l.arena, app);
            }
        }
    }
}

// name, id, path: one app per line (the cache, --dump-apps)
pub unsafe fn apps_write(b: &mut Buf, l: *mut AppList, n: i32) {
    for i in 0..n as usize {
        let a = &*(*l).apps.add(i);
        b.put_w(a.name);
        b.put(b"\t");
        b.put_w(a.id);
        b.put(b"\t");
        if !a.path.is_null() {
            b.put_w(a.path);
        }
        b.put(b"\n");
    }
}

unsafe fn apps_cache_save(l: *mut AppList) {
    let mut b = Buf::new();
    b.str("# MixLauncher apps v1\n");
    apps_write(&mut b, l, (*l).shell_count);
    buf_save_data(&mut b, w!("apps.cache"));
}

pub unsafe fn apps_cache_load() -> *mut AppList {
    let text = data_read(w!("apps.cache"));
    if text.is_null() {
        return null_mut();
    }
    let mut raw: RawApps = zeroed();
    raw_init(&mut raw);
    let mut cur = text;
    let mut name = vec![0u16; 1024];
    let mut id = vec![0u16; 1024];
    let mut path = vec![0u16; 2048];
    loop {
        let line = next_line(&mut cur);
        if line.is_null() {
            break;
        }
        let t1 = if *line == b'#' { null_mut() } else { strchr(line, b'\t' as i32) };
        let t2 = if t1.is_null() { null_mut() } else { strchr(t1.add(1), b'\t' as i32) };
        if t2.is_null() {
            continue;
        }
        *t1 = 0;
        *t2 = 0;
        utf8_to_w(line, -1, name.as_mut_ptr(), 1024);
        utf8_to_w(t1.add(1), -1, id.as_mut_ptr(), 1024);
        utf8_to_w(t2.add(1), -1, path.as_mut_ptr(), 2048);
        if name[0] != 0 && id[0] != 0 && !wstarts_with_i(name.as_ptr(), w!("ms-resource:")) && !raw_add(&mut raw, name.as_ptr(), id.as_ptr(), path.as_ptr()) {
            break;
        }
    }
    free(text as *mut c_void);
    let l = if raw.n != 0 { applist_build(&mut raw) } else { null_mut() };
    raw_free(&mut raw);
    l
}

static PKEY_Link_TargetParsingPath: windows::Win32::Foundation::PROPERTYKEY =
    windows::Win32::Foundation::PROPERTYKEY { fmtid: windows::core::GUID::from_u128(0xb9b4b3fc_2b51_4a42_b5d8_324146afcf25), pid: 2 };

// Everything in the Apps folder (Start's "All apps"): shortcuts, packaged apps, system entries.
pub unsafe fn apps_enumerate(unresolved: *mut i32) -> *mut AppList {
    if !unresolved.is_null() {
        *unresolved = 0;
    }
    let folder: wsh::IShellItem = match wsh::SHGetKnownFolderItem(&wsh::FOLDERID_AppsFolder, wsh::KF_FLAG_DEFAULT, None) {
        Ok(f) => f,
        Err(_) => {
            log_msg!("AppsFolder: SHGetKnownFolderItem failed");
            return null_mut();
        }
    };
    let en: wsh::IEnumShellItems = match folder.BindToHandler(None, &wsh::BHID_EnumItems) {
        Ok(e) => e,
        Err(e) => {
            log_msg!("AppsFolder: BindToHandler failed 0x{:08x}", e.code().0 as u32);
            return null_mut();
        }
    };
    drop(folder);
    let mut raw: RawApps = zeroed();
    raw_init(&mut raw);
    let mut ok = true;
    let mut path = vec![0u16; 2048];
    let mut norm = [0u16; 512];
    while ok {
        let mut items: [Option<wsh::IShellItem>; 1] = [None];
        let mut fetched = 0u32;
        if en.Next(&mut items, Some(&mut fetched)).is_err() || fetched != 1 {
            break;
        }
        let Some(it) = items[0].take() else { break };
        let name = it.GetDisplayName(wsh::SIGDN_NORMALDISPLAY).map(|p| p.0).unwrap_or(null_mut());
        let parse = it.GetDisplayName(wsh::SIGDN_PARENTRELATIVEPARSING).map(|p| p.0).unwrap_or(null_mut());
        let mut target: *mut u16 = null_mut();
        if let Ok(it2) = it.cast::<wsh::IShellItem2>() {
            target = it2.GetString(&PKEY_Link_TargetParsingPath).map(|p| p.0).unwrap_or(null_mut());
        }
        let mut disp: *const WCHAR = name;
        if !name.is_null() && !parse.is_null() && wstarts_with_i(name, w!("ms-resource:")) {
            disp = resolve_ms_resource(&mut raw.arena, name, parse);
            if disp.is_null() && !unresolved.is_null() {
                *unresolved += 1;
            }
        }
        if !disp.is_null() && !parse.is_null() && *disp != 0 && *parse != 0 {
            if !target.is_null() && *target != 0 {
                wcopy(path.as_mut_ptr(), 2048, target);
            } else {
                resolve_known_folder_path(parse, path.as_mut_ptr(), 2048);
            }
            if path[0] == ':' as u16 && path[1] == ':' as u16 {
                path[0] = 0; // a shell namespace item, no file
            }
            let nl = min(wlen(disp), 511);
            norm_str(norm.as_mut_ptr(), disp, nl);
            if !(g_cfg.hide_uninstallers && looks_like_noise(norm.as_ptr(), if path[0] != 0 { path.as_ptr() } else { null() })) {
                ok = raw_add(&mut raw, disp, parse, path.as_ptr());
            }
        }
        CoTaskMemFree(name as *const c_void);
        CoTaskMemFree(parse as *const c_void);
        CoTaskMemFree(target as *const c_void);
    }
    drop(en);
    let l = applist_build(&mut raw);
    raw_free(&mut raw);
    l
}

pub static mut g_index_event: HANDLE = null_mut();

pub unsafe fn apps_request_reindex() {
    if !g_index_event.is_null() {
        SetEvent(g_index_event);
    }
}

// Lists the apps again on request, when the Start menu folders change, and every 20 minutes. A list that
// differs from the last one is cached and posted to the window (WM_APP_APPS_READY).
unsafe extern "system" fn indexer_thread(param: *mut c_void) -> u32 {
    let mut last_sig = param as usize as u64;
    CoInitializeEx(null(), COINIT_APARTMENTTHREADED as u32);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    let mut retries = 0;
    let mut waits: [HANDLE; 3] = [null_mut(); 3];
    let mut nw = 0usize;
    waits[nw] = g_index_event;
    nw += 1;
    let dirs = [FOLDERID_Programs, FOLDERID_CommonPrograms];
    for d in &dirs {
        let mut p: PWSTR = null_mut();
        if succeeded(SHGetKnownFolderPath(d, 0, null_mut(), &mut p)) && !p.is_null() {
            let h = FindFirstChangeNotificationW(p, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
            if h != INVALID_HANDLE_VALUE {
                waits[nw] = h;
                nw += 1;
            }
        }
        CoTaskMemFree(p as *const c_void);
    }

    loop {
        let t0 = time_now();
        let mut unresolved = 0;
        let l = apps_enumerate(&mut unresolved);
        if !l.is_null() && (*l).signature != last_sig {
            last_sig = (*l).signature;
            apps_cache_save(l);
            log_msg!("apps indexed: {} in {} ms", (*l).shell_count, Ff((time_now() - t0) * 1000.0, 0));
            if PostMessageW(g_hwnd, WM_APP_APPS_READY, 0, l as LPARAM) == 0 {
                applist_free(l);
            }
        } else {
            applist_free(l);
        }
        // Unresolved names usually sort themselves out once the package update finishes.
        if unresolved != 0 {
            log_msg!("apps: {} unresolved names skipped", unresolved);
        }
        retries = if unresolved != 0 { retries + 1 } else { 0 };
        let wait = if unresolved != 0 && retries <= 10 { 30 * 1000 } else { 20 * 60 * 1000 };
        let r = WaitForMultipleObjects(nw as u32, waits.as_ptr(), FALSE, wait);
        if r >= WAIT_OBJECT_0 + 1 && r < WAIT_OBJECT_0 + nw as u32 {
            // a folder changed: wait for the burst of changes (an installer at work) to settle
            FindNextChangeNotification(waits[(r - WAIT_OBJECT_0) as usize]);
            loop {
                let r2 = WaitForMultipleObjects(nw as u32 - 1, waits.as_ptr().add(1), FALSE, 1500);
                if r2 == WAIT_TIMEOUT || r2 == WAIT_FAILED {
                    break;
                }
                FindNextChangeNotification(waits[1 + (r2 - WAIT_OBJECT_0) as usize]);
            }
        }
    }
}

pub unsafe fn apps_index_start(cached_signature: u64) {
    g_index_event = CreateEventW(null(), FALSE, FALSE, null());
    thread_start(indexer_thread, cached_signature as usize as *mut c_void);
}
