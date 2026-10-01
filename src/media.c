// media.c — "now playing" on the taskbar: cover, title, artist and playback buttons, in the spirit of
// FluentFlyout's taskbar widget. A worker reads the system media session (GSMTC) through raw WinRT
// vtables; the main thread draws a layered child window of the taskbar, the same way as the Start button.

#define MW_CLASS L"MixLauncherMediaWidget"
#define MW_TIMER_ANIM 1
#define MW_TIMER_SYNC 2

enum { MS_CLOSED, MS_OPENED, MS_CHANGING, MS_STOPPED, MS_PLAYING, MS_PAUSED };  // GSMTC playback status
enum { MC_NONE, MC_PREV, MC_TOGGLE, MC_NEXT };
enum { MP_NONE = -1, MP_INFO, MP_PREV, MP_TOGGLE, MP_NEXT };                  // parts of the widget

typedef struct MediaInfo {
    bool has;                 // a media session exists
    int status;               // MS_*
    bool can_prev, can_toggle, can_next;
    WCHAR title[256], artist[256], app[256];
} MediaInfo;

typedef struct MText {        // one line of text rasterized to coverage
    u8 *cov;
    int w, h, base;           // base: baseline row inside the bitmap
    int adv;                  // pen advance, pixels
} MText;

static struct {
    bool enabled;
    HANDLE thread, wake;
    SRWLOCK lock;
    // published by the worker
    MediaInfo pub;
    u32 *pub_art;             // art_px x art_px premultiplied BGRA, or NULL
    int pub_art_n;
    u32 pub_art_gen;
    RECT widgets_btn;         // the Widgets (weather) button, screen coordinates; empty if none
    // requests to the worker
    volatile LONG cmd;        // MC_*
    volatile LONG art_px;
    volatile LONG art_reload;
    volatile LONG want_widgets_btn;
    // main thread
    HWND wnd, taskbar;
    MediaInfo info;
    u32 *art;
    int art_n;
    u32 art_gen;
    bool light;
    UINT dpi;
    f32 s;
    MText title, artist, note;
    int w, h;
    int x;                    // position inside the taskbar
    f32 part[4][4];           // x0, y0, x1, y1 per MP_*
    f32 text_x, text_w;
    bool visible, want;
    f64 idle_since;           // status left "playing" at this time
    int hover, press;         // MP_*
    bool hover_any;
    f32 vis_t, swap_t, hov_t, hp_t[4], press_t[4], text_a;
    f32 scroll, scroll_t;     // marquee offset (pixels) and hover time
    bool toggle_local;        // play/pause clicked: show the new state before the player confirms
    int toggle_status;
    f64 toggle_until;
    f64 last_anim;
    HDC dc;
    HBITMAP bmp;
    u32 *bits;
    int bmp_w, bmp_h;
} M = { .hover = MP_NONE, .press = MP_NONE, .text_a = 1 };

// Worker: WinRT through raw vtables (there are no C headers for Windows.Media.Control)

typedef struct ML_HSTRING__ *ML_HSTRING;

static struct {
    HRESULT (WINAPI *get_factory)(ML_HSTRING, REFIID, void **);
    HRESULT (WINAPI *create_string)(const WCHAR *, UINT32, ML_HSTRING *);
    HRESULT (WINAPI *delete_string)(ML_HSTRING);
    const WCHAR *(WINAPI *string_buf)(ML_HSTRING, UINT32 *);
    HRESULT (WINAPI *stream_over)(IUnknown *, REFIID, void **);  // CreateStreamOverRandomAccessStream
} RTF;

