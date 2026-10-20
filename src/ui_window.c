// The launcher window: where it opens, showing and hiding it (fade, zoom, the slide from the taskbar), the
// PrtSc hand-off, and acting on the selected row.

static void ui_render(void);
static HWND taskbar_on_monitor(HMONITOR mon);

static void force_foreground(HWND h)
{
    if (GetForegroundWindow() == h) return;
    BringWindowToTop(h);
    SetForegroundWindow(h);
    if (GetForegroundWindow() != h) {
        HWND fg = GetForegroundWindow();
        DWORD fg_tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0, me = GetCurrentThreadId();
        if (fg_tid && fg_tid != me && AttachThreadInput(me, fg_tid, TRUE)) {
            BringWindowToTop(h);
            SetForegroundWindow(h);
            AttachThreadInput(me, fg_tid, FALSE);
        }
    }
    if (GetForegroundWindow() != h) {
        // Being the source of the last input event grants foreground rights.
        INPUT in[2];
        memset(in, 0, sizeof in);
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = 0xE8;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        inject_keys(in, 2);
        SetForegroundWindow(h);
    }
    SetFocus(h);
}

// Sizes the window for its monitor and puts it in place: centered, or at the taskbar ready to slide out.
static void ui_place_window(void)
{
    HMONITOR mon = open_monitor();
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    UINT dpi = monitor_dpi(mon);
    if (dpi != U.dpi) {
        ui_metrics(dpi);
        icons_reset(ui_icon_px());
    }
    RECT wa = mi.rcWork, mr = mi.rcMonitor;
    if (w10_on()) {  // a resized Windows 10 Start still fits the screen
        U.w10.wide = mr.right - mr.left >= 2560;
        w10_metrics();  // the search pane's width or the resized one, for the query it opens with
        U.H = MIN(U.H, (int)(wa.bottom - wa.top));
        U.W = MIN(U.W, (int)(wa.right - wa.left));
        U.list_y1 = (f32)U.H;
    }
    int x = wa.left + ((wa.right - wa.left) - U.W) / 2;
    int y = wa.top + (int)((f32)((wa.bottom - wa.top) - U.H) * 0.28f);
    U.mon = mon;
    int pos = launcher_position();
    U.slide = pos != POS_CENTER;
    U.slide_dy = 0;
    if (U.slide) {
        int m = w10_on() ? 0 : (int)SR(12);  // Windows 10 Start sits right against the taskbar and the screen edge
        HWND tray = taskbar_on_monitor(mon);
        RECT tr;
        bool top = tray && GetWindowRect(tray, &tr) && tr.bottom - mr.top < mr.bottom - tr.top;  // taskbar at the top: slide down from it
        if (pos == POS_BOTTOM_LEFT) x = wa.left + m;
        y = top ? wa.top + m : wa.bottom - U.H - m;
        // Start fully behind the taskbar edge, but never reach into a monitor beyond this one.
        U.slide_dy = top ? wa.top - (y + U.H) : wa.bottom - y;
        RECT past = top ? (RECT){ x, mr.top - U.H, x + U.W, mr.top } : (RECT){ x, mr.bottom, x + U.W, mr.bottom + U.H };
        if (MonitorFromRect(&past, MONITOR_DEFAULTTONULL)) U.slide_dy = top ? mr.top - y : mr.bottom - (y + U.H);
    }
    U.win_x = x;
    U.win_y = y;
    U.slide_y = anims_enabled() ? U.slide_dy : 0;
    U.slide_off = (f32)U.slide_y;
    U.slide_v = 0;
    SetWindowPos(g_hwnd, U.app_mode ? HWND_NOTOPMOST : HWND_TOPMOST, x, y + U.slide_y, U.W, U.H, SWP_NOACTIVATE);
    r_resize(U.W, U.H);
}

#define ROW_STAGGER_MAX 8

static f32 slide_omega(void) { return anim_omega(MAX(g_cfg.anim_open_ms, g_cfg.anim_close_ms)); }

static f32 vis_curve(f32 v, bool opening)
{
    v = CLAMP(v, 0.f, 1.f);
    if (opening) return ease_out_quart(v);
    return v * v;
}

// The slide is a spring: U.vis follows its position, so no curve on top.
static bool slide_spring(void) { return U.slide && U.slide_dy; }

