// The launcher's backdrop (glass, acrylic): the screen behind the window, blurred on the GPU and drawn
// by the renderer (r_backdrop) like any other rect, so it zooms and slides with the content. It is live: while the
// launcher shows, the window is left out of screen captures and Desktop Duplication hands over every new image of
// the screen behind it. Where that cannot run (another GPU's monitor, a rotated or HDR one), a still taken as the
// launcher opens. PrtSc and Win+Shift+S turn the exclusion off, so screenshots still show the launcher.

use crate::*;
use windows::Win32::Graphics::Direct3D11::*;
use windows::Win32::Graphics::Dxgi::Common::*;
use windows::Win32::Graphics::Dxgi::*;
use windows::core::Interface;

#[repr(C)]
pub struct Backdrop {
    pub rc: RECT, // the screen area R.bd_src holds, screen px
    pub mon: HMONITOR,
    pub dup: Option<IDXGIOutputDuplication>, // the live screen of mon
    pub out: RECT,                           // where mon's images sit on the screen
    pub retry: u32,                          // when to start dup again after it was lost (tick)
    pub want_live: bool,                     // keep dup going: the launcher shows, left out of captures
    pub first: bool,                         // dup's next image is its first: take it even if nothing changed
    pub ok: bool,                            // R.bd_src holds the screen behind
}

pub static mut BD: Backdrop = unsafe { zeroed() };

#[inline]
unsafe fn bd() -> &'static mut Backdrop {
    &mut *(&raw mut BD)
}

const WDA_EXCLUDEFROMCAPTURE_: u32 = 0x11;

// The radius of the window's corners, as DWM rounds them.
pub unsafe fn backdrop_radius() -> f32 {
    let c = LS().corner;
    if c == DWC_ROUND {
        SR(8.0)
    } else if c == DWC_ROUND_SMALL {
        SR(4.0)
    } else {
        0.0
    }
}

unsafe fn bd_dup_stop() {
    bd().dup = None;
}

// Desktop Duplication of BD.mon, on our device's GPU.
unsafe fn bd_dup_start() -> bool {
    let b = bd();
    if b.dup.is_some() {
        return true;
    }
    let dev = rr().dev.clone();
    let ad = dev.as_ref().and_then(|d| d.cast::<IDXGIDevice>().ok()).and_then(|dd| dd.GetAdapter().ok());
    if let Some(ad) = ad {
        let mut i = 0u32;
        while let Ok(o) = ad.EnumOutputs(i) {
            i += 1;
            let mut od: DXGI_OUTPUT_DESC = zeroed();
            if let Ok(d) = o.GetDesc() { od = d; }
            let mine = od.Monitor.0 == b.mon;
            if mine && od.Rotation.0 <= DXGI_MODE_ROTATION_IDENTITY.0 {
                if let Ok(o1) = o.cast::<IDXGIOutput1>() {
                    match o1.DuplicateOutput(dev.as_ref().unwrap()) {
                        Ok(d) => b.dup = Some(d),
                        Err(e) => log_msg!("backdrop: DuplicateOutput failed 0x{:08x}", e.code().0 as u32),
                    }
                    let dc = od.DesktopCoordinates;
                    b.out = RECT { left: dc.left, top: dc.top, right: dc.right, bottom: dc.bottom };
                }
            }
            if mine {
                break;
            }
        }
    }
    b.first = true;
    b.dup.is_some()
}

static mut DIRTY: [windows::Win32::Foundation::RECT; 64] = unsafe { zeroed() };

