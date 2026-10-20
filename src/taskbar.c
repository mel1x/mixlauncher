// The Start button: a layered child window of each taskbar over its own Start button (found with UI Automation)
// that opens the launcher and draws our icon on a plate of the bar's color. Also the search clicks
// (taskbar_search) and the UI Automation helpers the media widget shares.

#define TB_MAX 4
#define TB_CLASS L"MixLauncherStartButton"
#define TB_TIMER_ANIM 1

// ---- UI Automation

static IUIAutomation *uia_create(void)
{
    IUIAutomation *uia = NULL;
    if (FAILED(CoCreateInstance(&ML_CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &ML_IID_IUIAutomation, (void **)&uia))) uia = NULL;
    return uia;
}

// Matches a string property (UIA_AutomationIdPropertyId, UIA_ClassNamePropertyId).
static IUIAutomationCondition *uia_cond(IUIAutomation *uia, PROPERTYID prop, const WCHAR *value)
{
    IUIAutomationCondition *c = NULL;
    VARIANT v = variant_str(value);
    if (FAILED(IUIAutomation_CreatePropertyCondition(uia, prop, v, &c))) c = NULL;
    VariantClear(&v);
    return c;
}

// The first element in a window that matches the condition.
static IUIAutomationElement *uia_find(IUIAutomation *uia, HWND wnd, IUIAutomationCondition *cond)
{
    IUIAutomationElement *root = NULL, *el = NULL;
    if (wnd && cond && SUCCEEDED(IUIAutomation_ElementFromHandle(uia, wnd, &root)) && root) {
        IUIAutomationElement_FindFirst(root, TreeScope_Descendants, cond, &el);
        IUIAutomationElement_Release(root);
    }
    return el;
}


// ---- the Start button

typedef struct TbButton {
    HWND taskbar, wnd;
    RECT screen;
    int w, h;
    bool hover, press;
    f32 hover_t, press_t, open_t;
    f64 last_anim, bounce_at;  // when the launcher opened from here: the icon squeezes and springs back
    COLORREF plate, pending;   // the bar's color under the button, and a new one waiting to be confirmed
    bool plate_ok, pending_ok;
    f64 pending_at;
    Layer layer;
    f64 sampled;
    u32 *img;  // the custom icon, img_n square
    int img_n;
    u32 img_gen;
} TbButton;

static struct {
    bool enabled;
    bool search;  // clicks on the taskbar's search open the launcher (the hook thread swallows them)
    bool started;
    HANDLE wake;
    SRWLOCK lock;
    struct { HWND taskbar; RECT r; } found[TB_MAX], search_found[TB_MAX];
    int nfound, nsearch;
    volatile LONG menu_for;
    volatile LONG reset;
    volatile f64 fast_until;
    TbButton b[TB_MAX];
    int n;
    bool light;
    HMONITOR open_on;  // the launcher is open on this monitor: its button gets the open-window plate
    u32 icon_gen;
} TB = { .menu_for = 0, .icon_gen = 1 };

static RECT g_tb_hit[TB_MAX];
static volatile LONG g_tb_hit_count;

static bool tb_hit(POINT pt)
{
    LONG n = g_tb_hit_count;
    for (LONG i = 0; i < n && i < TB_MAX; i++)
        if (PtInRect(&g_tb_hit[i], pt)) return true;
    return false;
}

// A press on the search of a taskbar (Windows 11: icon, icon and label, box; Windows 10: icon or box) that nothing covers.
// Called from the mouse hook: no UI Automation here, only the rectangles the taskbar thread found.
static bool tb_search_hit(POINT pt)
{
    if (!TB.search) return false;
    HWND bar = NULL;
    AcquireSRWLockShared(&TB.lock);
    for (int i = 0; i < TB.nsearch; i++)
        if (PtInRect(&TB.search_found[i].r, pt)) bar = TB.search_found[i].taskbar;
    ReleaseSRWLockShared(&TB.lock);
    if (!bar) return false;
    HWND w = WindowFromPoint(pt);
    return w && GetAncestor(w, GA_ROOT) == bar && !window_is_ours(w);
}

static int tb_taskbars(HWND *out)
{
    int n = 0;
    HWND h = FindWindowW(L"Shell_TrayWnd", NULL);
    if (h) out[n++] = h;
    for (h = NULL; n < TB_MAX && (h = FindWindowExW(NULL, h, L"Shell_SecondaryTrayWnd", NULL)) != NULL;) out[n++] = h;
    return n;
}