static f32 vis_eased(void) { return slide_spring() ? CLAMP(U.vis, 0.f, 1.f) : vis_curve(U.vis, U.vis_target > 0.5f); }

static void vis_set_direction(bool opening)
{
    if (slide_spring()) {  // keeps its position and velocity and just turns around
        U.vis_target = opening ? 1.f : 0.f;
        return;
    }
    f32 a = vis_eased();
    U.vis_target = opening ? 1.f : 0.f;
    U.vis = opening ? 1.f - sqrtf(sqrtf(1.f - a)) : sqrtf(a);
}

static f32 row_in(int k, f64 now)
{
    f32 dur = anim_sec(g_cfg.anim_row_ms);
    if (!g_cfg.anim_cascade || dur <= 0.f) return 1.f;
    f32 t = (f32)(now - U.open_time) - anim_sec(g_cfg.anim_stagger_ms) * (f32)MIN(k, ROW_STAGGER_MAX);
    return ease_out_cubic(t / dur);
}

static bool rows_cascading(f64 now)
{
    return g_cfg.anim_cascade && now - U.open_time < anim_sec(g_cfg.anim_row_ms) + anim_sec(g_cfg.anim_stagger_ms) * ROW_STAGGER_MAX;
}

static void apply_window_alpha(void)
{
    f32 v = vis_eased();
    if (U.slide) v = MIN(v * 2.f, 1.f);  // the slide carries the motion, the fade only softens its start
    int a = (int)(v * 255.f + 0.5f);
    if (a != U.alpha) {
        U.alpha = a;
        SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)a, LWA_ALPHA);
    }
}

