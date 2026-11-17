// The launcher window: where it opens, showing and hiding it (fade, zoom, the slide from the taskbar), the
// PrtSc hand-off, and acting on the selected row.

use crate::*;
use core::sync::atomic::Ordering::SeqCst;
use windows::Win32::UI::Shell as wsh;

pub unsafe fn force_foreground(h: HWND) {
    if GetForegroundWindow() == h {
        return;
    }
    BringWindowToTop(h);
    SetForegroundWindow(h);
    if GetForegroundWindow() != h {
        let fg = GetForegroundWindow();
        let fg_tid = if fg.is_null() { 0 } else { GetWindowThreadProcessId(fg, null_mut()) };
        let me = GetCurrentThreadId();
        if fg_tid != 0 && fg_tid != me && AttachThreadInput(me, fg_tid, TRUE) != 0 {
            BringWindowToTop(h);
            SetForegroundWindow(h);
            AttachThreadInput(me, fg_tid, FALSE);
        }
    }
    if GetForegroundWindow() != h {
        // Being the source of the last input event grants foreground rights.
        let mut inp: [INPUT; 2] = zeroed();
        inp[0].r#type = INPUT_KEYBOARD;
        inp[1].r#type = INPUT_KEYBOARD;
        inp[0].Anonymous.ki.wVk = 0xE8;
        inp[1].Anonymous.ki.wVk = 0xE8;
        inp[1].Anonymous.ki.dwFlags = KEYEVENTF_KEYUP;
        for i in inp.iter_mut() {
            i.Anonymous.ki.dwExtraInfo = ML_INJECT_MAGIC;
        }
        SendInput(2, inp.as_ptr(), size_of::<INPUT>() as i32);
        SetForegroundWindow(h);
    }
    SetFocus(h);
}

// Sizes the window for its monitor and puts it in place: centered, or at the taskbar ready to slide out.
pub unsafe fn ui_place_window() {
    let mon = open_monitor();
    let mut mi: MONITORINFO = zeroed();
    mi.cbSize = size_of::<MONITORINFO>() as u32;
    GetMonitorInfoW(mon, &mut mi);
    let dpi = monitor_dpi(mon);
    if dpi != U.dpi {
        ui_metrics(dpi);
        icons_reset(ui_icon_px());
    }
    let wa = mi.rcWork;
    let mr = mi.rcMonitor;
    if w10_on() {
        // a resized Windows 10 Start still fits the screen
        U.w10.wide = mr.right - mr.left >= 2560;
        w10_metrics(); // the search pane's width or the resized one, for the query it opens with
        U.H = min(U.H, wa.bottom - wa.top);
        U.W = min(U.W, wa.right - wa.left);
        U.list_y1 = U.H as f32;
    }
    let mut x = wa.left + ((wa.right - wa.left) - U.W) / 2;
    let mut y = wa.top + (((wa.bottom - wa.top) - U.H) as f32 * 0.28) as i32;
    U.mon = mon;
    let pos = launcher_position();
    U.slide = pos != POS_CENTER;
    U.slide_dy = 0;
    if U.slide {
        // Windows 10 Start sits right against the taskbar and the screen edge; the capsule has its margin already
        let m = if w10_on() || cap_on() { 0 } else { SR(12.0) as i32 };
        let tray = taskbar_on_monitor(mon);
        let mut tr_: RECT = zeroed();
        let top = !tray.is_null() && GetWindowRect(tray, &mut tr_) != 0 && tr_.bottom - mr.top < mr.bottom - tr_.top; // taskbar at the top: slide down from it
        if pos == POS_BOTTOM_LEFT {
            x = wa.left + m;
        }
        y = if top { wa.top + m } else { wa.bottom - U.H - m };
        // Start fully behind the taskbar edge, but never reach into a monitor beyond this one.
        U.slide_dy = if top { wa.top - (y + U.H) } else { wa.bottom - y };
        let past = if top {
            RECT { left: x, top: mr.top - U.H, right: x + U.W, bottom: mr.top }
        } else {
            RECT { left: x, top: mr.bottom, right: x + U.W, bottom: mr.bottom + U.H }
        };
        if !MonitorFromRect(&past, MONITOR_DEFAULTTONULL).is_null() {
            U.slide_dy = if top { mr.top - y } else { mr.bottom - (y + U.H) };
        }
    }
    U.win_x = x;
    U.win_y = y;
    U.slide_y = if anims_enabled() { U.slide_dy } else { 0 };
    U.slide_off = U.slide_y as f32;
    U.slide_v = 0.0;
    SetWindowPos(g_hwnd, if U.app_mode { HWND_NOTOPMOST } else { HWND_TOPMOST }, x, y + U.slide_y, U.W, U.H, SWP_NOACTIVATE);
    r_resize(U.W, U.H);
}

