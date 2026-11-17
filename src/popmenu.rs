// Windows 11 style popup menu (tray, media widget): acrylic, Fluent icons, keyboard navigation.

use crate::*;

const PM_MAX: usize = 16;
const PM_ITEM_H: f32 = 34.0;
const PM_SEP_H: f32 = 9.0;
const PM_PADV: f32 = 4.0;
const PM_SCALE: f32 = 0.87; // a bit more compact than the mix-widgets menu

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PmItem {
    pub id: i32, // 0 = separator
    pub label: *const WCHAR,
    pub glyph: u32,
}

impl PmItem {
    pub const SEP: PmItem = PmItem { id: 0, label: core::ptr::null(), glyph: 0 };
}

#[repr(C)]
pub struct PopMenu {
    pub hwnd: HWND,
    pub target: RTarget,
    pub items: [PmItem; PM_MAX],
    pub n: i32,
    pub hover: i32,
    pub s: f32,
    pub w: f32,
    pub h: f32, // w, h in DIPs
    pub W: i32,
    pub H: i32,
    pub dirty: bool,
    pub animating: bool,
    pub backdrop: bool,
    pub t0: f64,
    pub pick: Option<unsafe fn(i32)>,
}

pub static mut PM: PopMenu = unsafe { zeroed() };

unsafe fn pm_item_y(i: i32) -> f32 {
    let mut y = PM_PADV;
    for k in 0..i as usize {
        y += if PM.items[k].id != 0 { PM_ITEM_H } else { PM_SEP_H };
    }
    y
}

unsafe fn pm_item_at(mx: i32, my: i32) -> i32 {
    let x = mx as f32 / PM.s;
    let y = my as f32 / PM.s;
    if x < 0.0 || x > PM.w {
        return -1;
    }
    let mut yy = PM_PADV;
    for i in 0..PM.n as usize {
        let h = if PM.items[i].id != 0 { PM_ITEM_H } else { PM_SEP_H };
        if y >= yy && y < yy + h {
            return if PM.items[i].id != 0 { i as i32 } else { -1 };
        }
        yy += h;
    }
    -1
}

unsafe fn pm_step(from: i32, dir: i32) -> i32 {
    let mut i = if from >= 0 {
        from
    } else if dir > 0 {
        -1
    } else {
        PM.n
    };
    for _ in 0..PM.n {
        i = (i + dir + PM.n) % PM.n;
        if PM.items[i as usize].id != 0 {
            return i;
        }
    }
    -1
}

pub unsafe fn pm_close() {
    let h = PM.hwnd;
    PM.hwnd = null_mut(); // DestroyWindow deactivates the menu, and WM_ACTIVATE comes back here
    if !h.is_null() {
        DestroyWindow(h);
    }
}

unsafe fn pm_activate(i: i32) {
    if i < 0 || i >= PM.n || PM.items[i as usize].id == 0 {
        return;
    }
    let id = PM.items[i as usize].id;
    let pick = PM.pick;
    pm_close();
    if let Some(p) = pick {
        p(id);
    }
}

pub unsafe fn pm_needs_render() -> bool {
    !PM.hwnd.is_null() && (PM.dirty || PM.animating) && PM.target.ok
}

static mut pm_t: f32 = 0.0; // the frame's opening progress, eased

unsafe fn pm_draw() {
    let e = pm_t;
    let s = PM.s;
    let th = &*(&raw const TH);
    let text = if th.dark { RGBA(255, 255, 255, 255) } else { RGBA(0, 0, 0, 228) };
    let hover = if th.dark { RGBA(255, 255, 255, 21) } else { RGBA(0, 0, 0, 15) };
    let divider = if th.dark { RGBA(255, 255, 255, 31) } else { RGBA(0, 0, 0, 31) };
    if !PM.backdrop {
        r_rect(0.0, 0.0, PM.W as f32, PM.H as f32, th.bg_solid, 0.0);
    }
    R.opacity = e;
    let oy = -(1.0 - e) * 10.0 * s;
    let fs = 14.0 * s;
    for i in 0..PM.n {
        let it = PM.items[i as usize];
        let y = (pm_item_y(i) * s + oy + 0.5).floor();
        if it.id == 0 {
            r_rect(0.0, (y + PM_SEP_H * 0.5 * s).floor(), PM.W as f32, 1.0, divider, 0.0);
            continue;
        }
        let rx = (4.0 * s + 0.5).floor();
        let ry = y + (2.0 * s + 0.5).floor();
        let rw = PM.W as f32 - 2.0 * rx;
        let rh = ((PM_ITEM_H - 4.0) * s + 0.5).floor();
        let cy = ry + rh * 0.5;
        if i == PM.hover {
            r_rect(rx, ry, rw, rh, hover, (4.0 * s + 0.5).floor());
        }
        if it.glyph != 0 {
            text_draw_icon(it.glyph, 16.0 * s, rx + 18.0 * s, cy, text);
        }
        let lx = rx + (40.0 * s + 0.5).floor();
        text_draw_fit(FONT_TEXT, fs, lx, text_baseline(FONT_TEXT, fs, cy), PM.W as f32 - lx - 12.0 * s, it.label, -1, text, false);
    }
    R.opacity = 1.0;
}