// Takes dup's next image into the source, waiting up to timeout ms; true when the screen behind changed.
unsafe fn bd_pull(timeout: u32) -> bool {
    let b = bd();
    let dup = b.dup.clone().unwrap();
    let mut fi: DXGI_OUTDUPL_FRAME_INFO = zeroed();
    let mut res: Option<IDXGIResource> = None;
    if let Err(e) = dup.AcquireNextFrame(timeout, &mut fi, &mut res) {
        if e.code() == DXGI_ERROR_WAIT_TIMEOUT {
            return false;
        }
        // lost: a mode change, the secure desktop (UAC), a fullscreen app
        bd_dup_stop();
        b.retry = GetTickCount().wrapping_add(500);
        return false;
    }
    let mut got = false;
    let mut behind = b.first;
    if fi.LastPresentTime != 0 && !behind {
        // not just the pointer: did it change behind the launcher?
        let mut sz = 0u32;
        let mr = dup.GetFrameMoveRects(0, null_mut(), &mut sz);
        if matches!(&mr, Err(e) if e.code() == DXGI_ERROR_MORE_DATA) || sz != 0 {
            behind = true; // scrolled
        }
        sz = 0;
        let dirty = &mut *(&raw mut DIRTY);
        if dup.GetFrameDirtyRects(size_of::<[RECT; 64]>() as u32, dirty.as_mut_ptr(), &mut sz).is_ok() {
            // Our own frames show up too, as the window's rect exactly, though the image leaves the window out:
            // taken as a change they would ask for frame after frame.
            let mut area = b.rc;
            let mut own: RECT = zeroed();
            let mut x: RECT = zeroed();
            OffsetRect(&mut area, -b.out.left, -b.out.top);
            GetWindowRect(g_hwnd, &mut own);
            OffsetRect(&mut own, -b.out.left, -b.out.top);
            let full = RECT { left: 0, top: 0, right: b.out.right - b.out.left, bottom: b.out.bottom - b.out.top };
            let o2 = own;
            IntersectRect(&mut own, &o2, &full);
            let n = sz as usize / size_of::<RECT>();
            let mut i = 0;
            while i < n && !behind {
                let d = dirty[i];
                let dr = RECT { left: d.left, top: d.top, right: d.right, bottom: d.bottom };
                behind = EqualRect(&dr, &own) == 0 && IntersectRect(&mut x, &dr, &area) != 0;
                i += 1;
            }
        } else {
            behind = true; // more rects than fit
        }
    }
    if behind {
        if let Some(tex) = res.as_ref().and_then(|r| r.cast::<ID3D11Texture2D>().ok()) {
            let bx = D3D11_BOX {
                left: (b.rc.left - b.out.left) as u32,
                top: (b.rc.top - b.out.top) as u32,
                front: 0,
                right: (b.rc.right - b.out.left) as u32,
                bottom: (b.rc.bottom - b.out.top) as u32,
                back: 1,
            };
            let r = rr();
            r.ctx.as_ref().unwrap().CopySubresourceRegion(r.bd_src.tex.as_ref().unwrap(), 0, 0, 0, 0, &tex, 0, Some(&bx));
            r.bd_dirty = true;
            got = true;
            b.first = false;
        }
    }
    drop(res);
    let _ = dup.ReleaseFrame();
    got
}

// The wallpaper under rc (screen px) as the desktop shows it, into l (fit to rc's size).
pub unsafe fn wallpaper_paint(l: *mut Layer, rc: RECT) -> bool {
    if !layer_fit(l, rc.right - rc.left, rc.bottom - rc.top) {
        return false;
    }
    SetViewportOrgEx((*l).dc, -rc.left, -rc.top, null_mut()); // PaintDesktop draws in screen coordinates
    let ok = PaintDesktop((*l).dc) != 0;
    SetViewportOrgEx((*l).dc, 0, 0, null_mut());
    GdiFlush();
    if !ok {
        log_msg!("backdrop: PaintDesktop failed ({})", GetLastError());
    }
    ok
}

// Mica: the wallpaper under BD.rc. A still: it changes only with the wallpaper.
unsafe fn bd_wallpaper() -> bool {
    let mut l: Layer = zeroed();
    let ok = wallpaper_paint(&mut l, BD.rc);
    if ok {
        r_bd_upload(l.bits);
    }
    layer_free(&mut l);
    ok
}

// A still of BD.rc through GDI.
unsafe fn bd_snapshot() -> bool {
    let mut l: Layer = zeroed();
    let w = BD.rc.right - BD.rc.left;
    let h = BD.rc.bottom - BD.rc.top;
    if !layer_fit(&mut l, w, h) {
        return false;
    }
    let scr = GetDC(null_mut());
    let ok = !scr.is_null() && BitBlt(l.dc, 0, 0, w, h, scr, BD.rc.left, BD.rc.top, SRCCOPY) != 0;
    if !scr.is_null() {
        ReleaseDC(null_mut(), scr);
    }
    GdiFlush();
    if ok {
        r_bd_upload(l.bits);
    } else {
        log_msg!("backdrop: screen capture failed ({})", GetLastError());
    }
    layer_free(&mut l);
    ok
}