static volatile LONG g_tb_centered = 1;

static bool tb_read_centered(void)
{
    DWORD v = 1;
    reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"TaskbarAl", &v);
    return v != 0;
}

static void CALLBACK tb_winevent(HWINEVENTHOOK hook, DWORD ev, HWND h, LONG obj, LONG child, DWORD tid, DWORD time)
{
    (void)hook, (void)ev, (void)tid, (void)time;
    // every window event in the system lands here: cheap checks first
    if (!g_tb_centered || obj != OBJID_WINDOW || child != CHILDID_SELF || !h) return;
    if (GetWindow(h, GW_OWNER) || (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return;
    if (GetAncestor(h, GA_ROOT) == h) {
        TB.fast_until = time_now() + 0.8;
        SetEvent(TB.wake);
    }
}

// Watches where the Start buttons (and the search) of every taskbar are: often while windows come and go on a
// centered bar (its buttons move with them), rarely otherwise. Posts WM_APP_TASKBAR when they moved.
static DWORD WINAPI tb_thread(void *param)
{
    (void)param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IUIAutomation *uia = uia_create();
    if (!uia) {
        log_msg("taskbar: UI Automation unavailable");
        return 0;
    }
    IUIAutomationCondition *cond = uia_cond(uia, UIA_AutomationIdPropertyId, L"StartButton"), *scond = NULL;
    // Windows 11 calls every kind of its search SearchButton; Windows 10 has a window of its own
    static const struct { bool cls; const WCHAR *val; } k_search[] = {
        { false, L"SearchButton" },
        { false, L"SearchBoxButton" },
        { true, L"TrayDummySearchControl" },
    };
    IUIAutomationCondition *sc[countof(k_search)] = { 0 };
    int nsc = 0;
    for (int i = 0; i < (int)countof(k_search); i++)
        if ((sc[nsc] = uia_cond(uia, k_search[i].cls ? UIA_ClassNamePropertyId : UIA_AutomationIdPropertyId, k_search[i].val)) != NULL) nsc++;
    if (nsc) IUIAutomation_CreateOrConditionFromNativeArray(uia, sc, nsc, &scond);
    for (int i = 0; i < nsc; i++) IUIAutomationCondition_Release(sc[i]);
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE, NULL, tb_winevent, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    HWND bars[TB_MAX] = { 0 };
    IUIAutomationElement *els[TB_MAX] = { 0 }, *sels[TB_MAX] = { 0 };
    RECT last[TB_MAX];
    int last_n = -1;
    f64 last_post = 0;
    f64 retry_at = 0, sretry_at = 0;
    memset(last, 0, sizeof last);
    for (;;) {
        f64 now = time_now();
        bool fast = now < TB.fast_until;
        bool active = TB.enabled || TB.search;
        MsgWaitForMultipleObjects(1, &TB.wake, FALSE, !active ? INFINITE : fast ? 15 : g_tb_centered ? 500 : 1000, QS_ALLINPUT);
        pump_messages();
        if (InterlockedExchange(&TB.reset, 0)) {
            for (int i = 0; i < TB_MAX; i++) {
                SAFE_RELEASE(els[i]);
                SAFE_RELEASE(sels[i]);
            }
            memset(bars, 0, sizeof bars);
            retry_at = sretry_at = 0;
            last_n = -1;
        }
        if (!TB.enabled && !TB.search) continue;
        if (!fast) g_tb_centered = tb_read_centered();

        HWND cur[TB_MAX];
        int n = tb_taskbars(cur);
        RECT rs[TB_MAX], srs[TB_MAX];
        HWND hs[TB_MAX], shs[TB_MAX];
        int nf = 0, ns = 0;
        for (int i = 0; i < n; i++) {
            if (bars[i] != cur[i]) {
                SAFE_RELEASE(els[i]);
                SAFE_RELEASE(sels[i]);
                bars[i] = cur[i];
            }
            if (TB.search) {
                if (!sels[i] && scond && time_now() >= sretry_at) {
                    sels[i] = uia_find(uia, cur[i], scond);
                    if (!sels[i]) sretry_at = time_now() + 3.0;  // search turned off in the taskbar settings
                }
                RECT r;
                BOOL off = FALSE;
                UIA_HWND sh = NULL;
                if (sels[i] && SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(sels[i], &r))) {
                    IUIAutomationElement_get_CurrentIsOffscreen(sels[i], &off);
                    IUIAutomationElement_get_CurrentNativeWindowHandle(sels[i], &sh);
                    if (sh && !IsWindowVisible((HWND)sh)) off = TRUE;
                    if (!off && r.right > r.left && r.bottom > r.top) {
                        shs[ns] = cur[i];
                        srs[ns++] = r;
                    }
                } else if (sels[i]) {
                    SAFE_RELEASE(sels[i]);
                }
            } else {
                SAFE_RELEASE(sels[i]);
            }
            if (!TB.enabled) {
                SAFE_RELEASE(els[i]);
                continue;
            }
            if (!els[i] && cond && time_now() >= retry_at) {
                els[i] = uia_find(uia, cur[i], cond);
                if (!els[i]) retry_at = time_now() + 1.0;
            }
            RECT r;
            if (els[i] && SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(els[i], &r))) {
                if (r.right > r.left && r.bottom > r.top) {
                    hs[nf] = cur[i];
                    rs[nf++] = r;
                }
            } else if (els[i]) {
                SAFE_RELEASE(els[i]);
            }
        }
        for (int i = n; i < TB_MAX; i++) {
            SAFE_RELEASE(els[i]);
            SAFE_RELEASE(sels[i]);
            bars[i] = NULL;
        }
        AcquireSRWLockExclusive(&TB.lock);
        TB.nsearch = ns;
        for (int i = 0; i < ns; i++) {
            TB.search_found[i].taskbar = shs[i];
            TB.search_found[i].r = srs[i];
        }
        ReleaseSRWLockExclusive(&TB.lock);

        LONG m = InterlockedExchange(&TB.menu_for, 0);
        if (m > 0) {
            HWND want = TB.b[m - 1].taskbar;
            for (int i = 0; i < n; i++)
                if (bars[i] == want && els[i]) {
                    IUIAutomationElement3 *e3 = NULL;
                    if (SUCCEEDED(IUIAutomationElement_QueryInterface(els[i], &ML_IID_IUIAutomationElement3, (void **)&e3)) && e3) {
                        HRESULT hr = IUIAutomationElement3_ShowContextMenu(e3);
                        if (FAILED(hr)) log_msg("taskbar: ShowContextMenu failed 0x%08lx", hr);
                        IUIAutomationElement3_Release(e3);
                    }
                }
        }

        if (!TB.enabled) continue;
        bool changed = nf != last_n;
        for (int i = 0; i < nf && !changed; i++)
            if (memcmp(&rs[i], &last[i], sizeof(RECT))) changed = true;
        if (changed || time_now() - last_post > 2.0) {
            last_post = time_now();
            AcquireSRWLockExclusive(&TB.lock);
            TB.nfound = nf;
            for (int i = 0; i < nf; i++) {
                TB.found[i].taskbar = hs[i];
                TB.found[i].r = rs[i];
            }
            ReleaseSRWLockExclusive(&TB.lock);
            memcpy(last, rs, sizeof(RECT) * nf);
            last_n = nf;
            PostMessageW(g_hwnd, WM_APP_TASKBAR, 0, 0);
            if (changed) TB.fast_until = MAX(TB.fast_until, time_now() + 0.3);
        }
    }
    if (hook) UnhookWinEvent(hook);
    return 0;
}

