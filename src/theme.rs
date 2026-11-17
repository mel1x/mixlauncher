// The look shared by every window: the theme colors (TH), the launcher styles (LS) and the motion settings.

use crate::*;

#[repr(C)]
pub struct Theme {
    pub dark: bool,
    pub bg: u32,
    pub bg_solid: u32,
    pub text: u32,
    pub dim: u32,
    pub faint: u32,
    pub sel: u32,
    pub sep: u32,
    pub accent: u32,
    pub text_sel: u32,
    pub key_bg: u32,
    pub key_border: u32,
    pub danger: u32,
    pub tile: u32,
    pub veil: f32,          // how much of their opacity the Start styles' dense fills keep over a see-through backdrop
    pub transparency: bool, // Windows' "Transparency effects": off, the backdrop is solid as the system's own are
}

pub static mut TH: Theme = unsafe { zeroed() };

// The backdrop in effect: the setting, unless Windows has transparency effects off.
pub unsafe fn backdrop_kind() -> i32 {
    if TH.transparency { g_cfg.background } else { BACKDROP_SOLID }
}

unsafe fn system_dark() -> bool {
    let mut light = 0u32;
    if reg_dword(HKEY_CURRENT_USER, w!("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), w!("AppsUseLightTheme"), &mut light) {
        return light == 0;
    }
    true
}

// Reads the theme setting and the system accent again.
pub unsafe fn theme_update() {
    let dark = if g_cfg.theme == 1 {
        true
    } else if g_cfg.theme == 2 {
        false
    } else {
        system_dark()
    };
    let t = &mut *(&raw mut TH);
    t.dark = dark;
    let mut tr_ = 1u32;
    reg_dword(HKEY_CURRENT_USER, w!("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), w!("EnableTransparency"), &mut tr_);
    t.transparency = tr_ != 0;
    let kind = backdrop_kind();

    let mut acc = RGBA(0, 120, 212, 255);
    let mut pal = [0u8; 32];
    let mut sz = 32u32;
    if RegGetValueW(
        HKEY_CURRENT_USER,
        w!("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent"),
        w!("AccentPalette"),
        RRF_RT_REG_BINARY,
        null_mut(),
        pal.as_mut_ptr() as *mut c_void,
        &mut sz,
    ) == ERROR_SUCCESS
        && sz >= 32
    {
        let i = if dark { 1 } else { 4 }; // the palette's light and dark shades
        acc = RGBA(pal[i * 4] as u32, pal[i * 4 + 1] as u32, pal[i * 4 + 2] as u32, 255);
    } else {
        let mut c = 0u32;
        if reg_dword(HKEY_CURRENT_USER, w!("Software\\Microsoft\\Windows\\DWM"), w!("AccentColor"), &mut c) {
            acc = RGBA(c & 255, (c >> 8) & 255, (c >> 16) & 255, 255);
        }
    }
    t.accent = acc;
    if dark {
        // over the backdrop: the glass's tint (acrylic tints itself in the shader)
        t.bg = if kind == BACKDROP_BLUR { RGBA(24, 24, 27, 140) } else { 0 };
        t.bg_solid = RGBA(32, 32, 34, 250);
        t.text = RGBA(255, 255, 255, 236);
        t.dim = RGBA(255, 255, 255, 150);
        t.faint = RGBA(255, 255, 255, 92);
        t.sel = RGBA(255, 255, 255, 20);
        t.sep = RGBA(255, 255, 255, 22);
        t.key_bg = RGBA(255, 255, 255, 24);
        t.key_border = RGBA(255, 255, 255, 26);
        t.danger = RGBA(255, 107, 107, 255);
        t.tile = RGBA(255, 255, 255, 30);
    } else {
        t.bg = if kind == BACKDROP_BLUR { RGBA(250, 250, 252, 140) } else { 0 };
        t.bg_solid = RGBA(249, 249, 251, 250);
        t.text = RGBA(0, 0, 0, 228);
        t.dim = RGBA(0, 0, 0, 140);
        t.faint = RGBA(0, 0, 0, 90);
        t.sel = RGBA(0, 0, 0, 13);
        t.sep = RGBA(0, 0, 0, 20);
        t.key_bg = RGBA(0, 0, 0, 14);
        t.key_border = RGBA(0, 0, 0, 22);
        t.danger = RGBA(196, 43, 28, 255);
        t.tile = RGBA(0, 0, 0, 22);
    }
    t.text_sel = color_alpha(acc, if dark { 0.45 } else { 0.30 });
    t.veil = if kind == BACKDROP_BLUR {
        0.72
    } else if kind == BACKDROP_SOLID {
        1.0
    } else {
        0.5
    };
    if !g_hwnd.is_null() {
        dwm_set(g_hwnd, DWA_DARK, dark as u32);
    }
}

// ---- launcher styles

pub const SEL_PILL: u8 = 0;
pub const SEL_FILL: u8 = 1;
pub const SEL_BAR: u8 = 2;

// Sizes in logical px.
#[derive(Clone, Copy)]
pub struct LStyle {
    pub row_h: f32,
    pub header_h: f32,
    pub icon: f32,
    pub icon_gap: f32,
    pub search_h: f32,
    pub search_fs: f32,
    pub search_icon: f32,
    pub name_fs: f32,
    pub footer_h: f32,
    pub inset: f32,
    pub sel_r: f32,
    pub tile_r: f32,
    pub key_r: f32,
    pub corner: u32, // DWC_*
    pub sel: u8,     // SEL_*: how the selected row is marked
    pub footer_tint: bool,
    pub all_notes: bool,
}

// The capsule style: the query field and the results are two floating panels. The window around them is see-through,
// with room for their shadows.
pub const CAP_MARGIN: f32 = 16.0;
pub const CAP_GAP: f32 = 10.0;

pub unsafe fn cap_on() -> bool {
    g_cfg.style == STYLE_CAPSULE
}

const fn ls(
    row_h: f32,
    header_h: f32,
    icon: f32,
    icon_gap: f32,
    search_h: f32,
    search_fs: f32,
    search_icon: f32,
    name_fs: f32,
    footer_h: f32,
    inset: f32,
    sel_r: f32,
    tile_r: f32,
    key_r: f32,
    corner: u32,
    sel: u8,
    footer_tint: bool,
    all_notes: bool,
) -> LStyle {
    LStyle { row_h, header_h, icon, icon_gap, search_h, search_fs, search_icon, name_fs, footer_h, inset, sel_r, tile_r, key_r, corner, sel, footer_tint, all_notes }
}

pub static k_lstyles: [LStyle; STYLE__COUNT as usize] = [
    ls(42., 30., 28., 12., 60., 20., 17., 14., 40., 8., 8., 7., 5., DWC_ROUND, SEL_PILL, false, false),
    ls(40., 30., 20., 12., 56., 18., 16., 14., 40., 8., 8., 5., 5., DWC_ROUND, SEL_FILL, true, true),
    ls(40., 32., 24., 14., 56., 18., 16., 14., 42., 6., 4., 4., 4., DWC_ROUND, SEL_BAR, false, false),
    ls(32., 26., 20., 10., 48., 16., 15., 13., 36., 6., 6., 5., 4., DWC_ROUND_SMALL, SEL_PILL, false, false),
    ls(44., 30., 22., 12., 60., 20., 17., 14., 46., CAP_MARGIN + 8., 12., 5., 6., DWC_SQUARE, SEL_FILL, false, true),
    ls(44., 34., 32., 14., 66., 14., 14., 14., 66., 39., 4., 4., 4., DWC_ROUND, SEL_PILL, false, false),
    ls(36., 36., 28., 12., 48., 14., 14., 14., 0., 4., 0., 0., 2., DWC_SQUARE, SEL_PILL, false, false),
];

pub unsafe fn LS() -> &'static LStyle {
    &k_lstyles[clamp(g_cfg.style, 0, STYLE__COUNT - 1) as usize]
}

// The launcher's corners. Its backdrop is not DWM's: the renderer draws it from the screen behind (backdrop.rs), so
// it zooms and slides with the window. True when it is see-through.
pub unsafe fn backdrop_apply(h: HWND) -> bool {
    dwm_set(h, DWA_CORNER, LS().corner);
    dwm_set(h, DWA_BORDER_COLOR, if cap_on() { DW_COLOR_NONE } else { DW_COLOR_DEFAULT }); // the capsule draws its own outlines
    backdrop_kind() != BACKDROP_SOLID
}

// ---- motion

pub static mut g_sys_anim: i32 = -1; // Windows "Animation effects", -1 until read (again after WM_SETTINGCHANGE)

pub unsafe fn anims_enabled() -> bool {
    if g_cfg.reduced_motion {
        return false;
    }
    if g_sys_anim < 0 {
        let mut sys: BOOL = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &mut sys as *mut BOOL as *mut c_void, 0);
        g_sys_anim = (sys != 0) as i32;
    }
    g_cfg.animations && g_sys_anim != 0
}

// A duration from the settings at the overall speed, in seconds.
pub unsafe fn anim_sec(ms: i32) -> f32 {
    if g_cfg.reduced_motion { 0.0 } else { ms as f32 / 1000.0 * 100.0 / max(g_cfg.anim_speed, 1) as f32 }
}

// The spring rate that settles in about ms.
pub unsafe fn anim_omega(ms: i32) -> f32 {
    let t = anim_sec(ms);
    if t > 0.001 { 4.7 / t } else { 0.0 }
}