static const GUID ML_IID_IGSMTCSessionManagerStatics = { 0x2050c4ee, 0x11a0, 0x57de, { 0xae, 0xd7, 0xc9, 0x7c, 0x70, 0x33, 0x82, 0x45 } };
static const GUID ML_IID_IAsyncInfo = { 0x00000036, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID ML_IID_IStream = { 0x0000000c, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

// Every call we need has the shape (this, out*). Slots count from the start of the vtable:
// IInspectable takes 0..5, so an interface's first method is slot 6.
typedef HRESULT (STDMETHODCALLTYPE *RtOutFn)(void *, void *);
#define RT_GET(o, slot, out) (((RtOutFn)(*(void ***)(o))[slot])((o), (out)))

enum {
    RT_STATICS_REQUEST = 6,                                            // IGlobalSystemMediaTransportControlsSessionManagerStatics
    RT_MGR_CURRENT = 6,                                                // ...SessionManager
    RT_SES_APP = 6, RT_SES_PROPS = 7, RT_SES_PLAYBACK = 9,             // ...Session
    RT_SES_NEXT = 16, RT_SES_PREV = 17, RT_SES_TOGGLE = 20,
    RT_PROPS_TITLE = 6, RT_PROPS_ALBUM_ARTIST = 8, RT_PROPS_ARTIST = 9, RT_PROPS_THUMB = 15,  // ...MediaProperties
    RT_PB_CONTROLS = 6, RT_PB_STATUS = 7,                              // ...PlaybackInfo
    RT_CTL_PLAY = 6, RT_CTL_PAUSE = 7, RT_CTL_NEXT = 12, RT_CTL_PREV = 13, RT_CTL_TOGGLE = 16,  // ...PlaybackControls
    RT_STREAMREF_OPEN = 6,                                             // IRandomAccessStreamReference
    RT_ASYNC_RESULTS = 8,                                              // IAsyncOperation<T>
    RT_INFO_STATUS = 7, RT_INFO_CANCEL = 9,                            // IAsyncInfo
};

static bool rt_load(void)
{
    HMODULE cb = LoadLibraryExW(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE sc = LoadLibraryExW(L"shcore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!cb) return false;
    RTF.get_factory = (HRESULT (WINAPI *)(ML_HSTRING, REFIID, void **))(void *)GetProcAddress(cb, "RoGetActivationFactory");
    RTF.create_string = (HRESULT (WINAPI *)(const WCHAR *, UINT32, ML_HSTRING *))(void *)GetProcAddress(cb, "WindowsCreateString");
    RTF.delete_string = (HRESULT (WINAPI *)(ML_HSTRING))(void *)GetProcAddress(cb, "WindowsDeleteString");
    RTF.string_buf = (const WCHAR *(WINAPI *)(ML_HSTRING, UINT32 *))(void *)GetProcAddress(cb, "WindowsGetStringRawBuffer");
    if (sc) RTF.stream_over = (HRESULT (WINAPI *)(IUnknown *, REFIID, void **))(void *)GetProcAddress(sc, "CreateStreamOverRandomAccessStream");
    return RTF.get_factory && RTF.create_string && RTF.delete_string && RTF.string_buf;
}

// Waits for an IAsyncOperation<T> and returns its result. Takes ownership of op.
static IUnknown *rt_await(IUnknown *op, DWORD timeout_ms)
{
    if (!op) return NULL;
    IUnknown *info = NULL, *res = NULL;
    if (SUCCEEDED(op->lpVtbl->QueryInterface(op, &ML_IID_IAsyncInfo, (void **)&info)) && info) {
        DWORD t0 = GetTickCount();
        INT32 st = 0;
        while (SUCCEEDED(RT_GET(info, RT_INFO_STATUS, &st)) && st == 0 /*Started*/ && GetTickCount() - t0 < timeout_ms) Sleep(2);
        if (st == 1 /*Completed*/) RT_GET(op, RT_ASYNC_RESULTS, &res);
        else if (st == 0) ((HRESULT (STDMETHODCALLTYPE *)(void *))(*(void ***)info)[RT_INFO_CANCEL])(info);
        info->lpVtbl->Release(info);
    }
    op->lpVtbl->Release(op);
    return res;
}

static void rt_str(IUnknown *o, int slot, WCHAR *out, int cap)
{
    ML_HSTRING s = NULL;
    out[0] = 0;
    if (FAILED(RT_GET(o, slot, &s)) || !s) return;
    UINT32 n = 0;
    const WCHAR *p = RTF.string_buf(s, &n);
    if (p) {
        n = MIN(n, (UINT32)cap - 1);
        memcpy(out, p, n * sizeof(WCHAR));
        out[n] = 0;
    }
    RTF.delete_string(s);
}

static bool rt_bool(IUnknown *o, int slot)
{
    boolean b = 0;
    return SUCCEEDED(RT_GET(o, slot, &b)) && b;
}

static IUnknown *media_manager(void)
{
    static const WCHAR cls[] = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";
    ML_HSTRING name = NULL;
    IUnknown *statics = NULL, *op = NULL, *mgr = NULL;
    if (FAILED(RTF.create_string(cls, (UINT32)wlen(cls), &name))) return NULL;
    if (SUCCEEDED(RTF.get_factory(name, &ML_IID_IGSMTCSessionManagerStatics, (void **)&statics)) && statics) {
        if (SUCCEEDED(RT_GET(statics, RT_STATICS_REQUEST, &op))) mgr = rt_await(op, 5000);
        statics->lpVtbl->Release(statics);
    }
    RTF.delete_string(name);
    if (!mgr) log_msg("media: session manager unavailable");
    return mgr;
}

// Cover art: center square, scaled to n x n, premultiplied BGRA.
static u32 *media_decode_art(IUnknown *thumb, int n, u64 *hash)
{
    static const GUID clsid = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
    static const GUID iid = { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
    static const GUID pbgra = { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10 } };
    if (!thumb || n <= 0 || n > 512 || !RTF.stream_over) return NULL;
    IUnknown *op = NULL, *ras = NULL;
    IStream *stm = NULL;
    IWICImagingFactory *f = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapClipper *clip = NULL;
    IWICBitmapScaler *sc = NULL;
    IWICFormatConverter *cv = NULL;
    u32 *out = NULL;
    if (FAILED(RT_GET(thumb, RT_STREAMREF_OPEN, &op))) return NULL;
    ras = rt_await(op, 3000);
    if (!ras) return NULL;
    if (FAILED(RTF.stream_over(ras, &ML_IID_IStream, (void **)&stm)) || !stm) goto done;
    if (FAILED(CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &iid, (void **)&f)) || !f) goto done;
    if (FAILED(IWICImagingFactory_CreateDecoderFromStream(f, stm, NULL, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
    if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &frame)) || !frame) goto done;
    UINT iw = 0, ih = 0;
    IWICBitmapFrameDecode_GetSize(frame, &iw, &ih);
    if (!iw || !ih) goto done;
    UINT side = MIN(iw, ih);
    WICRect r = { (INT)(iw - side) / 2, (INT)(ih - side) / 2, (INT)side, (INT)side };
    if (FAILED(IWICImagingFactory_CreateBitmapClipper(f, &clip)) ||
        FAILED(IWICBitmapClipper_Initialize(clip, (IWICBitmapSource *)frame, &r)) ||
        FAILED(IWICImagingFactory_CreateBitmapScaler(f, &sc)) ||
        FAILED(IWICBitmapScaler_Initialize(sc, (IWICBitmapSource *)clip, (UINT)n, (UINT)n, WICBitmapInterpolationModeFant)) ||
        FAILED(IWICImagingFactory_CreateFormatConverter(f, &cv)) ||
        FAILED(IWICFormatConverter_Initialize(cv, (IWICBitmapSource *)sc, &pbgra, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)))
        goto done;
    out = (u32 *)malloc((size_t)n * n * 4);
    if (out && FAILED(IWICFormatConverter_CopyPixels(cv, NULL, (UINT)n * 4, (UINT)(n * n * 4), (BYTE *)out))) {
        free(out);
        out = NULL;
    }
    if (out) *hash = hash_u64((u64)n) ^ (u64)hash_wstr((const WCHAR *)out, n * n * 2);
done:
    SAFE_RELEASE(cv);
    SAFE_RELEASE(sc);
    SAFE_RELEASE(clip);
    SAFE_RELEASE(frame);
    SAFE_RELEASE(dec);
    SAFE_RELEASE(f);
    SAFE_RELEASE(stm);
    SAFE_RELEASE(ras);
    return out;
}

static void media_read(IUnknown *ses, MediaInfo *in, IUnknown **thumb)
{
    in->has = true;
    rt_str(ses, RT_SES_APP, in->app, countof(in->app));
    IUnknown *pb = NULL, *ctl = NULL, *op = NULL, *props = NULL;
    if (SUCCEEDED(RT_GET(ses, RT_SES_PLAYBACK, &pb)) && pb) {
        INT32 st = 0;
        RT_GET(pb, RT_PB_STATUS, &st);
        in->status = st;
        if (SUCCEEDED(RT_GET(pb, RT_PB_CONTROLS, &ctl)) && ctl) {
            in->can_prev = rt_bool(ctl, RT_CTL_PREV);
            in->can_next = rt_bool(ctl, RT_CTL_NEXT);
            in->can_toggle = rt_bool(ctl, RT_CTL_TOGGLE) || rt_bool(ctl, RT_CTL_PLAY) || rt_bool(ctl, RT_CTL_PAUSE);
            ctl->lpVtbl->Release(ctl);
        }
        pb->lpVtbl->Release(pb);
    }
    if (SUCCEEDED(RT_GET(ses, RT_SES_PROPS, &op))) props = rt_await(op, 1500);
    if (props) {
        rt_str(props, RT_PROPS_TITLE, in->title, countof(in->title));
        rt_str(props, RT_PROPS_ARTIST, in->artist, countof(in->artist));
        if (!in->artist[0]) rt_str(props, RT_PROPS_ALBUM_ARTIST, in->artist, countof(in->artist));
        if (FAILED(RT_GET(props, RT_PROPS_THUMB, thumb))) *thumb = NULL;
        props->lpVtbl->Release(props);
    }
}

// The Widgets (weather) button at the left end of the taskbar, to sit right after it.
static bool media_widgets_button(IUIAutomation **uia, IUIAutomationElement **el, f64 *retry, RECT *out)
{
    memset(out, 0, sizeof *out);
    if (!*uia && FAILED(CoCreateInstance(&ML_CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &ML_IID_IUIAutomation, (void **)uia))) return false;
    if (!*el) {
        if (time_now() < *retry) return false;
        *retry = time_now() + 10.0;
        HWND tb = FindWindowW(L"Shell_TrayWnd", NULL);
        IUIAutomationElement *root = NULL;
        IUIAutomationCondition *cond = NULL;
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocString(L"WidgetsButton");
        IUIAutomation_CreatePropertyCondition(*uia, UIA_AutomationIdPropertyId, v, &cond);
        VariantClear(&v);
        if (tb && cond && SUCCEEDED(IUIAutomation_ElementFromHandle(*uia, tb, &root)) && root) {
            IUIAutomationElement_FindFirst(root, TreeScope_Descendants, cond, el);
            IUIAutomationElement_Release(root);
        }
        SAFE_RELEASE(cond);
        if (!*el) return false;
    }
    RECT r;
    if (FAILED(IUIAutomationElement_get_CurrentBoundingRectangle(*el, &r))) {
        SAFE_RELEASE(*el);  // gone (Explorer restarted)
        *retry = 0;
        return false;
    }
    if (r.right <= r.left || r.bottom <= r.top) return false;  // turned off
    *out = r;
    return true;
}

static DWORD WINAPI media_thread(void *param)
{
    (void)param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (!rt_load()) {
        log_msg("media: WinRT unavailable");
        return 0;
    }
    IUnknown *mgr = NULL;
    IUIAutomation *uia = NULL;
    IUIAutomationElement *wbtn = NULL;
    f64 mgr_retry = 0, wbtn_retry = 0, wbtn_next = 0;
    RECT wrect = { 0, 0, 0, 0 };
    MediaInfo last;
    memset(&last, 0, sizeof last);
    u64 art_hash = 0;
    int art_tries = 0, fast = 0;
    f64 art_due = 0;
    for (;;) {
        DWORD wait = !M.enabled ? INFINITE : fast > 0 ? 120 : last.has ? 500 : 1500;
        WaitForSingleObject(M.wake, wait);
        if (!M.enabled) {
            SAFE_RELEASE(wbtn);
            SAFE_RELEASE(uia);
            SAFE_RELEASE(mgr);
            memset(&last, 0, sizeof last);
            art_hash = 0;
            continue;
        }
        if (fast > 0) fast--;
        f64 now = time_now();
        if (!mgr && now >= mgr_retry) {
            mgr = media_manager();
            if (!mgr) mgr_retry = now + 10.0;
        }

        MediaInfo in;
        memset(&in, 0, sizeof in);
        IUnknown *ses = NULL, *thumb = NULL;
        if (mgr && FAILED(RT_GET(mgr, RT_MGR_CURRENT, &ses))) {
            SAFE_RELEASE(mgr);  // the service went away: get a new manager
            mgr_retry = now + 1.0;
        }
        LONG cmd = InterlockedExchange(&M.cmd, MC_NONE);
        if (ses) {
            if (cmd != MC_NONE) {
                IUnknown *op = NULL;
                RT_GET(ses, cmd == MC_PREV ? RT_SES_PREV : cmd == MC_NEXT ? RT_SES_NEXT : RT_SES_TOGGLE, &op);
                SAFE_RELEASE(op);  // fire and forget; the next polls pick up the result
                fast = 10;
            }
            media_read(ses, &in, &thumb);
            ses->lpVtbl->Release(ses);
        }

        bool track_changed = wcscmp(in.title, last.title) || wcscmp(in.artist, last.artist) || wcscmp(in.app, last.app);
        if (track_changed) {
            art_tries = 3;  // players often set the cover a moment after the title: look again later
            art_due = now;
        }
        if (InterlockedExchange(&M.art_reload, 0)) {
            art_tries = MAX(art_tries, 1);
            art_due = now;
            art_hash = 0;
        }
        bool art_new = false;
        u32 *art = NULL;
        int art_n = (int)M.art_px;
        if (thumb && art_tries > 0 && now >= art_due) {
            art_tries--;
            art_due = now + 1.5 * (3 - art_tries);
            u64 h = 0;
            art = media_decode_art(thumb, art_n, &h);
            if (art && h != art_hash) {
                art_hash = h;
                art_new = true;
            } else {
                free(art);
                art = NULL;
            }
        } else if (!thumb && track_changed && art_hash) {
            art_hash = 0;  // no cover for this one
            art_new = true;
        }
        SAFE_RELEASE(thumb);

        RECT wr = wrect;
        if (InterlockedCompareExchange(&M.want_widgets_btn, 0, 0)) {
            if (now >= wbtn_next) {
                wbtn_next = now + 2.0;
                media_widgets_button(&uia, &wbtn, &wbtn_retry, &wr);
            }
        } else {
            memset(&wr, 0, sizeof wr);
        }
        bool rect_changed = memcmp(&wr, &wrect, sizeof wr) != 0;
        wrect = wr;

        if (memcmp(&in, &last, sizeof in) || art_new || rect_changed) {
            last = in;
            u32 *old = NULL;
            AcquireSRWLockExclusive(&M.lock);
            M.pub = in;
            M.widgets_btn = wr;
            if (art_new) {
                old = M.pub_art;
                M.pub_art = art;
                M.pub_art_n = art_n;
                M.pub_art_gen++;
            }
            ReleaseSRWLockExclusive(&M.lock);
            free(old);
            PostMessageW(g_hwnd, WM_APP_MEDIA, 0, 0);
        }
    }
    return 0;
}

// Main thread: text

static void mtext_free(MText *t)
{
    free(t->cov);
    memset(t, 0, sizeof *t);
}

// Rasterizes a line with DirectWrite into an 8-bit coverage bitmap (grayscale antialiasing).
static void mtext_make(MText *t, int font, f32 size, const WCHAR *s)
{
    mtext_free(t);
    int len = MIN(wlen(s), 200);
    if (len <= 0 || !F.dw) return;
    f32 pen = 0;
    for (int i = 0; i < len;) pen += glyph_map(font, utf16_next(s, len, &i))->adv * size;
    int pad = (int)ceilf(size * 0.5f);
    t->w = (int)ceilf(pen) + 2 * pad;
    t->h = (int)ceilf(size * 2.f);
    t->base = (int)ceilf(size * 1.4f);
    t->adv = (int)ceilf(pen);
    t->cov = (u8 *)calloc((size_t)t->w * t->h, 1);
    if (!t->cov) return;

    static u8 tmp[1024 * 128];
    pen = 0;
    for (int i = 0; i < len;) {
        u32 cp = utf16_next(s, len, &i);
        GlyphMap *g = glyph_map(font, cp == '\t' ? ' ' : cp);
        f32 adv = g->adv * size;
        Face *f = cp == ' ' || cp == '\t' ? NULL : face_get(g->face);
        if (f) {
            u16 glyph = g->glyph;
            FLOAT a = 0;
            DW_GLYPH_OFFSET off = { 0, 0 };
            DW_GLYPH_RUN run;
            memset(&run, 0, sizeof run);
            run.fontFace = f->face;
            run.fontEmSize = size;
            run.glyphCount = 1;
            run.glyphIndices = &glyph;
            run.glyphAdvances = &a;
            run.glyphOffsets = &off;
            DWGlyphRunAnalysis *ana = NULL;
            UINT32 tex = DW_TEXTURE_CLEARTYPE_3x1;
            f32 ox = (f32)pad + pen, oy = (f32)t->base;
            HRESULT hr = E_FAIL;
            if (F.v2) {
                hr = F.dw->lpVtbl->CreateGlyphRunAnalysis2(F.dw, &run, NULL, DW_RENDERING_MODE_NATURAL_SYMMETRIC, DW_MEASURING_MODE_NATURAL,
                                                           DW_GRID_FIT_MODE_DEFAULT, DW_TEXT_ANTIALIAS_MODE_GRAYSCALE, ox, oy, &ana);
                tex = DW_TEXTURE_ALIASED_1x1;
            }
            if (FAILED(hr)) {
                hr = F.dw->lpVtbl->CreateGlyphRunAnalysis(F.dw, &run, 1.0f, NULL, DW_RENDERING_MODE_NATURAL_SYMMETRIC, DW_MEASURING_MODE_NATURAL,
                                                          ox, oy, &ana);
                tex = DW_TEXTURE_CLEARTYPE_3x1;
            }
            if (SUCCEEDED(hr) && ana) {
                RECT b = { 0, 0, 0, 0 };
                ana->lpVtbl->GetAlphaTextureBounds(ana, tex, &b);
                if (b.right <= b.left && tex == DW_TEXTURE_ALIASED_1x1) {
                    tex = DW_TEXTURE_CLEARTYPE_3x1;
                    ana->lpVtbl->GetAlphaTextureBounds(ana, tex, &b);
                }
                int w = b.right - b.left, h = b.bottom - b.top, bpp = tex == DW_TEXTURE_CLEARTYPE_3x1 ? 3 : 1;
                if (w > 0 && h > 0 && w * h * bpp <= (int)sizeof tmp &&
                    SUCCEEDED(ana->lpVtbl->CreateAlphaTexture(ana, tex, &b, tmp, (UINT32)(w * h * bpp)))) {
                    for (int y = 0; y < h; y++) {
                        int ty = b.top + y;
                        if (ty < 0 || ty >= t->h) continue;
                        for (int x = 0; x < w; x++) {
                            int tx = b.left + x;
                            if (tx < 0 || tx >= t->w) continue;
                            const u8 *p = tmp + ((size_t)y * w + x) * bpp;
                            int v = bpp == 1 ? p[0] : (p[0] + p[1] + p[2]) / 3;
                            u8 *d = &t->cov[(size_t)ty * t->w + tx];
                            *d = (u8)MIN(255, *d + v);
                        }
                    }
                }
                ana->lpVtbl->Release(ana);
            }
        }
        pen += adv;
    }
}

// Coverage at a fractional x (marquee scrolls smoothly), pen-relative: x = 0 is the pen start.
static f32 mtext_at(const MText *t, f32 x, int y)
{
    if (!t->cov || y < 0 || y >= t->h) return 0;
    int pad = (t->w - t->adv) / 2;
    x += (f32)pad;
    int x0 = (int)floorf(x);
    f32 fr = x - (f32)x0;
    const u8 *row = t->cov + (size_t)y * t->w;
    f32 a = x0 >= 0 && x0 < t->w ? row[x0] : 0, b = x0 + 1 >= 0 && x0 + 1 < t->w ? row[x0 + 1] : 0;
    return (a + (b - a) * fr) / 255.f;
}

// Main thread: geometry and drawing

static f32 MS(f32 v) { return floorf(v * M.s + 0.5f); }

static bool media_controls(void) { return g_cfg.media_controls; }

// Low taskbars ("small icons") get a smaller cover and the title alone.
static bool media_small(void) { return (f32)M.h < MS(36); }
static f32 media_cover(void) { return media_small() ? MIN(MS(24), (f32)M.h - MS(4)) : MS(32); }

static f32 media_text_size(void) { return floorf(12.f * M.s * 2.f + 0.5f) * 0.5f; }

static void media_layout(void)
{
    f32 size = media_text_size();
    mtext_make(&M.title, FONT_TEXT, size, M.info.title);
    mtext_make(&M.artist, FONT_TEXT, size, media_small() ? L"" : M.info.artist);
    static const WCHAR note[] = { 0xEC4F, 0 };  // MusicNote
    mtext_make(&M.note, FONT_ICON, floorf(16.f * M.s + 0.5f), note);

    f32 pad = MS(4), cover = media_cover(), gap = MS(8);
    f32 tw = (f32)MAX(M.title.adv, M.artist.adv);
    M.text_w = MIN(tw, MS(150));
    M.text_x = pad + cover + gap;
    f32 x = M.text_x + M.text_w;
    f32 h = (f32)M.h;
    M.part[MP_INFO][0] = 0;
    M.part[MP_INFO][1] = 0;
    M.part[MP_INFO][3] = h;
    if (media_controls()) {
        x += MS(6);
        M.part[MP_INFO][2] = x;
        f32 bw = MS(30), bh = MIN(MS(32), h - MS(4));
        for (int i = MP_PREV; i <= MP_NEXT; i++) {
            M.part[i][0] = x;
            M.part[i][1] = floorf((h - bh) * 0.5f);
            M.part[i][2] = x + bw;
            M.part[i][3] = M.part[i][1] + bh;
            x += bw;
        }
        x += MS(2);
    } else {
        x += MS(10);
        M.part[MP_INFO][2] = x;
        for (int i = MP_PREV; i <= MP_NEXT; i++) memset(M.part[i], 0, sizeof M.part[i]);
    }
    M.w = (int)x;
}

// iq's exact signed distance to a triangle.
static f32 sd_triangle(f32 px, f32 py, f32 x0, f32 y0, f32 x1, f32 y1, f32 x2, f32 y2)
{
    f32 e0x = x1 - x0, e0y = y1 - y0, e1x = x2 - x1, e1y = y2 - y1, e2x = x0 - x2, e2y = y0 - y2;
    f32 v0x = px - x0, v0y = py - y0, v1x = px - x1, v1y = py - y1, v2x = px - x2, v2y = py - y2;
    f32 t0 = CLAMP((v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y), 0.f, 1.f);
    f32 t1 = CLAMP((v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y), 0.f, 1.f);
    f32 t2 = CLAMP((v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y), 0.f, 1.f);
    f32 q0x = v0x - e0x * t0, q0y = v0y - e0y * t0, q1x = v1x - e1x * t1, q1y = v1y - e1y * t1, q2x = v2x - e2x * t2, q2y = v2y - e2y * t2;
    f32 sg = e0x * e2y - e0y * e2x > 0 ? 1.f : -1.f;
    f32 dx = MIN(MIN(q0x * q0x + q0y * q0y, q1x * q1x + q1y * q1y), q2x * q2x + q2y * q2y);
    f32 dy = MIN(MIN(sg * (v0x * e0y - v0y * e0x), sg * (v1x * e1y - v1y * e1x)), sg * (v2x * e2y - v2y * e2x));
    return -sqrtf(dx) * (dy > 0 ? 1.f : -1.f);
}

// Playback glyphs, filled with softly rounded corners. (fx, fy) relative to the button center.
static f32 media_icon(int part, bool playing, f32 fx, f32 fy)
{
    f32 u = M.s, r = 1.1f * u, d;
    switch (part) {
    case MP_TOGGLE:
        if (playing) {
            f32 a = tb_sdbox(fx + 3.f * u, fy, 1.6f * u, 6.f * u, 1.f * u), b = tb_sdbox(fx - 3.f * u, fy, 1.6f * u, 6.f * u, 1.f * u);
            d = MIN(a, b);
        } else {
            d = sd_triangle(fx, fy, -4.2f * u + r, -6.4f * u + r * 1.6f, 6.4f * u - r * 1.8f, 0, -4.2f * u + r, 6.4f * u - r * 1.6f) - r;
        }
        break;
    case MP_PREV:
    case MP_NEXT: {
        if (part == MP_NEXT) fx = -fx;
        f32 bar = tb_sdbox(fx + 4.6f * u, fy, 0.9f * u, 5.f * u, 0.9f * u);
        f32 tri = sd_triangle(fx, fy, 4.6f * u - r, -5.f * u + r * 1.6f, -3.f * u + r * 1.8f, 0, 4.6f * u - r, 5.f * u - r * 1.6f) - r;
        d = MIN(bar, tri);
        break;
    }
    default:
        return 0;
    }
    return CLAMP(0.5f - d, 0.f, 1.f);
}

static void media_free_bitmap(void)
{
    if (M.dc) DeleteDC(M.dc);
    if (M.bmp) DeleteObject(M.bmp);
    M.dc = NULL;
    M.bmp = NULL;
    M.bits = NULL;
    M.bmp_w = M.bmp_h = 0;
}

static bool media_playing(void)
{
    if (M.toggle_local && time_now() < M.toggle_until) return M.toggle_status == MS_PLAYING;
    return M.info.status == MS_PLAYING;
}

static void media_render(void)
{
    int w = M.w, h = M.h;
    if (w <= 0 || h <= 0) return;
    if (!M.bmp || M.bmp_w != w || M.bmp_h != h) {
        media_free_bitmap();
        BITMAPINFO bi;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        M.dc = CreateCompatibleDC(NULL);
        M.bmp = CreateDIBSection(M.dc, &bi, DIB_RGB_COLORS, (void **)&M.bits, NULL, 0);
        if (!M.bmp) {
            media_free_bitmap();
            return;
        }
        SelectObject(M.dc, M.bmp);
        M.bmp_w = w;
        M.bmp_h = h;
    }

    bool light = M.light, playing = media_playing();
    f32 fg[3];
    for (int k = 0; k < 3; k++) fg[k] = light ? 0.f : 1.f;
    f32 vis = M.vis_t, content = vis * (0.35f + 0.65f * M.swap_t);
    f32 hov_base = light ? 0.055f : 0.06f, hov_btn = light ? 0.06f : 0.08f;
    f32 bg_a = hov_base * M.hov_t * vis;
    f32 brad = MS(5), s = M.s;

    // cover
    f32 cover = media_cover(), cx0 = MS(4), cy0 = floorf(((f32)h - cover) * 0.5f), crad = MS(4);
    bool art = M.art && M.art_n == (int)cover;
    f32 ccx = cx0 + cover * 0.5f, ccy = cy0 + cover * 0.5f;
    f32 note_size = floorf(16.f * s + 0.5f);
    int note_base = (int)floorf(ccy + (font_ascent(FONT_ICON, note_size) - font_descent(FONT_ICON, note_size)) * 0.5f + 0.5f);
    // text lines
    f32 size = media_text_size(), cap = font_cap_height(FONT_TEXT, size);
    bool two = M.artist.cov != NULL;
    f32 c = (f32)h * 0.5f;
    int base1 = (int)floorf(two ? c - MS(8) + cap * 0.5f + 0.5f : c + cap * 0.5f + 0.5f), base2 = (int)floorf(c + MS(8.5f) + cap * 0.5f + 0.5f);
    f32 tx0 = M.text_x, tx1 = M.text_x + M.text_w, fade = MS(12), gap = MS(28);
    f32 scroll = M.scroll, text_a = content * M.text_a;
    bool over1 = (f32)M.title.adv > M.text_w + 0.5f, over2 = (f32)M.artist.adv > M.text_w + 0.5f;
    f32 per1 = (f32)M.title.adv + gap, per2 = (f32)M.artist.adv + gap;

    u32 *px = M.bits;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            f32 fx = (f32)x + 0.5f, fy = (f32)y + 0.5f;
            f32 col[3] = { 0, 0, 0 }, a = 0;
            if (bg_a > 0.f) tb_over(col, &a, fg, bg_a * CLAMP(0.5f - tb_sdbox(fx - (f32)w * 0.5f, fy - (f32)h * 0.5f, (f32)w * 0.5f, (f32)h * 0.5f - MS(1), brad), 0.f, 1.f));

            // buttons
            for (int p = MP_PREV; p <= MP_NEXT && media_controls(); p++) {
                const f32 *r = M.part[p];
                if (fx < r[0] || fx >= r[2] || fy < r[1] || fy >= r[3]) continue;
                f32 bx = (r[0] + r[2]) * 0.5f, by = (r[1] + r[3]) * 0.5f;
                f32 ha = (hov_btn * M.hp_t[p] + (light ? 0.03f : -0.02f) * M.press_t[p]) * vis;
                if (ha > 0.f) tb_over(col, &a, fg, ha * CLAMP(0.5f - tb_sdbox(fx - bx, fy - by, (r[2] - r[0]) * 0.5f - MS(1), (r[3] - r[1]) * 0.5f - MS(2), MS(4)), 0.f, 1.f));
                bool en = p == MP_PREV ? M.info.can_prev : p == MP_NEXT ? M.info.can_next : M.info.can_toggle;
                f32 zoom = 1.f - 0.1f * M.press_t[p];
                f32 m = media_icon(p, playing, (fx - bx) / zoom, (fy - by) / zoom);
                if (m > 0.f) tb_over(col, &a, fg, m * content * (en ? (light ? 0.86f : 0.92f) : 0.3f));
            }

            // cover
            if (fx >= cx0 - 1 && fx < cx0 + cover + 1 && fy >= cy0 - 1 && fy < cy0 + cover + 1) {
                f32 m = CLAMP(0.5f - tb_sdbox(fx - ccx, fy - ccy, cover * 0.5f, cover * 0.5f, crad), 0.f, 1.f) * content;
                if (m > 0.f && art) {
                    int ix = CLAMP(x - (int)cx0, 0, M.art_n - 1), iy = CLAMP(y - (int)cy0, 0, M.art_n - 1);
                    u32 p = M.art[iy * M.art_n + ix];
                    f32 ia = (f32)(p >> 24) / 255.f * m;
                    col[0] = (f32)((p >> 16) & 255) / 255.f * m + col[0] * (1.f - ia);
                    col[1] = (f32)((p >> 8) & 255) / 255.f * m + col[1] * (1.f - ia);
                    col[2] = (f32)(p & 255) / 255.f * m + col[2] * (1.f - ia);
                    a = ia + a * (1.f - ia);
                } else if (m > 0.f) {
                    tb_over(col, &a, fg, m * (light ? 0.07f : 0.09f));
                    if (M.note.cov) {
                        f32 v = mtext_at(&M.note, (f32)x - floorf(ccx - (f32)M.note.adv * 0.5f), y - note_base + M.note.base);
                        if (v > 0.f) tb_over(col, &a, fg, v * content * 0.55f);
                    }
                }
            }

            // text
            if (fx >= tx0 && fx < tx1 + 1) {
                for (int line = 0; line < 2; line++) {
                    const MText *t = line ? &M.artist : &M.title;
                    if (!t->cov) continue;
                    int ty = y - (line ? base2 : base1) + t->base;
                    if (ty < 0 || ty >= t->h) continue;
                    bool over = line ? over2 : over1;
                    f32 per = line ? per2 : per1;
                    f32 off = over ? fmodf(scroll, per) : 0.f;
                    f32 lx = (f32)x - tx0 + off;
                    f32 v = mtext_at(t, lx, ty);
                    if (over && lx + 1.f > per - gap) v = MAX(v, mtext_at(t, lx - per, ty));
                    if (v <= 0.f) continue;
                    f32 mask = 1.f;
                    if (over) {
                        mask *= CLAMP((tx1 - fx) / fade, 0.f, 1.f);
                        if (off > 0.f) mask *= CLAMP((fx - tx0) / fade, 0.f, 1.f);
                    } else if (fx > tx1) {
                        mask = 0;
                    }
                    tb_over(col, &a, fg, v * mask * text_a * (line ? (light ? 0.6f : 0.62f) : (light ? 0.9f : 1.f)));
                }
            }

            if (a < 1.f / 255.f) a = 1.f / 255.f;  // alpha 0 would let clicks through to the taskbar
            u32 A = (u32)(a * 255.f + 0.5f);
            u32 R = (u32)(CLAMP(col[0], 0.f, a) * 255.f + 0.5f), G = (u32)(CLAMP(col[1], 0.f, a) * 255.f + 0.5f), B = (u32)(CLAMP(col[2], 0.f, a) * 255.f + 0.5f);
            px[y * w + x] = (A << 24) | (R << 16) | (G << 8) | B;
        }
    }
    SIZE sz = { w, h };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (M.wnd) UpdateLayeredWindow(M.wnd, NULL, NULL, &sz, M.dc, &src, 0, &bf, ULW_ALPHA);
}

// Main thread: behavior

static void media_animate(void);

static void media_kick(void)
{
    if (!M.wnd) return;
    M.last_anim = time_now();
    SetTimer(M.wnd, MW_TIMER_ANIM, 15, NULL);
    media_animate();
}

static void media_animate(void)
{
    f64 now = time_now();
    f32 dt = (f32)MIN(now - M.last_anim, 0.05);
    M.last_anim = now;
    bool anim = false;
    anim |= approach(&M.vis_t, M.want ? 1.f : 0.f, 16.f, dt);
    anim |= approach(&M.swap_t, 1.f, 14.f, dt);
    anim |= approach(&M.hov_t, M.hover_any || M.press != MP_NONE ? 1.f : 0.f, 18.f, dt);
    for (int p = MP_PREV; p <= MP_NEXT; p++) {
        anim |= approach(&M.hp_t[p], M.hover == p || M.press == p ? 1.f : 0.f, 20.f, dt);
        anim |= approach(&M.press_t[p], M.press == p ? 1.f : 0.f, 30.f, dt);
    }
    // Marquee: long titles scroll while the pointer is over the widget; on leave they fade back to the start.
    bool over = (f32)MAX(M.title.adv, M.artist.adv) > M.text_w + 0.5f;
    if (M.hover_any && over && M.text_a > 0.99f) {
        M.scroll_t += dt;
        if (M.scroll_t > 0.6f) M.scroll += dt * 30.f * M.s;
        anim = true;
    } else if (M.scroll > 0.f) {
        M.scroll_t = 0;
        anim |= approach(&M.text_a, 0.f, 22.f, dt);
        if (M.text_a < 0.02f) M.scroll = 0;
        anim = true;
    } else {
        M.scroll_t = 0;
        anim |= approach(&M.text_a, 1.f, 16.f, dt);
    }
    if (M.vis_t <= 0.f && !M.want && M.visible) {
        ShowWindow(M.wnd, SW_HIDE);
        M.visible = false;
    }
    if (M.visible) media_render();
    if (anim && (M.visible || M.want)) SetTimer(M.wnd, MW_TIMER_ANIM, 15, NULL);
    else KillTimer(M.wnd, MW_TIMER_ANIM);
}

static bool media_win11(void)
{
    static int v = -1;
    if (v < 0) {
        WCHAR b[16] = L"";
        DWORD sz = sizeof b;
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber", RRF_RT_REG_SZ, NULL, b, &sz);
        v = _wtoi(b) >= 22000;
    }
    return v != 0;
}

// Icons on the left (Windows 10, or "Taskbar alignment: Left") take the left end: stay right then.
static bool media_on_left(void) { return g_cfg.media_position == 0 && media_win11() && tb_read_centered(); }

static void media_place(void)
{
    if (!M.wnd || !M.taskbar) return;
    RECT tr;
    GetWindowRect(M.taskbar, &tr);
    int tw = tr.right - tr.left, th = tr.bottom - tr.top;
    if (tw <= 0 || th <= 0 || th > tw) return;  // vertical taskbars are not supported
    int x;
    if (media_on_left()) {
        RECT wb;
        AcquireSRWLockShared(&M.lock);
        wb = M.widgets_btn;
        ReleaseSRWLockShared(&M.lock);
        x = (int)MS(12);
        if (wb.right > wb.left && wb.right < (tr.left + tr.right) / 2) x = wb.right - tr.left + (int)MS(4);
    } else {
        HWND tray = FindWindowExW(M.taskbar, NULL, L"TrayNotifyWnd", NULL);
        RECT r;
        if (tray && GetWindowRect(tray, &r) && r.left > tr.left) x = r.left - tr.left - M.w - (int)MS(8);
        else x = tw - M.w - (int)MS(20);
    }
    int y = (th - M.h) / 2;
    POINT p = { tr.left + x, tr.top + y };
    ScreenToClient(M.taskbar, &p);
    RECT cur;
    GetWindowRect(M.wnd, &cur);
    POINT cp = { cur.left, cur.top };
    ScreenToClient(M.taskbar, &cp);
    bool moved = cp.x != p.x || cp.y != p.y || cur.right - cur.left != M.w || cur.bottom - cur.top != M.h;
    bool covered = GetWindow(M.wnd, GW_HWNDPREV) != NULL;  // the taskbar's XAML island went above us
    if (moved || covered || !IsWindowVisible(M.wnd))
        SetWindowPos(M.wnd, HWND_TOP, p.x, p.y, M.w, M.h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    M.x = x;
}

static void media_sync(void);

static bool media_create(void)
{
    HWND tb = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!tb) return false;
    if (M.wnd && IsWindow(M.wnd) && M.taskbar == tb) return true;
    if (M.wnd && IsWindow(M.wnd)) DestroyWindow(M.wnd);
    M.wnd = NULL;
    media_free_bitmap();
    M.taskbar = tb;
    M.visible = false;
    M.vis_t = 0;
    M.wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, MW_CLASS, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, tb, NULL,
                            GetModuleHandleW(NULL), NULL);
    if (!M.wnd) {
        log_msg("media: could not create the widget window (%lu)", GetLastError());
        return false;
    }
    SetTimer(M.wnd, MW_TIMER_SYNC, 1000, NULL);
    M.dpi = 0;  // lay out again
    return true;
}

