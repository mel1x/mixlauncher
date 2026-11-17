// The Start button: a layered child window of each taskbar over its own Start button (found with UI Automation)
// that opens the launcher and draws our icon on a plate of the bar's color. Also the search clicks
// (taskbar_search) and the UI Automation helpers the media widget shares.

use crate::*;
use core::sync::atomic::{AtomicI32, Ordering::SeqCst};
use windows::Win32::Graphics::Imaging::*;
use windows::Win32::System::Variant as wvar;
use windows::Win32::UI::Accessibility as uia;
use windows::core::Interface;

const TB_MAX: usize = 4;
const TB_TIMER_ANIM: usize = 1;

// ---- UI Automation

pub unsafe fn uia_create() -> Option<uia::IUIAutomation> {
    windows::Win32::System::Com::CoCreateInstance(&uia::CUIAutomation, None, windows::Win32::System::Com::CLSCTX_INPROC_SERVER).ok()
}

// Matches a string property (UIA_AutomationIdPropertyId, UIA_ClassNamePropertyId).
pub unsafe fn uia_cond(u: &uia::IUIAutomation, prop: uia::UIA_PROPERTY_ID, value: &str) -> Option<uia::IUIAutomationCondition> {
    let v = wvar::VARIANT::from(windows::core::BSTR::from(value));
    u.CreatePropertyCondition(prop, &v).ok()
}

// The first element in a window that matches the condition.
pub unsafe fn uia_find(u: &uia::IUIAutomation, wnd: HWND, cond: Option<&uia::IUIAutomationCondition>) -> Option<uia::IUIAutomationElement> {
    let cond = cond?;
    if wnd.is_null() {
        return None;
    }
    let root = u.ElementFromHandle(whwnd(wnd)).ok()?;
    root.FindFirst(uia::TreeScope_Descendants, cond).ok()
}

// ---- the Start button

#[repr(C)]
pub struct TbButton {
    pub taskbar: HWND,
    pub wnd: HWND,
    pub screen: RECT,
    pub w: i32,
    pub h: i32,
    pub hover: bool,
    pub press: bool,
    pub hover_t: f32,
    pub press_t: f32,
    pub open_t: f32,
    pub last_anim: f64,
    pub bounce_at: f64, // when the launcher opened from here: the icon squeezes and springs back
    pub plate: COLORREF,
    pub pending: COLORREF, // the bar's color under the button, and a new one waiting to be confirmed
    pub plate_ok: bool,
    pub pending_ok: bool,
    pub pending_at: f64,
    pub layer: Layer,
    pub sampled: f64,
    pub img: *mut u32, // the custom icon, img_n square
    pub img_n: i32,
    pub img_gen: u32,
    pub drawn: f64,    // when the button was last drawn
    pub shot: *mut u32, // the real logo as the screen shows it (tb_grab_logo), shot_n square at (shot_x, shot_y) of the button
    pub shot_n: i32,
    pub shot_x: i32,
    pub shot_y: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct Found {
    taskbar: HWND,
    r: RECT,
}

#[repr(C)]
struct Taskbar {
    enabled: bool,
    search: bool, // clicks on the taskbar's search open the launcher (the hook thread swallows them)
    started: bool,
    wake: HANDLE,
    lock: SRWLOCK,
    found: [Found; TB_MAX],
    search_found: [Found; TB_MAX],
    nfound: i32,
    nsearch: i32,
    menu_for: AtomicI32,
    reset: AtomicI32,
    fast_until: f64,
    b: [TbButton; TB_MAX],
    n: i32,
    light: bool,
    open_on: HMONITOR, // the launcher is open on this monitor: its button gets the open-window plate
    icon_gen: u32,
}

static mut TB: Taskbar = unsafe { zeroed() };

#[inline]
unsafe fn tb() -> &'static mut Taskbar {
    &mut *(&raw mut TB)
}

unsafe fn fast_until() -> f64 {
    core::ptr::read_volatile(&raw const TB.fast_until)
}
unsafe fn set_fast_until(v: f64) {
    core::ptr::write_volatile(&raw mut TB.fast_until, v);
}

pub unsafe fn taskbar_static_init() {
    TB.icon_gen = 1;
}

static mut g_tb_hit: [RECT; TB_MAX] = unsafe { zeroed() };
static g_tb_hit_count: AtomicI32 = AtomicI32::new(0);

pub unsafe fn tb_hit(pt: POINT) -> bool {
    let n = g_tb_hit_count.load(SeqCst);
    let hits = &*(&raw const g_tb_hit);
    for i in 0..min(n, TB_MAX as i32) as usize {
        if PtInRect(&hits[i], pt) != 0 {
            return true;
        }
    }
    false
}

// A press on the search of a taskbar (Windows 11: icon, icon and label, box; Windows 10: icon or box) that nothing covers.
// Called from the mouse hook: no UI Automation here, only the rectangles the taskbar thread found.
pub unsafe fn tb_search_hit(pt: POINT) -> bool {
    let t = tb();
    if !t.search {
        return false;
    }
    let mut bar: HWND = null_mut();
    AcquireSRWLockShared(&mut t.lock);
    for i in 0..t.nsearch as usize {
        if PtInRect(&t.search_found[i].r, pt) != 0 {
            bar = t.search_found[i].taskbar;
        }
    }
    ReleaseSRWLockShared(&mut t.lock);
    if bar.is_null() {
        return false;
    }
    let w = WindowFromPoint(pt);
    !w.is_null() && GetAncestor(w, GA_ROOT) == bar && !window_is_ours(w)
}

