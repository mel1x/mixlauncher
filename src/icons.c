// icons.c — asynchronous shell icon loading into a GPU atlas.
//
// The UI asks for an icon by key every frame it is visible. Unknown keys are queued (LIFO, so
// what is on screen right now loads first) to two worker threads that call
// IShellItemImageFactory, convert to premultiplied BGRA and hand the pixels back. The UI thread
// uploads them into fixed-size atlas slots with LRU eviction.

enum { ICON_KIND_APP, ICON_KIND_FILE };
enum { ICON_EMPTY, ICON_PENDING, ICON_READY, ICON_FAILED };

typedef struct IconEntry {
    u64 hash;
    u8 state;
    u16 slot;
    u32 last_used;
} IconEntry;

typedef struct IconReq {
    u64 hash;
    u32 gen;
    int px;
    u8 kind;
    WCHAR *path;
} IconReq;

typedef struct IconRes {
    u64 hash;
    u32 gen;
    int px;
    u32 *pixels;   // NULL = failed
} IconRes;

#define ICON_HASH 4096
#define ICON_MAX_SLOTS 1024

static struct {
    IconEntry table[ICON_HASH];
    int px;               // icon size in physical pixels
    int slot_px;          // px + gutter
    int per_row, slot_count;
    u64 slot_owner[ICON_MAX_SLOTS];
    u32 slot_used[ICON_MAX_SLOTS];
    u32 frame;
    int used;             // hash slots ever occupied (entries are recycled, never removed)
    volatile LONG gen;

    SRWLOCK lock;
    CONDITION_VARIABLE cv;
    IconReq *reqs;
    int req_count, req_cap;
    IconRes *res;
    int res_count, res_cap;
    bool posted;
} I;

static IconEntry *icon_entry(u64 h, bool create)
{
    u32 i = (u32)hash_u64(h) & (ICON_HASH - 1);
    IconEntry *tomb = NULL;
    for (int p = 0; p < ICON_HASH; p++) {
        IconEntry *e = &I.table[(i + p) & (ICON_HASH - 1)];
        if (e->hash == h) return e;
        if (!e->hash) {
            if (!create) return NULL;
            if (!tomb) I.used++;
            e = tomb ? tomb : e;
            memset(e, 0, sizeof *e);
            e->hash = h;
            return e;
        }
        if (e->state == ICON_EMPTY && !tomb) tomb = e;
    }
    if (tomb && create) {
        memset(tomb, 0, sizeof *tomb);
        tomb->hash = h;
        return tomb;
    }
    return NULL;
}

// Drop every icon (DPI change / device re-creation). In-flight results are discarded by gen.
static void icons_reset(int px)
{
    InterlockedIncrement(&I.gen);
    memset(I.table, 0, sizeof I.table);
    I.used = 0;
    memset(I.slot_owner, 0, sizeof I.slot_owner);
    memset(I.slot_used, 0, sizeof I.slot_used);
    I.px = px;
    I.slot_px = px + 2;
    I.per_row = ICON_ATLAS / I.slot_px;
    I.slot_count = MIN(I.per_row * I.per_row, ICON_MAX_SLOTS);
    AcquireSRWLockExclusive(&I.lock);
    for (int i = 0; i < I.req_count; i++) free(I.reqs[i].path);
    I.req_count = 0;
    ReleaseSRWLockExclusive(&I.lock);
}

static void icons_begin_frame(void)
{
    I.frame++;
    // Thousands of distinct file icons over a long session: start over rather than degrade.
    if (I.used > ICON_HASH * 3 / 4) icons_reset(I.px);
}

// Returns true and the uv rect if the icon is ready; otherwise schedules a load.
static bool icon_get(u64 h, u8 kind, const WCHAR *path, f32 uv[4])
{
    if (!h || !path) return false;
    IconEntry *e = icon_entry(h, true);
    if (!e) return false;
    if (e->state == ICON_READY) {
        e->last_used = I.frame;
        I.slot_used[e->slot] = I.frame;
        int sx = (e->slot % I.per_row) * I.slot_px + 1, sy = (e->slot / I.per_row) * I.slot_px + 1;
        uv[0] = (f32)sx / ICON_ATLAS;
        uv[1] = (f32)sy / ICON_ATLAS;
        uv[2] = (f32)(sx + I.px) / ICON_ATLAS;
        uv[3] = (f32)(sy + I.px) / ICON_ATLAS;
        return true;
    }
    if (e->state == ICON_EMPTY) {
        e->state = ICON_PENDING;
        AcquireSRWLockExclusive(&I.lock);
        if (I.req_count == I.req_cap) {
            int nc = I.req_cap ? I.req_cap * 2 : 128;
            IconReq *nr = (IconReq *)realloc(I.reqs, sizeof(IconReq) * nc);
            if (nr) {
                I.reqs = nr;
                I.req_cap = nc;
            }
        }
        if (I.req_count < I.req_cap) {
            IconReq *r = &I.reqs[I.req_count++];
            r->hash = h;
            r->gen = (u32)I.gen;
            r->px = I.px;
            r->kind = kind;
            r->path = wdup_heap(path);
        } else {
            e->state = ICON_EMPTY;
        }
        ReleaseSRWLockExclusive(&I.lock);
        WakeConditionVariable(&I.cv);
    }
    return false;
}