// Everything that can change the picture or the position: new media state, DPI, taskbar moves.
static void media_sync(void)
{
    if (!M.enabled) return;
    if (!media_create()) return;

    MediaInfo in;
    u32 *art = NULL;
    int art_n = 0;
    AcquireSRWLockShared(&M.lock);
    in = M.pub;
    bool new_art = M.pub_art_gen != M.art_gen;
    if (new_art && M.pub_art) {
        art_n = M.pub_art_n;
        art = (u32 *)malloc((size_t)art_n * art_n * 4);
        if (art) memcpy(art, M.pub_art, (size_t)art_n * art_n * 4);
    }
    M.art_gen = M.pub_art_gen;
    ReleaseSRWLockShared(&M.lock);
    if (new_art) {
        free(M.art);
        M.art = art;
        M.art_n = art ? art_n : 0;
    }

    bool track = wcscmp(in.title, M.info.title) || wcscmp(in.artist, M.info.artist);
    if (M.toggle_local && (in.status != M.info.status || time_now() >= M.toggle_until)) M.toggle_local = false;
    M.info = in;

    UINT dpi = monitor_dpi(MonitorFromWindow(M.taskbar, MONITOR_DEFAULTTONEAREST));
    RECT tr;
    GetWindowRect(M.taskbar, &tr);
    int h = MIN((int)floorf(40.f * (f32)dpi / 96.f + 0.5f), (int)(tr.bottom - tr.top) - 2);
    bool relayout = track || dpi != M.dpi || h != M.h;
    if (dpi != M.dpi) {
        M.dpi = dpi;
        M.s = (f32)dpi / 96.f;
    }
    InterlockedExchange(&M.want_widgets_btn, media_on_left());
    if (relayout) {
        M.h = MAX(h, 16);
        LONG px = (LONG)media_cover();
        if (InterlockedExchange(&M.art_px, px) != px) {
            InterlockedExchange(&M.art_reload, 1);
            SetEvent(M.wake);
        }
        media_layout();
        M.scroll = 0;
        M.scroll_t = 0;
        if (track && M.visible) M.swap_t = 0;
    }

    bool active = in.has && in.title[0] && in.status != MS_CLOSED;
    bool playing = in.status == MS_PLAYING || (M.toggle_local && M.toggle_status == MS_PLAYING);
    f64 now = time_now();
    if (playing || !M.idle_since) M.idle_since = playing ? 0 : now;
    bool want = active && (!g_cfg.media_hide_paused || playing || now - M.idle_since < 1.0);
    if (want != M.want) M.want = want;
    if (want) {
        media_place();
        M.visible = true;
    }
    if (M.visible) media_kick();
}

