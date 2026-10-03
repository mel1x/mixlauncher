#include "base.h"
#include "com_min.h"
#include "config.c"
#include "render.c"
#include "font.c"
#include "match.c"
#include "history.c"
#include "apps.c"
#include "icons.c"
#include "everything.c"
#include "tags.c"
#include "hotkey.c"
#include "elevation.c"
#include "launch.c"
#include "update.c"
#include "tray.c"
#include "ui.c"
#include "settings.c"
#include "taskbar.c"
#include "media.c"

#define WINDOW_CLASS L"MixLauncherWindow"

enum { TM_OPEN = 1, TM_SETTINGS, TM_EXIT };

static void tray_menu(void)
{
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_DEFAULT, TM_OPEN, TR("Открыть MixLauncher", "Open MixLauncher"));
    AppendMenuW(m, MF_STRING, TM_SETTINGS, TR("Настройки…", "Settings…"));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, TM_EXIT, TR("Выход", "Exit"));
    theme_update();
    menus_set_dark(U.th.dark);
    POINT pt;
    GetCursorPos(&pt);
    U.menu_open = true;
    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    U.menu_open = false;
    DestroyMenu(m);
    switch (cmd) {
    case TM_OPEN:
        ui_show();
        break;
    case TM_SETTINGS:
        settings_open();
        break;
    case TM_EXIT:
        PostMessageW(g_hwnd, WM_APP_EXIT, 0, 0);
        break;
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (U.closing && ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST))) return 0;
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp) return 0;
        break;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_NCACTIVATE:
        // DWM draws the acrylic backdrop only for an active frame; an inactive one gets a flat gray fallback and
        // a crossfade on activation. The launcher always reports an active frame (-1: no non-client repaint).
        return DefWindowProcW(h, msg, TRUE, -1);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        ui_invalidate();
        return 0;
    }
    case WM_SIZE:
        r_resize(LOWORD(lp), HIWORD(lp));
        ui_invalidate();
        return 0;
    case WM_ACTIVATE:
        if (g_ktrace_on) {
            WCHAR fgc[64] = L"";
            GetClassNameW(GetForegroundWindow(), fgc, 64);
            log_msg("activate t=%lu state=%d other=%p fg=%ls", GetTickCount(), LOWORD(wp), (void *)lp, fgc);
        }
        if (LOWORD(wp) == WA_INACTIVE && U.visible && !U.menu_open && !g_pinned) ui_hide();
        return 0;
    case WM_ACTIVATEAPP:
        if (!wp && U.visible && !U.menu_open && !g_pinned) ui_hide();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_DPICHANGED: {
        UINT dpi = LOWORD(wp);
        if (dpi != U.dpi && U.visible) {
            RECT *sug = (RECT *)lp;
            ui_metrics(dpi);
            icons_reset(ui_icon_px());
            int cx = (sug->left + sug->right) / 2, cy = (sug->top + sug->bottom) / 2;
            SetWindowPos(h, HWND_TOPMOST, cx - U.W / 2, cy - U.H / 2, U.W, U.H, SWP_NOACTIVATE);
            rebuild_rows(true);
            snap_highlight();
            ui_invalidate();
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if ((wp == VK_LWIN || wp == VK_RWIN) && hotkey_is_win_tap()) return 0;
        if (ui_keydown(wp)) return 0;
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if ((wp == VK_LWIN || wp == VK_RWIN) && hotkey_is_win_tap()) return 0;
        break;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_TASKLIST && hotkey_is_win_tap()) return 0;
        break;
    case WM_SYSCHAR:
        return 0;
    case WM_CHAR:
        ui_char((WCHAR)wp);
        return 0;
    case WM_MOUSEMOVE:
        ui_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONDOWN:
        ui_mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), false);
        return 0;
    case WM_LBUTTONDBLCLK:
        ui_mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), true);
        return 0;
    case WM_LBUTTONUP:
        ui_mouse_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (U.tg.open || cm_inside(x, y)) return 0;
        int r = row_at(y);
        if (row_selectable(r)) {
            U.sel = r;
            U.armed = NULL;
            ui_context_menu(x, y, false);
        } else {
            cm_close(true);
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        ui_wheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    case WM_TIMER:
        if (wp == TIMER_REHOOK) {
            KillTimer(h, TIMER_REHOOK);
            hook_rearm();
            if (U.app_mode) taskbar_button_remove();
            return 0;
        }
        if (wp == TIMER_UPDATE) {
            upd_on_timer();
            return 0;
        }
        if (wp == TIMER_CARET) {
            U.caret_on = !U.caret_on;
            ui_invalidate();
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (lp && !wcscmp((const WCHAR *)lp, L"ImmersiveColorSet")) {
            theme_update();
            ui_invalidate();
            tb_theme_changed();
            media_theme_changed();
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) hook_reinstall();
        return TRUE;
    case WM_QUERYENDSESSION:
        history_save();
        return TRUE;
    case WM_ENDSESSION:
        if (wp) history_save();
        return 0;
    case WM_CLOSE:
        ui_hide();
        return 0;
    case WM_APP_TOGGLE:
        if (g_ktrace_on) log_msg("toggle t=%lu: visible=%d closing=%d fg=%p", GetTickCount(), U.visible, U.closing, (void *)GetForegroundWindow());
        ui_toggle();
        return 0;
    case WM_APP_SHOW:
        ui_show();
        return 0;
    case WM_APP_APPS_READY:
        on_apps_ready((AppList *)lp);
        return 0;
    case WM_APP_ICONS_READY:
        if (icons_process_results()) ui_invalidate();
        return 0;
    case WM_APP_FILES_READY:
        on_files_ready((FileResults *)lp);
        return 0;
    case WM_APP_SETTINGS:
        settings_open();
        return 0;
    case WM_APP_TASKBAR:
        tb_sync();
        return 0;
    case WM_APP_MEDIA:
        media_sync();
        return 0;
    case WM_APP_UPDATE:
        upd_on_message();
        ui_invalidate();
        settings_invalidate();
        return 0;
    case WM_APP_CLICK_OUTSIDE:
        if (U.visible && !U.closing && !U.menu_open && !g_pinned) {
            U.no_focus_restore = true;
            ui_hide();
        }
        return 0;
    case WM_APP_LAUNCH_FAILED: {
        WCHAR *key = (WCHAR *)lp;
        if (key) {
            history_forget((u8)wp, key);
            free(key);
        }
        return 0;
    }
    case WM_APP_TRAY:
        if (lp == WM_LBUTTONUP) ui_toggle();
        else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) tray_menu();
        return 0;
    case WM_APP_EXIT:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (g_ktrace_on) ktrace_dump();
        tray_remove();
        history_save();
        if (g_ev_own_allowed) ev_own_stop(0);
        PostQuitMessage(0);
        return 0;
    default:
        if (msg == g_wm_taskbar_created && msg) {
            tray_add();
            tb_explorer_restarted();
            media_explorer_restarted();
            return 0;
        }
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void write_utf8_file(const WCHAR *path, Buf *b) { write_file_atomic(path, b->data ? b->data : "", (DWORD)b->len); }

static int cmd_dump_apps(const WCHAR *out)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    f64 t0 = time_now();
    AppList *l = apps_enumerate();
    f64 t1 = time_now();
    Buf b = { 0 };
    buf_printf(&b, "enumerated in %.1f ms, %d apps\n", (t1 - t0) * 1000.0, l ? l->shell_count : -1);
    for (int i = 0; l && i < l->count; i++) {
        App *a = &l->apps[i];
        buf_put_w(&b, a->name);
        buf_put(&b, "\t", 1);
        buf_put_w(&b, a->id);
        buf_put(&b, "\t", 1);
        if (a->path) buf_put_w(&b, a->path);
        buf_put(&b, "\n", 1);
    }
    write_utf8_file(out, &b);
    free(b.data);
    return 0;
}