static void apply_window_slide(void)
{
    if (!U.slide) return;
    int y = (int)lroundf(U.slide_off);
    if (y == U.slide_y) return;
    U.slide_y = y;
    SetWindowPos(g_hwnd, NULL, U.win_x, U.win_y + y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void set_click_through(bool on)
{
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    LONG_PTR nx = on ? (ex | WS_EX_TRANSPARENT) : (ex & ~(LONG_PTR)WS_EX_TRANSPARENT);
    if (nx != ex) SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, nx);
}

// Never hand focus to these: activating the taskbar while Win is down opens Start.
static bool is_shell_window(HWND h)
{
    WCHAR cls[64];
    if (!h || !GetClassNameW(h, cls, countof(cls))) return true;
    static const WCHAR *shell[] = { L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW",
                                    L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow",
                                    L"TopLevelWindowForOverflowXamlIsland", L"NotifyIconOverflowWindow" };
    for (int i = 0; i < countof(shell); i++)
        if (!wcscmp(cls, shell[i])) return true;
    return false;
}

static HWND taskbar_on_monitor(HMONITOR mon)
{
    HWND h = FindWindowW(L"Shell_TrayWnd", NULL);
    if (h && MonitorFromWindow(h, MONITOR_DEFAULTTONULL) == mon) return h;
    for (h = NULL; (h = FindWindowExW(NULL, h, L"Shell_SecondaryTrayWnd", NULL)) != NULL;)
        if (MonitorFromWindow(h, MONITOR_DEFAULTTONULL) == mon) return h;
    return NULL;
}

// Right under the taskbar in z-order, so the slide comes out from beneath it.
static void slide_below_taskbar(void)
{
    if (!U.slide || U.app_mode) return;
    HWND tray = taskbar_on_monitor(U.mon);
    if (!tray) return;
    SetWindowPos(g_hwnd, tray, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // The taskbar itself can sit below regular windows; following it would bury the launcher too.
    for (HWND w = GetWindow(g_hwnd, GW_HWNDPREV); w; w = GetWindow(w, GW_HWNDPREV)) {
        if (IsWindowVisible(w) && !(GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
            SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowPos(tray, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);  // taskbar back over the launcher
            break;
        }
    }
}

// Fullscreen app over the taskbar: open as a regular window so the shell brings the taskbar back.
static bool taskbar_covered(HMONITOR mon)
{
    HWND tray = taskbar_on_monitor(mon);
    if (!tray || !IsWindowVisible(tray)) return false;
    RECT r;
    GetWindowRect(tray, &r);
    POINT c = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
    HWND at = WindowFromPoint(c);
    HWND root = at ? GetAncestor(at, GA_ROOT) : NULL;
    if (!root || root == tray || root == g_hwnd) return false;
    return !(GetWindowLongW(root, GWL_EXSTYLE) & WS_EX_TOPMOST);
}

static void set_app_window_mode(bool app)
{
    U.app_mode = app;
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    LONG_PTR nx = app ? (ex & ~(LONG_PTR)WS_EX_TOOLWINDOW) : (ex | WS_EX_TOOLWINDOW);
    if (nx != ex) SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, nx);
    SetWindowPos(g_hwnd, app ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (nx != ex ? SWP_FRAMECHANGED : 0));
}

// App mode shows the window on the taskbar (to bring the taskbar up over a fullscreen app), but no button for it.
static void taskbar_button_remove(void)
{
    static ITaskbarList *tbl;
    static bool tried;
    if (!tried) {
        tried = true;
        if (SUCCEEDED(CoCreateInstance(&ML_CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, &ML_IID_ITaskbarList, (void **)&tbl)) && tbl) {
            if (FAILED(ITaskbarList_HrInit(tbl))) {
                ITaskbarList_Release(tbl);
                tbl = NULL;
            }
        }
    }
    if (tbl) ITaskbarList_DeleteTab(tbl, g_hwnd);
}

static void ui_hide_finish(void)
{
    // settings from the launcher open once it has faded out, while it still has the foreground
    if (U.settings_after_hide) {
        U.settings_after_hide = false;
        settings_open();
    }
    U.cm.open = U.cm.closing = false;
    U.visible = false;
    U.closing = false;
    U.vis = U.vis_target = 0;
    ShowWindow(g_hwnd, SW_HIDE);
    bd_close();
    set_click_through(false);
    set_app_window_mode(false);
    hook_watchdog_sync();
}

// Opens the launcher (or brings it back while it closes) with a fresh view; a query typed shortly before stays.
static void ui_show(void)
{
    if (U.visible && !U.closing) {
        force_foreground(g_hwnd);
        return;
    }
    if (U.visible && U.closing) {
        U.closing = false;
        U.settings_after_hide = false;
        vis_set_direction(true);
        U.open_time = 0;
        hook_set_visible(true);
        set_click_through(false);
        if (!g_pinned) force_foreground(g_hwnd);
        slide_below_taskbar();
        tb_set_open(U.mon);
        U.caret_on = true;
        SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), NULL);
        U.animating = true;
        return;
    }
    f64 now = time_now();
    hook_watchdog_off();
    hook_set_visible(true);
    HWND fg = GetForegroundWindow();
    U.prev_fg = fg != g_hwnd ? fg : NULL;

    U.search_mode = U.want_search && (st_on() || w10_on());
    U.want_search = false;
    bool keep = U.qlen && !U.search_mode && (now - U.hide_time) < (f64)g_cfg.keep_query_seconds;
    if (!keep) {  // before placing: the Windows 10 style sizes itself by the query
        U.qlen = 0;
        U.q[0] = 0;
        U.caret = U.anchor = 0;
    } else {
        U.anchor = 0;
        U.caret = U.qlen;
    }

    theme_update();
    ui_place_window();
    RECT win = { U.win_x, U.win_y, U.win_x + U.W, U.win_y + U.H };
    bd_open(win, U.mon);  // what it shows through, starting while it is still hidden
    set_app_window_mode(taskbar_covered(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST)));
    // Take the keyboard while still transparent, so keys typed right after Win land here.
    SetLayeredWindowAttributes(g_hwnd, 0, 0, LWA_ALPHA);
    U.alpha = 0;
    set_click_through(false);
    if (g_pinned) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    } else {
        ShowWindow(g_hwnd, SW_SHOW);
        force_foreground(g_hwnd);
        if (GetForegroundWindow() != g_hwnd) {
            // Without the foreground a regular (non-topmost) window would open behind everything: stay on top.
            log_msg("show: could not take foreground");
            if (U.app_mode) set_app_window_mode(false);
            SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    if (U.app_mode) taskbar_button_remove();
    slide_below_taskbar();
    tb_set_open(U.mon);

    U.st.all_view = false;
    U.st.fb_hover = U.st.fb_press = -1;
    U.st.title_press = false;
    U.tsel = U.w10.press_tile = U.w10.rail_hover = U.w10.rail_press = U.w10.act_hover = U.w10.act_press = -1;
    U.w10.rail_open = false;
    U.w10.rail_t = 0;
    U.w10.tscroll = U.w10.tscroll_target = 0;
    U.st.title_hover = false;
    refresh_frecency();
    refresh_empty_view();
    U.q_scroll = 0;
    if (U.qlen) {
        search_apps();
        request_files();
    }
    rebuild_rows(false);
    U.scroll = U.scroll_target = U.scroll_v = 0;
    snap_highlight();
    U.armed = NULL;
    U.cm.open = U.cm.closing = false;
    U.cm.press = -1;
    U.tg.press = -1;
    U.sub_text = NULL;
    U.sub_t = 0;
    U.caret_on = true;
    U.visible = true;
    U.closing = false;
    U.vis = anims_enabled() ? 0.f : 1.f;
    U.slide_v = -slide_omega() * U.slide_off;  // starts fast and decelerates, like Start
    U.open_time = anims_enabled() ? now : 0;
    U.settings_hover_t = 0;
    U.sb_t = U.sb_prev = 0;
    U.sb_seen = 0;
    U.sb_hot = false;
    U.vis_target = 1;
    U.press_row = -1;
    POINT cur;
    GetCursorPos(&cur);
    ScreenToClient(g_hwnd, &cur);
    U.mouse_x = cur.x;
    U.mouse_y = cur.y;
    U.dirty = true;
    ui_render();
    SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), NULL);
    SetTimer(g_hwnd, TIMER_REHOOK, 60, NULL);
    if (now - U.last_reindex > 180.0) {
        U.last_reindex = now;
        apps_request_reindex();
    }
}