static void media_send(int cmd)
{
    InterlockedExchange(&M.cmd, cmd);
    SetEvent(M.wake);
}

typedef struct { const WCHAR *app; HWND found; } MediaFind;

static BOOL CALLBACK media_find_window(HWND h, LPARAM lp)
{
    MediaFind *mf = (MediaFind *)lp;
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) || (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) || !GetWindowTextLengthW(h)) return TRUE;
    // UWP apps and anything with an explicit AppUserModelID carry it on the window.
    IPropertyStore *ps = NULL;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(h, &IID_IPropertyStore, (void **)&ps)) && ps) {
        static const PROPERTYKEY key = { { 0x9f4c2855, 0x9f79, 0x4b39, { 0xa8, 0xd0, 0xe1, 0xd4, 0x2d, 0xe1, 0xd5, 0xf3 } }, 5 };
        PROPVARIANT v;
        PropVariantInit(&v);
        bool hit = SUCCEEDED(IPropertyStore_GetValue(ps, &key, &v)) && v.vt == VT_LPWSTR && v.pwszVal && !_wcsicmp(v.pwszVal, mf->app);
        PropVariantClear(&v);
        IPropertyStore_Release(ps);
        if (hit) {
            mf->found = h;
            return FALSE;
        }
    }
    // Desktop players report their exe name ("Spotify.exe").
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc) {
        WCHAR path[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(proc, 0, path, &n)) {
            const WCHAR *name = PathFindFileNameW(path);
            int an = wlen(mf->app);
            bool hit = !_wcsicmp(name, mf->app) || (!_wcsnicmp(name, mf->app, an) && !_wcsicmp(name + an, L".exe"));
            if (hit) mf->found = h;
        }
        CloseHandle(proc);
    }
    return mf->found ? FALSE : TRUE;
}