static bool icon_failed(u64 h)
{
    IconEntry *e = icon_entry(h, false);
    return e && e->state == ICON_FAILED;
}

static int icon_alloc_slot(void)
{
    int best = -1;
    u32 best_used = UINT32_MAX;
    for (int i = 0; i < I.slot_count; i++) {
        if (!I.slot_owner[i]) return i;
        if (I.slot_used[i] != I.frame && I.slot_used[i] < best_used) {
            best_used = I.slot_used[i];
            best = i;
        }
    }
    if (best >= 0) {
        IconEntry *old = icon_entry(I.slot_owner[best], false);
        if (old) old->state = ICON_EMPTY;
    }
    return best;
}

// UI thread: move finished icons into the atlas. Returns true if anything changed.
static bool icons_process_results(void)
{
    IconRes local[64];
    bool any = false;
    for (;;) {
        int n = 0;
        AcquireSRWLockExclusive(&I.lock);
        n = MIN(I.res_count, countof(local));
        memcpy(local, I.res, sizeof(IconRes) * n);
        memmove(I.res, I.res + n, sizeof(IconRes) * (I.res_count - n));
        I.res_count -= n;
        if (!I.res_count) I.posted = false;
        ReleaseSRWLockExclusive(&I.lock);
        if (!n) break;
        for (int i = 0; i < n; i++) {
            IconRes *r = &local[i];
            IconEntry *e = (r->gen == (u32)I.gen && r->px == I.px) ? icon_entry(r->hash, false) : NULL;
            if (e && e->state == ICON_PENDING) {
                if (!r->pixels) {
                    e->state = ICON_FAILED;
                } else {
                    int slot = icon_alloc_slot();
                    if (slot < 0) {
                        e->state = ICON_EMPTY;
                    } else {
                        I.slot_owner[slot] = r->hash;
                        I.slot_used[slot] = I.frame;
                        e->slot = (u16)slot;
                        e->state = ICON_READY;
                        e->last_used = I.frame;
                        int sx = (slot % I.per_row) * I.slot_px + 1, sy = (slot / I.per_row) * I.slot_px + 1;
                        r_upload_icon(sx, sy, I.px, r->pixels);
                    }
                }
                any = true;
            }
            free(r->pixels);
        }
    }
    return any;
}

// ---------------------------------------------------------------------------------------------
// Worker side

