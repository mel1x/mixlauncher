// MixLauncher: a fast Start menu replacement for Windows 10 and 11. The entry point, the main window and the
// command line. (The earlier C version is in the old branch.)

#![windows_subsystem = "windows"]
#![deny(unreachable_patterns)]
#![allow(
    static_mut_refs,
    non_snake_case,
    non_upper_case_globals,
    non_camel_case_types,
    unsafe_op_in_unsafe_fn,
    dead_code,
    unused_imports,
    unused_variables,
    unused_mut,
    unused_unsafe,
    unused_assignments,
    suspicious_runtime_symbol_definitions,
    clippy::all
)]

pub use core::ffi::c_void;
pub use core::mem::{size_of, zeroed};
pub use core::ptr::{null, null_mut};
pub use windows_sys::Win32::Foundation::*;
pub use windows_sys::Win32::Globalization::*;
pub use windows_sys::Win32::Graphics::Dwm::*;
pub use windows_sys::Win32::Graphics::Gdi::*;
pub use windows_sys::Win32::Security::*;
pub use windows_sys::Win32::Storage::FileSystem::*;
pub use windows_sys::Win32::System::Com::*;
pub use windows_sys::Win32::System::DataExchange::*;
pub use windows_sys::Win32::System::Diagnostics::Debug::FatalAppExitW;
pub use windows_sys::Win32::System::Environment::*;
pub use windows_sys::Win32::System::IO::*;
pub use windows_sys::Win32::System::LibraryLoader::*;
pub use windows_sys::Win32::System::Memory::*;
pub use windows_sys::Win32::System::Performance::*;
pub use windows_sys::Win32::System::Power::*;
pub use windows_sys::Win32::System::Registry::*;
pub use windows_sys::Win32::System::Shutdown::*;
pub use windows_sys::Win32::System::SystemInformation::*;
pub use windows_sys::Win32::System::Threading::*;
pub use windows_sys::Win32::System::WindowsProgramming::GetUserNameW;
pub use windows_sys::Win32::UI::Accessibility::*;
pub use windows_sys::Win32::UI::Controls::{MARGINS, WM_MOUSELEAVE};
pub use windows_sys::Win32::UI::HiDpi::*;
pub use windows_sys::Win32::UI::Input::KeyboardAndMouse::*;
pub use windows_sys::Win32::UI::Input::*;
pub use windows_sys::Win32::UI::Shell::*;
pub use windows_sys::Win32::UI::WindowsAndMessaging::*;
pub use windows_sys::core::{BOOL, GUID, HRESULT, PCWSTR, PWSTR};
pub use windows_sys::{s, w};

pub use base::MAX_PATH;

#[macro_use]
pub mod base;
pub mod apps;
pub mod backdrop;
pub mod config;
pub mod ctxmenu;
pub mod donate;
pub mod elevation;
pub mod everything;
pub mod font;
pub mod history;
pub mod hotkey;
pub mod icons;
pub mod launch;
pub mod matcher;
pub mod media;
pub mod popmenu;
pub mod render;
#[macro_use]
pub mod settings;
pub mod settings_wnd;
pub mod start;
pub mod start10;
pub mod tagdlg;
pub mod tags;
pub mod taskbar;
pub mod theme;
pub mod tray;
pub mod ui;
pub mod ui_draw;
pub mod ui_input;
pub mod ui_window;
pub mod update;

pub use apps::*;
pub use backdrop::*;
pub use base::*;
pub use config::*;
pub use ctxmenu::*;
pub use donate::*;
pub use elevation::*;
pub use everything::*;
pub use font::*;
pub use history::*;
pub use hotkey::*;
pub use icons::*;
pub use launch::*;
pub use matcher::*;
pub use media::*;
pub use popmenu::*;
pub use render::*;
pub use settings::*;
pub use settings_wnd::*;
pub use start::*;
pub use start10::*;
pub use tagdlg::*;
pub use tags::*;
pub use taskbar::*;
pub use theme::*;
pub use tray::*;
pub use ui::*;
pub use ui_draw::*;
pub use ui_input::*;
pub use ui_window::*;
pub use update::*;

use core::sync::atomic::Ordering::SeqCst;

const WINDOW_CLASS: &str = "MixLauncherWindow";

