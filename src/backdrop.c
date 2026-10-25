// The launcher's backdrop (glass, acrylic): the screen behind the window, blurred on the GPU and drawn
// by the renderer (r_backdrop) like any other rect, so it zooms and slides with the content. It is live: while the
// launcher shows, the window is left out of screen captures and Desktop Duplication hands over every new image of
// the screen behind it. Where that cannot run (another GPU's monitor, a rotated or HDR one), a still taken as the
// launcher opens. PrtSc and Win+Shift+S turn the exclusion off, so screenshots still show the launcher.

typedef struct Backdrop {
    RECT rc;                      // the screen area R.bd_src holds, screen px
    HMONITOR mon;
    IDXGIOutputDuplication *dup;  // the live screen of mon
    RECT out;                     // where mon's images sit on the screen
    DWORD retry;                  // when to start dup again after it was lost (tick)
    bool want_live;               // keep dup going: the launcher shows, left out of captures
    bool first;                   // dup's next image is its first: take it even if nothing changed
    bool ok;                      // R.bd_src holds the screen behind
} Backdrop;

static Backdrop BD;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x11
#endif

// The radius of the window's corners, as DWM rounds them.
static f32 backdrop_radius(void)
{
    int c = LS()->corner;
    return c == DWC_ROUND ? SR(8) : c == DWC_ROUND_SMALL ? SR(4) : 0.f;
}

static void bd_dup_stop(void) { SAFE_RELEASE(BD.dup); }

// Desktop Duplication of BD.mon, on our device's GPU.
static bool bd_dup_start(void)
{
    if (BD.dup) return true;
    IDXGIDevice *dd = NULL;
    IDXGIAdapter *ad = NULL;
    ID3D11Device_QueryInterface(R.dev, &ML_IID_IDXGIDevice, (void **)&dd);
    if (dd) IDXGIDevice_GetAdapter(dd, &ad);
    IDXGIOutput *o = NULL;
    for (UINT i = 0; ad && SUCCEEDED(IDXGIAdapter_EnumOutputs(ad, i, &o)); i++) {
        DXGI_OUTPUT_DESC od;
        bool mine = SUCCEEDED(IDXGIOutput_GetDesc(o, &od)) && od.Monitor == BD.mon;
        IDXGIOutput1 *o1 = NULL;
        if (mine && od.Rotation <= DXGI_MODE_ROTATION_IDENTITY && SUCCEEDED(IDXGIOutput_QueryInterface(o, &ML_IID_IDXGIOutput1, (void **)&o1))) {
            HRESULT hr = IDXGIOutput1_DuplicateOutput(o1, (IUnknown *)R.dev, &BD.dup);
            if (FAILED(hr)) log_msg("backdrop: DuplicateOutput failed 0x%08lx", hr);
            BD.out = od.DesktopCoordinates;
            IDXGIOutput1_Release(o1);
        }
        IDXGIOutput_Release(o);
        if (mine) break;
    }
    SAFE_RELEASE(ad);
    SAFE_RELEASE(dd);
    BD.first = true;
    return BD.dup != NULL;
}