static bool taskbar_light(void)
{
    DWORD v = 0;
    reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme", &v);
    return v != 0;
}

static int tb_color_dist(COLORREF a, COLORREF b)
{
    return MAX(abs(GetRValue(a) - GetRValue(b)), MAX(abs(GetGValue(a) - GetGValue(b)), abs(GetBValue(a) - GetBValue(b))));
}

// Samples the bar color around the button: every point must show the bar itself and agree, and a new color
// needs a second sample to confirm it. Returns true when the plate color changed.
static bool tb_sample(TbButton *b)
{
    b->sampled = time_now();
    RECT tr;
    if (!GetWindowRect(b->taskbar, &tr)) return false;
    int h = b->screen.bottom - b->screen.top;
    int xs[2] = { b->screen.left - 3, b->screen.right + 3 };
    int ys[2] = { b->screen.top + h / 10 + 1, b->screen.bottom - h / 10 - 2 };
    COLORREF cs[4];
    int n = 0;
    HDC sdc = GetDC(NULL);
    if (!sdc) return false;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            POINT p = { xs[i], ys[j] };
            if (!PtInRect(&tr, p)) continue;
            // something (a fullscreen game) covers the bar
            if (GetAncestor(WindowFromPoint(p), GA_ROOT) != b->taskbar) {
                ReleaseDC(NULL, sdc);
                return false;
            }
            COLORREF c = GetPixel(sdc, p.x, p.y);
            if (c != CLR_INVALID) cs[n++] = c;
        }
    ReleaseDC(NULL, sdc);
    if (!n) return false;
    int r = 0, g = 0, bl = 0;
    for (int i = 0; i < n; i++) {
        for (int k = i + 1; k < n; k++)
            if (tb_color_dist(cs[i], cs[k]) > 12) return false;
        r += GetRValue(cs[i]);
        g += GetGValue(cs[i]);
        bl += GetBValue(cs[i]);
    }
    COLORREF c = RGB(r / n, g / n, bl / n);
    if (b->plate_ok && tb_color_dist(c, b->plate) <= 2) {
        b->pending_ok = false;
        return false;
    }
    // the first color is taken as is
    bool confirmed = !b->plate_ok || (b->pending_ok && tb_color_dist(c, b->pending) <= 4 && b->sampled - b->pending_at >= 0.3);
    if (!confirmed) {
        if (!b->pending_ok || tb_color_dist(c, b->pending) > 4) b->pending_at = b->sampled;
        b->pending = c;
        b->pending_ok = true;
        return false;
    }
    b->pending_ok = false;
    b->plate = c;
    b->plate_ok = true;
    return true;
}

