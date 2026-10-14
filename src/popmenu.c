// Windows 11-style popup menu in its own window (the tray menu): DWM acrylic, rounded corners, Fluent icons,
// a slide-in from the top edge, keyboard navigation. Same look as the mix-widgets tray menu.

#define PM_CLASS L"MixLauncherMenu"
#define PM_MAX 16
#define PM_ITEM_H 34.f
#define PM_SEP_H 9.f
#define PM_PADV 4.f
#define PM_SCALE 0.87f  // a bit more compact than the mix-widgets menu

typedef struct PmItem {
    int id;  // 0 = separator
    const WCHAR *label;
    u32 glyph;
} PmItem;

static struct {
    HWND hwnd;
    RTarget target;
    PmItem items[PM_MAX];
    int n, hover;
    f32 s, w, h;  // w, h in DIPs
    int W, H;
    bool dirty, animating, backdrop;
    f64 t0;
    void (*pick)(int id);
} PM = { .hover = -1 };

static f32 pm_item_y(int i)
{
    f32 y = PM_PADV;
    for (int k = 0; k < i; k++) y += PM.items[k].id ? PM_ITEM_H : PM_SEP_H;
    return y;
}

static int pm_item_at(int mx, int my)
{
    f32 x = (f32)mx / PM.s, y = (f32)my / PM.s;
    if (x < 0 || x > PM.w) return -1;
    f32 yy = PM_PADV;
    for (int i = 0; i < PM.n; i++) {
        f32 h = PM.items[i].id ? PM_ITEM_H : PM_SEP_H;
        if (y >= yy && y < yy + h) return PM.items[i].id ? i : -1;
        yy += h;
    }
    return -1;
}

static int pm_step(int from, int dir)
{
    int i = from >= 0 ? from : dir > 0 ? -1 : PM.n;
    for (int k = 0; k < PM.n; k++) {
        i = (i + dir + PM.n) % PM.n;
        if (PM.items[i].id) return i;
    }
    return -1;
}

static void pm_close(void)
{
    HWND h = PM.hwnd;
    PM.hwnd = NULL;  // DestroyWindow deactivates the menu, and WM_ACTIVATE comes back here
    if (h) DestroyWindow(h);
}

static void pm_activate(int i)
{
    if (i < 0 || i >= PM.n || !PM.items[i].id) return;
    int id = PM.items[i].id;
    void (*pick)(int) = PM.pick;
    pm_close();
    if (pick) pick(id);
}

static bool pm_needs_render(void) { return PM.hwnd && (PM.dirty || PM.animating) && PM.target.ok; }

static void pm_render(void)
{
    if (!PM.hwnd || !PM.target.ok || !R.ok) return;
    r_use(&PM.target);
    r_wait_frame();
    f32 dur = anims_enabled() ? 0.22f : 0.f;
    f32 t = dur > 0 ? CLAMP((f32)(time_now() - PM.t0) / dur, 0.f, 1.f) : 1.f;
    PM.animating = t < 1.f;
    f32 e = ease_out_cubic(t), s = PM.s;
    Theme *th = &U.th;
    u32 text = th->dark ? RGBA(255, 255, 255, 255) : RGBA(0, 0, 0, 228);
    u32 hover = th->dark ? RGBA(255, 255, 255, 21) : RGBA(0, 0, 0, 15);
    u32 divider = th->dark ? RGBA(255, 255, 255, 31) : RGBA(0, 0, 0, 31);
    for (int pass = 0; pass < 2; pass++) {
        R.scale = 1.f;
        r_begin();
        if (!PM.backdrop) r_rect(0, 0, (f32)PM.W, (f32)PM.H, th->bg_solid, 0);
        R.opacity = e;
        f32 oy = -(1.f - e) * 10.f * s, fs = 14.f * s;
        for (int i = 0; i < PM.n; i++) {
            const PmItem *it = &PM.items[i];
            f32 y = floorf(pm_item_y(i) * s + oy + 0.5f);
            if (!it->id) {
                r_rect(0, floorf(y + PM_SEP_H * 0.5f * s), (f32)PM.W, 1, divider, 0);
                continue;
            }
            f32 rx = floorf(4.f * s + 0.5f), ry = y + floorf(2.f * s + 0.5f), rw = (f32)PM.W - 2.f * rx, rh = floorf((PM_ITEM_H - 4.f) * s + 0.5f);
            f32 cy = ry + rh * 0.5f;
            if (i == PM.hover) r_rect(rx, ry, rw, rh, hover, floorf(4.f * s + 0.5f));
            if (it->glyph) text_draw_icon(it->glyph, 16.f * s, rx + 18.f * s, cy, text);
            f32 lx = rx + floorf(40.f * s + 0.5f);
            text_draw_fit(FONT_TEXT, fs, lx, floorf(cy + font_cap_height(FONT_TEXT, fs) * 0.5f + 0.5f), (f32)PM.W - lx - 12.f * s, it->label, -1, text,
                          false);
        }
        R.opacity = 1.f;
        if (!F.overflow) break;
        font_reset_atlas();
        F.overflow = false;
    }
    r_end_and_present();
    r_use(&R.main);
    PM.dirty = false;
}