pub const ROW_STAGGER_MAX: i32 = 8;

pub unsafe fn slide_omega() -> f32 {
    anim_omega(max(g_cfg.anim_open_ms, g_cfg.anim_close_ms))
}

fn vis_curve(v: f32, opening: bool) -> f32 {
    let v = clamp(v, 0.0, 1.0);
    if opening {
        return ease_out_quart(v);
    }
    v * v
}

// The slide is a spring: U.vis follows its position, so no curve on top.
pub unsafe fn slide_spring() -> bool {
    U.slide && U.slide_dy != 0
}

pub unsafe fn vis_eased() -> f32 {
    if slide_spring() { clamp(U.vis, 0.0, 1.0) } else { vis_curve(U.vis, U.vis_target > 0.5) }
}

unsafe fn vis_set_direction(opening: bool) {
    if slide_spring() {
        // keeps its position and velocity and just turns around
        U.vis_target = if opening { 1.0 } else { 0.0 };
        return;
    }
    let a = vis_eased();
    U.vis_target = if opening { 1.0 } else { 0.0 };
    U.vis = if opening { 1.0 - (1.0 - a).sqrt().sqrt() } else { a.sqrt() };
}

pub unsafe fn row_in(k: i32, now: f64) -> f32 {
    let dur = anim_sec(g_cfg.anim_row_ms);
    if g_cfg.anim_cascade == 0 || dur <= 0.0 {
        return 1.0;
    }
    let t = (now - U.open_time) as f32 - anim_sec(g_cfg.anim_stagger_ms) * min(k, ROW_STAGGER_MAX) as f32;
    ease_out_cubic(t / dur)
}

pub unsafe fn rows_cascading(now: f64) -> bool {
    g_cfg.anim_cascade != 0 && now - U.open_time < (anim_sec(g_cfg.anim_row_ms) + anim_sec(g_cfg.anim_stagger_ms) * ROW_STAGGER_MAX as f32) as f64
}

pub unsafe fn apply_window_alpha() {
    let mut v = vis_eased();
    if U.slide {
        v = min(v * 2.0, 1.0); // the slide carries the motion, the fade only softens its start
    }
    let a = (v * 255.0 + 0.5) as i32;
    if a != U.alpha {
        U.alpha = a;
        SetLayeredWindowAttributes(g_hwnd, 0, a as u8, LWA_ALPHA);
    }
}

