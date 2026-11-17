// The app icon (drawn, not loaded), the tray icon and its menu.

use crate::*;
use core::mem::ManuallyDrop;
use windows::Win32::System::Com::StructuredStorage::PROPVARIANT;
use windows::Win32::UI::Shell::PropertiesSystem::{IPropertyStore, SHGetPropertyStoreForWindow};

// Logo: three isometric plates (rounded squares turned 45 degrees and squashed flat), the top one brightest.
// In units of the logo box, 152 per side: plate centers 36 apart, half side 56, corner 9. On the dark tile
// the box takes 152/256 of the tile; without it (tray, small icons) it fills the whole icon.
const APP_LOGO_SQUASH: f32 = 0.53;
const APP_LOGO_FILL: f32 = 152.0 / 256.0;
const APP_LOGO_TILE: u32 = 0x1F1D26;
const k_logo_rgb: [u32; 3] = [0x5B4A8E, 0x8468D0, 0xB79CFF]; // bottom to top
const k_logo_dy: [f32; 3] = [36.0, 0.0, -36.0];

// Straight-alpha "over": icon bits are not premultiplied.
fn logo_over(c: &mut [f32; 3], a: &mut f32, rgb: u32, m: f32) {
    let na = m + *a * (1.0 - m);
    if na <= 0.0 {
        return;
    }
    for k in 0..3 {
        c[k] = (((rgb >> (16 - 8 * k)) & 255) as f32 * m + c[k] * *a * (1.0 - m)) / na;
    }
    *a = na;
}

// Share of pixel (x, y) covered by a plate centered at (cx, cy), 4x4 samples.
fn logo_plate_cov(x: i32, y: i32, cx: f32, cy: f32, h: f32, r: f32) -> f32 {
    let mut hit = 0;
    for j in 0..4 {
        for i in 0..4 {
            let px = x as f32 + (i as f32 + 0.5) * 0.25 - cx;
            let py = (y as f32 + (j as f32 + 0.5) * 0.25 - cy) / APP_LOGO_SQUASH;
            if sd_box((px + py) * 0.70710678, (py - px) * 0.70710678, h, h, r) <= 0.0 {
                hit += 1;
            }
        }
    }
    hit as f32 / 16.0
}