unsafe fn tb_taskbars(out: &mut [HWND; TB_MAX]) -> usize {
    let mut n = 0;
    let h = FindWindowW(w!("Shell_TrayWnd"), null());
    if !h.is_null() {
        out[n] = h;
        n += 1;
    }
    let mut h: HWND = null_mut();
    while n < TB_MAX {
        h = FindWindowExW(null_mut(), h, w!("Shell_SecondaryTrayWnd"), null());
        if h.is_null() {
            break;
        }
        out[n] = h;
        n += 1;
    }
    n
}

static g_tb_centered: AtomicI32 = AtomicI32::new(1);

unsafe fn tb_read_centered() -> bool {
    let mut v = 1u32;
    reg_dword(HKEY_CURRENT_USER, w!("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"), w!("TaskbarAl"), &mut v);
    v != 0
}

unsafe extern "system" fn tb_winevent(_hook: HWINEVENTHOOK, _ev: u32, h: HWND, obj: i32, child: i32, _tid: u32, _time: u32) {
    // every window event in the system lands here: cheap checks first
    if g_tb_centered.load(SeqCst) == 0 || obj != OBJID_WINDOW || child != CHILDID_SELF as i32 || h.is_null() {
        return;
    }
    if !GetWindow(h, GW_OWNER).is_null() || (GetWindowLongW(h, GWL_EXSTYLE) as u32 & WS_EX_TOOLWINDOW) != 0 {
        return;
    }
    if GetAncestor(h, GA_ROOT) == h {
        set_fast_until(time_now() + 0.8);
        SetEvent(TB.wake);
    }
}

// Watches where the Start buttons (and the search) of every taskbar are: often while windows come and go on a
// centered bar (its buttons move with them), rarely otherwise. Posts WM_APP_TASKBAR when they moved.
unsafe extern "system" fn tb_thread(_param: *mut c_void) -> u32 {
    CoInitializeEx(null(), COINIT_MULTITHREADED as _);
    let Some(u) = uia_create() else {
        log_msg!("taskbar: UI Automation unavailable");
        return 0;
    };
    let cond = uia_cond(&u, uia::UIA_AutomationIdPropertyId, "StartButton");
    // Windows 11 calls every kind of its search SearchButton; Windows 10 has a window of its own
    let k_search: [(bool, &str); 3] = [(false, "SearchButton"), (false, "SearchBoxButton"), (true, "TrayDummySearchControl")];
    let mut sc: Vec<Option<uia::IUIAutomationCondition>> = Vec::new();
    for (cls, val) in k_search {
        if let Some(c) = uia_cond(&u, if cls { uia::UIA_ClassNamePropertyId } else { uia::UIA_AutomationIdPropertyId }, val) {
            sc.push(Some(c));
        }
    }
    let scond = if !sc.is_empty() { u.CreateOrConditionFromNativeArray(&sc).ok() } else { None };
    drop(sc);
    let hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE, null_mut(), Some(tb_winevent), 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    let mut bars: [HWND; TB_MAX] = [null_mut(); TB_MAX];
    let mut els: [Option<uia::IUIAutomationElement>; TB_MAX] = Default::default();
    let mut sels: [Option<uia::IUIAutomationElement>; TB_MAX] = Default::default();
    let mut last: [RECT; TB_MAX] = zeroed();
    let mut last_n: i32 = -1;
    let mut last_post = 0.0;
    let mut retry_at = 0.0;
    let mut sretry_at = 0.0;
    let t = tb();
    loop {
        let now = time_now();
        let fast = now < fast_until();
        let active = t.enabled || t.search;
        MsgWaitForMultipleObjects(
            1,
            &t.wake,
            FALSE,
            if !active {
                INFINITE
            } else if fast {
                15
            } else if g_tb_centered.load(SeqCst) != 0 {
                500
            } else {
                1000
            },
            QS_ALLINPUT,
        );
        pump_messages();
        if t.reset.swap(0, SeqCst) != 0 {
            for i in 0..TB_MAX {
                els[i] = None;
                sels[i] = None;
            }
            bars = [null_mut(); TB_MAX];
            retry_at = 0.0;
            sretry_at = 0.0;
            last_n = -1;
        }
        if !t.enabled && !t.search {
            continue;
        }
        if !fast {
            g_tb_centered.store(tb_read_centered() as i32, SeqCst);
        }

        let mut cur: [HWND; TB_MAX] = [null_mut(); TB_MAX];
        let n = tb_taskbars(&mut cur);
        let mut rs: [RECT; TB_MAX] = zeroed();
        let mut srs: [RECT; TB_MAX] = zeroed();
        let mut hs: [HWND; TB_MAX] = [null_mut(); TB_MAX];
        let mut shs: [HWND; TB_MAX] = [null_mut(); TB_MAX];
        let mut nf = 0usize;
        let mut ns = 0usize;
        for i in 0..n {
            if bars[i] != cur[i] {
                els[i] = None;
                sels[i] = None;
                bars[i] = cur[i];
            }
            if t.search {
                if sels[i].is_none() && scond.is_some() && time_now() >= sretry_at {
                    sels[i] = uia_find(&u, cur[i], scond.as_ref());
                    if sels[i].is_none() {
                        sretry_at = time_now() + 3.0; // search turned off in the taskbar settings
                    }
                }
                let rect = sels[i].as_ref().map(|e| e.CurrentBoundingRectangle());
                match rect {
                    Some(Ok(r)) => {
                        let e = sels[i].as_ref().unwrap();
                        let mut off = e.CurrentIsOffscreen().map(|b| b.as_bool()).unwrap_or(false);
                        let sh = e.CurrentNativeWindowHandle().map(|h| h.0).unwrap_or(null_mut());
                        if !sh.is_null() && IsWindowVisible(sh as HWND) == 0 {
                            off = true;
                        }
                        if !off && r.right > r.left && r.bottom > r.top {
                            shs[ns] = cur[i];
                            srs[ns] = RECT { left: r.left, top: r.top, right: r.right, bottom: r.bottom };
                            ns += 1;
                        }
                    }
                    Some(Err(_)) => sels[i] = None,
                    None => {}
                }
            } else {
                sels[i] = None;
            }
            if !t.enabled {
                els[i] = None;
                continue;
            }
            if els[i].is_none() && cond.is_some() && time_now() >= retry_at {
                els[i] = uia_find(&u, cur[i], cond.as_ref());
                if els[i].is_none() {
                    retry_at = time_now() + 1.0;
                }
            }
            match els[i].as_ref().map(|e| e.CurrentBoundingRectangle()) {
                Some(Ok(r)) => {
                    if r.right > r.left && r.bottom > r.top {
                        hs[nf] = cur[i];
                        rs[nf] = RECT { left: r.left, top: r.top, right: r.right, bottom: r.bottom };
                        nf += 1;
                    }
                }
                Some(Err(_)) => els[i] = None,
                None => {}
            }
        }
        for i in n..TB_MAX {
            els[i] = None;
            sels[i] = None;
            bars[i] = null_mut();
        }
        AcquireSRWLockExclusive(&mut t.lock);
        t.nsearch = ns as i32;
        for i in 0..ns {
            t.search_found[i] = Found { taskbar: shs[i], r: srs[i] };
        }
        ReleaseSRWLockExclusive(&mut t.lock);

        let m = t.menu_for.swap(0, SeqCst);
        if m > 0 {
            let want = t.b[m as usize - 1].taskbar;
            for i in 0..n {
                if bars[i] == want {
                    if let Some(e) = els[i].as_ref() {
                        if let Ok(e3) = e.cast::<uia::IUIAutomationElement3>() {
                            if let Err(err) = e3.ShowContextMenu() {
                                log_msg!("taskbar: ShowContextMenu failed 0x{:08x}", err.code().0 as u32);
                            }
                        }
                    }
                }
            }
        }

        if !t.enabled {
            continue;
        }
        let mut changed = nf as i32 != last_n;
        for i in 0..nf {
            if changed {
                break;
            }
            if EqualRect(&rs[i], &last[i]) == 0 {
                changed = true;
            }
        }
        if changed || time_now() - last_post > 2.0 {
            last_post = time_now();
            AcquireSRWLockExclusive(&mut t.lock);
            t.nfound = nf as i32;
            for i in 0..nf {
                t.found[i] = Found { taskbar: hs[i], r: rs[i] };
            }
            ReleaseSRWLockExclusive(&mut t.lock);
            last[..nf].copy_from_slice(&rs[..nf]);
            last_n = nf as i32;
            PostMessageW(g_hwnd, WM_APP_TASKBAR, 0, 0);
            if changed {
                set_fast_until(max(fast_until(), time_now() + 0.3));
            }
        }
        let _ = hook;
    }
}