pub unsafe fn apply_window_slide() {
    if !U.slide {
        return;
    }
    let y = U.slide_off.round() as i32;
    if y == U.slide_y {
        return;
    }
    U.slide_y = y;
    SetWindowPos(g_hwnd, null_mut(), U.win_x, U.win_y + y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

unsafe fn set_click_through(on: bool) {
    let ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    let nx = if on { ex | WS_EX_TRANSPARENT as isize } else { ex & !(WS_EX_TRANSPARENT as isize) };
    if nx != ex {
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, nx);
    }
}

// Never hand focus to these: activating the taskbar while Win is down opens Start.
unsafe fn is_shell_window(h: HWND) -> bool {
    let mut cls = [0u16; 64];
    if h.is_null() || GetClassNameW(h, cls.as_mut_ptr(), 64) == 0 {
        return true;
    }
    let shell = [
        w!("Shell_TrayWnd"), w!("Shell_SecondaryTrayWnd"), w!("Progman"), w!("WorkerW"), w!("Windows.UI.Core.CoreWindow"), w!("XamlExplorerHostIslandWindow"),
        w!("TopLevelWindowForOverflowXamlIsland"), w!("NotifyIconOverflowWindow"),
    ];
    for s in shell {
        if wcscmp(cls.as_ptr(), s) == 0 {
            return true;
        }
    }
    false
}

pub unsafe fn taskbar_on_monitor(mon: HMONITOR) -> HWND {
    let h = FindWindowW(w!("Shell_TrayWnd"), null());
    if !h.is_null() && MonitorFromWindow(h, MONITOR_DEFAULTTONULL) == mon {
        return h;
    }
    let mut h: HWND = null_mut();
    loop {
        h = FindWindowExW(null_mut(), h, w!("Shell_SecondaryTrayWnd"), null());
        if h.is_null() {
            break;
        }
        if MonitorFromWindow(h, MONITOR_DEFAULTTONULL) == mon {
            return h;
        }
    }
    null_mut()
}

// Right under the taskbar in z-order, so the slide comes out from beneath it.
unsafe fn slide_below_taskbar() {
    if !U.slide || U.app_mode {
        return;
    }
    let tray = taskbar_on_monitor(U.mon);
    if tray.is_null() {
        return;
    }
    SetWindowPos(g_hwnd, tray, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // The taskbar itself can sit below regular windows; following it would bury the launcher too.
    let mut w = GetWindow(g_hwnd, GW_HWNDPREV);
    while !w.is_null() {
        if IsWindowVisible(w) != 0 && (GetWindowLongW(w, GWL_EXSTYLE) as u32 & WS_EX_TOPMOST) == 0 {
            SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowPos(tray, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); // taskbar back over the launcher
            break;
        }
        w = GetWindow(w, GW_HWNDPREV);
    }
}

// Fullscreen app over the taskbar: open as a regular window so the shell brings the taskbar back.
unsafe fn taskbar_covered(mon: HMONITOR) -> bool {
    let tray = taskbar_on_monitor(mon);
    if tray.is_null() || IsWindowVisible(tray) == 0 {
        return false;
    }
    let mut r: RECT = zeroed();
    GetWindowRect(tray, &mut r);
    let c = POINT { x: (r.left + r.right) / 2, y: (r.top + r.bottom) / 2 };
    let at = WindowFromPoint(c);
    let root = if at.is_null() { null_mut() } else { GetAncestor(at, GA_ROOT) };
    if root.is_null() || root == tray || root == g_hwnd {
        return false;
    }
    (GetWindowLongW(root, GWL_EXSTYLE) as u32 & WS_EX_TOPMOST) == 0
}

unsafe fn set_app_window_mode(app: bool) {
    U.app_mode = app;
    let ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    let nx = if app { ex & !(WS_EX_TOOLWINDOW as isize) } else { ex | WS_EX_TOOLWINDOW as isize };
    if nx != ex {
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, nx);
    }
    SetWindowPos(
        g_hwnd,
        if app { HWND_NOTOPMOST } else { HWND_TOPMOST },
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | if nx != ex { SWP_FRAMECHANGED } else { 0 },
    );
}

static mut TBL: Option<wsh::ITaskbarList> = None;
static mut TBL_TRIED: bool = false;

// App mode shows the window on the taskbar (to bring the taskbar up over a fullscreen app), but no button for it.
pub unsafe fn taskbar_button_remove() {
    if !TBL_TRIED {
        TBL_TRIED = true;
        if let Ok(t) = windows::Win32::System::Com::CoCreateInstance::<_, wsh::ITaskbarList>(&wsh::TaskbarList, None, windows::Win32::System::Com::CLSCTX_INPROC_SERVER) {
            if t.HrInit().is_ok() {
                TBL = Some(t);
            }
        }
    }
    if let Some(t) = (*(&raw const TBL)).as_ref() {
        let _ = t.DeleteTab(whwnd(g_hwnd));
    }
}

unsafe fn ui_hide_finish() {
    // settings from the launcher open once it has faded out, while it still has the foreground
    if U.settings_after_hide {
        U.settings_after_hide = false;
        settings_open();
    }
    U.cm.open = false;
    U.cm.closing = false;
    U.visible = false;
    U.closing = false;
    U.vis = 0.0;
    U.vis_target = 0.0;
    ShowWindow(g_hwnd, SW_HIDE);
    bd_close();
    set_click_through(false);
    set_app_window_mode(false);
    hook_watchdog_sync();
}

pub unsafe fn ui_hide_finish_pub() {
    ui_hide_finish();
}

// Opens the launcher (or brings it back while it closes) with a fresh view; a query typed shortly before stays.
pub unsafe fn ui_show() {
    if U.visible && !U.closing {
        force_foreground(g_hwnd);
        return;
    }
    if U.visible && U.closing {
        U.closing = false;
        U.settings_after_hide = false;
        vis_set_direction(true);
        U.open_time = 0.0;
        hook_set_visible(true);
        set_click_through(false);
        if !g_pinned {
            force_foreground(g_hwnd);
        }
        slide_below_taskbar();
        tb_set_open(U.mon);
        U.caret_on = true;
        SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), None);
        U.animating = true;
        return;
    }
    let now = time_now();
    hook_watchdog_off();
    hook_set_visible(true);
    let fg = GetForegroundWindow();
    U.prev_fg = if fg != g_hwnd { fg } else { null_mut() };

    U.search_mode = U.want_search && (st_on() || w10_on());
    U.want_search = false;
    let keep = U.qlen != 0 && !U.search_mode && (now - U.hide_time) < g_cfg.keep_query_seconds as f64;
    if !keep {
        // before placing: the Windows 10 style sizes itself by the query
        U.qlen = 0;
        U.q[0] = 0;
        U.caret = 0;
        U.anchor = 0;
    } else {
        U.anchor = 0;
        U.caret = U.qlen;
    }

    theme_update();
    ui_place_window();
    let win = RECT { left: U.win_x, top: U.win_y, right: U.win_x + U.W, bottom: U.win_y + U.H };
    bd_open(win, U.mon); // what it shows through, starting while it is still hidden
    set_app_window_mode(taskbar_covered(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST)));
    // Take the keyboard while still transparent, so keys typed right after Win land here.
    SetLayeredWindowAttributes(g_hwnd, 0, 0, LWA_ALPHA);
    U.alpha = 0;
    set_click_through(false);
    if g_pinned {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    } else {
        ShowWindow(g_hwnd, SW_SHOW);
        force_foreground(g_hwnd);
        if GetForegroundWindow() != g_hwnd {
            // Without the foreground a regular (non-topmost) window would open behind everything: stay on top.
            log_msg!("show: could not take foreground");
            if U.app_mode {
                set_app_window_mode(false);
            }
            SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    if U.app_mode {
        taskbar_button_remove();
    }
    slide_below_taskbar();
    tb_set_open(U.mon);

    U.st.all_view = false;
    U.st.fb_hover = -1;
    U.st.fb_press = -1;
    U.st.title_press = false;
    U.tsel = -1;
    U.w10.press_tile = -1;
    U.w10.rail_hover = -1;
    U.w10.rail_press = -1;
    U.w10.act_hover = -1;
    U.w10.act_press = -1;
    U.w10.rail_open = false;
    U.w10.rail_t = 0.0;
    U.w10.tscroll = 0.0;
    U.w10.tscroll_target = 0.0;
    U.st.title_hover = false;
    refresh_frecency();
    refresh_empty_view();
    U.q_scroll = 0.0;
    if U.qlen != 0 {
        search_apps();
        search_tags();
        request_files();
    }
    rebuild_rows(false);
    U.scroll = 0.0;
    U.scroll_target = 0.0;
    U.scroll_v = 0.0;
    snap_highlight();
    U.armed = null_mut();
    U.cm.open = false;
    U.cm.closing = false;
    U.cm.press = -1;
    U.tg.press = -1;
    U.sub_text = null();
    U.sub_t = 0.0;
    U.caret_on = true;
    U.visible = true;
    U.closing = false;
    U.vis = if anims_enabled() { 0.0 } else { 1.0 };
    U.slide_v = -slide_omega() * U.slide_off; // starts fast and decelerates, like Start
    U.open_time = if anims_enabled() { now } else { 0.0 };
    U.settings_hover_t = 0.0;
    U.sb_t = 0.0;
    U.sb_prev = 0.0;
    U.sb_seen = 0.0;
    U.sb_hot = false;
    U.vis_target = 1.0;
    U.press_row = -1;
    let mut cur = POINT { x: 0, y: 0 };
    GetCursorPos(&mut cur);
    ScreenToClient(g_hwnd, &mut cur);
    U.mouse_x = cur.x;
    U.mouse_y = cur.y;
    U.dirty = true;
    ui_render();
    SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), None);
    SetTimer(g_hwnd, TIMER_REHOOK, 60, None);
    if now - U.last_reindex > 180.0 {
        U.last_reindex = now;
        apps_request_reindex();
    }
}