static void prtsc_handoff_end(void)
{
    if (!K.prtsc_from) return;
    K.prtsc_from = NULL;
    KillTimer(g_hwnd, TIMER_PRTSC);
}

static void ui_hide(void)
{
    if (!U.visible || U.closing) return;
    U.hide_time = time_now();
    U.armed = NULL;
    U.text_drag = false;
    U.tg.open = U.tg.closing = false;
    KillTimer(g_hwnd, TIMER_CARET);
    bool restore = !U.no_focus_restore;
    U.no_focus_restore = false;
    if (K.prtsc_from == g_hwnd) prtsc_handoff_end();
    hook_set_visible(false);
    tb_set_open(NULL);
    if (restore && !g_pinned && GetForegroundWindow() == g_hwnd && U.prev_fg && IsWindow(U.prev_fg) &&
        IsWindowVisible(U.prev_fg) && !is_shell_window(U.prev_fg))
        SetForegroundWindow(U.prev_fg);
    history_save();
    if (!anims_enabled() || U.vis <= 0.f) {
        ui_hide_finish();
        return;
    }
    U.closing = true;
    vis_set_direction(false);
    U.animating = true;
    set_click_through(true);
    ui_invalidate();
}

static void prtsc_handoff(void)
{
    U.prtsc_ticks = 0;
    SetTimer(g_hwnd, TIMER_PRTSC, 600, NULL);
}

// A screenshot tool's overlay over our window, even one that has not taken the foreground (yet).
static bool prtsc_covered(HWND h)
{
    RECT r;
    if (!GetWindowRect(h, &r)) return false;
    POINT c = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
    HWND top = GetAncestor(WindowFromPoint(c), GA_ROOT);
    return top && !window_is_ours(top);
}

// The tool either never took the foreground or gave it back to the window we handed it to: return it to ours.
static void prtsc_handoff_tick(void)
{
    SetTimer(g_hwnd, TIMER_PRTSC, 250, NULL);
    HWND from = K.prtsc_from, fg = GetForegroundWindow();
    if (!from || !IsWindow(from) || !IsWindowVisible(from) || (from == g_hwnd && (!U.visible || U.closing)) || window_is_ours(fg)) {
        prtsc_handoff_end();
        return;
    }
    if (fg == K.prtsc_to && !prtsc_covered(from)) {
        prtsc_handoff_end();
        force_foreground(from);
    } else if (++U.prtsc_ticks > 4 * 120) {
        prtsc_handoff_end();
        if (from == g_hwnd) {
            U.no_focus_restore = true;
            ui_hide();
        }
    }
}

// The Start styles between their home and an empty search, without closing.
static void ui_set_search_mode(bool on)
{
    U.search_mode = on;
    U.st.all_view = false;
    if (U.qlen) edit_replace(0, U.qlen, L"", 0);
    else on_query_changed();
    ui_invalidate();
}

