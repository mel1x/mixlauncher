// The look shared by every window: the theme colors (TH), the launcher styles (LS) and the motion settings.

typedef struct Theme {
    bool dark;
    u32 bg, bg_solid;
    u32 text, dim, faint;
    u32 sel, sep, accent, text_sel;
    u32 key_bg, key_border, danger, tile;
} Theme;

static Theme TH;

static bool system_dark(void)
{
    DWORD light = 0;
    if (reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", &light)) return light == 0;
    return true;
}

// Reads the theme setting and the system accent again.
static void theme_update(void)
{
    bool dark = g_cfg.theme == 1 ? true : g_cfg.theme == 2 ? false : system_dark();
    Theme *t = &TH;
    t->dark = dark;

    u32 acc = RGBA(0, 120, 212, 255);
    BYTE pal[32];
    DWORD sz = sizeof pal;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette",
                     RRF_RT_REG_BINARY, NULL, pal, &sz) == ERROR_SUCCESS && sz >= 32) {
        int i = dark ? 1 : 4;  // the palette's light and dark shades
        acc = RGBA(pal[i * 4], pal[i * 4 + 1], pal[i * 4 + 2], 255);
    } else {
        DWORD c;
        if (reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", &c)) acc = RGBA(c & 255, (c >> 8) & 255, (c >> 16) & 255, 255);
    }
    t->accent = acc;
    if (dark) {
        t->bg = g_cfg.backdrop == BACKDROP_BLUR ? RGBA(24, 24, 27, 150) : RGBA(28, 28, 30, 90);
        t->bg_solid = RGBA(32, 32, 34, 250);
        t->text = RGBA(255, 255, 255, 236);
        t->dim = RGBA(255, 255, 255, 150);
        t->faint = RGBA(255, 255, 255, 92);
        t->sel = RGBA(255, 255, 255, 20);
        t->sep = RGBA(255, 255, 255, 22);
        t->key_bg = RGBA(255, 255, 255, 24);
        t->key_border = RGBA(255, 255, 255, 26);
        t->danger = RGBA(255, 107, 107, 255);
        t->tile = RGBA(255, 255, 255, 30);
    } else {
        t->bg = g_cfg.backdrop == BACKDROP_BLUR ? RGBA(250, 250, 252, 150) : RGBA(252, 252, 253, 110);
        t->bg_solid = RGBA(249, 249, 251, 250);
        t->text = RGBA(0, 0, 0, 228);
        t->dim = RGBA(0, 0, 0, 140);
        t->faint = RGBA(0, 0, 0, 90);
        t->sel = RGBA(0, 0, 0, 13);
        t->sep = RGBA(0, 0, 0, 20);
        t->key_bg = RGBA(0, 0, 0, 14);
        t->key_border = RGBA(0, 0, 0, 22);
        t->danger = RGBA(196, 43, 28, 255);
        t->tile = RGBA(0, 0, 0, 22);
    }
    t->text_sel = color_alpha(acc, dark ? 0.45f : 0.30f);
    if (g_hwnd) dwm_set(g_hwnd, DWA_DARK, dark);
}

// ---- launcher styles

enum { SEL_PILL, SEL_FILL, SEL_BAR };

// Sizes in logical px.
typedef struct LStyle {
    f32 row_h, header_h, icon, icon_gap;
    f32 search_h, search_fs, search_icon, name_fs, footer_h;
    f32 inset, sel_r, tile_r, key_r;
    int corner;  // DWC_*
    u8 sel;      // SEL_*: how the selected row is marked
    bool footer_tint, all_notes;
} LStyle;

static const LStyle k_lstyles[STYLE__COUNT] = {
    [STYLE_STANDARD] = { 42, 30, 28, 12, 60, 20, 17, 14, 40, 8, 8, 7, 5, DWC_ROUND, SEL_PILL, false, false },
    [STYLE_RAYCAST] = { 40, 30, 20, 12, 56, 18, 16, 14, 40, 8, 8, 5, 5, DWC_ROUND, SEL_FILL, true, true },
    [STYLE_FLUENT] = { 40, 32, 24, 14, 56, 18, 16, 14, 42, 6, 4, 4, 4, DWC_ROUND, SEL_BAR, false, false },
    [STYLE_COMPACT] = { 32, 26, 20, 10, 48, 16, 15, 13, 36, 6, 6, 5, 4, DWC_ROUND_SMALL, SEL_PILL, false, false },
    [STYLE_START] = { 44, 34, 32, 14, 66, 14, 14, 14, 66, 39, 4, 4, 4, DWC_ROUND, SEL_PILL, false, false },
    [STYLE_WIN10] = { 36, 36, 28, 12, 48, 14, 14, 14, 0, 4, 0, 0, 2, DWC_SQUARE, SEL_PILL, false, false },
};

static const LStyle *LS(void) { return &k_lstyles[CLAMP(g_cfg.style, 0, STYLE__COUNT - 1)]; }

// The launcher's corners and background (system acrylic, or the blur behind it). True when it is see-through.
static bool backdrop_apply(HWND h)
{
    dwm_set(h, DWA_CORNER, (DWORD)LS()->corner);
    PFN_SetWindowCompositionAttribute swca = (PFN_SetWindowCompositionAttribute)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    int mode = g_cfg.backdrop;
    if (mode == BACKDROP_ACRYLIC) {
        if (dwm_backdrop(h, DWB_ACRYLIC)) return true;
        mode = BACKDROP_BLUR;
    }
    dwm_set(h, DWA_BACKDROP, DWB_NONE);
    MARGINS zero = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(h, &zero);
    ML_ACCENT_POLICY ap = { 0, 0, 0, 0 };
    if (mode == BACKDROP_BLUR) {
        ap.accent_state = 4;  // ACCENT_ENABLE_ACRYLICBLURBEHIND
        ap.accent_flags = 2;
        ap.gradient_color = TH.dark ? 0x30201e1eu : 0x30f8f6f6u;
    }
    ML_WINCOMPATTR_DATA d = { 19 /*WCA_ACCENT_POLICY*/, &ap, sizeof ap };
    bool ok = swca && swca(h, &d);
    return mode == BACKDROP_BLUR && ok;
}

// ---- motion

static int g_sys_anim = -1;  // Windows "Animation effects", -1 until read (again after WM_SETTINGCHANGE)

static bool anims_enabled(void)
{
    if (g_cfg.reduced_motion) return false;
    if (g_sys_anim < 0) {
        BOOL sys = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &sys, 0);
        g_sys_anim = sys != 0;
    }
    return g_cfg.animations && g_sys_anim;
}

// A duration from the settings at the overall speed, in seconds.
static f32 anim_sec(int ms) { return g_cfg.reduced_motion ? 0.f : (f32)ms / 1000.f * 100.f / (f32)MAX(g_cfg.anim_speed, 1); }

// The spring rate that settles in about ms.
static f32 anim_omega(int ms)
{
    f32 t = anim_sec(ms);
    return t > 0.001f ? 4.7f / t : 0.f;
}