// Takes dup's next image into the source, waiting up to timeout ms; true when the screen behind changed.
static bool bd_pull(UINT timeout)
{
    DXGI_OUTDUPL_FRAME_INFO fi;
    IDXGIResource *res = NULL;
    HRESULT hr = IDXGIOutputDuplication_AcquireNextFrame(BD.dup, timeout, &fi, &res);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
    if (FAILED(hr)) {  // lost: a mode change, the secure desktop (UAC), a fullscreen app
        bd_dup_stop();
        BD.retry = GetTickCount() + 500;
        return false;
    }
    bool got = false, behind = BD.first;
    if (fi.LastPresentTime.QuadPart && !behind) {  // not just the pointer: did it change behind the launcher?
        static RECT dirty[64];
        UINT sz = 0;
        if (IDXGIOutputDuplication_GetFrameMoveRects(BD.dup, 0, NULL, &sz) == DXGI_ERROR_MORE_DATA || sz) behind = true;  // scrolled
        sz = 0;
        if (SUCCEEDED(IDXGIOutputDuplication_GetFrameDirtyRects(BD.dup, sizeof dirty, dirty, &sz))) {
            // Our own frames show up too, as the window's rect exactly, though the image leaves the window out:
            // taken as a change they would ask for frame after frame.
            RECT area = BD.rc, own = { 0 }, x;
            OffsetRect(&area, -BD.out.left, -BD.out.top);
            GetWindowRect(g_hwnd, &own);
            OffsetRect(&own, -BD.out.left, -BD.out.top);
            IntersectRect(&own, &own, &(RECT){ 0, 0, BD.out.right - BD.out.left, BD.out.bottom - BD.out.top });
            for (UINT i = 0; i < sz / sizeof(RECT) && !behind; i++)
                behind = !EqualRect(&dirty[i], &own) && IntersectRect(&x, &dirty[i], &area);
        } else {
            behind = true;  // more rects than fit
        }
    }
    if (behind) {
        ID3D11Texture2D *tex = NULL;
        if (SUCCEEDED(IDXGIResource_QueryInterface(res, &ML_IID_ID3D11Texture2D, (void **)&tex))) {
            D3D11_BOX box = { (UINT)(BD.rc.left - BD.out.left), (UINT)(BD.rc.top - BD.out.top), 0,
                              (UINT)(BD.rc.right - BD.out.left), (UINT)(BD.rc.bottom - BD.out.top), 1 };
            ID3D11DeviceContext_CopySubresourceRegion(R.ctx, (ID3D11Resource *)R.bd_src.tex, 0, 0, 0, 0, (ID3D11Resource *)tex, 0, &box);
            ID3D11Texture2D_Release(tex);
            R.bd_dirty = got = true;
            BD.first = false;
        }
    }
    SAFE_RELEASE(res);
    IDXGIOutputDuplication_ReleaseFrame(BD.dup);
    return got;
}

// The wallpaper under rc (screen px) as the desktop shows it, into l (fit to rc's size).
static bool wallpaper_paint(Layer *l, RECT rc)
{
    if (!layer_fit(l, rc.right - rc.left, rc.bottom - rc.top)) return false;
    SetViewportOrgEx(l->dc, -rc.left, -rc.top, NULL);  // PaintDesktop draws in screen coordinates
    bool ok = PaintDesktop(l->dc) != 0;
    SetViewportOrgEx(l->dc, 0, 0, NULL);
    GdiFlush();
    if (!ok) log_msg("backdrop: PaintDesktop failed (%lu)", GetLastError());
    return ok;
}

// Mica: the wallpaper under BD.rc. A still: it changes only with the wallpaper.
static bool bd_wallpaper(void)
{
    Layer l = { 0 };
    bool ok = wallpaper_paint(&l, BD.rc);
    if (ok) r_bd_upload(l.bits);
    layer_free(&l);
    return ok;
}

// A still of BD.rc through GDI.
static bool bd_snapshot(void)
{
    Layer l = { 0 };
    int w = BD.rc.right - BD.rc.left, h = BD.rc.bottom - BD.rc.top;
    if (!layer_fit(&l, w, h)) return false;
    HDC scr = GetDC(NULL);
    bool ok = scr && BitBlt(l.dc, 0, 0, w, h, scr, BD.rc.left, BD.rc.top, SRCCOPY);
    if (scr) ReleaseDC(NULL, scr);
    GdiFlush();
    if (ok) r_bd_upload(l.bits);
    else log_msg("backdrop: screen capture failed (%lu)", GetLastError());
    layer_free(&l);
    return ok;
}