pub unsafe fn app_icon_render(out: *mut u32, S_: i32, tile: bool) {
    let s = S_ as f32;
    let inset = s * 0.0625;
    let half = s * 0.5 - inset;
    let rad = s * 0.225;
    let u = (if tile { half * 2.0 * APP_LOGO_FILL } else { s }) / 152.0;
    for y in 0..S_ {
        for x in 0..S_ {
            let mut c = [0.0f32; 3];
            let mut a = 0.0f32;
            if tile {
                let dbg = sd_box(x as f32 + 0.5 - s * 0.5, y as f32 + 0.5 - s * 0.5, half, half, rad);
                let ring = clamp(0.5 - ((dbg + 0.5 + s * 0.004).abs() - 0.5), 0.0, 1.0) * 0.10;
                for k in 0..3 {
                    c[k] = ((APP_LOGO_TILE >> (16 - 8 * k)) & 255) as f32 * (1.0 - ring) + 255.0 * ring;
                }
                a = clamp(0.5 - dbg, 0.0, 1.0);
            }
            for i in 0..3 {
                let m = logo_plate_cov(x, y, s * 0.5, s * 0.5 + k_logo_dy[i] * u, 56.0 * u, 9.0 * u);
                if m > 0.0 {
                    logo_over(&mut c, &mut a, k_logo_rgb[i], m);
                }
            }
            let r = (c[0] + 0.5) as u32;
            let g = (c[1] + 0.5) as u32;
            let b = (c[2] + 0.5) as u32;
            *out.add((y * S_ + x) as usize) = (((a * 255.0 + 0.5) as u32) << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

pub unsafe fn app_icon_create(S_: i32, tile: bool) -> HICON {
    let bi = dib32(S_, S_);
    let mut bits: *mut c_void = null_mut();
    let color = CreateDIBSection(null_mut(), &bi, DIB_RGB_COLORS, &mut bits, null_mut(), 0);
    if color.is_null() {
        return null_mut();
    }
    app_icon_render(bits as *mut u32, S_, tile);
    let mask = CreateBitmap(S_, S_, 1, 1, null());
    let mut ii: ICONINFO = zeroed();
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    let icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    icon
}

// One icon image as an .ico entry: a bottom-up DIB with an empty mask, or (big sizes) a PNG, which takes a
// fraction of the room and which Windows reads in icons since Vista.
unsafe fn ico_image(S_: i32, out: &mut Buf) -> bool {
    let mut px = vec![0u32; (S_ * S_) as usize];
    app_icon_render(px.as_mut_ptr(), S_, S_ >= 32); // small sizes go without the tile, like the tray
    let mut ok = true;
    if S_ >= 64 {
        ok = wic_png(px.as_ptr(), S_, S_, out);
    } else {
        let mut bh: BITMAPINFOHEADER = zeroed();
        bh.biSize = 40;
        bh.biWidth = S_;
        bh.biHeight = S_ * 2; // the color rows and the mask rows
        bh.biPlanes = 1;
        bh.biBitCount = 32;
        out.put(core::slice::from_raw_parts(&bh as *const _ as *const u8, 40));
        for y in (0..S_).rev() {
            out.put(core::slice::from_raw_parts(px.as_ptr().add((y * S_) as usize) as *const u8, S_ as usize * 4));
        }
        let zero_ = [0u8; 64];
        for _ in 0..S_ {
            out.put(&zero_[..(S_ as usize + 31) / 32 * 4]);
        }
    }
    ok
}

// --write-icon: res\mixlauncher.ico, every size the shell asks for.
pub unsafe fn app_icon_write_ico(path: *const WCHAR) -> bool {
    let sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128];
    let mut img: Vec<Buf> = (0..sizes.len()).map(|_| Buf::new()).collect();
    let mut ok = true;
    let mut i = 0;
    while i < sizes.len() && ok {
        ok = ico_image(sizes[i], &mut img[i]);
        i += 1;
    }
    let mut b = Buf::new();
    let hdr: [u16; 3] = [0, 1, sizes.len() as u16];
    b.put(core::slice::from_raw_parts(hdr.as_ptr() as *const u8, 6));
    let mut offset = 6 + 16 * sizes.len() as u32;
    for i in 0..sizes.len() {
        let mut e = [0u8; 16];
        let size = img[i].len() as u32;
        e[0] = if sizes[i] >= 256 { 0 } else { sizes[i] as u8 };
        e[1] = e[0];
        e[4] = 1; // planes
        e[6] = 32; // bits per pixel
        e[8..12].copy_from_slice(&size.to_le_bytes());
        e[12..16].copy_from_slice(&offset.to_le_bytes());
        b.put(&e);
        offset += size;
    }
    for i in 0..sizes.len() {
        let d = core::mem::take(&mut img[i].data);
        b.put(&d);
    }
    if !ok {
        return false;
    }
    buf_save(&mut b, path)
}

pub static mut g_nid: NOTIFYICONDATAW = unsafe { zeroed() };
pub static mut g_wm_taskbar_created: u32 = 0;
pub static mut g_icon_small: HICON = null_mut();
pub static mut g_icon_big: HICON = null_mut();

static PKEY_AppUserModel_ID: windows::Win32::Foundation::PROPERTYKEY =
    windows::Win32::Foundation::PROPERTYKEY { fmtid: windows::core::GUID::from_u128(0x9f4c2855_9f79_4b39_a8d0_e1d42de1d5f3), pid: 5 };

// Gives a window our icons, without the plate, on the taskbar too. The taskbar ties a window to the Start menu
// shortcut of its exe and shows that icon instead (the exe's, plate and all); a window AppUserModelID of its
// own unties it. Explorer runs below our (elevated) level: WM_GETICON is let through UIPI.
pub unsafe fn window_icons(h: HWND) {
    if let Ok(ps) = SHGetPropertyStoreForWindow::<IPropertyStore>(whwnd(h)) {
        let mut v: ManuallyDrop<PROPVARIANT> = ManuallyDrop::new(PROPVARIANT::default());
        (*v.Anonymous.Anonymous).vt = windows::Win32::System::Variant::VT_LPWSTR;
        (*v.Anonymous.Anonymous).Anonymous.pwszVal = windows::core::PWSTR(w!("mel1x.MixLauncher.Window") as *mut u16);
        let _ = ps.SetValue(&PKEY_AppUserModel_ID, &*v);
        let _ = ps.Commit();
    }
    uipi_allow(h, WM_GETICON);
    SendMessageW(h, WM_SETICON, ICON_BIG as WPARAM, g_icon_big as LPARAM);
    SendMessageW(h, WM_SETICON, ICON_SMALL as WPARAM, g_icon_small as LPARAM);
}

pub unsafe fn tray_add() {
    g_nid = zeroed();
    g_nid.cbSize = size_of::<NOTIFYICONDATAW>() as u32;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = g_icon_small;
    wcopy(g_nid.szTip.as_mut_ptr(), 128, w!("MixLauncher"));
    Shell_NotifyIconW(NIM_ADD, &raw const g_nid);
}

pub unsafe fn tray_remove() {
    Shell_NotifyIconW(NIM_DELETE, &raw const g_nid);
}

const TM_OPEN: i32 = 1;
const TM_SETTINGS: i32 = 2;
const TM_EXIT: i32 = 3;

unsafe fn tray_pick(id: i32) {
    match id {
        TM_OPEN => ui_show(),
        TM_SETTINGS => settings_open(),
        TM_EXIT => {
            PostMessageW(g_hwnd, WM_APP_EXIT, 0, 0);
        }
        _ => {}
    }
}

pub unsafe fn tray_menu() {
    let items = [
        PmItem { id: TM_OPEN, label: tr!("Открыть MixLauncher", "Open MixLauncher"), glyph: 0xE721 },
        PmItem { id: TM_SETTINGS, label: tr!("Настройки", "Settings"), glyph: 0xE713 },
        PmItem::SEP,
        PmItem { id: TM_EXIT, label: tr!("Выход", "Exit"), glyph: 0xE7E8 },
    ];
    let mut pt = POINT { x: 0, y: 0 };
    GetCursorPos(&mut pt);
    pm_open(&items, pt, tray_pick);
}