pub unsafe fn prtsc_handoff_end() {
    if K.prtsc_from.load(SeqCst).is_null() {
        return;
    }
    K.prtsc_from.store(null_mut(), SeqCst);
    KillTimer(g_hwnd, TIMER_PRTSC);
}

pub unsafe fn ui_hide() {
    if !U.visible || U.closing {
        return;
    }
    U.hide_time = time_now();
    U.armed = null_mut();
    U.text_drag = false;
    U.tg.open = false;
    U.tg.closing = false;
    KillTimer(g_hwnd, TIMER_CARET);
    let restore = !U.no_focus_restore;
    U.no_focus_restore = false;
    if K.prtsc_from.load(SeqCst) == g_hwnd {
        prtsc_handoff_end();
    }
    hook_set_visible(false);
    tb_set_open(null_mut());
    if restore
        && !g_pinned
        && GetForegroundWindow() == g_hwnd
        && !U.prev_fg.is_null()
        && IsWindow(U.prev_fg) != 0
        && IsWindowVisible(U.prev_fg) != 0
        && !is_shell_window(U.prev_fg)
    {
        SetForegroundWindow(U.prev_fg);
    }
    history_save();
    if !anims_enabled() || U.vis <= 0.0 {
        ui_hide_finish();
        return;
    }
    U.closing = true;
    vis_set_direction(false);
    U.animating = true;
    set_click_through(true);
    ui_invalidate();
}