unsafe extern "system" fn wndproc(h: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    if U.closing && ((WM_KEYFIRST..=WM_KEYLAST).contains(&msg) || (WM_MOUSEFIRST..=WM_MOUSELAST).contains(&msg)) {
        return 0;
    }
    match msg {
        WM_NCCALCSIZE => {
            if wp != 0 {
                return 0;
            }
        }
        WM_NCHITTEST => return HTCLIENT as LRESULT,
        WM_SETCURSOR => {
            if loword(lp as usize) == HTCLIENT && w10_on() {
                let c = w10_edge_cursor();
                if !c.is_null() {
                    SetCursor(LoadCursorW(null_mut(), c));
                    return TRUE as LRESULT;
                }
            }
        }
        // DWM draws acrylic only for an active frame: always report one (-1: no non-client repaint).
        WM_NCACTIVATE => return DefWindowProcW(h, msg, TRUE as WPARAM, -1),
        WM_ERASEBKGND => return 1,
        WM_PAINT => {
            let mut ps: PAINTSTRUCT = zeroed();
            BeginPaint(h, &mut ps);
            EndPaint(h, &ps);
            ui_invalidate();
            return 0;
        }
        WM_SIZE => {
            r_resize(loword(lp as usize) as i32, hiword(lp as usize) as i32);
            ui_invalidate();
            return 0;
        }
        WM_ACTIVATE => {
            if g_ktrace_on {
                let mut fgc = [0u16; 64];
                GetClassNameW(GetForegroundWindow(), fgc.as_mut_ptr(), 64);
                log_msg!("activate t={} state={} other={:p} fg={}", GetTickCount(), loword(wp), lp as *const c_void, Ws(fgc.as_ptr()));
            }
            if loword(wp) != WA_INACTIVE {
                prtsc_handoff_end();
            } else if U.visible && !U.menu_open && !g_pinned && K.prtsc_from.load(SeqCst) != h {
                ui_hide();
            }
            return 0;
        }
        WM_ACTIVATEAPP => {
            if wp == 0 && U.visible && !U.menu_open && !g_pinned && K.prtsc_from.load(SeqCst) != h {
                ui_hide();
            }
            return 0;
        }
        WM_MOUSEACTIVATE => return MA_ACTIVATE as LRESULT,
        WM_DPICHANGED => {
            let dpi = loword(wp);
            if dpi != U.dpi && U.visible {
                let sug = &*(lp as *const RECT);
                ui_metrics(dpi);
                icons_reset(ui_icon_px());
                let cx = (sug.left + sug.right) / 2;
                let cy = (sug.top + sug.bottom) / 2;
                SetWindowPos(h, HWND_TOPMOST, cx - U.W / 2, cy - U.H / 2, U.W, U.H, SWP_NOACTIVATE);
                bd_refresh(); // another monitor behind it
                rebuild_rows(true);
                snap_highlight();
                ui_invalidate();
            }
            return 0;
        }
        WM_KEYDOWN | WM_SYSKEYDOWN => {
            if (wp as u16 == VK_LWIN || wp as u16 == VK_RWIN) && hotkey_is_win_tap() {
                return 0;
            }
            if ui_keydown(wp) {
                return 0;
            }
        }
        WM_KEYUP | WM_SYSKEYUP => {
            if (wp as u16 == VK_LWIN || wp as u16 == VK_RWIN) && hotkey_is_win_tap() {
                return 0;
            }
        }
        WM_SYSCOMMAND => {
            if (wp & 0xFFF0) == SC_TASKLIST as usize && hotkey_is_win_tap() {
                return 0;
            }
        }
        WM_SYSCHAR => return 0,
        WM_CHAR => {
            ui_char(wp as u16);
            return 0;
        }
        WM_MOUSEMOVE => {
            ui_mouse_move(get_x_lparam(lp), get_y_lparam(lp));
            return 0;
        }
        WM_LBUTTONDOWN => {
            ui_mouse_down(get_x_lparam(lp), get_y_lparam(lp), false);
            return 0;
        }
        WM_LBUTTONDBLCLK => {
            ui_mouse_down(get_x_lparam(lp), get_y_lparam(lp), true);
            return 0;
        }
        WM_LBUTTONUP => {
            ui_mouse_up(get_x_lparam(lp), get_y_lparam(lp));
            return 0;
        }
        WM_RBUTTONUP => {
            let x = get_x_lparam(lp);
            let y = get_y_lparam(lp);
            if U.tg.open || cm_inside(x, y) {
                return 0;
            }
            if w10_on() && w10_rclick(x, y) {
                return 0;
            }
            let r = row_at(x, y);
            if row_selectable(r) {
                U.sel = r;
                U.armed = null_mut();
                ui_context_menu(x, y, false);
            } else {
                cm_close(true);
            }
            return 0;
        }
        WM_MOUSEWHEEL => {
            ui_wheel(hiword(wp) as u16 as i16 as i32);
            return 0;
        }
        WM_TIMER => {
            if wp == TIMER_REHOOK {
                KillTimer(h, TIMER_REHOOK);
                hook_rearm();
                if U.app_mode {
                    taskbar_button_remove();
                }
                return 0;
            }
            if wp == TIMER_PRTSC {
                prtsc_handoff_tick();
                return 0;
            }
            if wp == TIMER_UPDATE {
                upd_on_timer();
                return 0;
            }
            if wp == TIMER_CARET {
                U.caret_on = !U.caret_on;
                ui_invalidate();
            }
            return 0;
        }
        WM_SETTINGCHANGE => {
            if wp == SPI_SETCLIENTAREAANIMATION as usize {
                g_sys_anim = -1;
            }
            if lp != 0 && wcscmp(lp as *const u16, w!("ImmersiveColorSet")) == 0 {
                theme_update();
                ui_invalidate();
                tb_theme_changed();
                media_theme_changed();
            }
            return 0;
        }
        WM_DISPLAYCHANGE => {
            media_sync(); // the taskbar is another size now
        }
        WM_POWERBROADCAST => {
            if wp == PBT_APMRESUMEAUTOMATIC as usize || wp == PBT_APMRESUMESUSPEND as usize {
                hook_reinstall();
            }
            return TRUE as LRESULT;
        }
        WM_QUERYENDSESSION => {
            history_save();
            return TRUE as LRESULT;
        }
        WM_ENDSESSION => {
            if wp != 0 {
                history_save();
            }
            return 0;
        }
        WM_CLOSE => {
            ui_hide();
            return 0;
        }
        WM_APP_TOGGLE => {
            if g_ktrace_on {
                log_msg!("toggle t={}: visible={} closing={} fg={:p}", GetTickCount(), U.visible as i32, U.closing as i32, GetForegroundWindow());
            }
            ui_toggle();
            return 0;
        }
        WM_APP_SHOW => {
            ui_show();
            return 0;
        }
        WM_APP_SEARCH => {
            ui_search_clicked();
            return 0;
        }
        WM_APP_APPS_READY => {
            on_apps_ready(lp as *mut AppList);
            return 0;
        }
        WM_APP_ICONS_READY => {
            if icons_process_results() {
                ui_invalidate();
            }
            return 0;
        }
        WM_APP_FILES_READY => {
            on_files_ready(lp as *mut FileResults);
            return 0;
        }
        WM_APP_SETTINGS => {
            settings_open();
            return 0;
        }
        WM_APP_TASKBAR => {
            tb_sync();
            return 0;
        }
        WM_APP_MEDIA => {
            media_sync();
            return 0;
        }
        WM_APP_AUTOSTART => {
            settings_autostart_done(wp as i32, lp != 0);
            return 0;
        }
        WM_APP_UPDATE => {
            upd_on_message();
            ui_invalidate();
            settings_invalidate();
            return 0;
        }
        WM_APP_PRTSC => {
            prtsc_handoff();
            return 0;
        }
        WM_APP_SCREENSHOT => {
            bd_screenshot();
            return 0;
        }
        WM_APP_CLICK_OUTSIDE => {
            if U.visible && !U.closing && !U.menu_open && !g_pinned {
                U.no_focus_restore = true;
                ui_hide();
            }
            return 0;
        }
        WM_APP_LAUNCH_FAILED => {
            let key = lp as *mut WCHAR;
            if !key.is_null() {
                history_forget(wp as u8, key);
                free(key as *mut c_void);
            }
            return 0;
        }
        WM_APP_TRAY => {
            if lp as u32 == WM_LBUTTONUP {
                ui_toggle();
            } else if lp as u32 == WM_RBUTTONUP || lp as u32 == WM_CONTEXTMENU {
                tray_menu();
            }
            return 0;
        }
        WM_APP_EXIT => {
            DestroyWindow(h);
            return 0;
        }
        WM_DESTROY => {
            if g_ktrace_on {
                ktrace_dump();
            }
            tray_remove();
            history_save();
            if g_ev_own_allowed {
                ev_own_stop(0);
            }
            PostQuitMessage(0);
            return 0;
        }
        _ => {
            if msg == g_wm_taskbar_created && msg != 0 {
                tray_add();
                tb_explorer_restarted();
                media_explorer_restarted();
                return 0;
            }
        }
    }
    DefWindowProcW(h, msg, wp, lp)
}