// The custom icon file fitted into n x n (its largest frame for an .ico), premultiplied BGRA.
static u32 *tb_load_image(const WCHAR *path, int n)
{
    if (!path || !path[0] || n <= 0) return NULL;
    IWICImagingFactory *f = wic_factory();
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    u32 *out = NULL, *tmp = NULL;
    if (!f || FAILED(IWICImagingFactory_CreateDecoderFromFilename(f, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
    UINT frames = 0, best = 0, best_w = 0;
    IWICBitmapDecoder_GetFrameCount(dec, &frames);
    for (UINT i = 0; i < frames && i < 32; i++) {
        IWICBitmapFrameDecode *fr = NULL;
        UINT fw = 0, fh = 0;
        if (SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, i, &fr)) && fr) {
            IWICBitmapFrameDecode_GetSize(fr, &fw, &fh);
            if (fw > best_w) {
                best_w = fw;
                best = i;
            }
            IWICBitmapFrameDecode_Release(fr);
        }
    }
    if (FAILED(IWICBitmapDecoder_GetFrame(dec, best, &frame)) || !frame) goto done;
    UINT iw = 0, ih = 0;
    IWICBitmapFrameDecode_GetSize(frame, &iw, &ih);
    if (!iw || !ih) goto done;
    UINT tw = iw >= ih ? (UINT)n : MAX(1u, (UINT)((u64)n * iw / ih)), th = ih >= iw ? (UINT)n : MAX(1u, (UINT)((u64)n * ih / iw));
    tmp = wic_pixels(f, (IWICBitmapSource *)frame, tw, th);
    out = tmp ? (u32 *)calloc((size_t)n * n, 4) : NULL;
    for (UINT y = 0; out && y < th; y++) memcpy(out + (size_t)(y + (n - th) / 2) * n + (n - tw) / 2, tmp + (size_t)y * tw, (size_t)tw * 4);
done:
    free(tmp);
    SAFE_RELEASE(frame);
    SAFE_RELEASE(dec);
    SAFE_RELEASE(f);
    return out;
}

static f32 tb_sample_img(const u32 *img, int n, f32 x, f32 y, f32 *c)
{
    x -= 0.5f;
    y -= 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    f32 tx = x - (f32)x0, ty = y - (f32)y0, acc[4] = { 0, 0, 0, 0 };
    for (int k = 0; k < 4; k++) {
        int xx = x0 + (k & 1), yy = y0 + (k >> 1);
        if (xx < 0 || yy < 0 || xx >= n || yy >= n) continue;
        f32 wgt = ((k & 1) ? tx : 1.f - tx) * ((k >> 1) ? ty : 1.f - ty);
        u32 p = img[yy * n + xx];
        acc[0] += wgt * (f32)((p >> 16) & 255);
        acc[1] += wgt * (f32)((p >> 8) & 255);
        acc[2] += wgt * (f32)(p & 255);
        acc[3] += wgt * (f32)(p >> 24);
    }
    for (int k = 0; k < 3; k++) c[k] = acc[k] / 255.f;
    return acc[3] / 255.f;
}

static void tb_shape(int style, f32 fx, f32 fy, f32 u, f32 dim, const f32 *fg, f32 *c, f32 *a)
{
    if (style == TBI_GRID) {
        f32 t = 4.6f * u, g = 1.6f * u, off = t + g * 0.5f;
        for (int d = 0; d < 4; d++) {
            f32 sx = (d & 1) ? 1.f : -1.f, sy = (d & 2) ? 1.f : -1.f;
            f32 m = CLAMP(0.5f - sd_box(fx - sx * off, fy - sy * off, t, t, 1.6f * u), 0.f, 1.f);
            if (m > 0.f) px_over(c, a, fg, m * (d == 0 ? 1.f : dim));
        }
    } else if (style == TBI_PETALS) {
        // four quarter discs around the center, the top left one bright
        f32 g = 1.2f * u, rad = 9.3f * u;
        for (int d = 0; d < 4; d++) {
            f32 sx = (d & 1) ? 1.f : -1.f, sy = (d & 2) ? 1.f : -1.f;
            f32 qx = (fx - sx * g) * sx, qy = (fy - sy * g) * sy;
            f32 sd = MAX(sqrtf(qx * qx + qy * qy) - rad, MAX(-qx, -qy));
            f32 m = CLAMP(0.5f - sd, 0.f, 1.f);
            if (m > 0.f) px_over(c, a, fg, m * (d == 0 ? 1.f : dim));
        }
    } else if (style == TBI_LAYERS) {
        // a card behind another, the front one bright
        f32 half = 7.1f * u, off = 3.4f * u, rad = 3.2f * u;
        f32 m = CLAMP(0.5f - sd_box(fx + off, fy + off, half, half, rad), 0.f, 1.f);
        if (m > 0.f) px_over(c, a, fg, m * dim);
        m = CLAMP(0.5f - sd_box(fx - off, fy - off, half, half, rad), 0.f, 1.f);
        if (m > 0.f) px_over(c, a, fg, m);
    } else if (style == TBI_DOTS) {
        // nine dots, the middle one bright
        f32 step = 8.f * u, rad = 2.6f * u;
        for (int d = 0; d < 9; d++) {
            f32 qx = fx - (f32)(d % 3 - 1) * step, qy = fy - (f32)(d / 3 - 1) * step;
            f32 m = CLAMP(0.5f - (sqrtf(qx * qx + qy * qy) - rad), 0.f, 1.f);
            if (m > 0.f) px_over(c, a, fg, m * (d == 4 ? 1.f : dim));
        }
    } else {
        static const f32 k_dots[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
        f32 half = 3.3f * u, rad = 1.2f * u, off = 6.5f * u;
        for (int d = 0; d < 4; d++) {
            f32 qx = fx - k_dots[d][0] * off, qy = fy - k_dots[d][1] * off;
            f32 rx = (qx + qy) * 0.70710678f, ry = (qy - qx) * 0.70710678f;
            f32 m = CLAMP(0.5f - sd_box(rx, ry, half, half, rad), 0.f, 1.f);
            if (m > 0.f) px_over(c, a, fg, m * (d == 0 ? 1.f : dim));
        }
    }
}

// How much the icon is squeezed: a damped spring, down to about 0.84 and a little past 1 on the way back, as Start's logo does.
static f32 tb_bounce(TbButton *b)
{
    f32 t = (f32)(time_now() - b->bounce_at);
    if (!b->bounce_at || t >= 0.6f || g_cfg.reduced_motion) return 0.f;
    return 0.35f * expf(-9.f * t) * sinf(18.f * t);
}

static void tb_render(TbButton *b)
{
    int w = b->w, h = b->h;
    if (w <= 0 || h <= 0 || !b->wnd || !layer_fit(&b->layer, w, h)) return;

    f32 s = (f32)monitor_dpi(MonitorFromWindow(b->taskbar, MONITOR_DEFAULTTONEAREST)) / 96.f;
    f32 logo = floorf(24.f * s + 0.5f), plate = logo + 2.f * floorf(1.f * s + 0.5f);
    f32 cx = (f32)w > (f32)h * 1.05f ? (f32)w - (f32)h * 0.475f : (f32)w * 0.5f, cy = (f32)h * 0.5f;
    cx = floorf(cx) + ((int)logo & 1 ? 0.5f : 0.f);
    cy = floorf(cy) + ((int)logo & 1 ? 0.5f : 0.f);
    f32 box = MIN(floorf(40.f * s + 0.5f), (f32)h - 2.f * s), brad = 4.f * s;
    bool light = TB.light;
    f32 fg[3], plate_c[3], hov_c[3];
    for (int k = 0; k < 3; k++) {
        fg[k] = light ? 0.f : 1.f;
        hov_c[k] = light ? 0.f : 1.f;
    }
    COLORREF pc = b->plate_ok ? b->plate : (light ? RGB(238, 238, 238) : RGB(28, 28, 28));
    plate_c[0] = (f32)GetRValue(pc) / 255.f;
    plate_c[1] = (f32)GetGValue(pc) / 255.f;
    plate_c[2] = (f32)GetBValue(pc) / 255.f;
    f32 hov_a = light ? 0.055f * b->hover_t + 0.03f * b->press_t : 0.075f * b->hover_t - 0.02f * b->press_t;
    hov_a += (light ? 0.04f : 0.06f) * b->open_t * (1.f - 0.5f * b->hover_t);  // like a running app's icon
    f32 zoom = (1.f - 0.08f * b->press_t) * (1.f - tb_bounce(b)), u = logo / 24.f * zoom;
    f32 dim = (light ? 0.5f : 0.42f) + 0.18f * b->hover_t;
    bool replace = g_cfg.taskbar_replace_icon;
    int style = CLAMP(g_cfg.taskbar_icon, 0, TBI__COUNT - 1);
    if (replace && style == TBI_CUSTOM) {
        int n = (int)logo;
        if (!b->img || b->img_n != n || b->img_gen != TB.icon_gen) {
            free(b->img);
            b->img = tb_load_image(g_cfg.taskbar_icon_path, n);
            b->img_n = n;
            b->img_gen = TB.icon_gen;
        }
        if (!b->img) style = TBI_DIAMOND;
    }

    u32 *px = b->layer.bits;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            f32 fx = (f32)x + 0.5f - cx, fy = (f32)y + 0.5f - cy;
            f32 c[3] = { 0, 0, 0 }, a = 0;
            // without our icon only the hover highlight is drawn over the real button
            if (replace) px_over(c, &a, plate_c, CLAMP(0.5f - sd_box(fx, fy, plate * 0.5f, plate * 0.5f, 2.f * s), 0.f, 1.f));
            if (hov_a > 0.f) px_over(c, &a, hov_c, hov_a * CLAMP(0.5f - sd_box(fx, fy, box * 0.5f, box * 0.5f, brad), 0.f, 1.f));
            if (replace && style == TBI_CUSTOM) {
                f32 ic[3], ia = tb_sample_img(b->img, b->img_n, fx / zoom + logo * 0.5f, fy / zoom + logo * 0.5f, ic);
                if (ia > 0.f) {
                    f32 k = 1.f - 0.1f * b->press_t;
                    for (int q = 0; q < 3; q++) c[q] = ic[q] * k + c[q] * (1.f - ia);
                    a = ia + a * (1.f - ia);
                }
            } else if (replace) {
                tb_shape(style, fx, fy, u, dim, fg, c, &a);
            }
            px[y * w + x] = px_pack(c, a);
        }
    }
    layer_show(&b->layer, b->wnd);
}