static FileResults *g_test_result;
static FileResults *g_test_pages[4];
static int g_test_page_count;

static LRESULT CALLBACK test_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_APP_FILES_READY) {
        FileResults *r = (FileResults *)lp;
        if (r && r->page) g_test_pages[g_test_page_count++] = r;
        else g_test_result = r;
        if (r && r->status == EV_OK && r->fetched < r->total && g_test_page_count < (int)countof(g_test_pages)) ev_request_more(r->id);
        else PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int cmd_test_everything(const WCHAR *query, const WCHAR *out)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = test_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MixLauncherTest";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    g_ev_own_allowed = g_elevated;
    ev_start();
    f64 t0 = time_now();
    ev_request(1, query);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) DispatchMessageW(&msg);
    FileResults *r = g_test_result;
    f64 t1 = time_now();
    Buf b = { 0 };
    if (!r) {
        buf_printf(&b, "no results\n");
    } else {
        buf_printf(&b, "status %d, total %u, fetched %u, count %d, %.1f ms incl. pages\n", r->status, r->total, r->fetched, r->count,
                   (t1 - t0) * 1000.0);
        for (int i = 0; i < r->count && i < 60; i++) {
            buf_printf(&b, "%8.0f  %s  ", r->items[i].score, (r->items[i].flags & FI_FOLDER) ? "D" : "F");
            buf_put_w(&b, r->items[i].full);
            buf_put(&b, "\n", 1);
        }
        for (int k = 0; k < g_test_page_count; k++) {
            FileResults *pg = g_test_pages[k];
            buf_printf(&b, "page %d: status %d, count %d, fetched %u of %u\n", k + 1, pg->status, pg->count, pg->fetched, pg->total);
            for (int i = 0; i < pg->count && i < 3; i++) {
                buf_printf(&b, "  %8.0f  ", pg->items[i].score);
                buf_put_w(&b, pg->items[i].full);
                buf_put(&b, "\n", 1);
            }
        }
    }
    write_utf8_file(out, &b);
    free(b.data);
    return 0;
}

