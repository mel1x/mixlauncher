#define TB_MAX 4
#define TB_CLASS L"MixLauncherStartButton"
#define TB_TIMER_ANIM 1

static const GUID ML_CLSID_CUIAutomation = { 0xff48dba4, 0x60ef, 0x4201, { 0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e } };
static const GUID ML_IID_IUIAutomation = { 0x30cbe57d, 0xd9d0, 0x452a, { 0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee } };
static const GUID ML_IID_IUIAutomationElement3 = { 0x8471df34, 0xaee0, 0x4a01, { 0xa7, 0xde, 0x7d, 0xb9, 0xaf, 0x12, 0xc2, 0x96 } };

typedef struct TbButton {
    HWND taskbar, wnd;
    RECT screen;
    int w, h;
    bool hover, press;
    f32 hover_t, press_t;
    f64 last_anim;
    COLORREF plate;
    bool plate_ok;
    HDC dc;
    HBITMAP bmp;
    u32 *bits;
    int bmp_w, bmp_h;
    f64 sampled;
    u32 *img;
    int img_n;
    u32 img_gen;
} TbButton;

static struct {
    bool enabled;
    HANDLE thread, wake;
    SRWLOCK lock;
    struct { HWND taskbar; RECT r; } found[TB_MAX];
    int nfound;
    volatile LONG menu_for;
    volatile LONG reset;
    volatile f64 fast_until;
    TbButton b[TB_MAX];
    int n;
    bool light;
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
    DWORD v = 1, sz = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"TaskbarAl", RRF_RT_REG_DWORD, NULL, &v, &sz);
    return v != 0;
}