// The screen behind the launcher's window win (screen px) on mon, with a margin the blur and the lens reach into.
// The Windows 10 style resizes and takes another width for a query: everything to its right, the full height.
pub unsafe fn bd_open(win: RECT, mon: HMONITOR) {
    let b = bd();
    b.ok = false;
    b.want_live = false;
    let kind = backdrop_kind();
    if kind == BACKDROP_SOLID || !R.ok {
        return;
    }
    let mut mi: MONITORINFO = zeroed();
    mi.cbSize = size_of::<MONITORINFO>() as u32;
    if GetMonitorInfoW(mon, &mut mi) == 0 {
        return;
    }
    let m = SR(56.0) as i32;
    let mut rc = RECT { left: win.left - m, top: win.top - m, right: win.right + m, bottom: win.bottom + m };
    if w10_on() {
        rc.right = mi.rcMonitor.right;
        rc.top = mi.rcMonitor.top;
        rc.bottom = mi.rcMonitor.bottom;
    }
    let rc0 = rc;
    if IntersectRect(&mut rc, &rc0, &mi.rcMonitor) == 0 || !r_bd_source(rc.right - rc.left, rc.bottom - rc.top) {
        return;
    }
    if b.dup.is_some() && (mon != b.mon || EqualRect(&rc, &b.rc) == 0) {
        b.first = true; // a new area: its first image whole
    }
    if mon != b.mon {
        bd_dup_stop();
    }
    b.rc = rc;
    b.mon = mon;
    R.bd_sigma = (if kind == BACKDROP_MICA { 60.0 } else { 30.0 }) * U.s;
    let t0 = time_now();
    if kind == BACKDROP_MICA {
        // the wallpaper only: nothing live, the launcher stays in screen captures
        b.ok = bd_wallpaper();
        R.bd_dirty = true;
        if g_ktrace_on {
            log_msg!("backdrop mica {}x{} {} in {} ms", rc.right - rc.left, rc.bottom - rc.top, if b.ok { "ok" } else { "none" }, Ff((time_now() - t0) * 1000.0, 1));
        }
        return;
    }
    // Left out of captures from now on: Desktop Duplication (and a still taken while it shows) sees what is behind.
    let excluded = SetWindowDisplayAffinity(g_hwnd, WDA_EXCLUDEFROMCAPTURE_) != 0;
    let shown = IsWindowVisible(g_hwnd) != 0;
    if excluded && shown {
        DwmFlush();
    }
    b.want_live = excluded;
    b.ok = b.want_live && bd_dup_start() && bd_pull(30);
    let live = b.ok;
    if !b.ok && (excluded || !shown) {
        b.ok = bd_snapshot();
    }
    R.bd_dirty = true;
    if g_ktrace_on {
        log_msg!(
            "backdrop {}x{} {} in {} ms",
            rc.right - rc.left,
            rc.bottom - rc.top,
            if live {
                "live"
            } else if b.ok {
                "still"
            } else {
                "none"
            },
            Ff((time_now() - t0) * 1000.0, 1)
        );
    }
}

// The launcher closed: no more images, back in screen captures.
pub unsafe fn bd_close() {
    bd_dup_stop();
    if BD.want_live {
        SetWindowDisplayAffinity(g_hwnd, WDA_NONE);
    }
    BD.want_live = false;
    BD.ok = false;
}

// A screenshot is being taken: the launcher shows in it again, over the last image of the screen behind.
pub unsafe fn bd_screenshot() {
    if !BD.want_live {
        return;
    }
    bd_close();
    BD.ok = rr().bd_src.srv.is_some();
}

// The setting changed, another monitor, a new device: take it all again where the window is now.
pub unsafe fn bd_refresh() {
    let mut w: RECT = zeroed();
    if !U.visible || GetWindowRect(g_hwnd, &mut w) == 0 {
        return;
    }
    OffsetRect(&mut w, 0, -U.slide_y);
    w.bottom = w.top + U.H;
    bd_close();
    bd_open(w, MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST));
}

pub unsafe fn bd_live() -> bool {
    BD.want_live
}

// Every turn of the message loop while the launcher shows: a new image of the screen behind asks for a frame.
pub unsafe fn bd_poll() {
    if !BD.want_live {
        return;
    }
    if bd().dup.is_none() && (GetTickCount().wrapping_sub(BD.retry) as i32) >= 0 && !bd_dup_start() {
        BD.retry = GetTickCount().wrapping_add(1000);
    }
    if bd().dup.is_some() && bd_pull(0) {
        U.dirty = true;
    }
}

// The message loop's wait while a live backdrop has nothing new: on the screen's next image (Desktop Duplication
// has no event to wait on with the messages), up to ms, then input gets its turn. False without one to wait on.
pub unsafe fn bd_wait(ms: u32) -> bool {
    if !BD.want_live || bd().dup.is_none() {
        return false;
    }
    if bd_pull(ms) {
        U.dirty = true;
    }
    true
}

// Whether this frame has a backdrop to draw.
pub unsafe fn bd_ready() -> bool {
    BD.ok && backdrop_kind() != BACKDROP_SOLID && rr().bd_src.srv.is_some()
}

// The shader's kind of backdrop (bd1.x).
pub fn bd_shading(kind: i32) -> f32 {
    if kind == BACKDROP_BLUR {
        0.0
    } else if kind == BACKDROP_ACRYLIC {
        1.0
    } else {
        2.0
    }
}

// The frame's constants: where the window is on the screen as it goes out.
pub unsafe fn bd_place() {
    let mut w: RECT = zeroed();
    if !BD.ok || GetWindowRect(g_hwnd, &mut w) == 0 {
        return;
    }
    let kind = backdrop_kind();
    let r = rr();
    r.bd[0] = (w.left - BD.rc.left) as f32;
    r.bd[1] = (w.top - BD.rc.top) as f32;
    r.bd[2] = 1.0 / r.bd_src.w as f32;
    r.bd[3] = 1.0 / r.bd_src.h as f32;
    r.bd[4] = bd_shading(kind);
    r.bd[5] = if TH.dark { 1.0 } else { 0.0 };
    r.bd[6] = 0.0;
    r.bd[7] = 0.0;
}