static void media_open_player(void)
{
    if (!M.info.app[0]) return;
    MediaFind mf = { M.info.app, NULL };
    EnumWindows(media_find_window, (LPARAM)&mf);
    if (mf.found) {
        if (IsIconic(mf.found)) ShowWindow(mf.found, SW_RESTORE);
        if (!SetForegroundWindow(mf.found)) SwitchToThisWindow(mf.found, TRUE);
        return;
    }
    LaunchJob j;
    memset(&j, 0, sizeof j);
    j.act = ACT_OPEN;
    j.is_app = true;
    j.target = wdup_heap(M.info.app);
    launch_submit(&j);
}

enum { MM_PLAYER = 1, MM_SETTINGS, MM_HIDE };

static void media_set_enabled(bool on);

static void media_menu(void)
{
    HMENU m = CreatePopupMenu();
    if (M.info.app[0]) AppendMenuW(m, MF_STRING, MM_PLAYER, TR("Открыть плеер", "Open the player"));
    AppendMenuW(m, MF_STRING, MM_SETTINGS, TR("Настройки…", "Settings…"));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, MM_HIDE, TR("Убрать с панели задач", "Remove from the taskbar"));
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
    case MM_PLAYER:
        media_open_player();
        break;
    case MM_SETTINGS:
        settings_open();
        break;
    case MM_HIDE:
        g_cfg.media_widget = false;
        config_set("media_widget", "0");
        media_set_enabled(false);
        settings_invalidate();
        break;
    }
}