pub unsafe fn pm_render() {
    if PM.hwnd.is_null() || !PM.target.ok || !R.ok {
        return;
    }
    r_use(&raw mut PM.target);
    r_wait_frame();
    let dur = if anims_enabled() { 0.22 } else { 0.0 };
    let t = if dur > 0.0 { clamp((time_now() - PM.t0) as f32 / dur, 0.0, 1.0) } else { 1.0 };
    PM.animating = t < 1.0;
    pm_t = ease_out_cubic(t);
    R.scale = 1.0;
    r_draw(pm_draw);
    r_end_and_present();
    r_use(&raw mut R.main);
    PM.dirty = false;
}

unsafe extern "system" fn pm_wndproc(h: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    match msg {
        WM_NCCALCSIZE => {
            if wp != 0 {
                return 0;
            }
        }
        WM_NCHITTEST => return HTCLIENT as LRESULT,
        // keeps acrylic, see the launcher's WM_NCACTIVATE
        WM_NCACTIVATE => return DefWindowProcW(h, msg, TRUE as WPARAM, -1),
        WM_ERASEBKGND => return 1,
        WM_PAINT => {
            let mut ps: PAINTSTRUCT = zeroed();
            BeginPaint(h, &mut ps);
            EndPaint(h, &ps);
            PM.dirty = true;
            return 0;
        }
        WM_MOUSEMOVE => {
            let i = pm_item_at(get_x_lparam(lp), get_y_lparam(lp));
            if i != PM.hover {
                PM.hover = i;
                PM.dirty = true;
            }
            track_mouse_leave(h);
            return 0;
        }
        WM_MOUSELEAVE => {
            if PM.hover >= 0 {
                PM.hover = -1;
                PM.dirty = true;
            }
            return 0;
        }
        WM_LBUTTONUP | WM_RBUTTONUP => {
            pm_activate(pm_item_at(get_x_lparam(lp), get_y_lparam(lp)));
            return 0;
        }
        WM_KEYDOWN | WM_SYSKEYDOWN => {
            match wp as u16 {
                VK_ESCAPE | VK_MENU => pm_close(),
                VK_UP | VK_DOWN => {
                    PM.hover = pm_step(PM.hover, if wp as u16 == VK_DOWN { 1 } else { -1 });
                    PM.dirty = true;
                }
                VK_RETURN | VK_SPACE => pm_activate(PM.hover),
                _ => {}
            }
            return 0;
        }
        WM_ACTIVATE => {
            if loword(wp) == WA_INACTIVE {
                pm_close();
            }
            return 0;
        }
        WM_DESTROY => {
            r_target_release(&raw mut PM.target);
            if PM.hwnd == h {
                PM.hwnd = null_mut();
            }
            U.menu_open = false;
            return 0;
        }
        _ => {}
    }
    DefWindowProcW(h, msg, wp, lp)
}

// Opens the menu at a screen point (the cursor), below-right of it, flipping to fit the work area.
pub unsafe fn pm_open(items: &[PmItem], at: POINT, pick: unsafe fn(i32)) {
    pm_close();
    window_class(w!("MixLauncherMenu"), pm_wndproc, 0, null_mut(), null_mut());
    theme_update();
    PM.n = min(items.len(), PM_MAX) as i32;
    for i in 0..PM.n as usize {
        PM.items[i] = items[i];
    }
    PM.hover = -1;
    PM.pick = Some(pick);

    let mon = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST);
    let wa = monitor_work(mon);
    PM.s = monitor_dpi(mon) as f32 / 96.0 * PM_SCALE;
    let mut tw: f32 = 0.0;
    for i in 0..PM.n as usize {
        if PM.items[i].id != 0 {
            tw = max(tw, text_width(FONT_TEXT, 14.0 * PM.s, PM.items[i].label, -1) / PM.s);
        }
    }
    PM.w = clamp(tw + 52.0 + 24.0, 170.0, 360.0);
    PM.h = 2.0 * PM_PADV;
    for i in 0..PM.n as usize {
        PM.h += if PM.items[i].id != 0 { PM_ITEM_H } else { PM_SEP_H };
    }
    PM.W = (PM.w * PM.s + 0.5) as i32;
    PM.H = (PM.h * PM.s + 0.5) as i32;
    let mut x = at.x;
    let mut y = at.y;
    if x + PM.W > wa.right {
        x -= PM.W;
    }
    if y + PM.H > wa.bottom {
        y = max(min(y, wa.bottom) - PM.H, wa.top); // above the taskbar, not over it
    }
    x = max(x, wa.left);

    PM.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
        w!("MixLauncherMenu"),
        w!("MixLauncher"),
        WS_POPUP,
        x,
        y,
        PM.W,
        PM.H,
        null_mut(),
        null_mut(),
        GetModuleHandleW(null()),
        null(),
    );
    if PM.hwnd.is_null() {
        return;
    }
    dwm_set(PM.hwnd, DWA_DARK, TH.dark as u32);
    dwm_set(PM.hwnd, DWA_CORNER, DWC_ROUND);
    dwm_set(PM.hwnd, DWA_BORDER_COLOR, DW_COLOR_NONE);
    PM.backdrop = dwm_backdrop(PM.hwnd, DWB_ACRYLIC);
    SetWindowPos(PM.hwnd, null_mut(), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if !r_target_init(&raw mut PM.target, PM.hwnd, PM.W, PM.H) {
        DestroyWindow(PM.hwnd);
        return;
    }
    U.menu_open = true;
    PM.t0 = time_now();
    PM.dirty = true;
    pm_render();
    ShowWindow(PM.hwnd, SW_SHOW);
    SetForegroundWindow(PM.hwnd);
}