// ---- diagnostics: --dump-apps, --test-everything, --bench

unsafe extern "system" fn bench_wndproc(h: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    DefWindowProcW(h, msg, wp, lp)
}

// The hot paths with the user's own data, timed: the app cache, the matcher, a keystroke (search and rows), a
// frame built on the CPU. Also the training run of the PGO build (pgo.bat).
unsafe fn cmd_bench(reps: i32, out: *const WCHAR) -> i32 {
    let reps = max(reps, 1);
    config_load();
    g_cfg.reduced_motion = true; // no cascade: every frame draws the same
    lang_apply();
    history_load();
    tags_load();
    arena_init(&raw mut U.recent_arena, 1 << 20);
    CoInitializeEx(null(), COINIT_APARTMENTTHREADED as _);
    let mut b = Buf::new();

    let t0 = time_now();
    for _ in 0..reps {
        applist_free(apps_cache_load());
    }
    let t_cache = (time_now() - t0) / reps as f64;
    let l = apps_cache_load();
    if l.is_null() {
        b.str("no apps.cache\n");
        buf_save(&mut b, out);
        return 1;
    }
    window_class(w!("MixLauncherBench"), bench_wndproc, 0, null_mut(), null_mut());
    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP,
        w!("MixLauncherBench"),
        w!(""),
        WS_POPUP,
        0,
        0,
        10,
        10,
        null_mut(),
        null_mut(),
        GetModuleHandleW(null()),
        null(),
    );
    theme_update();
    ui_metrics(96);
    on_apps_ready(l);

    let words = ["chrome", "vsc", "code", "ghbdtn", "settings", "notepad", "word", "terminal", "photoshop", "zzzz", "браузер", "control panel", "steam", "calc", "paint"];
    let mut qs = [[0u16; 64]; 15];
    let mut qn = [[0u16; 64]; 15];
    let mut ql = [0i32; 15];
    for k in 0..words.len() {
        for (j, c) in words[k].encode_utf16().enumerate() {
            qs[k][j] = c;
        }
        ql[k] = wlen(qs[k].as_ptr());
        norm_str(qn[k].as_mut_ptr(), qs[k].as_ptr(), ql[k]);
    }
    let la = &*l;
    let mut sink = 0i64;
    let mut calls = 0i64;
    let t0 = time_now();
    for _ in 0..reps * 10 {
        for k in 0..words.len() {
            for i in 0..la.count as usize {
                let a = &*la.apps.add(i);
                sink += core::hint::black_box(score_query(a.norm, a.len, a.ws, qn[k].as_ptr(), ql[k])) as i64;
                calls += 1;
            }
        }
    }
    let t_score = (time_now() - t0) / calls as f64;

    let mut keys = 0i64;
    let t0 = time_now();
    for _ in 0..reps {
        for k in 0..words.len() {
            for n in 1..=ql[k] {
                edit_replace(0, U.qlen, qs[k].as_ptr(), n);
                keys += 1;
            }
        }
    }
    let t_key = (time_now() - t0) / keys as f64;

    if !font_init() || !r_init(g_hwnd, U.W, U.H) {
        b.str("no renderer\n");
        buf_save(&mut b, out);
        return 1;
    }
    font_reset_atlas();
    icons_reset(ui_icon_px());
    edit_replace(0, U.qlen, w!(""), 0);
    r_draw(ui_draw);
    let t0 = time_now();
    for _ in 0..reps * 5 {
        r_draw(ui_draw);
    }
    let t_home = (time_now() - t0) / (reps * 5) as f64;
    edit_replace(0, U.qlen, w!("c"), 1);
    r_draw(ui_draw);
    let t0 = time_now();
    for _ in 0..reps * 5 {
        r_draw(ui_draw);
    }
    let t_query = (time_now() - t0) / (reps * 5) as f64;

    buf_printf!(b, "apps={} reps={}\n", la.count, reps);
    buf_printf!(b, "cache_load_us={}\n", Ff(t_cache * 1e6, 2));
    buf_printf!(b, "score_ns={}\n", Ff(t_score * 1e9, 2));
    buf_printf!(b, "keystroke_us={}\n", Ff(t_key * 1e6, 2));
    buf_printf!(b, "frame_home_us={}\n", Ff(t_home * 1e6, 2));
    buf_printf!(b, "frame_query_us={}\n", Ff(t_query * 1e6, 2));
    buf_printf!(b, "sink={}\n", sink);
    buf_save(&mut b, out);
    0
}