static int media_part_at(int x, int y)
{
    for (int p = MP_INFO; p <= MP_NEXT; p++) {
        const f32 *r = M.part[p];
        if ((f32)x >= r[0] && (f32)x < r[2] && (f32)y >= r[1] && (f32)y < r[3]) return p;
    }
    return MP_INFO;  // the gaps around the buttons belong to the info area
}

static void media_click(int part)
{
    switch (part) {
    case MP_INFO:
        AllowSetForegroundWindow(ASFW_ANY);
        media_open_player();
        break;
    case MP_PREV:
        if (M.info.can_prev) media_send(MC_PREV);
        break;
    case MP_NEXT:
        if (M.info.can_next) media_send(MC_NEXT);
        break;
    case MP_TOGGLE:
        if (M.info.can_toggle) {
            M.toggle_status = media_playing() ? MS_PAUSED : MS_PLAYING;
            M.toggle_local = true;
            M.toggle_until = time_now() + 1.5;
            media_send(MC_TOGGLE);
        }
        break;
    }
}

static LRESULT CALLBACK media_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
        return TRUE;
    case WM_MOUSEMOVE: {
        int p = media_part_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (!M.hover_any) {
            M.hover_any = true;
            TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
            media_kick();
        }
        if (p != M.hover) {
            M.hover = p;
            media_kick();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        M.hover_any = false;
        M.hover = MP_NONE;
        if (GetCapture() != h) M.press = MP_NONE;
        media_kick();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        M.press = media_part_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        SetCapture(h);
        media_kick();
        return 0;
    case WM_LBUTTONUP: {
        int was = M.press;
        M.press = MP_NONE;
        ReleaseCapture();
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        bool inside = x >= 0 && y >= 0 && x < M.w && y < M.h;
        if (was != MP_NONE && inside && media_part_at(x, y) == was) media_click(was);
        media_kick();
        return 0;
    }
    case WM_RBUTTONUP:
        if (U.visible && !U.closing) ui_hide();
        media_menu();
        return 0;
    case WM_TIMER:
        if (wp == MW_TIMER_ANIM) media_animate();
        else if (wp == MW_TIMER_SYNC) media_sync();
        return 0;
    case WM_DESTROY:
        if (M.wnd == h) {
            M.wnd = NULL;
            M.visible = false;
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void media_theme_changed(void)
{
    bool l = taskbar_light();
    if (l == M.light) return;
    M.light = l;
    if (M.visible) media_render();
}

static void media_explorer_restarted(void)
{
    if (!M.enabled) return;
    if (M.wnd && IsWindow(M.wnd)) DestroyWindow(M.wnd);
    M.wnd = NULL;
    media_free_bitmap();
    M.taskbar = NULL;
    media_sync();
}

static void media_settings_changed(void)
{
    if (!M.enabled) return;
    M.dpi = 0;  // lay out again
    media_sync();
}

static void media_set_enabled(bool on)
{
    M.enabled = on;
    if (on) {
        static bool registered;
        if (!registered) {
            registered = true;
            WNDCLASSEXW wc;
            memset(&wc, 0, sizeof wc);
            wc.cbSize = sizeof wc;
            wc.style = CS_DBLCLKS;
            wc.lpfnWndProc = media_wndproc;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.lpszClassName = MW_CLASS;
            RegisterClassExW(&wc);
        }
        M.light = taskbar_light();
        if (!M.thread) {
            InitializeSRWLock(&M.lock);
            M.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
            M.thread = CreateThread(NULL, 0, media_thread, NULL, 0, NULL);
        }
        SetEvent(M.wake);
        media_sync();
    } else {
        if (M.wnd && IsWindow(M.wnd)) DestroyWindow(M.wnd);
        M.wnd = NULL;
        M.want = false;
        M.vis_t = 0;
        media_free_bitmap();
        AcquireSRWLockExclusive(&M.lock);
        memset(&M.pub, 0, sizeof M.pub);
        free(M.pub_art);
        M.pub_art = NULL;
        M.pub_art_gen++;
        ReleaseSRWLockExclusive(&M.lock);
        memset(&M.info, 0, sizeof M.info);
        if (M.wake) SetEvent(M.wake);
    }
}

// Diagnostics: read the current session once and draw the widget over a taskbar-like
// background into a BMP, without touching the real taskbar. --media-dump <out.bmp> [dpi] [light] [sample]
// ("sample" draws a long title without a cover).
static int media_dump(const WCHAR *out, int dpi, bool light, bool sample)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (!rt_load() || !font_init()) return 1;
    M.s = (f32)dpi / 96.f;
    M.light = light;
    IUnknown *mgr = media_manager(), *ses = NULL, *thumb = NULL;
    if (mgr) RT_GET(mgr, RT_MGR_CURRENT, &ses);
    if (ses) media_read(ses, &M.info, &thumb);
    u64 h = 0;
    M.h = (int)floorf(40.f * M.s + 0.5f);
    M.art_n = (int)media_cover();
    M.art = media_decode_art(thumb, M.art_n, &h);
    if (sample || !M.info.title[0]) {
        wcopy(M.info.title, countof(M.info.title), L"A rather long track title that does not fit");
        free(M.art);
        M.art = NULL;
    }
    media_layout();
    M.vis_t = M.swap_t = M.text_a = 1;
    M.hov_t = 0;
    media_render();
    int w = M.w, hh = (int)floorf(48.f * M.s + 0.5f), y0 = (hh - M.h) / 2;
    u32 *img = (u32 *)malloc((size_t)w * hh * 4);
    if (!img || !M.bits) return 1;
    u32 bg = light ? 0xF3F3F3 : 0x1C1C1C;
    for (int y = 0; y < hh; y++)
        for (int x = 0; x < w; x++) {
            u32 d = bg;
            if (y >= y0 && y < y0 + M.h) {
                u32 p = M.bits[(y - y0) * w + x], a = p >> 24, r = 0;
                for (int k = 0; k < 24; k += 8) r |= (MIN(255u, ((p >> k) & 255) + ((bg >> k) & 255) * (255 - a) / 255)) << k;
                d = r;
            }
            img[(hh - 1 - y) * w + x] = d;
        }
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    memset(&fh, 0, sizeof fh);
    memset(&ih, 0, sizeof ih);
    ih.biSize = sizeof ih;
    ih.biWidth = w;
    ih.biHeight = hh;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + (DWORD)(w * hh * 4);
    Buf b = { 0 };
    buf_put(&b, &fh, sizeof fh);
    buf_put(&b, &ih, sizeof ih);
    buf_put(&b, img, (size_t)w * hh * 4);
    write_file_atomic(out, b.data, (DWORD)b.len);
    Buf t = { 0 };
    buf_printf(&t, "has=%d status=%d prev=%d toggle=%d next=%d art=%d\n", M.info.has, M.info.status, M.info.can_prev, M.info.can_toggle,
               M.info.can_next, M.art != NULL);
    buf_put_w(&t, M.info.title);
    buf_put(&t, "\n", 1);
    buf_put_w(&t, M.info.artist);
    buf_put(&t, "\n", 1);
    buf_put_w(&t, M.info.app);
    buf_put(&t, "\n", 1);
    WCHAR txt[MAX_PATH + 8];
    _snwprintf(txt, countof(txt), L"%s.txt", out);
    txt[countof(txt) - 1] = 0;
    write_file_atomic(txt, t.data, (DWORD)t.len);
    return 0;
}