unsafe fn taskbar_light() -> bool {
    let mut v = 0u32;
    reg_dword(HKEY_CURRENT_USER, w!("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), w!("SystemUsesLightTheme"), &mut v);
    v != 0
}

fn get_r(c: COLORREF) -> i32 {
    (c & 255) as i32
}
fn get_g(c: COLORREF) -> i32 {
    ((c >> 8) & 255) as i32
}
fn get_b(c: COLORREF) -> i32 {
    ((c >> 16) & 255) as i32
}
fn rgb(r: i32, g: i32, b: i32) -> COLORREF {
    (r as u32) | ((g as u32) << 8) | ((b as u32) << 16)
}

fn tb_color_dist(a: COLORREF, b: COLORREF) -> i32 {
    max((get_r(a) - get_r(b)).abs(), max((get_g(a) - get_g(b)).abs(), (get_b(a) - get_b(b)).abs()))
}

// Where the icon sits in the button: the monitor's scale, the icon's side and its center.
unsafe fn tb_geometry(b: &TbButton) -> (f32, f32, f32, f32) {
    let s = monitor_dpi(MonitorFromWindow(b.taskbar, MONITOR_DEFAULTTONEAREST)) as f32 / 96.0;
    let logo = (24.0 * s + 0.5).floor();
    let w = b.w as f32;
    let h = b.h as f32;
    let odd = if (logo as i32) & 1 != 0 { 0.5 } else { 0.0 };
    let cx = (if w > h * 1.05 { w - h * 0.475 } else { w * 0.5 }).floor() + odd;
    let cy = (h * 0.5).floor() + odd;
    (s, logo, cx, cy)
}

// Without our icon the real logo shows through the button, and nothing of ours can squeeze it when the button is
// pressed. Its picture is taken from the screen while the button is at rest (nothing of ours over it), and that is
// what tb_render scales.
unsafe fn tb_grab_logo(b: &mut TbButton, screen: HDC) {
    if g_cfg.taskbar_replace_icon || b.hover || b.press || b.hover_t > 0.0 || b.press_t > 0.0 || b.open_t > 0.0 || tb_bounce(b) != 0.0 {
        return;
    }
    if time_now() - b.drawn < 0.2 {
        return; // the screen may still show our last frame
    }
    let (s, logo, cx, cy) = tb_geometry(b);
    let n = (logo + 2.0 * (1.0 * s + 0.5).floor()) as i32;
    let x0 = (cx - n as f32 * 0.5 + 0.5).floor() as i32;
    let y0 = (cy - n as f32 * 0.5 + 0.5).floor() as i32;
    if n <= 0 || x0 < 0 || y0 < 0 || x0 + n > b.w || y0 + n > b.h {
        return;
    }
    let mut l: Layer = zeroed();
    let px = if layer_fit(&mut l, n, n) { realloc(b.shot as *mut c_void, (n * n * 4) as usize) as *mut u32 } else { null_mut() };
    if !px.is_null() {
        b.shot = px;
        b.shot_n = 0;
        if BitBlt(l.dc, 0, 0, n, n, screen, b.screen.left + x0, b.screen.top + y0, SRCCOPY) != 0 {
            GdiFlush();
            for i in 0..(n * n) as usize {
                *px.add(i) = *l.bits.add(i) | 0xFF000000;
            }
            b.shot_n = n;
            b.shot_x = x0;
            b.shot_y = y0;
        }
    }
    layer_free(&mut l);
}

// Samples the bar color around the button: every point must show the bar itself and agree, and a new color
// needs a second sample to confirm it. Returns true when the plate color changed.
unsafe fn tb_sample(b: &mut TbButton) -> bool {
    b.sampled = time_now();
    let mut tr_: RECT = zeroed();
    if GetWindowRect(b.taskbar, &mut tr_) == 0 {
        return false;
    }
    let h = b.screen.bottom - b.screen.top;
    let xs = [b.screen.left - 3, b.screen.right + 3];
    let ys = [b.screen.top + h / 10 + 1, b.screen.bottom - h / 10 - 2];
    let mut cs = [0u32; 4];
    let mut n = 0usize;
    let sdc = GetDC(null_mut());
    if sdc.is_null() {
        return false;
    }
    for i in 0..2 {
        for j in 0..2 {
            let p = POINT { x: xs[i], y: ys[j] };
            if PtInRect(&tr_, p) == 0 {
                continue;
            }
            // something (a fullscreen game) covers the bar
            if GetAncestor(WindowFromPoint(p), GA_ROOT) != b.taskbar {
                ReleaseDC(null_mut(), sdc);
                return false;
            }
            let c = GetPixel(sdc, p.x, p.y);
            if c != CLR_INVALID {
                cs[n] = c;
                n += 1;
            }
        }
    }
    tb_grab_logo(b, sdc);
    ReleaseDC(null_mut(), sdc);
    if n == 0 {
        return false;
    }
    let (mut r, mut g, mut bl) = (0, 0, 0);
    for i in 0..n {
        for k in i + 1..n {
            if tb_color_dist(cs[i], cs[k]) > 12 {
                return false;
            }
        }
        r += get_r(cs[i]);
        g += get_g(cs[i]);
        bl += get_b(cs[i]);
    }
    let n = n as i32;
    let c = rgb(r / n, g / n, bl / n);
    if b.plate_ok && tb_color_dist(c, b.plate) <= 2 {
        b.pending_ok = false;
        return false;
    }
    // the first color is taken as is
    let confirmed = !b.plate_ok || (b.pending_ok && tb_color_dist(c, b.pending) <= 4 && b.sampled - b.pending_at >= 0.3);
    if !confirmed {
        if !b.pending_ok || tb_color_dist(c, b.pending) > 4 {
            b.pending_at = b.sampled;
        }
        b.pending = c;
        b.pending_ok = true;
        return false;
    }
    b.pending_ok = false;
    b.plate = c;
    b.plate_ok = true;
    true
}

// The custom icon file fitted into n x n (its largest frame for an .ico), premultiplied BGRA.
unsafe fn tb_load_image(path: *const WCHAR, n: i32) -> *mut u32 {
    if path.is_null() || *path == 0 || n <= 0 {
        return null_mut();
    }
    let Some(f) = wic_factory() else { return null_mut() };
    let dec = match f.CreateDecoderFromFilename(windows::core::PCWSTR(path), None, windows::Win32::Foundation::GENERIC_READ, WICDecodeMetadataCacheOnDemand) {
        Ok(d) => d,
        Err(_) => return null_mut(),
    };
    let frames = dec.GetFrameCount().unwrap_or(0);
    let mut best = 0u32;
    let mut best_w = 0u32;
    let mut i = 0;
    while i < frames && i < 32 {
        if let Ok(fr) = dec.GetFrame(i) {
            let (mut fw, mut fh) = (0u32, 0u32);
            let _ = fr.GetSize(&mut fw, &mut fh);
            if fw > best_w {
                best_w = fw;
                best = i;
            }
        }
        i += 1;
    }
    let Ok(frame) = dec.GetFrame(best) else { return null_mut() };
    let (mut iw, mut ih) = (0u32, 0u32);
    let _ = frame.GetSize(&mut iw, &mut ih);
    if iw == 0 || ih == 0 {
        return null_mut();
    }
    let n_ = n as u32;
    let tw = if iw >= ih { n_ } else { max(1, (n_ as u64 * iw as u64 / ih as u64) as u32) };
    let th = if ih >= iw { n_ } else { max(1, (n_ as u64 * ih as u64 / iw as u64) as u32) };
    let src: IWICBitmapSource = frame.cast().unwrap();
    let tmp = wic_pixels(&f, &src, tw, th);
    let out = if tmp.is_null() { null_mut() } else { calloc((n * n) as usize, 4) as *mut u32 };
    if !out.is_null() {
        for y in 0..th {
            memcpy(out.add(((y + (n_ - th) / 2) * n_ + (n_ - tw) / 2) as usize), tmp.add((y * tw) as usize), tw as usize * 4);
        }
    }
    free(tmp as *mut c_void);
    out
}

unsafe fn tb_sample_img(img: *const u32, n: i32, x: f32, y: f32, c: &mut [f32; 3]) -> f32 {
    let x = x - 0.5;
    let y = y - 0.5;
    let x0 = x.floor() as i32;
    let y0 = y.floor() as i32;
    let tx = x - x0 as f32;
    let ty = y - y0 as f32;
    let mut acc = [0.0f32; 4];
    for k in 0..4 {
        let xx = x0 + (k & 1);
        let yy = y0 + (k >> 1);
        if xx < 0 || yy < 0 || xx >= n || yy >= n {
            continue;
        }
        let wgt = (if (k & 1) != 0 { tx } else { 1.0 - tx }) * (if (k >> 1) != 0 { ty } else { 1.0 - ty });
        let p = *img.add((yy * n + xx) as usize);
        acc[0] += wgt * ((p >> 16) & 255) as f32;
        acc[1] += wgt * ((p >> 8) & 255) as f32;
        acc[2] += wgt * (p & 255) as f32;
        acc[3] += wgt * (p >> 24) as f32;
    }
    for k in 0..3 {
        c[k] = acc[k] / 255.0;
    }
    acc[3] / 255.0
}

fn tb_shape(style: i32, fx: f32, fy: f32, u: f32, dim: f32, fg: &[f32; 3], c: &mut [f32; 3], a: &mut f32) {
    if style == TBI_GRID {
        let t = 4.6 * u;
        let g = 1.6 * u;
        let off = t + g * 0.5;
        for d in 0..4 {
            let sx = if d & 1 != 0 { 1.0 } else { -1.0 };
            let sy = if d & 2 != 0 { 1.0 } else { -1.0 };
            let m = clamp(0.5 - sd_box(fx - sx * off, fy - sy * off, t, t, 1.6 * u), 0.0, 1.0);
            if m > 0.0 {
                px_over(c, a, fg, m * if d == 0 { 1.0 } else { dim });
            }
        }
    } else if style == TBI_PETALS {
        // four quarter discs around the center, the top left one bright
        let g = 1.2 * u;
        let rad = 9.3 * u;
        for d in 0..4 {
            let sx = if d & 1 != 0 { 1.0 } else { -1.0 };
            let sy = if d & 2 != 0 { 1.0 } else { -1.0 };
            let qx = (fx - sx * g) * sx;
            let qy = (fy - sy * g) * sy;
            let sd = max((qx * qx + qy * qy).sqrt() - rad, max(-qx, -qy));
            let m = clamp(0.5 - sd, 0.0, 1.0);
            if m > 0.0 {
                px_over(c, a, fg, m * if d == 0 { 1.0 } else { dim });
            }
        }
    } else if style == TBI_LAYERS {
        // a card behind another, the front one bright
        let half = 7.1 * u;
        let off = 3.4 * u;
        let rad = 3.2 * u;
        let m = clamp(0.5 - sd_box(fx + off, fy + off, half, half, rad), 0.0, 1.0);
        if m > 0.0 {
            px_over(c, a, fg, m * dim);
        }
        let m = clamp(0.5 - sd_box(fx - off, fy - off, half, half, rad), 0.0, 1.0);
        if m > 0.0 {
            px_over(c, a, fg, m);
        }
    } else if style == TBI_DOTS {
        // nine dots, the middle one bright
        let step = 8.0 * u;
        let rad = 2.6 * u;
        for d in 0..9 {
            let qx = fx - (d % 3 - 1) as f32 * step;
            let qy = fy - (d / 3 - 1) as f32 * step;
            let m = clamp(0.5 - ((qx * qx + qy * qy).sqrt() - rad), 0.0, 1.0);
            if m > 0.0 {
                px_over(c, a, fg, m * if d == 4 { 1.0 } else { dim });
            }
        }
    } else {
        let k_dots: [[f32; 2]; 4] = [[0.0, -1.0], [1.0, 0.0], [0.0, 1.0], [-1.0, 0.0]];
        let half = 3.3 * u;
        let rad = 1.2 * u;
        let off = 6.5 * u;
        for d in 0..4 {
            let qx = fx - k_dots[d][0] * off;
            let qy = fy - k_dots[d][1] * off;
            let rx = (qx + qy) * 0.70710678;
            let ry = (qy - qx) * 0.70710678;
            let m = clamp(0.5 - sd_box(rx, ry, half, half, rad), 0.0, 1.0);
            if m > 0.0 {
                px_over(c, a, fg, m * if d == 0 { 1.0 } else { dim });
            }
        }
    }
}

// How much the icon is squeezed: a damped spring, down to about 0.84 and a little past 1 on the way back, as Start's logo does.
unsafe fn tb_bounce(b: &TbButton) -> f32 {
    let t = (time_now() - b.bounce_at) as f32;
    if b.bounce_at == 0.0 || t >= 0.6 || g_cfg.reduced_motion {
        return 0.0;
    }
    0.35 * (-9.0 * t).exp() * (18.0 * t).sin()
}

unsafe fn tb_render(b: &mut TbButton) {
    let w = b.w;
    let h = b.h;
    if w <= 0 || h <= 0 || b.wnd.is_null() || !layer_fit(&mut b.layer, w, h) {
        return;
    }

    let (s, logo, cx, cy) = tb_geometry(b);
    let plate = logo + 2.0 * (1.0 * s + 0.5).floor();
    let bx = min((40.0 * s + 0.5).floor(), h as f32 - 2.0 * s);
    let brad = 4.0 * s;
    let light = TB.light;
    let fg = [if light { 0.0 } else { 1.0 }; 3];
    let hov_c = [if light { 0.0 } else { 1.0 }; 3];
    let pc = if b.plate_ok {
        b.plate
    } else if light {
        rgb(238, 238, 238)
    } else {
        rgb(28, 28, 28)
    };
    let plate_c = [get_r(pc) as f32 / 255.0, get_g(pc) as f32 / 255.0, get_b(pc) as f32 / 255.0];
    let mut hov_a = if light { 0.055 * b.hover_t + 0.03 * b.press_t } else { 0.075 * b.hover_t - 0.02 * b.press_t };
    hov_a += (if light { 0.04 } else { 0.06 }) * b.open_t * (1.0 - 0.5 * b.hover_t); // like a running app's icon
    let zoom = (1.0 - 0.08 * b.press_t) * (1.0 - tb_bounce(b));
    let u = logo / 24.0 * zoom;
    let dim = (if light { 0.5 } else { 0.42 }) + 0.18 * b.hover_t;
    let replace = g_cfg.taskbar_replace_icon;
    let mut style = clamp(g_cfg.taskbar_icon, 0, TBI__COUNT - 1);
    if replace && style == TBI_CUSTOM {
        let n = logo as i32;
        if b.img.is_null() || b.img_n != n || b.img_gen != TB.icon_gen {
            free(b.img as *mut c_void);
            b.img = tb_load_image(g_cfg.taskbar_icon_path.as_ptr(), n);
            b.img_n = n;
            b.img_gen = TB.icon_gen;
        }
        if b.img.is_null() {
            style = TBI_DIAMOND;
        }
    }
    // With our icon the highlight is not a veil over the bar and the plate (whatever the bar shows under it, the
    // plate would stand out as a square): it is the plate's own color, lit, spread over the whole highlight box,
    // and the box turns solid as the highlight comes. cover: how far it has come; box_c at that opacity gives the
    // bar lit by hov_a.
    hov_a = max(hov_a, 0.0);
    let cover = max(b.hover_t, max(b.open_t, b.press_t));
    let mut lit = [0.0f32; 3];
    let mut box_c = [0.0f32; 3];
    for k in 0..3 {
        lit[k] = plate_c[k] + (hov_c[k] - plate_c[k]) * hov_a;
        box_c[k] = plate_c[k] + (hov_c[k] - plate_c[k]) * if cover > 0.0 { min(hov_a / cover, 1.0) } else { 0.0 };
    }
    // Without it: the real logo's picture, squeezed, while the button is pressed or bounces.
    let sn = b.shot_n;
    let squeeze = !replace && !b.shot.is_null() && sn > 0 && zoom != 1.0;
    let scx = b.shot_x as f32 + sn as f32 * 0.5;
    let scy = b.shot_y as f32 + sn as f32 * 0.5;

    let px = b.layer.bits;
    for y in 0..h {
        for x in 0..w {
            let fx = x as f32 + 0.5 - cx;
            let fy = y as f32 + 0.5 - cy;
            let mut c = [0.0f32; 3];
            let mut a = 0.0f32;
            let box_m = if hov_a > 0.0 || cover > 0.0 { clamp(0.5 - sd_box(fx, fy, bx * 0.5, bx * 0.5, brad), 0.0, 1.0) } else { 0.0 };
            if replace {
                if cover > 0.0 && box_m > 0.0 {
                    px_over(&mut c, &mut a, &box_c, cover * box_m);
                }
                px_over(&mut c, &mut a, &lit, clamp(0.5 - sd_box(fx, fy, plate * 0.5, plate * 0.5, 2.0 * s), 0.0, 1.0));
            } else {
                if squeeze && x >= b.shot_x && x < b.shot_x + sn && y >= b.shot_y && y < b.shot_y + sn {
                    let mut ic = [0.0f32; 3];
                    let ia = tb_sample_img(b.shot, sn, (x as f32 + 0.5 - scx) / zoom + sn as f32 * 0.5, (y as f32 + 0.5 - scy) / zoom + sn as f32 * 0.5, &mut ic);
                    for q in 0..3 {
                        c[q] = ic[q] + plate_c[q] * (1.0 - ia); // the bar where the picture ends
                    }
                    a = 1.0;
                }
                if hov_a > 0.0 {
                    px_over(&mut c, &mut a, &hov_c, hov_a * box_m); // the highlight alone, over the real button
                }
            }
            if replace && style == TBI_CUSTOM {
                let mut ic = [0.0f32; 3];
                let ia = tb_sample_img(b.img, b.img_n, fx / zoom + logo * 0.5, fy / zoom + logo * 0.5, &mut ic);
                if ia > 0.0 {
                    let k = 1.0 - 0.1 * b.press_t;
                    for q in 0..3 {
                        c[q] = ic[q] * k + c[q] * (1.0 - ia);
                    }
                    a = ia + a * (1.0 - ia);
                }
            } else if replace {
                tb_shape(style, fx, fy, u, dim, &fg, &mut c, &mut a);
            }
            *px.add((y * w + x) as usize) = px_pack(&c, a);
        }
    }
    layer_show(&b.layer, b.wnd);
    b.drawn = time_now();
}

unsafe fn tb_from_hwnd(h: HWND) -> *mut TbButton {
    let t = tb();
    for i in 0..t.n as usize {
        if t.b[i].wnd == h {
            return &mut t.b[i];
        }
    }
    null_mut()
}

unsafe fn tb_animate(b: &mut TbButton) {
    let now = time_now();
    let dt = min(now - b.last_anim, 0.05) as f32;
    b.last_anim = now;
    let mut anim = false;
    anim |= approach(&mut b.hover_t, if b.hover || b.press { 1.0 } else { 0.0 }, 18.0, dt);
    anim |= approach(&mut b.press_t, if b.press { 1.0 } else { 0.0 }, 30.0, dt);
    let open = !TB.open_on.is_null() && MonitorFromWindow(b.taskbar, MONITOR_DEFAULTTONEAREST) == TB.open_on;
    anim |= approach(&mut b.open_t, if open { 1.0 } else { 0.0 }, 18.0, dt);
    anim |= b.bounce_at != 0.0 && now - b.bounce_at < 0.6;
    tb_render(b);
    if anim {
        SetTimer(b.wnd, TB_TIMER_ANIM, 15, None);
    } else {
        KillTimer(b.wnd, TB_TIMER_ANIM);
    }
}

unsafe fn tb_kick(b: &mut TbButton) {
    b.last_anim = time_now();
    SetTimer(b.wnd, TB_TIMER_ANIM, 15, None);
    tb_animate(b);
}

unsafe extern "system" fn tb_wndproc(h: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    let b = tb_from_hwnd(h);
    match msg {
        WM_MOUSEACTIVATE => return MA_NOACTIVATE as LRESULT,
        WM_NCHITTEST => return HTCLIENT as LRESULT,
        WM_SETCURSOR => {
            SetCursor(LoadCursorW(null_mut(), IDC_ARROW));
            return TRUE as LRESULT;
        }
        WM_MOUSEMOVE => {
            if !b.is_null() && !(*b).hover {
                (*b).hover = true;
                track_mouse_leave(h);
                tb_kick(&mut *b);
            }
            return 0;
        }
        WM_MOUSELEAVE => {
            if !b.is_null() {
                (*b).hover = false;
                (*b).press = false;
                tb_kick(&mut *b);
            }
            return 0;
        }
        WM_LBUTTONDOWN | WM_LBUTTONDBLCLK => {
            if !b.is_null() {
                (*b).press = true;
                SetCapture(h);
                tb_kick(&mut *b);
            }
            return 0;
        }
        WM_LBUTTONUP => {
            if b.is_null() {
                return 0;
            }
            let was = (*b).press;
            (*b).press = false;
            ReleaseCapture();
            let p = POINT { x: get_x_lparam(lp), y: get_y_lparam(lp) };
            let mut rc: RECT = zeroed();
            GetClientRect(h, &mut rc);
            tb_kick(&mut *b);
            if was && PtInRect(&rc, p) != 0 {
                AllowSetForegroundWindow(ASFW_ANY);
                ui_toggle();
            }
            return 0;
        }
        WM_RBUTTONUP => {
            if !b.is_null() {
                if U.visible && !U.closing {
                    ui_hide();
                }
                let idx = (b as usize - (&raw const TB.b) as usize) / size_of::<TbButton>();
                TB.menu_for.store(idx as i32 + 1, SeqCst);
                SetEvent(TB.wake);
            }
            return 0;
        }
        WM_TIMER => {
            if wp == TB_TIMER_ANIM && !b.is_null() {
                tb_animate(&mut *b);
            }
            return 0;
        }
        WM_DESTROY => {
            if !b.is_null() {
                (*b).wnd = null_mut();
            }
            return 0;
        }
        _ => {}
    }
    DefWindowProcW(h, msg, wp, lp)
}

unsafe fn tb_destroy(b: &mut TbButton) {
    if !b.wnd.is_null() && IsWindow(b.wnd) != 0 {
        DestroyWindow(b.wnd);
    }
    b.wnd = null_mut();
    free(b.img as *mut c_void);
    b.img = null_mut();
    free(b.shot as *mut c_void);
    b.shot = null_mut();
    layer_free(&mut b.layer);
}

unsafe fn tb_publish_hits() {
    let t = tb();
    let hits = &mut *(&raw mut g_tb_hit);
    let mut n = 0;
    for i in 0..t.n as usize {
        if !t.b[i].wnd.is_null() {
            hits[n] = t.b[i].screen;
            n += 1;
        }
    }
    g_tb_hit_count.store(n as i32, SeqCst);
}

// Something of the taskbar's own (its XAML island) went above `h`. Our other taskbar windows don't count:
// raising over each other in turn makes the taskbar flicker.
unsafe fn tb_covered(h: HWND) -> bool {
    let mut w = GetWindow(h, GW_HWNDPREV);
    while !w.is_null() {
        if !window_is_ours(w) {
            return true;
        }
        w = GetWindow(w, GW_HWNDPREV);
    }
    false
}

pub unsafe fn tb_sync() {
    let t = tb();
    if !t.enabled {
        return;
    }
    AcquireSRWLockShared(&mut t.lock);
    let nf = t.nfound as usize;
    let f: [Found; TB_MAX] = t.found;
    ReleaseSRWLockShared(&mut t.lock);

    let mut nb: [TbButton; TB_MAX] = zeroed();
    for i in 0..nf {
        let b = &mut nb[i];
        for k in 0..t.n as usize {
            if t.b[k].taskbar == f[i].taskbar && !t.b[k].wnd.is_null() && IsWindow(t.b[k].wnd) != 0 {
                core::ptr::copy_nonoverlapping(&t.b[k], b, 1); // moves its window, icon and pixels over
                t.b[k].wnd = null_mut();
                t.b[k].img = null_mut();
                t.b[k].shot = null_mut();
                t.b[k].layer = zeroed();
            }
        }
        b.taskbar = f[i].taskbar;
        b.screen = f[i].r;
    }
    for k in 0..t.n as usize {
        tb_destroy(&mut t.b[k]);
    }
    core::ptr::copy_nonoverlapping(nb.as_ptr(), t.b.as_mut_ptr(), TB_MAX);
    core::mem::forget(nb);
    t.n = nf as i32;

    for i in 0..t.n as usize {
        let b = &mut t.b[i];
        let mut p = POINT { x: b.screen.left, y: b.screen.top };
        ScreenToClient(b.taskbar, &mut p);
        let w = b.screen.right - b.screen.left;
        let h = b.screen.bottom - b.screen.top;
        if b.wnd.is_null() {
            b.wnd = CreateWindowExW(
                WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                w!("MixLauncherStartButton"),
                w!(""),
                WS_CHILD | WS_CLIPSIBLINGS,
                p.x,
                p.y,
                w,
                h,
                b.taskbar,
                null_mut(),
                GetModuleHandleW(null()),
                null(),
            );
            if b.wnd.is_null() {
                log_msg!("taskbar: could not create the button window ({})", GetLastError());
                continue;
            }
            b.w = w;
            b.h = h;
            tb_sample(b);
            tb_render(b);
            SetWindowPos(b.wnd, HWND_TOP, p.x, p.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            continue;
        }
        let resized = w != b.w || h != b.h;
        b.w = w;
        b.h = h;
        let mut cur: RECT = zeroed();
        GetWindowRect(b.wnd, &mut cur);
        let moved = EqualRect(&cur, &b.screen) == 0;
        let since = time_now() - b.sampled;
        let resample = moved || since > 5.0 || ((b.pending_ok || !b.plate_ok) && since > 0.3);
        if (resample && tb_sample(b)) || resized {
            tb_render(b);
        }
        let covered = tb_covered(b.wnd);
        if moved || covered || IsWindowVisible(b.wnd) == 0 {
            SetWindowPos(b.wnd, HWND_TOP, p.x, p.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    tb_publish_hits();
}

// The launcher opened on a monitor, or closed (NULL).
pub unsafe fn tb_set_open(mon: HMONITOR) {
    let t = tb();
    t.open_on = mon;
    for i in 0..t.n as usize {
        let b = &mut t.b[i];
        if b.wnd.is_null() {
            continue;
        }
        if !mon.is_null() && MonitorFromWindow(b.taskbar, MONITOR_DEFAULTTONEAREST) == mon {
            b.bounce_at = time_now();
        }
        tb_kick(b);
    }
}

pub unsafe fn tb_icon_changed() {
    let t = tb();
    t.icon_gen += 1;
    for i in 0..t.n as usize {
        t.b[i].shot_n = 0; // taken again once the real logo shows
        tb_render(&mut t.b[i]);
    }
}

pub unsafe fn tb_theme_changed() {
    let t = tb();
    let l = taskbar_light();
    if l == t.light {
        return;
    }
    t.light = l;
    for i in 0..t.n as usize {
        t.b[i].plate_ok = false;
        t.b[i].pending_ok = false;
        t.b[i].shot_n = 0;
        tb_render(&mut t.b[i]);
    }
}

pub unsafe fn tb_explorer_restarted() {
    let t = tb();
    for i in 0..t.n as usize {
        tb_destroy(&mut t.b[i]);
    }
    t.n = 0;
    tb_publish_hits();
    AcquireSRWLockExclusive(&mut t.lock);
    t.nsearch = 0;
    ReleaseSRWLockExclusive(&mut t.lock);
    t.reset.store(1, SeqCst);
    set_fast_until(time_now() + 3.0);
    if !t.wake.is_null() {
        SetEvent(t.wake);
    }
}

unsafe fn tb_thread_start() {
    let t = tb();
    if !t.started {
        t.started = true;
        InitializeSRWLock(&mut t.lock);
        t.wake = CreateEventW(null(), FALSE, FALSE, null());
        thread_start(tb_thread, null_mut());
    }
    t.reset.store(1, SeqCst);
    SetEvent(t.wake);
}

pub unsafe fn tb_set_search(on: bool) {
    let t = tb();
    t.search = on;
    hook_search_clicks(on);
    if on {
        tb_thread_start();
    } else {
        AcquireSRWLockExclusive(&mut t.lock);
        t.nsearch = 0;
        ReleaseSRWLockExclusive(&mut t.lock);
        if !t.wake.is_null() {
            SetEvent(t.wake);
        }
    }
}

pub unsafe fn tb_set_enabled(on: bool) {
    let t = tb();
    t.enabled = on;
    if on {
        window_class(w!("MixLauncherStartButton"), tb_wndproc, CS_DBLCLKS, null_mut(), null_mut());
        t.light = taskbar_light();
        tb_thread_start();
    } else {
        for i in 0..t.n as usize {
            tb_destroy(&mut t.b[i]);
        }
        t.n = 0;
        tb_publish_hits();
        AcquireSRWLockExclusive(&mut t.lock);
        t.nfound = 0;
        ReleaseSRWLockExclusive(&mut t.lock);
        if !t.wake.is_null() {
            SetEvent(t.wake);
        }
    }
}

pub unsafe fn tb_read_centered_pub() -> bool {
    tb_read_centered()
}
pub unsafe fn tb_covered_pub(h: HWND) -> bool {
    tb_covered(h)
}
pub unsafe fn taskbar_light_pub() -> bool {
    taskbar_light()
}