unsafe fn cmd_dump_apps(out: *const WCHAR) -> i32 {
    CoInitializeEx(null(), COINIT_APARTMENTTHREADED as _);
    let t0 = time_now();
    let l = apps_enumerate(null_mut());
    let mut b = Buf::new();
    buf_printf!(b, "enumerated in {} ms, {} apps\n", Ff((time_now() - t0) * 1000.0, 1), if l.is_null() { -1 } else { (*l).shell_count });
    if !l.is_null() {
        apps_write(&mut b, l, (*l).count);
    }
    buf_save(&mut b, out);
    0
}

static mut g_test_result: *mut FileResults = null_mut();
static mut g_test_pages: [*mut FileResults; 4] = [null_mut(); 4];
static mut g_test_page_count: usize = 0;

unsafe extern "system" fn test_wndproc(h: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    if msg == WM_APP_FILES_READY {
        let r = lp as *mut FileResults;
        if !r.is_null() && (*r).page {
            g_test_pages[g_test_page_count] = r;
            g_test_page_count += 1;
        } else {
            g_test_result = r;
        }
        if !r.is_null() && (*r).status == EV_OK && (*r).fetched < (*r).total && g_test_page_count < 4 {
            ev_request_more((*r).id);
        } else {
            PostQuitMessage(0);
        }
        return 0;
    }
    DefWindowProcW(h, msg, wp, lp)
}