// Installer hooks: close every running launcher and our copy of Everything, and wait for them,
// so their files can be replaced or removed.
static void shutdown_running(void)
{
    HWND h = NULL;
    while ((h = FindWindowExW(NULL, h, WINDOW_CLASS, NULL)) != NULL) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
        PostMessageW(h, WM_APP_EXIT, 0, 0);
        if (p) {
            WaitForSingleObject(p, 5000);
            CloseHandle(p);
        }
    }
    ev_own_stop(5000);
}

static void init_paths(void)
{
    GetModuleFileNameW(NULL, g_exe_path, MAX_PATH);
    WCHAR base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base);
    _snwprintf(g_data_dir, MAX_PATH, L"%s\\MixLauncher", base);
    g_data_dir[MAX_PATH - 1] = 0;
    CreateDirectoryW(g_data_dir, NULL);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline_a, int show)
{
    (void)prev;
    (void)cmdline_a;
    (void)show;
    time_init();
    opt_out_of_throttling(NULL);
    g_lang_ru = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
    init_paths();
    elevation_init();

    typedef BOOL (WINAPI *PFN_SetDpiCtx)(HANDLE);
    PFN_SetDpiCtx set_dpi = (PFN_SetDpiCtx)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (set_dpi) set_dpi((HANDLE)(intptr_t)-4 /*PER_MONITOR_AWARE_V2*/);

    int argc = 0;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool opt_show = false, opt_settings = false;
    const WCHAR *opt_query = NULL, *opt_update_as = NULL;
    DWORD opt_after_update = 0;
    int opt_backdrop = -1, opt_theme = -1, opt_lang = -1, opt_style = -1;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--exit")) {
            HWND other = FindWindowW(WINDOW_CLASS, NULL);
            if (other) PostMessageW(other, WM_APP_EXIT, 0, 0);
            return 0;
        } else if (!wcscmp(argv[i], L"--shutdown")) {
            shutdown_running();
            return 0;
        } else if (!wcscmp(argv[i], L"--uninstall")) {
            shutdown_running();
            autostart_set(false);
            run_key_set(false);
            return 0;
        } else if (!wcscmp(argv[i], L"--autostart") && i + 1 < argc) {
            return autostart_set(!wcscmp(argv[i + 1], L"on")) ? 0 : 1;
        } else if (!wcscmp(argv[i], L"--test-unelevated") && i + 1 < argc) {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            WCHAR args[MAX_PATH + 64];
            _snwprintf(args, countof(args), L"/c whoami /groups > \"%s\"", argv[i + 1]);
            args[countof(args) - 1] = 0;
            return shell_exec_unelevated_ex(L"cmd.exe", args, NULL, NULL, SW_HIDE) ? 0 : 1;
        } else if (!wcscmp(argv[i], L"--write-icon") && i + 1 < argc) {
            return app_icon_write_ico(argv[i + 1]) ? 0 : 1;
        } else if (!wcscmp(argv[i], L"--media-dump") && i + 1 < argc) {
            config_load();
            return media_dump(argv[i + 1], i + 2 < argc ? _wtoi(argv[i + 2]) : 96, i + 3 < argc && !wcscmp(argv[i + 3], L"light"),
                              i + 4 < argc ? argv[i + 4] : NULL);
        } else if (!wcscmp(argv[i], L"--dump-apps") && i + 1 < argc) {
            config_load();
            return cmd_dump_apps(argv[i + 1]);
        } else if (!wcscmp(argv[i], L"--test-everything") && i + 2 < argc) {
            config_load();
            return cmd_test_everything(argv[i + 1], argv[i + 2]);
        } else if (!wcscmp(argv[i], L"--backdrop") && i + 1 < argc) {
            i++;
            opt_backdrop = !wcscmp(argv[i], L"solid") ? BACKDROP_SOLID : !wcscmp(argv[i], L"acrylic") ? BACKDROP_ACRYLIC : BACKDROP_BLUR;
        } else if (!wcscmp(argv[i], L"--theme") && i + 1 < argc) {
            i++;
            opt_theme = !wcscmp(argv[i], L"dark") ? 1 : !wcscmp(argv[i], L"light") ? 2 : 0;
        } else if (!wcscmp(argv[i], L"--style") && i + 1 < argc) {
            i++;
            for (int k = 0; k < STYLE__COUNT; k++)
                if (!_wcsicmp(argv[i], k == STYLE_RAYCAST ? L"raycast" : k == STYLE_WIN11 ? L"windows" : k == STYLE_COMPACT ? L"compact" : L"standard"))
                    opt_style = k;
        } else if (!wcscmp(argv[i], L"--lang") && i + 1 < argc) {
            i++;
            opt_lang = !wcscmp(argv[i], L"ru") ? 1 : !wcscmp(argv[i], L"en") ? 2 : 0;
        } else if (!wcscmp(argv[i], L"--other-monitor")) {
            g_other_monitor = true;
            g_ktrace_on = true;
        } else if (!wcscmp(argv[i], L"--after-update") && i + 1 < argc) {
            opt_after_update = (DWORD)_wtoi(argv[++i]);
        } else if (!wcscmp(argv[i], L"--update-as") && i + 1 < argc) {
            opt_update_as = argv[++i];  // testing: pretend to be this version
        } else if (!wcscmp(argv[i], L"--update-dry")) {
            UPD.dry_run = true;  // testing: download the update but do not install it
        } else if (!wcscmp(argv[i], L"--pin")) {
            g_pinned = true;
        } else if (!wcscmp(argv[i], L"--settings")) {
            opt_settings = true;
        } else if (!wcscmp(argv[i], L"--show")) {
            opt_show = true;
        } else if (!wcscmp(argv[i], L"--query") && i + 1 < argc) {
            opt_query = argv[++i];
            opt_show = true;
        }
    }

    if (opt_after_update) {
        // A portable update: the old instance is exiting and still holds the single-instance mutex.
        HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, opt_after_update);
        if (old) {
            WaitForSingleObject(old, 15000);
            CloseHandle(old);
        }
    }
    HANDLE mutex = g_pinned ? INVALID_HANDLE_VALUE : CreateMutexW(NULL, TRUE, L"MixLauncher.SingleInstance.7f3c1e2a");
    if (!g_pinned && (GetLastError() == ERROR_ALREADY_EXISTS || !mutex)) {  // no mutex: owned by an elevated instance
        HWND other = FindWindowW(WINDOW_CLASS, NULL);
        if (other) {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(other, opt_settings ? WM_APP_SETTINGS : WM_APP_SHOW, 0, 0);
        }
        return 0;
    }

    config_load();
    if (opt_backdrop >= 0) g_cfg.backdrop = opt_backdrop;
    if (opt_theme >= 0) g_cfg.theme = opt_theme;
    if (opt_style >= 0) g_cfg.style = opt_style;
    if (opt_lang >= 0) {
        g_cfg.language = opt_lang;
        lang_apply();
    }
    history_load();
    tags_load();
    arena_init(&U.recent_arena, 1ull << 20);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    theme_update();

    g_icon_big = app_icon_create(GetSystemMetrics(SM_CXICON));
    g_icon_small = app_icon_create(GetSystemMetrics(SM_CXSMICON));

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hIcon = g_icon_big;
    wc.hIconSm = g_icon_small;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS;
    RegisterClassExW(&wc);

    POINT origin = { 0, 0 };
    ui_metrics(monitor_dpi(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY)));
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED, WINDOW_CLASS, L"MixLauncher",
                             WS_POPUP, 0, 0, U.W, U.H, NULL, NULL, inst, NULL);
    if (!g_hwnd) return 1;
    theme_update();
    U.backdrop_ok = backdrop_apply(g_hwnd);
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    BOOL no_dwm_anim = TRUE;
    DwmSetWindowAttribute(g_hwnd, 3 /*DWMWA_TRANSITIONS_FORCEDISABLED*/, &no_dwm_anim, sizeof no_dwm_anim);

    if (!font_init()) {
        MessageBoxW(NULL, TR("Не удалось инициализировать DirectWrite.", "Failed to initialize DirectWrite."), L"MixLauncher", MB_ICONERROR);
        return 1;
    }
    if (!r_init(g_hwnd, U.W, U.H)) {
        MessageBoxW(NULL, TR("Не удалось инициализировать Direct3D 11.", "Failed to initialize Direct3D 11."), L"MixLauncher", MB_ICONERROR);
        return 1;
    }
    font_reset_atlas();
    icons_start(ui_icon_px());

    AppList *cached = apps_cache_load();
    u64 cached_sig = cached ? cached->signature : 0;
    if (cached) on_apps_ready(cached);
    g_index_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    U.last_reindex = time_now();
    HANDLE t = CreateThread(NULL, 0, indexer_thread, (void *)(uintptr_t)cached_sig, 0, NULL);
    if (t) CloseHandle(t);

    g_ev_own_allowed = g_elevated && !g_pinned;
    ev_start();
    launch_start();
    if (!g_pinned) hook_start(g_cfg.hk);

    g_wm_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(g_hwnd, g_wm_taskbar_created, MSGFLT_ALLOW, NULL);
    uipi_allow(g_hwnd, WM_APP_TRAY);
    uipi_allow(g_hwnd, WM_APP_EXIT);
    uipi_allow(g_hwnd, WM_APP_SHOW);
    uipi_allow(g_hwnd, WM_APP_SETTINGS);
    if (!g_pinned) tray_add();
    if (g_cfg.taskbar_button && !g_pinned) tb_set_enabled(true);
    if (g_cfg.media_widget && !g_pinned) media_set_enabled(true);
    elevation_housekeeping_start();
    upd_init(opt_update_as);
    upd_schedule(15000);
    if (opt_update_as) upd_check();

    if (opt_settings) settings_open();
    if (opt_show) {
        ui_show();
        if (opt_query) edit_replace(0, U.qlen, opt_query, wlen(opt_query));
    }
    LocalFree(argv);

    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        bool rendered = false, anim = false;
        if (settings_needs_render()) {
            settings_render();
            rendered = true;
            anim |= SW.animating;
        }
        if (U.visible && (U.dirty || U.animating)) {
            ui_render();
            rendered = true;
            anim |= U.animating;
        }
        if (anim) DwmFlush();  // composition swap chains give no back-pressure
        if (rendered) continue;
        MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
done:
    history_save();
    ExitProcess(0);
}