static bool ui_empty_search(void) { return U.search_mode && !U.qlen; }

// From the taskbar's search: the Start styles open straight on an empty search, or switch to it while open.
// Another click on an empty search closes it, like the other styles.
static void ui_search_clicked(void)
{
    bool start = st_on() || w10_on();
    if (U.visible && !U.closing) {
        if (start && !ui_empty_search()) ui_set_search_mode(true);
        else ui_hide();
        return;
    }
    U.want_search = true;
    if (U.visible && U.closing && start && !ui_empty_search()) ui_set_search_mode(true);
    ui_show();
    U.want_search = false;
}

static void ui_toggle(void)
{
    // Win on an empty search goes to the launcher's home instead of closing it
    if (U.visible && !U.closing && ui_empty_search() && (st_on() || w10_on())) ui_set_search_mode(false);
    else if (U.visible && !U.closing) ui_hide();
    else ui_show();
}

// ---- the selected row

// The selected row, or the Windows 10 tile under the pointer.
static Row *sel_row(void)
{
    if (w10_on() && U.tsel >= 0 && U.tsel < U.w10.tile_count) return &U.w10.tiles[U.tsel];
    return row_selectable(U.sel) ? &U.rows[U.sel] : NULL;
}

static const WCHAR *row_path(Row *r)
{
    if (!r) return NULL;
    if (r->kind == ROW_FILE) return r->file->full;
    if (r->kind == ROW_APP && r->app->kind == APP_SHELL) return r->app->path;
    return NULL;
}

// Runs an ACT_* on the selected row through the launch thread and closes the launcher. A risky command first
// only arms itself.
static void ui_activate(int act)
{
    Row *r = sel_row();
    if (!r) return;
    LaunchJob j;
    memset(&j, 0, sizeof j);
    j.act = act;
    WCHAR qn[Q_MAX + 1];
    int ql = query_norm(qn);
    if (r->kind == ROW_APP) {
        App *a = r->app;
        if (a->kind == APP_CMD) {
            if (act != ACT_OPEN) return;
            if (a->danger && U.armed != a) {
                U.armed = a;
                ui_invalidate();
                return;
            }
            j.cmd = a->cmd;
            history_record(IT_CMD, a->id, qn, ql);
        } else {
            if ((act == ACT_REVEAL || act == ACT_PROPERTIES) && !a->path) return;
            j.is_app = true;
            j.target = wdup_heap(a->id);
            j.path = wdup_heap(a->path);
            if (act != ACT_PROPERTIES) history_record(IT_APP, a->id, qn, ql);
        }
    } else {
        FileItem *f = r->file;
        j.target = wdup_heap(f->full);
        j.dir = wdup_heap(f->dir);
        j.hist_type = file_item_type(f);
        j.hist_key = wdup_heap(f->full);
        if (act != ACT_PROPERTIES && !(f->full[0] == '\\' && f->full[1] == '\\')) history_record(j.hist_type, f->full, qn, ql);
    }
    AllowSetForegroundWindow(ASFW_ANY);
    ui_hide();
    launch_submit(&j);
}

static void ui_copy_path(void)
{
    Row *r = sel_row();
    if (!r) return;
    const WCHAR *p = row_path(r);
    clipboard_set(p ? p : r->app->name, -1);
}

static void move_sel(int delta)
{
    if (U.sel < 0) return;
    int dir = delta > 0 ? 1 : -1, steps = abs(delta), i = U.sel;
    while (steps-- > 0) {
        int n = first_selectable(i + dir, dir);
        if (n < 0) break;
        i = n;
    }
    if (i != U.sel) {
        U.sel = i;
        U.armed = NULL;
        ensure_visible(i, true);
        ui_invalidate();
    }
}

static int header_from(int i, int dir)
{
    for (; i >= 0 && i < U.row_count; i += dir)
        if (U.rows[i].kind == ROW_HEADER) return i;
    return -1;
}

// Tab: the first row of the next section (or of the previous one, back).
static void jump_section(int dir)
{
    if (U.sel < 0) return;
    int h = dir > 0 ? header_from(U.sel + 1, 1) : header_from(header_from(U.sel, -1) - 1, -1);
    int n = h >= 0 ? first_selectable(h + 1, 1) : -1;
    if (n >= 0) {
        U.sel = n;
        ensure_visible(n, true);
    }
    U.armed = NULL;
    ui_invalidate();
}