unsafe fn cmd_test_everything(query: *const WCHAR, out: *const WCHAR) -> i32 {
    g_hwnd = message_window(w!("MixLauncherTest"), test_wndproc);
    g_ev_own_allowed = g_elevated;
    ev_start();
    let t0 = time_now();
    ev_request(1, query);
    let mut msg: MSG = zeroed();
    while GetMessageW(&mut msg, null_mut(), 0, 0) > 0 {
        DispatchMessageW(&msg);
    }
    let r = g_test_result;
    let t1 = time_now();
    let mut b = Buf::new();
    if r.is_null() {
        b.str("no results\n");
    } else {
        let r = &*r;
        buf_printf!(b, "status {}, total {}, fetched {}, count {}, {} ms incl. pages\n", r.status, r.total, r.fetched, r.count, Ff((t1 - t0) * 1000.0, 1));
        for i in 0..min(r.count, 60) as usize {
            let it = &*r.items.add(i);
            buf_printf!(b, "{:>8}  {}  ", Ff(it.score as f64, 0), if (it.flags & FI_FOLDER) != 0 { "D" } else { "F" });
            b.put_w(it.full);
            b.put(b"\n");
        }
        for k in 0..g_test_page_count {
            let pg = &*g_test_pages[k];
            buf_printf!(b, "page {}: status {}, count {}, fetched {} of {}\n", k + 1, pg.status, pg.count, pg.fetched, pg.total);
            for i in 0..min(pg.count, 3) as usize {
                let it = &*pg.items.add(i);
                buf_printf!(b, "  {:>8}  ", Ff(it.score as f64, 0));
                b.put_w(it.full);
                b.put(b"\n");
            }
        }
    }
    buf_save(&mut b, out);
    0
}

// Installer hooks: close every launcher and our Everything and wait, so their files can be replaced.
unsafe fn shutdown_running() {
    let cls = wstr(WINDOW_CLASS);
    let mut h: HWND = null_mut();
    loop {
        h = FindWindowExW(null_mut(), h, cls.as_ptr(), null());
        if h.is_null() {
            break;
        }
        let mut pid = 0u32;
        GetWindowThreadProcessId(h, &mut pid);
        let p = OpenProcess(PROCESS_SYNCHRONIZE, FALSE, pid);
        PostMessageW(h, WM_APP_EXIT, 0, 0);
        if !p.is_null() {
            WaitForSingleObject(p, 5000);
            CloseHandle(p);
        }
    }
    ev_own_stop(5000);
}

// "--style" -> the style field. --lang is short for --language.
unsafe fn option_field(arg: *const WCHAR) -> i32 {
    if wcsncmp(arg, w!("--"), 2) != 0 {
        return CF_NONE;
    }
    let mut key = [0u8; 64];
    w_to_utf8(arg.add(2), -1, key.as_mut_ptr(), 64);
    if strcmp(key.as_ptr(), b"lang\0".as_ptr()) == 0 {
        return cfg_find(b"language\0".as_ptr());
    }
    cfg_find(key.as_ptr())
}