static void CALLBACK tb_winevent(HWINEVENTHOOK hook, DWORD ev, HWND h, LONG obj, LONG child, DWORD tid, DWORD time)
{
    (void)hook, (void)ev, (void)tid, (void)time;
    if (!g_tb_centered || GetWindow(h, GW_OWNER) || (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return;
    if (obj == OBJID_WINDOW && child == CHILDID_SELF && h && GetAncestor(h, GA_ROOT) == h) {
        TB.fast_until = time_now() + 0.8;
        SetEvent(TB.wake);
    }
}

static DWORD WINAPI tb_thread(void *param)
{
    (void)param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IUIAutomation *uia = NULL;
    if (FAILED(CoCreateInstance(&ML_CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &ML_IID_IUIAutomation, (void **)&uia)) || !uia) {
        log_msg("taskbar: UI Automation unavailable");
        return 0;
    }
    IUIAutomationCondition *cond = NULL;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(L"StartButton");
    IUIAutomation_CreatePropertyCondition(uia, UIA_AutomationIdPropertyId, v, &cond);
    VariantClear(&v);
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE, NULL, tb_winevent, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    HWND bars[TB_MAX] = { 0 };
    IUIAutomationElement *els[TB_MAX] = { 0 };
    RECT last[TB_MAX];
    int last_n = -1;
    f64 last_post = 0;
    f64 retry_at = 0;
    memset(last, 0, sizeof last);
    for (;;) {
        f64 now = time_now();
        bool fast = now < TB.fast_until;
        MsgWaitForMultipleObjects(1, &TB.wake, FALSE, !TB.enabled ? INFINITE : fast ? 15 : g_tb_centered ? 500 : 1000, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        if (!TB.enabled) continue;
        if (!fast) g_tb_centered = tb_read_centered();
        if (InterlockedExchange(&TB.reset, 0)) {
            for (int i = 0; i < TB_MAX; i++) SAFE_RELEASE(els[i]);
            memset(bars, 0, sizeof bars);
            retry_at = 0;
            last_n = -1;
        }

        HWND cur[TB_MAX];
        int n = tb_taskbars(cur);
        RECT rs[TB_MAX];
        HWND hs[TB_MAX];
        int nf = 0;
        for (int i = 0; i < n; i++) {
            if (bars[i] != cur[i]) {
                SAFE_RELEASE(els[i]);
                bars[i] = cur[i];
            }
            if (!els[i] && cond && time_now() >= retry_at) {
                IUIAutomationElement *root = NULL;
                if (SUCCEEDED(IUIAutomation_ElementFromHandle(uia, cur[i], &root)) && root) {
                    IUIAutomationElement_FindFirst(root, TreeScope_Descendants, cond, &els[i]);
                    IUIAutomationElement_Release(root);
                }
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
            bars[i] = NULL;
        }

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
    DWORD v = 0, sz = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL,
                     &v, &sz) == ERROR_SUCCESS)
        return v != 0;
    return false;
}

static f32 tb_sdbox(f32 px, f32 py, f32 hx, f32 hy, f32 r)
{
    f32 qx = fabsf(px) - hx + r, qy = fabsf(py) - hy + r;
    f32 ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
    return sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - r;
}

static void tb_over(f32 *c, f32 *a, const f32 *col, f32 alpha)
{
    for (int k = 0; k < 3; k++) c[k] = col[k] * alpha + c[k] * (1.f - alpha);
    *a = alpha + *a * (1.f - alpha);
}

static void tb_free_bitmap(TbButton *b)
{
    if (b->dc) DeleteDC(b->dc);
    if (b->bmp) DeleteObject(b->bmp);
    b->dc = NULL;
    b->bmp = NULL;
    b->bits = NULL;
    b->bmp_w = b->bmp_h = 0;
}

static bool tb_sample(TbButton *b)
{
    b->sampled = time_now();
    HDC sdc = GetDC(NULL);
    if (!sdc) return false;
    int x = b->screen.right + 3, h = b->screen.bottom - b->screen.top;
    COLORREF c0 = GetPixel(sdc, x, b->screen.top + h / 10 + 1), c1 = GetPixel(sdc, x, b->screen.bottom - h / 10 - 2);
    ReleaseDC(NULL, sdc);
    if (c0 == CLR_INVALID || c1 == CLR_INVALID) return false;
    COLORREF c = RGB((GetRValue(c0) + GetRValue(c1)) / 2, (GetGValue(c0) + GetGValue(c1)) / 2, (GetBValue(c0) + GetBValue(c1)) / 2);
    bool same = b->plate_ok && abs(GetRValue(c) - GetRValue(b->plate)) <= 2 && abs(GetGValue(c) - GetGValue(b->plate)) <= 2 &&
                abs(GetBValue(c) - GetBValue(b->plate)) <= 2;
    b->plate = c;
    b->plate_ok = true;
    return !same;
}

static u32 *tb_load_image(const WCHAR *path, int n)
{
    static const GUID clsid = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
    static const GUID iid = { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
    static const GUID pbgra = { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10 } };
    if (!path || !path[0] || n <= 0) return NULL;
    IWICImagingFactory *f = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *sc = NULL;
    IWICFormatConverter *cv = NULL;
    u32 *out = NULL, *tmp = NULL;
    if (FAILED(CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &iid, (void **)&f)) || !f) goto done;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(f, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
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
    if (FAILED(IWICImagingFactory_CreateBitmapScaler(f, &sc)) ||
        FAILED(IWICBitmapScaler_Initialize(sc, (IWICBitmapSource *)frame, tw, th, WICBitmapInterpolationModeFant)) ||
        FAILED(IWICImagingFactory_CreateFormatConverter(f, &cv)) ||
        FAILED(IWICFormatConverter_Initialize(cv, (IWICBitmapSource *)sc, &pbgra, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)))
        goto done;
    tmp = (u32 *)malloc((size_t)tw * th * 4);
    out = (u32 *)calloc((size_t)n * n, 4);
    if (!tmp || !out || FAILED(IWICFormatConverter_CopyPixels(cv, NULL, tw * 4, tw * th * 4, (BYTE *)tmp))) {
        free(out);
        out = NULL;
        goto done;
    }
    for (UINT y = 0; y < th; y++) memcpy(out + (size_t)(y + (n - th) / 2) * n + (n - tw) / 2, tmp + (size_t)y * tw, (size_t)tw * 4);
done:
    free(tmp);
    SAFE_RELEASE(cv);
    SAFE_RELEASE(sc);
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
            f32 m = CLAMP(0.5f - tb_sdbox(fx - sx * off, fy - sy * off, t, t, 1.6f * u), 0.f, 1.f);
            if (m > 0.f) tb_over(c, a, fg, m * (d == 0 ? 1.f : dim));
        }
    } else if (style == TBI_LINES) {
        f32 m = CLAMP(0.5f - (fabsf(tb_sdbox(fx, fy + 5.2f * u, 9.5f * u, 3.1f * u, 3.1f * u)) - 0.8f * u), 0.f, 1.f);
        if (m > 0.f) tb_over(c, a, fg, m);
        m = CLAMP(0.5f - tb_sdbox(fx + 1.5f * u, fy - 1.8f * u, 8.f * u, 1.f * u, 1.f * u), 0.f, 1.f);
        if (m > 0.f) tb_over(c, a, fg, m * (dim + 0.13f));
        m = CLAMP(0.5f - tb_sdbox(fx + 3.5f * u, fy - 6.f * u, 6.f * u, 1.f * u, 1.f * u), 0.f, 1.f);
        if (m > 0.f) tb_over(c, a, fg, m * (dim - 0.07f));
    } else {
        static const f32 k_dots[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
        f32 half = 3.3f * u, rad = 1.2f * u, off = 6.5f * u;
        for (int d = 0; d < 4; d++) {
            f32 qx = fx - k_dots[d][0] * off, qy = fy - k_dots[d][1] * off;
            f32 rx = (qx + qy) * 0.70710678f, ry = (qy - qx) * 0.70710678f;
            f32 m = CLAMP(0.5f - tb_sdbox(rx, ry, half, half, rad), 0.f, 1.f);
            if (m > 0.f) tb_over(c, a, fg, m * (d == 0 ? 1.f : dim));
        }
    }
}

static void tb_render(TbButton *b)
{
    int w = b->w, h = b->h;
    if (w <= 0 || h <= 0 || !b->wnd) return;
    if (!b->bmp || b->bmp_w != w || b->bmp_h != h) {
        tb_free_bitmap(b);
        BITMAPINFO bi;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        b->dc = CreateCompatibleDC(NULL);
        b->bmp = CreateDIBSection(b->dc, &bi, DIB_RGB_COLORS, (void **)&b->bits, NULL, 0);
        if (!b->bmp) {
            tb_free_bitmap(b);
            return;
        }
        SelectObject(b->dc, b->bmp);
        b->bmp_w = w;
        b->bmp_h = h;
    }

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
    f32 zoom = 1.f - 0.08f * b->press_t, u = logo / 24.f * zoom;
    f32 dim = (light ? 0.5f : 0.42f) + 0.18f * b->hover_t;
    int style = CLAMP(g_cfg.taskbar_icon, 0, TBI__COUNT - 1);
    if (style == TBI_CUSTOM) {
        int n = (int)logo;
        if (!b->img || b->img_n != n || b->img_gen != TB.icon_gen) {
            free(b->img);
            b->img = tb_load_image(g_cfg.taskbar_icon_path, n);
            b->img_n = n;
            b->img_gen = TB.icon_gen;
        }
        if (!b->img) style = TBI_DIAMOND;
    }

    u32 *px = b->bits;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            f32 fx = (f32)x + 0.5f - cx, fy = (f32)y + 0.5f - cy;
            f32 c[3] = { 0, 0, 0 }, a = 0;
            tb_over(c, &a, plate_c, CLAMP(0.5f - tb_sdbox(fx, fy, plate * 0.5f, plate * 0.5f, 2.f * s), 0.f, 1.f));
            if (hov_a > 0.f) tb_over(c, &a, hov_c, hov_a * CLAMP(0.5f - tb_sdbox(fx, fy, box * 0.5f, box * 0.5f, brad), 0.f, 1.f));
            if (style == TBI_CUSTOM) {
                f32 ic[3], ia = tb_sample_img(b->img, b->img_n, fx / zoom + logo * 0.5f, fy / zoom + logo * 0.5f, ic);
                if (ia > 0.f) {
                    f32 k = 1.f - 0.1f * b->press_t;
                    for (int q = 0; q < 3; q++) c[q] = ic[q] * k + c[q] * (1.f - ia);
                    a = ia + a * (1.f - ia);
                }
            } else {
                tb_shape(style, fx, fy, u, dim, fg, c, &a);
            }
            if (a < 1.f / 255.f) a = 1.f / 255.f;  // alpha 0 would pass clicks to the real button
            u32 A = (u32)(a * 255.f + 0.5f);
            u32 R = (u32)(CLAMP(c[0], 0.f, a) * 255.f + 0.5f), G = (u32)(CLAMP(c[1], 0.f, a) * 255.f + 0.5f), B = (u32)(CLAMP(c[2], 0.f, a) * 255.f + 0.5f);
            px[y * w + x] = (A << 24) | (R << 16) | (G << 8) | B;
        }
    }
    SIZE size = { w, h };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(b->wnd, NULL, NULL, &size, b->dc, &src, 0, &bf, ULW_ALPHA);
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
            TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
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
    tb_free_bitmap(b);
}