static LRESULT CALLBACK pm_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp) return 0;
        break;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_NCACTIVATE:
        // Keeps the acrylic backdrop (DWM draws it only for an active frame), as the launcher window does.
        return DefWindowProcW(h, msg, TRUE, -1);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        PM.dirty = true;
        return 0;
    }
    case WM_MOUSEMOVE: {
        int i = pm_item_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i != PM.hover) {
            PM.hover = i;
            PM.dirty = true;
        }
        TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (PM.hover >= 0) {
            PM.hover = -1;
            PM.dirty = true;
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        pm_activate(pm_item_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        switch (wp) {
        case VK_ESCAPE:
        case VK_MENU:
            pm_close();
            break;
        case VK_UP:
        case VK_DOWN:
            PM.hover = pm_step(PM.hover, wp == VK_DOWN ? 1 : -1);
            PM.dirty = true;
            break;
        case VK_RETURN:
        case VK_SPACE:
            pm_activate(PM.hover);
            break;
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) pm_close();
        return 0;
    case WM_DESTROY:
        r_target_release(&PM.target);
        if (PM.hwnd == h) PM.hwnd = NULL;
        U.menu_open = false;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// Opens the menu at a screen point (the cursor), below-right of it, flipping to fit the work area.
static void pm_open(const PmItem *items, int n, POINT at, void (*pick)(int id))
{
    pm_close();
    static bool registered;
    if (!registered) {
        registered = true;
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = pm_wndproc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = PM_CLASS;
        RegisterClassExW(&wc);
    }
    theme_update();
    PM.n = MIN(n, PM_MAX);
    memcpy(PM.items, items, sizeof(PmItem) * PM.n);
    PM.hover = -1;
    PM.pick = pick;

    HMONITOR mon = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    RECT wa = mi.rcWork;
    PM.s = (f32)monitor_dpi(mon) / 96.f * PM_SCALE;
    f32 tw = 0;
    for (int i = 0; i < PM.n; i++)
        if (PM.items[i].id) tw = MAX(tw, text_width(FONT_TEXT, 14.f * PM.s, PM.items[i].label, -1) / PM.s);
    PM.w = CLAMP(tw + 52.f + 24.f, 170.f, 360.f);
    PM.h = 2.f * PM_PADV;
    for (int i = 0; i < PM.n; i++) PM.h += PM.items[i].id ? PM_ITEM_H : PM_SEP_H;
    PM.W = (int)(PM.w * PM.s + 0.5f);
    PM.H = (int)(PM.h * PM.s + 0.5f);
    int x = at.x, y = at.y;
    if (x + PM.W > wa.right) x -= PM.W;
    if (y + PM.H > wa.bottom) y = MAX(MIN(y, (int)wa.bottom) - PM.H, (int)wa.top);  // above the taskbar, not over it
    x = MAX(x, (int)wa.left);

    PM.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, PM_CLASS, L"MixLauncher", WS_POPUP, x, y, PM.W, PM.H,
                              NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!PM.hwnd) return;
    BOOL d = U.th.dark;
    DwmSetWindowAttribute(PM.hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &d, sizeof d);
    int corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(PM.hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof corner);
    COLORREF none = 0xFFFFFFFE;  // DWMWA_COLOR_NONE: no outline
    DwmSetWindowAttribute(PM.hwnd, 34 /*DWMWA_BORDER_COLOR*/, &none, sizeof none);
    MARGINS m = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(PM.hwnd, &m);
    int type = 3;  // DWMSBT_TRANSIENTWINDOW
    PM.backdrop = SUCCEEDED(DwmSetWindowAttribute(PM.hwnd, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &type, sizeof type));
    SetWindowPos(PM.hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (!r_target_init(&PM.target, PM.hwnd, PM.W, PM.H)) {
        DestroyWindow(PM.hwnd);
        return;
    }
    U.menu_open = true;
    PM.t0 = time_now();
    PM.dirty = true;
    pm_render();
    ShowWindow(PM.hwnd, SW_SHOW);
    SetForegroundWindow(PM.hwnd);
}