static TbButton *tb_from_hwnd(HWND h)
{
    for (int i = 0; i < TB.n; i++)
        if (TB.b[i].wnd == h) return &TB.b[i];
    return NULL;
}

static void tb_animate(TbButton *b)
{
    f64 now = time_now();
    f32 dt = (f32)MIN(now - b->last_anim, 0.05);
    b->last_anim = now;
    bool anim = false;
    anim |= approach(&b->hover_t, b->hover || b->press ? 1.f : 0.f, 18.f, dt);
    anim |= approach(&b->press_t, b->press ? 1.f : 0.f, 30.f, dt);
    bool open = TB.open_on && MonitorFromWindow(b->taskbar, MONITOR_DEFAULTTONEAREST) == TB.open_on;
    anim |= approach(&b->open_t, open ? 1.f : 0.f, 18.f, dt);
    anim |= b->bounce_at && now - b->bounce_at < 0.6;
    tb_render(b);
    if (anim) SetTimer(b->wnd, TB_TIMER_ANIM, 15, NULL);
    else KillTimer(b->wnd, TB_TIMER_ANIM);
}

static void tb_kick(TbButton *b)
{
    b->last_anim = time_now();
    SetTimer(b->wnd, TB_TIMER_ANIM, 15, NULL);
    tb_animate(b);
}

