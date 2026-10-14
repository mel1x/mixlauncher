// Logo: three isometric plates (rounded squares turned 45 degrees and squashed flat), the top one brightest.
// In units of the logo box, 152 per side: plate centers 36 apart, half side 56, corner 9. On the dark tile
// the box takes 152/256 of the tile; without it (tray, small icons) it fills the whole icon.
#define APP_LOGO_SQUASH 0.53f
#define APP_LOGO_FILL   (152.f / 256.f)
#define APP_LOGO_TILE   0x1F1D26u
static const u32 k_logo_rgb[3] = { 0x5B4A8Eu, 0x8468D0u, 0xB79CFFu };  // bottom to top
static const f32 k_logo_dy[3] = { 36.f, 0.f, -36.f };

// Straight-alpha "over": icon bits are not premultiplied.
static void logo_over(f32 *c, f32 *a, u32 rgb, f32 m)
{
    f32 na = m + *a * (1.f - m);
    if (na <= 0.f) return;
    for (int k = 0; k < 3; k++) c[k] = ((f32)((rgb >> (16 - 8 * k)) & 255) * m + c[k] * *a * (1.f - m)) / na;
    *a = na;
}

// Share of pixel (x, y) covered by a plate centered at (cx, cy), 4x4 samples.
static f32 logo_plate_cov(int x, int y, f32 cx, f32 cy, f32 h, f32 r)
{
    int hit = 0;
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            f32 px = (f32)x + ((f32)i + 0.5f) * 0.25f - cx, py = ((f32)y + ((f32)j + 0.5f) * 0.25f - cy) / APP_LOGO_SQUASH;
            hit += sd_box((px + py) * 0.70710678f, (py - px) * 0.70710678f, h, h, r) <= 0.f;
        }
    }
    return (f32)hit / 16.f;
}

static void app_icon_render(u32 *out, int S, bool tile)
{
    f32 s = (f32)S;
    f32 inset = s * 0.0625f, half = s * 0.5f - inset, rad = s * 0.225f;
    f32 u = (tile ? half * 2.f * APP_LOGO_FILL : s) / 152.f;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            f32 c[3] = { 0, 0, 0 }, a = 0.f;
            if (tile) {
                f32 dbg = sd_box(x + 0.5f - s * 0.5f, y + 0.5f - s * 0.5f, half, half, rad);
                f32 ring = CLAMP(0.5f - (fabsf(dbg + 0.5f + s * 0.004f) - 0.5f), 0.f, 1.f) * 0.10f;
                for (int k = 0; k < 3; k++) c[k] = (f32)((APP_LOGO_TILE >> (16 - 8 * k)) & 255) * (1.f - ring) + 255.f * ring;
                a = CLAMP(0.5f - dbg, 0.f, 1.f);
            }
            for (int i = 0; i < 3; i++) {
                f32 m = logo_plate_cov(x, y, s * 0.5f, s * 0.5f + k_logo_dy[i] * u, 56.f * u, 9.f * u);
                if (m > 0.f) logo_over(c, &a, k_logo_rgb[i], m);
            }
            u32 r = (u32)(c[0] + 0.5f), g = (u32)(c[1] + 0.5f), b = (u32)(c[2] + 0.5f);
            out[y * S + x] = ((u32)(a * 255.f + 0.5f) << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

static HICON app_icon_create(int S, bool tile)
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
    app_icon_render((u32 *)bits, S, tile);
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
        app_icon_render(px, S, S >= 32);  // small sizes go without the tile, like the tray
        for (int y = S - 1; y >= 0; y--) buf_put(&b, px + y * S, (size_t)S * 4);
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

static NOTIFYICONDATAW g_nid;
static UINT g_wm_taskbar_created;
static HICON g_icon_small, g_icon_big;

// Gives a window our icons outright and lets Explorer ask for them: it runs below our (elevated) integrity
// level, and when the question does not get through the taskbar shows the exe's icon, plate and all.
static void window_icons(HWND h)
{
    uipi_allow(h, WM_GETICON);
    SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)g_icon_big);
    SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)g_icon_small);
}

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

enum { TM_OPEN = 1, TM_SETTINGS, TM_EXIT };

static void tray_pick(int id)
{
    switch (id) {
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

static void tray_menu(void)
{
    PmItem items[] = {
        { TM_OPEN, TR("Открыть MixLauncher", "Open MixLauncher"), 0xE721 },
        { TM_SETTINGS, TR("Настройки", "Settings"), 0xE713 },
        { 0 },
        { TM_EXIT, TR("Выход", "Exit"), 0xE7E8 },
    };
    POINT pt;
    GetCursorPos(&pt);
    pm_open(items, countof(items), pt, tray_pick);
}