pub unsafe fn prtsc_handoff() {
    U.prtsc_ticks = 0;
    SetTimer(g_hwnd, TIMER_PRTSC, 600, None);
}

// A screenshot tool's overlay over our window, even one that has not taken the foreground (yet).
unsafe fn prtsc_covered(h: HWND) -> bool {
    let mut r: RECT = zeroed();
    if GetWindowRect(h, &mut r) == 0 {
        return false;
    }
    let c = POINT { x: (r.left + r.right) / 2, y: (r.top + r.bottom) / 2 };
    let top = GetAncestor(WindowFromPoint(c), GA_ROOT);
    !top.is_null() && !window_is_ours(top)
}

// The tool either never took the foreground or gave it back to the window we handed it to: return it to ours.
pub unsafe fn prtsc_handoff_tick() {
    SetTimer(g_hwnd, TIMER_PRTSC, 250, None);
    let from = K.prtsc_from.load(SeqCst) as HWND;
    let fg = GetForegroundWindow();
    if from.is_null() || IsWindow(from) == 0 || IsWindowVisible(from) == 0 || (from == g_hwnd && (!U.visible || U.closing)) || window_is_ours(fg) {
        prtsc_handoff_end();
        return;
    }
    U.prtsc_ticks += 1;
    if fg == K.prtsc_to && !prtsc_covered(from) {
        prtsc_handoff_end();
        force_foreground(from);
    } else if U.prtsc_ticks > 4 * 120 {
        prtsc_handoff_end();
        if from == g_hwnd {
            U.no_focus_restore = true;
            ui_hide();
        }
    }
}

// The Start styles between their home and an empty search, without closing.
pub unsafe fn ui_set_search_mode(on: bool) {
    U.search_mode = on;
    U.st.all_view = false;
    if U.qlen != 0 {
        edit_replace(0, U.qlen, w!(""), 0);
    } else {
        on_query_changed();
    }
    ui_invalidate();
}

pub unsafe fn ui_empty_search() -> bool {
    U.search_mode && U.qlen == 0
}

// From the taskbar's search: the Start styles open straight on an empty search, or switch to it while open.
// Another click on an empty search closes it, like the other styles.
pub unsafe fn ui_search_clicked() {
    let start = st_on() || w10_on();
    if U.visible && !U.closing {
        if start && !ui_empty_search() {
            ui_set_search_mode(true);
        } else {
            ui_hide();
        }
        return;
    }
    U.want_search = true;
    if U.visible && U.closing && start && !ui_empty_search() {
        ui_set_search_mode(true);
    }
    ui_show();
    U.want_search = false;
}