unsafe fn init_paths() {
    GetModuleFileNameW(null_mut(), g_exe_path.as_mut_ptr(), MAX_PATH as u32);
    let mut base = [0u16; MAX_PATH];
    let n = GetEnvironmentVariableW(w!("LOCALAPPDATA"), base.as_mut_ptr(), MAX_PATH as u32);
    if n == 0 || n as usize >= MAX_PATH {
        SHGetFolderPathW(null_mut(), CSIDL_LOCAL_APPDATA as i32, null_mut(), 0, base.as_mut_ptr());
    }
    wfmt!(g_data_dir.as_mut_ptr(), MAX_PATH, "{}\\MixLauncher", Ws(base.as_ptr()));
    CreateDirectoryW(g_data_dir.as_ptr(), null());
}

unsafe fn eq(a: *const WCHAR, b: *const WCHAR) -> bool {
    wcscmp(a, b) == 0
}

static mut RECOVER_AT: f64 = 0.0;

unsafe fn win_main() -> i32 {
    time_init();
    opt_out_of_throttling(null_mut());
    g_lang_ru = primarylangid(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
    settings_static_init();
    taskbar_static_init();
    media_static_init();
    init_paths();
    elevation_init();

    type PfnSetDpiCtx = unsafe extern "system" fn(HANDLE) -> BOOL;
    let set_dpi: Option<PfnSetDpiCtx> = core::mem::transmute(GetProcAddress(GetModuleHandleW(w!("user32.dll")), s!("SetProcessDpiAwarenessContext")));
    if let Some(f) = set_dpi {
        f(-4isize as HANDLE /*PER_MONITOR_AWARE_V2*/);
    }

    let mut argc = 0i32;
    let argv = CommandLineToArgvW(GetCommandLineW(), &mut argc);
    let arg = |i: i32| -> *const WCHAR { *argv.add(i as usize) };
    let mut opt_show = false;
    let mut opt_settings = false;
    let mut opt_query: *const WCHAR = null();
    let mut opt_update_as: *const WCHAR = null();
    let mut opt_after_update = 0u32;
    let mut overrides = [0i32; 16];
    let mut noverrides = 0usize; // "--<config key> <value>" (--lang for language): this run only
    let cls = wstr(WINDOW_CLASS);
    let mut i = 1;
    while i < argc {
        let a = arg(i);
        if eq(a, w!("--exit")) {
            let other = FindWindowW(cls.as_ptr(), null());
            if !other.is_null() {
                PostMessageW(other, WM_APP_EXIT, 0, 0);
            }
            return 0;
        } else if eq(a, w!("--shutdown")) {
            shutdown_running();
            return 0;
        } else if eq(a, w!("--uninstall")) {
            shutdown_running();
            autostart_set(false);
            run_key_set(false);
            return 0;
        } else if eq(a, w!("--autostart")) && i + 1 < argc {
            return if autostart_set(eq(arg(i + 1), w!("on"))) { 0 } else { 1 };
        } else if eq(a, w!("--test-unelevated")) && i + 1 < argc {
            CoInitializeEx(null(), COINIT_APARTMENTTHREADED as _);
            let args = wstr(&format!("/c whoami /groups > \"{}\"", Ws(arg(i + 1))));
            return if shell_exec_unelevated_ex(w!("cmd.exe"), args.as_ptr(), null(), null(), SW_HIDE) { 0 } else { 1 };
        } else if eq(a, w!("--write-icon")) && i + 1 < argc {
            CoInitializeEx(null(), COINIT_APARTMENTTHREADED as _);
            return if app_icon_write_ico(arg(i + 1)) { 0 } else { 1 };
        } else if eq(a, w!("--media-dump")) && i + 1 < argc {
            config_load();
            return media_dump(
                arg(i + 1),
                if i + 2 < argc { _wtoi(arg(i + 2)) } else { 96 },
                i + 3 < argc && eq(arg(i + 3), w!("light")),
                if i + 4 < argc { arg(i + 4) } else { null() },
            );
        } else if eq(a, w!("--dump-apps")) && i + 1 < argc {
            config_load();
            return cmd_dump_apps(arg(i + 1));
        } else if eq(a, w!("--test-everything")) && i + 2 < argc {
            config_load();
            return cmd_test_everything(arg(i + 1), arg(i + 2));
        } else if eq(a, w!("--bench")) && i + 2 < argc {
            return cmd_bench(_wtoi(arg(i + 1)), arg(i + 2));
        } else if eq(a, w!("--other-monitor")) {
            g_other_monitor = true;
            g_ktrace_on = true;
        } else if eq(a, w!("--after-update")) && i + 1 < argc {
            i += 1;
            opt_after_update = _wtoi(arg(i)) as u32;
        } else if eq(a, w!("--update-as")) && i + 1 < argc {
            i += 1;
            opt_update_as = arg(i); // testing: pretend to be this version
        } else if eq(a, w!("--update-dry")) {
            UPD.dry_run = true; // testing: download the update but do not install it
        } else if eq(a, w!("--pin")) {
            g_pinned = true;
        } else if eq(a, w!("--settings")) {
            opt_settings = true;
        } else if eq(a, w!("--show")) {
            opt_show = true;
        } else if eq(a, w!("--query")) && i + 1 < argc {
            i += 1;
            opt_query = arg(i);
            opt_show = true;
        } else if option_field(a) != 0 && i + 1 < argc && noverrides < overrides.len() {
            overrides[noverrides] = i;
            noverrides += 1;
            i += 1;
        }
        i += 1;
    }

    if opt_after_update != 0 {
        // A portable update: the old instance is exiting and still holds the single-instance mutex.
        let old = OpenProcess(PROCESS_SYNCHRONIZE, FALSE, opt_after_update);
        if !old.is_null() {
            WaitForSingleObject(old, 15000);
            CloseHandle(old);
        }
    }
    let mutex = if g_pinned { INVALID_HANDLE_VALUE } else { CreateMutexW(null(), TRUE, w!("MixLauncher.SingleInstance.7f3c1e2a")) };
    if !g_pinned && (GetLastError() == ERROR_ALREADY_EXISTS || mutex.is_null()) {
        // no mutex: owned by an elevated instance
        let other = FindWindowW(cls.as_ptr(), null());
        if !other.is_null() {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(other, if opt_settings { WM_APP_SETTINGS } else { WM_APP_SHOW }, 0, 0);
        }
        return 0;
    }

    config_load();
    for k in 0..noverrides {
        let mut v = [0u8; 1024];
        w_to_utf8(arg(overrides[k] + 1), -1, v.as_mut_ptr(), 1024);
        cfg_parse(option_field(arg(overrides[k])), v.as_ptr());
    }
    lang_apply();
    history_load();
    tags_load();
    arena_init(&raw mut U.recent_arena, 1 << 20);
    CoInitializeEx(null(), (COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) as _);

    // Window icons (tray, taskbar, Alt+Tab) are the logo without its tile; only the exe icon keeps it.
    g_icon_big = app_icon_create(GetSystemMetrics(SM_CXICON), false);
    g_icon_small = app_icon_create(GetSystemMetrics(SM_CXSMICON), false);

    window_class(cls.as_ptr(), wndproc, CS_DBLCLKS, g_icon_big, g_icon_small);

    let origin = POINT { x: 0, y: 0 };
    ui_metrics(monitor_dpi(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY)));
    let inst = GetModuleHandleW(null());
    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED,
        cls.as_ptr(),
        w!("MixLauncher"),
        WS_POPUP,
        0,
        0,
        U.W,
        U.H,
        null_mut(),
        null_mut(),
        inst,
        null(),
    );
    if g_hwnd.is_null() {
        return 1;
    }
    theme_update();
    U.backdrop_ok = backdrop_apply(g_hwnd);
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    dwm_set(g_hwnd, DWA_NO_TRANSITIONS, TRUE as u32);

    if !font_init() {
        MessageBoxW(null_mut(), tr!("Не удалось инициализировать DirectWrite.", "Failed to initialize DirectWrite."), w!("MixLauncher"), MB_ICONERROR);
        return 1;
    }
    if !r_init(g_hwnd, U.W, U.H) {
        MessageBoxW(null_mut(), tr!("Не удалось инициализировать Direct3D 11.", "Failed to initialize Direct3D 11."), w!("MixLauncher"), MB_ICONERROR);
        return 1;
    }
    font_reset_atlas();
    icons_start(ui_icon_px());

    let cached = apps_cache_load();
    apps_index_start(if cached.is_null() { 0 } else { (*cached).signature });
    if !cached.is_null() {
        on_apps_ready(cached);
    }
    U.last_reindex = time_now();

    g_ev_own_allowed = g_elevated && !g_pinned;
    ev_start();
    launch_start();
    K.prtsc_handoff = g_elevated;
    if !g_pinned {
        hook_start(g_cfg.hk);
    }

    g_wm_taskbar_created = RegisterWindowMessageW(w!("TaskbarCreated"));
    ChangeWindowMessageFilterEx(g_hwnd, g_wm_taskbar_created, MSGFLT_ALLOW, null_mut());
    uipi_allow(g_hwnd, WM_APP_TRAY);
    uipi_allow(g_hwnd, WM_APP_EXIT);
    uipi_allow(g_hwnd, WM_APP_SHOW);
    uipi_allow(g_hwnd, WM_APP_SETTINGS);
    window_icons(g_hwnd); // on the taskbar over a fullscreen app (app mode)
    if !g_pinned {
        tray_add();
    }
    if g_cfg.taskbar_button && !g_pinned {
        tb_set_enabled(true);
    }
    if g_cfg.taskbar_search && !g_pinned {
        tb_set_search(true);
    }
    if g_cfg.media_widget && !g_pinned {
        media_set_enabled(true);
    }
    elevation_housekeeping_start();
    upd_init(opt_update_as);
    upd_schedule(15000);
    if !opt_update_as.is_null() {
        upd_check();
    }

    if opt_settings {
        settings_open();
    }
    if opt_show {
        ui_show();
        if !opt_query.is_null() {
            edit_replace(0, U.qlen, opt_query, wlen(opt_query));
        }
    }
    LocalFree(argv as *mut c_void);

    // Messages first, then a frame for every window that needs one; idle until the next message.
    'outer: loop {
        let mut msg: MSG = zeroed();
        while PeekMessageW(&mut msg, null_mut(), 0, 0, PM_REMOVE) != 0 {
            if msg.message == WM_QUIT {
                break 'outer;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if U.visible {
            bd_poll();
        }
        // The GPU was reset while only the settings or a menu show: nothing else would bring the device back, and
        // their frames, never drawn, would spin this loop. No device yet: the frames are dropped, a try a second.
        if !R.ok && (settings_needs_render() || pm_needs_render()) {
            let now = time_now();
            if now < RECOVER_AT || !renderer_recover() {
                if now >= RECOVER_AT {
                    RECOVER_AT = now + 1.0;
                }
                SW.dirty = false;
                SW.animating = false;
                PM.dirty = false;
                PM.animating = false;
            }
        }
        let mut rendered = false;
        let mut anim = false;
        if settings_needs_render() {
            settings_render();
            rendered = true;
            anim |= SW.animating;
        }
        if pm_needs_render() {
            pm_render();
            rendered = true;
            anim |= PM.animating;
        }
        if U.visible && (U.dirty || U.animating) {
            ui_render();
            rendered = true;
            anim |= U.animating;
        }
        if anim {
            DwmFlush(); // composition swap chains give no back-pressure
        }
        if rendered {
            continue;
        }
        // a live backdrop waits on the screen behind instead, a few ms at a time, input between
        if U.visible && hiword(GetQueueStatus(QS_ALLINPUT) as usize) == 0 && bd_wait(4) {
            continue;
        }
        MsgWaitForMultipleObjectsEx(0, null(), INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    history_save();
    ExitProcess(0);
}

fn main() {
    unsafe {
        // a -v3 or -v2 build copied to an older processor: say so instead of crashing on the first new instruction
        if (cfg!(target_feature = "avx2") && !cpu_v3()) || (cfg!(target_feature = "sse4.2") && !cpu_v2()) {
            g_lang_ru = primarylangid(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
            MessageBoxW(
                null_mut(),
                tr!(
                    "Эта сборка MixLauncher не для этого процессора. Скачайте MixLauncher-portable.exe.",
                    "This MixLauncher build is not for this processor. Download MixLauncher-portable.exe instead."
                ),
                w!("MixLauncher"),
                MB_ICONERROR,
            );
            ExitProcess(1);
        }
        let code = win_main();
        // as returning from WinMain: the C runtime's exit, so atexit handlers run (the PGO profile is written there;
        // std::process::exit goes straight to ExitProcess)
        unsafe extern "C" {
            fn exit(code: i32) -> !;
        }
        exit(code);
    }
}