static void tb_publish_hits(void)
{
    LONG n = 0;
    for (int i = 0; i < TB.n; i++)
        if (TB.b[i].wnd) g_tb_hit[n++] = TB.b[i].screen;
    g_tb_hit_count = n;
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
                *b = TB.b[k];
                TB.b[k].wnd = NULL;
                TB.b[k].img = NULL;
                TB.b[k].dc = NULL;
                TB.b[k].bmp = NULL;
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
        bool resample = moved || time_now() - b->sampled > 5.0;
        if ((resample && tb_sample(b)) || resized) tb_render(b);
        bool covered = GetWindow(b->wnd, GW_HWNDPREV) != NULL;
        if (moved || covered || !IsWindowVisible(b->wnd)) SetWindowPos(b->wnd, HWND_TOP, p.x, p.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    tb_publish_hits();
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
        tb_render(&TB.b[i]);
    }
}

static void tb_explorer_restarted(void)
{
    for (int i = 0; i < TB.n; i++) tb_destroy(&TB.b[i]);
    TB.n = 0;
    tb_publish_hits();
    InterlockedExchange(&TB.reset, 1);
    TB.fast_until = time_now() + 3.0;
    if (TB.wake) SetEvent(TB.wake);
}

static void tb_set_enabled(bool on)
{
    TB.enabled = on;
    if (on) {
        static bool registered;
        if (!registered) {
            registered = true;
            WNDCLASSEXW wc;
            memset(&wc, 0, sizeof wc);
            wc.cbSize = sizeof wc;
            wc.style = CS_DBLCLKS;
            wc.lpfnWndProc = tb_wndproc;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.lpszClassName = TB_CLASS;
            RegisterClassExW(&wc);
        }
        TB.light = taskbar_light();
        if (!TB.thread) {
            InitializeSRWLock(&TB.lock);
            TB.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
            TB.thread = CreateThread(NULL, 0, tb_thread, NULL, 0, NULL);
        }
        InterlockedExchange(&TB.reset, 1);
        SetEvent(TB.wake);
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