pub unsafe fn ui_toggle() {
    // Win on an empty search goes to the launcher's home instead of closing it
    if U.visible && !U.closing && ui_empty_search() && (st_on() || w10_on()) {
        ui_set_search_mode(false);
    } else if U.visible && !U.closing {
        ui_hide();
    } else {
        ui_show();
    }
}

// ---- the selected row

// The selected row, or the Windows 10 tile under the pointer.
pub unsafe fn sel_row() -> *mut Row {
    if w10_on() && U.tsel >= 0 && U.tsel < U.w10.tile_count {
        return &raw mut U.w10.tiles[U.tsel as usize];
    }
    if row_selectable(U.sel) { U.rows.add(U.sel as usize) } else { null_mut() }
}

pub unsafe fn row_path(r: *const Row) -> *const WCHAR {
    if r.is_null() {
        return null();
    }
    if (*r).kind == ROW_FILE {
        return (*(*r).file).full;
    }
    if (*r).kind == ROW_APP && (*(*r).app).kind == APP_SHELL {
        return (*(*r).app).path;
    }
    null()
}

// Runs an ACT_* on the selected row through the launch thread and closes the launcher. A risky command first
// only arms itself.
pub unsafe fn ui_activate(act: i32) {
    let r = sel_row();
    if r.is_null() {
        return;
    }
    let mut j: LaunchJob = zeroed();
    j.act = act;
    let mut qn = [0u16; Q_MAX + 1];
    let ql = query_norm(qn.as_mut_ptr());
    if (*r).kind == ROW_APP {
        let a = (*r).app;
        if (*a).kind == APP_CMD {
            if act != ACT_OPEN {
                return;
            }
            if (*a).danger && U.armed != a {
                U.armed = a;
                ui_invalidate();
                return;
            }
            j.cmd = (*a).cmd;
            history_record(IT_CMD, (*a).id, qn.as_ptr(), ql);
        } else {
            if (act == ACT_REVEAL || act == ACT_PROPERTIES) && (*a).path.is_null() {
                return;
            }
            j.is_app = true;
            j.target = wdup_heap((*a).id);
            j.path = wdup_heap((*a).path);
            if act != ACT_PROPERTIES {
                history_record(IT_APP, (*a).id, qn.as_ptr(), ql);
            }
        }
    } else {
        let f = (*r).file;
        j.target = wdup_heap((*f).full);
        j.dir = wdup_heap((*f).dir);
        j.hist_type = file_item_type(f);
        j.hist_key = wdup_heap((*f).full);
        let full = (*f).full;
        if act != ACT_PROPERTIES && !(*full == '\\' as u16 && *full.add(1) == '\\' as u16) {
            history_record(j.hist_type, full, qn.as_ptr(), ql);
        }
    }
    AllowSetForegroundWindow(ASFW_ANY);
    ui_hide();
    launch_submit(&j);
}

pub unsafe fn ui_copy_path() {
    let r = sel_row();
    if r.is_null() {
        return;
    }
    let p = row_path(r);
    clipboard_set(if p.is_null() { (*(*r).app).name } else { p }, -1);
}

pub unsafe fn move_sel(delta: i32) {
    if U.sel < 0 {
        return;
    }
    let dir = if delta > 0 { 1 } else { -1 };
    let mut steps = delta.abs();
    let mut i = U.sel;
    while steps > 0 {
        steps -= 1;
        let n = first_selectable(i + dir, dir);
        if n < 0 {
            break;
        }
        i = n;
    }
    if i != U.sel {
        U.sel = i;
        U.armed = null_mut();
        ensure_visible(i, true);
        ui_invalidate();
    }
}

unsafe fn header_from(mut i: i32, dir: i32) -> i32 {
    while i >= 0 && i < U.row_count {
        if row(i).kind == ROW_HEADER {
            return i;
        }
        i += dir;
    }
    -1
}

// Tab: the first row of the next section (or of the previous one, back).
pub unsafe fn jump_section(dir: i32) {
    if U.sel < 0 {
        return;
    }
    let h = if dir > 0 { header_from(U.sel + 1, 1) } else { header_from(header_from(U.sel, -1) - 1, -1) };
    let n = if h >= 0 { first_selectable(h + 1, 1) } else { -1 };
    if n >= 0 {
        U.sel = n;
        ensure_visible(n, true);
    }
    U.armed = null_mut();
    ui_invalidate();
}
