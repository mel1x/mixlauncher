// tray.c — procedural app icon, notification-area icon, autostart and dark native menus.

// The icon is drawn with signed distance fields at any size: a near-black squircle with a
// hairline edge and a white search field (pill outline + caret), i.e. the launcher itself.
static f32 sd_round_box(f32 px, f32 py, f32 hx, f32 hy, f32 r)
{
    f32 qx = fabsf(px) - hx + r, qy = fabsf(py) - hy + r;
    f32 ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
    return sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - r;
}

// Straight-alpha BGRA, top-down.
static void app_icon_render(u32 *out, int S)
{
    f32 s = (f32)S;
    f32 inset = s * 0.0625f, half = s * 0.5f - inset, rad = s * 0.225f;
    f32 pw = MAX(s * 0.035f, 0.75f);                        // field outline width
    f32 cw = MAX(s * 0.024f, 0.65f), ch = MAX(s * 0.068f, 1.5f);  // caret
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            f32 px = x + 0.5f - s * 0.5f, py = y + 0.5f - s * 0.5f;
            f32 dbg = sd_round_box(px, py, half, half, rad);
            f32 abg = CLAMP(0.5f - dbg, 0.f, 1.f);
            f32 ring = CLAMP(0.5f - (fabsf(dbg + 0.5f + s * 0.004f) - 0.5f), 0.f, 1.f) * 0.10f;
            f32 v = 17.f + (255.f - 17.f) * ring;
            f32 dp = fabsf(sd_round_box(px, py, s * 0.30f, s * 0.13f, s * 0.13f)) - pw;
            f32 dc = sd_round_box(px + s * 0.14f, py, cw, ch, cw);
            f32 m = CLAMP(0.5f - MIN(dp, dc), 0.f, 1.f);
            v = v * (1 - m) + 255.f * m;
            u32 a = (u32)(abg * 255.f + 0.5f), c = (u32)(v + 0.5f);
            out[y * S + x] = (a << 24) | (c << 16) | (c << 8) | c;
        }
    }
}

static HICON app_icon_create(int S)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = S;
    bi.bmiHeader.biHeight = -S;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!color) return NULL;
    app_icon_render((u32 *)bits, S);
    HBITMAP mask = CreateBitmap(S, S, 1, 1, NULL);
    ICONINFO ii;
    memset(&ii, 0, sizeof ii);
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

// Write a multi-resolution .ico (used once to produce res/mixlauncher.ico).
static bool app_icon_write_ico(const WCHAR *path)
{
    static const int sizes[] = { 16, 20, 24, 32, 40, 48, 64, 96, 128 };
    Buf b = { 0 };
    u16 hdr[3] = { 0, 1, (u16)countof(sizes) };
    buf_put(&b, hdr, 6);
    u32 offset = 6 + 16 * countof(sizes);
    u32 sizes_bytes[countof(sizes)];
    for (int i = 0; i < countof(sizes); i++) {
        int S = sizes[i];
        u32 mask_row = (u32)((S + 31) / 32) * 4;
        sizes_bytes[i] = 40 + (u32)S * S * 4 + mask_row * S;
        u8 e[16];
        memset(e, 0, sizeof e);
        e[0] = (u8)(S >= 256 ? 0 : S);
        e[1] = (u8)(S >= 256 ? 0 : S);
        e[4] = 1;
        e[6] = 32;
        memcpy(e + 8, &sizes_bytes[i], 4);
        memcpy(e + 12, &offset, 4);
        buf_put(&b, e, 16);
        offset += sizes_bytes[i];
    }
    for (int i = 0; i < countof(sizes); i++) {
        int S = sizes[i];
        BITMAPINFOHEADER bh;
        memset(&bh, 0, sizeof bh);
        bh.biSize = 40;
        bh.biWidth = S;
        bh.biHeight = S * 2;
        bh.biPlanes = 1;
        bh.biBitCount = 32;
        buf_put(&b, &bh, 40);
        u32 *px = (u32 *)malloc((size_t)S * S * 4);
        if (!px) return false;
        app_icon_render(px, S);
        for (int y = S - 1; y >= 0; y--) buf_put(&b, px + y * S, (size_t)S * 4);  // bottom-up
        free(px);
        u32 mask_row = (u32)((S + 31) / 32) * 4;
        u8 zero[64];
        memset(zero, 0, sizeof zero);
        for (int y = 0; y < S; y++) buf_put(&b, zero, mask_row);
    }
    bool ok = write_file_atomic(path, b.data, (DWORD)b.len);
    free(b.data);
    return ok;
}

// ---------------------------------------------------------------------------------------------

static NOTIFYICONDATAW g_nid;
static UINT g_wm_taskbar_created;
static HICON g_icon_small, g_icon_big;

static void tray_add(void)
{
    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = g_icon_small;
    wcopy(g_nid.szTip, countof(g_nid.szTip), L"MixLauncher");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void tray_remove(void) { Shell_NotifyIconW(NIM_DELETE, &g_nid); }

// Native popup menus follow the app theme only through uxtheme's (stable, undocumented)
// SetPreferredAppMode ordinal — the same thing Explorer's own menus use.
static void menus_set_dark(bool dark)
{
    static HMODULE ux;
    static int (WINAPI *set_mode)(int);
    static void (WINAPI *flush)(void);
    if (!ux) {
        ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (ux) {
            set_mode = (int (WINAPI *)(int))(void *)GetProcAddress(ux, MAKEINTRESOURCEA(135));
            flush = (void (WINAPI *)(void))(void *)GetProcAddress(ux, MAKEINTRESOURCEA(136));
        }
    }
    if (set_mode) set_mode(dark ? 2 /*ForceDark*/ : 3 /*ForceLight*/);
    if (flush) flush();
}