static u32 *hbitmap_to_pixels(HBITMAP bmp, int px)
{
    BITMAP bm;
    if (!GetObjectW(bmp, sizeof bm, &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return NULL;
    int w = bm.bmWidth, h = bm.bmHeight;
    u32 *src = (u32 *)malloc((size_t)w * h * 4);
    if (!src) return NULL;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(NULL);
    int got = GetDIBits(dc, bmp, 0, (UINT)h, src, &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);
    if (got != h) {
        free(src);
        return NULL;
    }
    // Alpha sanity: legacy icons may come without alpha; bitmaps may be straight alpha.
    bool any_alpha = false, straight = false;
    for (int i = 0; i < w * h; i++) {
        u32 p = src[i];
        u32 a = p >> 24;
        if (a) any_alpha = true;
        if (((p >> 16) & 255) > a || ((p >> 8) & 255) > a || (p & 255) > a) straight = true;
    }
    if (!any_alpha) {
        for (int i = 0; i < w * h; i++) src[i] |= 0xFF000000u;
    } else if (straight) {
        for (int i = 0; i < w * h; i++) {
            u32 p = src[i], a = p >> 24;
            u32 r = ((p >> 16) & 255) * a / 255, g = ((p >> 8) & 255) * a / 255, b = (p & 255) * a / 255;
            src[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    u32 *out = (u32 *)calloc((size_t)px * px, 4);
    if (!out) {
        free(src);
        return NULL;
    }
    if (w == px && h == px) {
        memcpy(out, src, (size_t)px * px * 4);
    } else {
        // Box-filter resample into px x px keeping aspect, centered.
        f32 scale = (f32)px / (f32)MAX(w, h);
        int dw = MAX(1, (int)(w * scale + 0.5f)), dh = MAX(1, (int)(h * scale + 0.5f));
        int ox = (px - dw) / 2, oy = (px - dh) / 2;
        for (int y = 0; y < dh; y++) {
            int sy0 = y * h / dh, sy1 = MAX(sy0 + 1, (y + 1) * h / dh);
            for (int x = 0; x < dw; x++) {
                int sx0 = x * w / dw, sx1 = MAX(sx0 + 1, (x + 1) * w / dw);
                u32 acc[4] = { 0, 0, 0, 0 }, cnt = 0;
                for (int sy = sy0; sy < sy1; sy++)
                    for (int sx = sx0; sx < sx1; sx++) {
                        u32 p = src[sy * w + sx];
                        acc[0] += p & 255;
                        acc[1] += (p >> 8) & 255;
                        acc[2] += (p >> 16) & 255;
                        acc[3] += p >> 24;
                        cnt++;
                    }
                out[(oy + y) * px + ox + x] = (acc[0] / cnt) | ((acc[1] / cnt) << 8) | ((acc[2] / cnt) << 16) | ((acc[3] / cnt) << 24);
            }
        }
    }
    free(src);
    return out;
}

static u32 *icon_load(IconReq *r)
{
    IShellItem *item = NULL;
    WCHAR buf[1024];
    const WCHAR *path = r->path;
    if (r->kind == ICON_KIND_APP) {
        _snwprintf(buf, countof(buf), L"shell:AppsFolder\\%s", r->path);
        buf[countof(buf) - 1] = 0;
        path = buf;
    }
    if (FAILED(SHCreateItemFromParsingName(path, NULL, &ML_IID_IShellItem, (void **)&item)) || !item) return NULL;
    IShellItemImageFactory *f = NULL;
    u32 *px = NULL;
    if (SUCCEEDED(IShellItem_QueryInterface(item, &ML_IID_IShellItemImageFactory, (void **)&f)) && f) {
        SIZE sz = { r->px, r->px };
        HBITMAP bmp = NULL;
        HRESULT hr = IShellItemImageFactory_GetImage(f, sz, SIIGBF_ICONONLY, &bmp);
        if (FAILED(hr)) hr = IShellItemImageFactory_GetImage(f, sz, SIIGBF_RESIZETOFIT, &bmp);
        if (SUCCEEDED(hr) && bmp) {
            px = hbitmap_to_pixels(bmp, r->px);
            DeleteObject(bmp);
        }
        IShellItemImageFactory_Release(f);
    }
    IShellItem_Release(item);
    return px;
}

static DWORD WINAPI icon_worker(void *param)
{
    (void)param;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        IconReq req;
        AcquireSRWLockExclusive(&I.lock);
        while (!I.req_count) SleepConditionVariableSRW(&I.cv, &I.lock, INFINITE, 0);
        req = I.reqs[--I.req_count];   // LIFO: newest request first
        ReleaseSRWLockExclusive(&I.lock);

        IconRes res = { req.hash, req.gen, req.px, NULL };
        if (req.gen == (u32)I.gen) res.pixels = icon_load(&req);
        free(req.path);

        AcquireSRWLockExclusive(&I.lock);
        if (I.res_count == I.res_cap) {
            int nc = I.res_cap ? I.res_cap * 2 : 64;
            IconRes *nr = (IconRes *)realloc(I.res, sizeof(IconRes) * nc);
            if (nr) {
                I.res = nr;
                I.res_cap = nc;
            }
        }
        bool post = false;
        if (I.res_count < I.res_cap) {
            I.res[I.res_count++] = res;
            if (!I.posted) {
                I.posted = true;
                post = true;
            }
        } else {
            free(res.pixels);
        }
        ReleaseSRWLockExclusive(&I.lock);
        if (post) PostMessageW(g_hwnd, WM_APP_ICONS_READY, 0, 0);
    }
    return 0;
}

static void icons_start(int px)
{
    InitializeSRWLock(&I.lock);
    InitializeConditionVariable(&I.cv);
    icons_reset(px);
    for (int i = 0; i < 2; i++) {
        HANDLE t = CreateThread(NULL, 0, icon_worker, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
}

// Files with per-file icons get their own key; everything else shares one icon per extension.
static u64 icon_key_for_file(const WCHAR *full, bool folder)
{
    if (folder) return hash_wstr_i(full, -1) ^ 0x1111;
    const WCHAR *ext = PathFindExtensionW(full);
    static const WCHAR *own[] = { L".exe", L".lnk", L".ico", L".url", L".cur", L".ani", L".appref-ms", L".msc", L".cpl", L".scr", L".website", L".msi", L"" };
    for (int i = 0; i < countof(own); i++)
        if (!_wcsicmp(ext, own[i])) return hash_wstr_i(full, -1) ^ 0x2222;
    return hash_wstr_i(ext, -1) ^ 0x3333;
}