static LRESULT CALLBACK tb_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    TbButton *b = tb_from_hwnd(h);
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
        return TRUE;
    case WM_MOUSEMOVE:
        if (b && !b->hover) {
            b->hover = true;
            track_mouse_leave(h);
            tb_kick(b);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (b) {
            b->hover = false;
            b->press = false;
            tb_kick(b);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (b) {
            b->press = true;
            SetCapture(h);
            tb_kick(b);
        }
        return 0;
    case WM_LBUTTONUP: {
        if (!b) return 0;
        bool was = b->press;
        b->press = false;
        ReleaseCapture();
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc;
        GetClientRect(h, &rc);
        tb_kick(b);
        if (was && PtInRect(&rc, p)) {
            AllowSetForegroundWindow(ASFW_ANY);
            ui_toggle();
        }
        return 0;
    }
    case WM_RBUTTONUP:
        if (b) {
            if (U.visible && !U.closing) ui_hide();
            InterlockedExchange(&TB.menu_for, (LONG)(b - TB.b) + 1);
            SetEvent(TB.wake);
        }
        return 0;
    case WM_TIMER:
        if (wp == TB_TIMER_ANIM && b) tb_animate(b);
        return 0;
    case WM_DESTROY:
        if (b) b->wnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void tb_destroy(TbButton *b)
{
    if (b->wnd && IsWindow(b->wnd)) DestroyWindow(b->wnd);
    b->wnd = NULL;
    free(b->img);
    b->img = NULL;
    layer_free(&b->layer);
}

static void tb_publish_hits(void)
{
    LONG n = 0;
    for (int i = 0; i < TB.n; i++)
        if (TB.b[i].wnd) g_tb_hit[n++] = TB.b[i].screen;
    g_tb_hit_count = n;
}

// Something of the taskbar's own (its XAML island) went above `h`. Our other taskbar windows don't count:
// raising over each other in turn makes the taskbar flicker.
static bool tb_covered(HWND h)
{
    for (HWND w = GetWindow(h, GW_HWNDPREV); w; w = GetWindow(w, GW_HWNDPREV))
        if (!window_is_ours(w)) return true;
    return false;
}

static void tb_sync(void)
{
    if (!TB.enabled) return;
    struct { HWND taskbar; RECT r; } f[TB_MAX];
    AcquireSRWLockShared(&TB.lock);
    int nf = TB.nfound;
    memcpy(f, TB.found, sizeof(f[0]) * nf);
    ReleaseSRWLockShared(&TB.lock);

    TbButton nb[TB_MAX];
    memset(nb, 0, sizeof nb);
    for (int i = 0; i < nf; i++) {
        TbButton *b = &nb[i];
        for (int k = 0; k < TB.n; k++)
            if (TB.b[k].taskbar == f[i].taskbar && TB.b[k].wnd && IsWindow(TB.b[k].wnd)) {
                *b = TB.b[k];  // moves its window, icon and pixels over
                TB.b[k].wnd = NULL;
                TB.b[k].img = NULL;
                memset(&TB.b[k].layer, 0, sizeof TB.b[k].layer);
            }
        b->taskbar = f[i].taskbar;
        b->screen = f[i].r;
    }
    for (int k = 0; k < TB.n; k++) tb_destroy(&TB.b[k]);
    memcpy(TB.b, nb, sizeof nb);
    TB.n = nf;

    for (int i = 0; i < TB.n; i++) {
        TbButton *b = &TB.b[i];
        POINT p = { b->screen.left, b->screen.top };
        ScreenToClient(b->taskbar, &p);
        int w = b->screen.right - b->screen.left, h = b->screen.bottom - b->screen.top;
        if (!b->wnd) {
            b->wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, TB_CLASS, L"", WS_CHILD | WS_CLIPSIBLINGS, p.x, p.y,
                                     w, h, b->taskbar, NULL, GetModuleHandleW(NULL), NULL);
            if (!b->wnd) {
                log_msg("taskbar: could not create the button window (%lu)", GetLastError());
                continue;
            }
            b->w = w;
            b->h = h;
            tb_sample(b);
            tb_render(b);
            SetWindowPos(b->wnd, HWND_TOP, p.x, p.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            continue;
        }
        bool resized = w != b->w || h != b->h;
        b->w = w;
        b->h = h;
        RECT cur;
        GetWindowRect(b->wnd, &cur);
        bool moved = memcmp(&cur, &b->screen, sizeof cur) != 0;
        f64 since = time_now() - b->sampled;
        bool resample = moved || since > 5.0 || ((b->pending_ok || !b->plate_ok) && since > 0.3);
        if ((resample && tb_sample(b)) || resized) tb_render(b);
        bool covered = tb_covered(b->wnd);
        if (moved || covered || !IsWindowVisible(b->wnd)) SetWindowPos(b->wnd, HWND_TOP, p.x, p.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    tb_publish_hits();
}

// The launcher opened on a monitor, or closed (NULL).
static void tb_set_open(HMONITOR mon)
{
    TB.open_on = mon;
    for (int i = 0; i < TB.n; i++) {
        TbButton *b = &TB.b[i];
        if (!b->wnd) continue;
        if (mon && MonitorFromWindow(b->taskbar, MONITOR_DEFAULTTONEAREST) == mon) b->bounce_at = time_now();
        tb_kick(b);
    }
}

static void tb_icon_changed(void)
{
    TB.icon_gen++;
    for (int i = 0; i < TB.n; i++) tb_render(&TB.b[i]);
}

static void tb_theme_changed(void)
{
    bool l = taskbar_light();
    if (l == TB.light) return;
    TB.light = l;
    for (int i = 0; i < TB.n; i++) {
        TB.b[i].plate_ok = false;
        TB.b[i].pending_ok = false;
        tb_render(&TB.b[i]);
    }
}

static void tb_explorer_restarted(void)
{
    for (int i = 0; i < TB.n; i++) tb_destroy(&TB.b[i]);
    TB.n = 0;
    tb_publish_hits();
    AcquireSRWLockExclusive(&TB.lock);
    TB.nsearch = 0;
    ReleaseSRWLockExclusive(&TB.lock);
    InterlockedExchange(&TB.reset, 1);
    TB.fast_until = time_now() + 3.0;
    if (TB.wake) SetEvent(TB.wake);
}

static void tb_thread_start(void)
{
    if (!TB.started) {
        TB.started = true;
        InitializeSRWLock(&TB.lock);
        TB.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        thread_start(tb_thread, NULL);
    }
    InterlockedExchange(&TB.reset, 1);
    SetEvent(TB.wake);
}

static void tb_set_search(bool on)
{
    TB.search = on;
    hook_search_clicks(on);
    if (on) {
        tb_thread_start();
    } else {
        AcquireSRWLockExclusive(&TB.lock);
        TB.nsearch = 0;
        ReleaseSRWLockExclusive(&TB.lock);
        if (TB.wake) SetEvent(TB.wake);
    }
}

static void tb_set_enabled(bool on)
{
    TB.enabled = on;
    if (on) {
        window_class(TB_CLASS, tb_wndproc, CS_DBLCLKS, NULL, NULL);
        TB.light = taskbar_light();
        tb_thread_start();
    } else {
        for (int i = 0; i < TB.n; i++) tb_destroy(&TB.b[i]);
        TB.n = 0;
        tb_publish_hits();
        AcquireSRWLockExclusive(&TB.lock);
        TB.nfound = 0;
        ReleaseSRWLockExclusive(&TB.lock);
        if (TB.wake) SetEvent(TB.wake);
    }
}