// The screen behind the launcher's window win (screen px) on mon, with a margin the blur and the lens reach into.
// The Windows 10 style resizes and takes another width for a query: everything to its right, the full height.
static void bd_open(RECT win, HMONITOR mon)
{
    BD.ok = BD.want_live = false;
    int kind = backdrop_kind();
    if (kind == BACKDROP_SOLID || !R.ok) return;
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi)) return;
    int m = (int)SR(56);
    RECT rc = { win.left - m, win.top - m, win.right + m, win.bottom + m };
    if (w10_on()) {
        rc.right = mi.rcMonitor.right;
        rc.top = mi.rcMonitor.top;
        rc.bottom = mi.rcMonitor.bottom;
    }
    if (!IntersectRect(&rc, &rc, &mi.rcMonitor) || !r_bd_source(rc.right - rc.left, rc.bottom - rc.top)) return;
    if (BD.dup && (mon != BD.mon || !EqualRect(&rc, &BD.rc))) BD.first = true;  // a new area: its first image whole
    if (mon != BD.mon) bd_dup_stop();
    BD.rc = rc;
    BD.mon = mon;
    R.bd_sigma = (kind == BACKDROP_MICA ? 60.f : 30.f) * U.s;
    f64 t0 = time_now();
    if (kind == BACKDROP_MICA) {  // the wallpaper only: nothing live, the launcher stays in screen captures
        BD.ok = bd_wallpaper();
        R.bd_dirty = true;
        if (g_ktrace_on) log_msg("backdrop mica %ldx%ld %s in %.1f ms", rc.right - rc.left, rc.bottom - rc.top, BD.ok ? "ok" : "none", (time_now() - t0) * 1000.0);
        return;
    }
    // Left out of captures from now on: Desktop Duplication (and a still taken while it shows) sees what is behind.
    bool excluded = SetWindowDisplayAffinity(g_hwnd, WDA_EXCLUDEFROMCAPTURE) != 0;
    bool shown = IsWindowVisible(g_hwnd);
    if (excluded && shown) DwmFlush();
    BD.want_live = excluded;
    BD.ok = BD.want_live && bd_dup_start() && bd_pull(30);
    bool live = BD.ok;
    if (!BD.ok && (excluded || !shown)) BD.ok = bd_snapshot();
    R.bd_dirty = true;
    if (g_ktrace_on)
        log_msg("backdrop %ldx%ld %s in %.1f ms", rc.right - rc.left, rc.bottom - rc.top, live ? "live" : BD.ok ? "still" : "none", (time_now() - t0) * 1000.0);
}

// The launcher closed: no more images, back in screen captures.
static void bd_close(void)
{
    bd_dup_stop();
    if (BD.want_live) SetWindowDisplayAffinity(g_hwnd, WDA_NONE);
    BD.want_live = BD.ok = false;
}

// A screenshot is being taken: the launcher shows in it again, over the last image of the screen behind.
static void bd_screenshot(void)
{
    if (!BD.want_live) return;
    bd_close();
    BD.ok = R.bd_src.srv != NULL;
}

// The setting changed, another monitor, a new device: take it all again where the window is now.
static void bd_refresh(void)
{
    RECT w;
    if (!U.visible || !GetWindowRect(g_hwnd, &w)) return;
    OffsetRect(&w, 0, -U.slide_y);
    w.bottom = w.top + U.H;
    bd_close();
    bd_open(w, MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST));
}

static bool bd_live(void) { return BD.want_live; }

// Every turn of the message loop while the launcher shows: a new image of the screen behind asks for a frame.
static void bd_poll(void)
{
    if (!BD.want_live) return;
    if (!BD.dup && (LONG)(GetTickCount() - BD.retry) >= 0 && !bd_dup_start()) BD.retry = GetTickCount() + 1000;
    if (BD.dup && bd_pull(0)) U.dirty = true;
}

// The message loop's wait while a live backdrop has nothing new: on the screen's next image (Desktop Duplication
// has no event to wait on with the messages), up to ms, then input gets its turn. False without one to wait on.
static bool bd_wait(UINT ms)
{
    if (!BD.want_live || !BD.dup) return false;
    if (bd_pull(ms)) U.dirty = true;
    return true;
}

// Whether this frame has a backdrop to draw.
static bool bd_ready(void) { return BD.ok && backdrop_kind() != BACKDROP_SOLID && R.bd_src.srv; }

// The shader's kind of backdrop (bd1.x).
static f32 bd_shading(int kind) { return kind == BACKDROP_BLUR ? 0.f : kind == BACKDROP_ACRYLIC ? 1.f : 2.f; }

// The frame's constants: where the window is on the screen as it goes out.
static void bd_place(void)
{
    RECT w;
    if (!BD.ok || !GetWindowRect(g_hwnd, &w)) return;
    int kind = backdrop_kind();
    R.bd[0] = (f32)(w.left - BD.rc.left);
    R.bd[1] = (f32)(w.top - BD.rc.top);
    R.bd[2] = 1.f / (f32)R.bd_src.w;
    R.bd[3] = 1.f / (f32)R.bd_src.h;
    R.bd[4] = bd_shading(kind);
    R.bd[5] = TH.dark ? 1.f : 0.f;
    R.bd[6] = R.bd[7] = 0;
}
