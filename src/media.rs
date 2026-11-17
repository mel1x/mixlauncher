// "Now playing" on the taskbar: a worker reads the current media session through WinRT (title, artist, cover,
// what can be pressed) and the UI thread draws a layered child window of the taskbar from it.

use crate::*;
use core::mem::ManuallyDrop;
use core::sync::atomic::{AtomicI32, Ordering::SeqCst};
use windows_future::{AsyncStatus, IAsyncInfo, IAsyncOperation};
use windows::Media::Control as mc;
use windows::Win32::Graphics::Imaging::*;
use windows::Win32::System::Com::IStream;
use windows::Win32::System::Com::StructuredStorage::{PROPVARIANT, PropVariantClear};
use windows::Win32::UI::Shell::PropertiesSystem::{IPropertyStore, SHGetPropertyStoreForWindow};
use windows::core::Interface;

const MW_TIMER_ANIM: usize = 1;
const MW_TIMER_SYNC: usize = 2;

const MS_CLOSED: i32 = 0;
const MS_OPENED: i32 = 1;
const MS_CHANGING: i32 = 2;
const MS_STOPPED: i32 = 3;
const MS_PLAYING: i32 = 4;
const MS_PAUSED: i32 = 5;
const MC_NONE: i32 = 0;
const MC_PREV: i32 = 1;
const MC_TOGGLE: i32 = 2;
const MC_NEXT: i32 = 3;
const MP_NONE: i32 = -1;
const MP_INFO: i32 = 0;
const MP_PREV: i32 = 1;
const MP_TOGGLE: i32 = 2;
const MP_NEXT: i32 = 3;

#[repr(C)]
#[derive(Clone, Copy, PartialEq)]
struct MediaInfo {
    has: bool,
    status: i32,
    can_prev: bool,
    can_toggle: bool,
    can_next: bool,
    title: [WCHAR; 256],
    artist: [WCHAR; 256],
    app: [WCHAR; 256],
}

#[repr(C)]
struct MText {
    cov: *mut u8,
    w: i32,
    h: i32,
    base: i32,
    adv: i32,
}

#[repr(C)]
struct Media {
    enabled: bool,
    started: bool,
    wake: HANDLE,
    lock: SRWLOCK,
    pub_: MediaInfo,
    pub_art: *mut u32,
    pub_art_n: i32,
    pub_art_gen: u32,
    widgets_btn: RECT,
    cmd: AtomicI32,
    art_px: AtomicI32,
    art_reload: AtomicI32,
    want_widgets_btn: AtomicI32,
    epoch: AtomicI32, // counts the times the widget was turned on
    wnd: HWND,
    taskbar: HWND,
    moves: HWINEVENTHOOK, // the taskbar and its tray moving or resizing
    info: MediaInfo,
    art: *mut u32,
    art_n: i32,
    art_gen: u32,
    light: bool,
    dpi: u32,
    s: f32,
    title: MText,
    artist: MText,
    note: MText,
    w: i32,
    h: i32,
    x: i32,
    part: [[f32; 4]; 4],
    text_x: f32,
    text_w: f32,
    visible: bool,
    want: bool,
    idle_since: f64,
    hover: i32,
    press: i32,
    hover_any: bool,
    vis_t: f32,
    swap_t: f32,
    hov_t: f32,
    hp_t: [f32; 4],
    press_t: [f32; 4],
    text_a: f32,
    scroll_t: f32,
    mq_off: [f32; 2],
    toggle_local: bool,
    toggle_status: i32,
    toggle_until: f64,
    last_anim: f64,
    layer: Layer,
}

static mut M: Media = unsafe { zeroed() };

#[inline]
unsafe fn mm() -> &'static mut Media {
    &mut *(&raw mut M)
}

pub unsafe fn media_static_init() {
    M.hover = MP_NONE;
    M.press = MP_NONE;
    M.text_a = 1.0;
}

// Worker: WinRT (Windows.Media.Control)

// Waits for an operation up to timeout_ms; cancels it if it is still running.
unsafe fn rt_await<T: windows::core::RuntimeType + 'static>(op: IAsyncOperation<T>, timeout_ms: u32) -> Option<T> {
    let info: IAsyncInfo = op.cast().ok()?;
    let t0 = GetTickCount();
    let mut st = info.Status().unwrap_or(AsyncStatus::Error);
    while st == AsyncStatus::Started && GetTickCount().wrapping_sub(t0) < timeout_ms {
        Sleep(2);
        st = info.Status().unwrap_or(AsyncStatus::Error);
    }
    if st == AsyncStatus::Completed {
        return op.GetResults().ok();
    }
    if st == AsyncStatus::Started {
        let _ = info.Cancel();
    }
    None
}

fn rt_str(s: windows::core::Result<windows::core::HSTRING>, out: &mut [WCHAR; 256]) {
    out[0] = 0;
    if let Ok(h) = s {
        let w: &[u16] = &h;
        let n = min(w.len(), 255);
        out[..n].copy_from_slice(&w[..n]);
        out[n] = 0;
    }
}

unsafe fn media_manager() -> Option<mc::GlobalSystemMediaTransportControlsSessionManager> {
    let mgr = mc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().ok().and_then(|op| rt_await(op, 5000));
    if mgr.is_none() {
        log_msg!("media: session manager unavailable");
    }
    mgr
}

// The cover cut to a centered square and scaled to n x n; *hash tells a new picture from the same one again.
unsafe fn media_decode_art(thumb: Option<&windows::Storage::Streams::IRandomAccessStreamReference>, n: i32, hash: &mut u64) -> *mut u32 {
    let Some(thumb) = thumb else { return null_mut() };
    if n <= 0 || n > 512 {
        return null_mut();
    }
    let Some(ras) = thumb.OpenReadAsync().ok().and_then(|op| rt_await(op, 3000)) else { return null_mut() };
    let r = (|| -> Option<*mut u32> {
        let stm: IStream = windows::Win32::System::WinRT::CreateStreamOverRandomAccessStream(&ras).ok()?;
        let f = wic_factory()?;
        let dec = f.CreateDecoderFromStream(&stm, core::ptr::null(), WICDecodeMetadataCacheOnDemand).ok()?;
        let frame = dec.GetFrame(0).ok()?;
        let (mut iw, mut ih) = (0u32, 0u32);
        frame.GetSize(&mut iw, &mut ih).ok()?;
        if iw == 0 || ih == 0 {
            return None;
        }
        let side = min(iw, ih);
        let rc = WICRect { X: ((iw - side) / 2) as i32, Y: ((ih - side) / 2) as i32, Width: side as i32, Height: side as i32 };
        let clip = f.CreateBitmapClipper().ok()?;
        clip.Initialize(&frame, &rc).ok()?;
        let src: IWICBitmapSource = clip.cast().ok()?;
        let out = wic_pixels(&f, &src, n as u32, n as u32);
        if !out.is_null() {
            *hash = hash_u64(n as u64) ^ hash_wstr(out as *const u16, n * n * 2);
        }
        Some(out)
    })();
    r.unwrap_or(null_mut())
}

unsafe fn media_read(ses: &mc::GlobalSystemMediaTransportControlsSession, inp: &mut MediaInfo, thumb: &mut Option<windows::Storage::Streams::IRandomAccessStreamReference>) {
    inp.has = true;
    rt_str(ses.SourceAppUserModelId(), &mut inp.app);
    if let Ok(pb) = ses.GetPlaybackInfo() {
        inp.status = pb.PlaybackStatus().map(|s| s.0).unwrap_or(0);
        if let Ok(ctl) = pb.Controls() {
            inp.can_prev = ctl.IsPreviousEnabled().unwrap_or(false);
            inp.can_next = ctl.IsNextEnabled().unwrap_or(false);
            inp.can_toggle = ctl.IsPlayPauseToggleEnabled().unwrap_or(false) || ctl.IsPlayEnabled().unwrap_or(false) || ctl.IsPauseEnabled().unwrap_or(false);
        }
    }
    let props = ses.TryGetMediaPropertiesAsync().ok().and_then(|op| rt_await(op, 1500));
    if let Some(props) = props {
        rt_str(props.Title(), &mut inp.title);
        rt_str(props.Artist(), &mut inp.artist);
        if inp.artist[0] == 0 {
            rt_str(props.AlbumArtist(), &mut inp.artist);
        }
        *thumb = props.Thumbnail().ok();
    }
}

// Where the Widgets button is: the widget sits right of it on the left of a centered taskbar.
unsafe fn media_widgets_button(
    u: &mut Option<windows::Win32::UI::Accessibility::IUIAutomation>,
    el: &mut Option<windows::Win32::UI::Accessibility::IUIAutomationElement>,
    retry: &mut f64,
    out: &mut RECT,
) -> bool {
    *out = zeroed();
    if u.is_none() {
        *u = uia_create();
        if u.is_none() {
            return false;
        }
    }
    if el.is_none() {
        if time_now() < *retry {
            return false;
        }
        *retry = time_now() + 10.0;
        let cond = uia_cond(u.as_ref().unwrap(), windows::Win32::UI::Accessibility::UIA_AutomationIdPropertyId, "WidgetsButton");
        *el = uia_find(u.as_ref().unwrap(), FindWindowW(w!("Shell_TrayWnd"), null()), cond.as_ref());
        if el.is_none() {
            return false;
        }
    }
    let r = match el.as_ref().unwrap().CurrentBoundingRectangle() {
        Ok(r) => r,
        Err(_) => {
            *el = None;
            *retry = 0.0;
            return false;
        }
    };
    if r.right <= r.left || r.bottom <= r.top {
        return false;
    }
    *out = RECT { left: r.left, top: r.top, right: r.right, bottom: r.bottom };
    true
}

// Polls the current session (fast for a moment after a button press) and publishes what changed to the UI
// thread (M.pub, WM_APP_MEDIA). Runs the playback commands it is sent.
unsafe extern "system" fn media_thread(_param: *mut c_void) -> u32 {
    CoInitializeEx(null(), COINIT_MULTITHREADED as _);
    let m = mm();
    let mut mgr: Option<mc::GlobalSystemMediaTransportControlsSessionManager> = None;
    let mut u = None;
    let mut wbtn = None;
    let mut mgr_retry = 0.0;
    let mut wbtn_retry = 0.0;
    let mut wbtn_next = 0.0;
    let mut wrect: RECT = zeroed();
    let mut last: MediaInfo = zeroed();
    let mut art_hash = 0u64;
    let mut art_tries = 0;
    let mut fast = 0;
    let mut seen_epoch = 0;
    let mut art_due = 0.0;
    loop {
        let wait = if !m.enabled {
            INFINITE
        } else if fast > 0 {
            120
        } else if last.has {
            500
        } else {
            1500
        };
        WaitForSingleObject(m.wake, wait);
        if !m.enabled {
            wbtn = None;
            u = None;
            mgr = None;
            last = zeroed();
            art_hash = 0;
            continue;
        }
        let epoch = m.epoch.load(SeqCst);
        if epoch != seen_epoch {
            // turned off and on again within one turn: what was published is gone, send it all anew
            seen_epoch = epoch;
            last = zeroed();
            art_hash = 0;
        }
        if fast > 0 {
            fast -= 1;
        }
        let now = time_now();
        if mgr.is_none() && now >= mgr_retry {
            mgr = media_manager();
            if mgr.is_none() {
                mgr_retry = now + 10.0;
            }
        }

        let mut inp: MediaInfo = zeroed();
        let mut thumb = None;
        let mut ses = None;
        if let Some(mg) = mgr.as_ref() {
            match mg.GetCurrentSession() {
                Ok(s) => ses = Some(s),
                Err(e) => {
                    // no session is a null result, an error is a dead manager
                    if e.code().is_err() {
                        mgr = None;
                        mgr_retry = now + 1.0;
                    }
                }
            }
        }
        let cmd = m.cmd.swap(MC_NONE, SeqCst);
        if let Some(s) = ses.as_ref() {
            if cmd != MC_NONE {
                let _ = match cmd {
                    MC_PREV => s.TrySkipPreviousAsync().map(|_| ()),
                    MC_NEXT => s.TrySkipNextAsync().map(|_| ()),
                    _ => s.TryTogglePlayPauseAsync().map(|_| ()),
                };
                fast = 10;
            }
            media_read(s, &mut inp, &mut thumb);
        }
        drop(ses);

        let track_changed = inp.title != last.title || inp.artist != last.artist || inp.app != last.app;
        if track_changed {
            art_tries = 3; // players often set the cover a moment after the title: look again later
            art_due = now;
        }
        if m.art_reload.swap(0, SeqCst) != 0 {
            art_tries = max(art_tries, 1);
            art_due = now;
            art_hash = 0;
        }
        let mut art_new = false;
        let mut art: *mut u32 = null_mut();
        let art_n = m.art_px.load(SeqCst);
        if thumb.is_some() && art_tries > 0 && now >= art_due {
            art_tries -= 1;
            art_due = now + 1.5 * (3 - art_tries) as f64;
            let mut h = 0u64;
            art = media_decode_art(thumb.as_ref(), art_n, &mut h);
            if !art.is_null() && h != art_hash {
                art_hash = h;
                art_new = true;
            } else {
                free(art as *mut c_void);
                art = null_mut();
            }
        } else if thumb.is_none() && track_changed && art_hash != 0 {
            art_hash = 0;
            art_new = true;
        }
        drop(thumb);

        let mut wr = wrect;
        if m.want_widgets_btn.load(SeqCst) != 0 {
            if now >= wbtn_next {
                wbtn_next = now + 2.0;
                media_widgets_button(&mut u, &mut wbtn, &mut wbtn_retry, &mut wr);
            }
        } else {
            wr = zeroed();
        }
        let rect_changed = EqualRect(&wr, &wrect) == 0;
        wrect = wr;

        if inp != last || art_new || rect_changed {
            last = inp;
            let mut old: *mut u32 = null_mut();
            AcquireSRWLockExclusive(&mut m.lock);
            m.pub_ = inp;
            m.widgets_btn = wr;
            if art_new {
                old = m.pub_art;
                m.pub_art = art;
                m.pub_art_n = art_n;
                m.pub_art_gen = m.pub_art_gen.wrapping_add(1);
            }
            ReleaseSRWLockExclusive(&mut m.lock);
            free(old as *mut c_void);
            PostMessageW(g_hwnd, WM_APP_MEDIA, 0, 0);
        }
    }
}

unsafe fn mtext_free(t: &mut MText) {
    free(t.cov as *mut c_void);
    *t = zeroed();
}

static mut MT_TMP: [u8; 1024 * 128] = [0; 1024 * 128];

// A line of text as a coverage strip (the widget is drawn on the CPU, not through the glyph atlas).
unsafe fn mtext_make(t: &mut MText, font: i32, size: f32, s: *const WCHAR) {
    mtext_free(t);
    let len = min(wlen(s), 200);
    if len <= 0 || F.dw.is_none() {
        return;
    }
    let mut pen = 0.0;
    let mut i = 0;
    while i < len {
        pen += (*glyph_map(font, utf16_next(s, len, &mut i))).adv * size;
    }
    let pad = (size * 0.5).ceil() as i32;
    t.w = pen.ceil() as i32 + 2 * pad;
    t.h = (size * 2.0).ceil() as i32;
    t.base = (size * 1.4).ceil() as i32;
    t.adv = pen.ceil() as i32;
    t.cov = calloc((t.w * t.h) as usize, 1) as *mut u8;
    if t.cov.is_null() {
        return;
    }
    let tmp = (&raw mut MT_TMP) as *mut u8;
    pen = 0.0;
    i = 0;
    while i < len {
        let mut cp = utf16_next(s, len, &mut i);
        if cp == '\t' as u32 {
            cp = ' ' as u32;
        }
        let g = glyph_map(font, cp);
        let f = if cp == ' ' as u32 { null_mut() } else { face_get_pub((*g).face as i32) };
        let mut b = RECT { left: 0, top: 0, right: 0, bottom: 0 };
        if !f.is_null() && glyph_alpha(f, (*g).glyph, size, pad as f32 + pen, t.base as f32, &mut b, tmp, 1024 * 128) {
            let w = b.right - b.left;
            for y in max(b.top, 0)..min(b.bottom, t.h) {
                for x in max(b.left, 0)..min(b.right, t.w) {
                    let d = t.cov.add((y * t.w + x) as usize);
                    *d = min(255, *d as i32 + *tmp.add(((y - b.top) * w + (x - b.left)) as usize) as i32) as u8;
                }
            }
        }
        pen += (*g).adv * size;
    }
}

unsafe fn mtext_at(t: &MText, x: f32, y: i32) -> f32 {
    if t.cov.is_null() || y < 0 || y >= t.h {
        return 0.0;
    }
    let pad = (t.w - t.adv) / 2;
    let x = x + pad as f32;
    let x0 = x.floor() as i32;
    let fr = x - x0 as f32;
    let row_ = t.cov.add((y * t.w) as usize);
    let a = if x0 >= 0 && x0 < t.w { *row_.add(x0 as usize) as f32 } else { 0.0 };
    let b = if x0 + 1 >= 0 && x0 + 1 < t.w { *row_.add(x0 as usize + 1) as f32 } else { 0.0 };
    (a + (b - a) * fr) / 255.0
}

unsafe fn MS(v: f32) -> f32 {
    (v * M.s + 0.5).floor()
}

unsafe fn media_controls() -> bool {
    g_cfg.media_controls
}

unsafe fn media_small() -> bool {
    (M.h as f32) < MS(36.0)
}
unsafe fn media_cover() -> f32 {
    if media_small() { min(MS(24.0), M.h as f32 - MS(4.0)) } else { MS(32.0) }
}

unsafe fn media_text_size() -> f32 {
    (12.0 * M.s * 2.0 + 0.5).floor() * 0.5
}

unsafe fn media_layout() {
    let m = mm();
    let size = media_text_size();
    mtext_make(&mut m.title, FONT_TEXT, size, m.info.title.as_ptr());
    mtext_make(&mut m.artist, FONT_TEXT, size, if media_small() { w!("") } else { m.info.artist.as_ptr() });
    let note: [WCHAR; 2] = [0xEC4F, 0];
    mtext_make(&mut m.note, FONT_ICON, (16.0 * m.s + 0.5).floor(), note.as_ptr());

    let pad = MS(4.0);
    let cover = media_cover();
    let gap = MS(8.0);
    let tw = max(m.title.adv, m.artist.adv) as f32;
    m.text_w = min(tw, MS(150.0));
    m.text_x = pad + cover + gap;
    let mut x = m.text_x + m.text_w;
    let h = m.h as f32;
    m.part[MP_INFO as usize][0] = 0.0;
    m.part[MP_INFO as usize][1] = 0.0;
    m.part[MP_INFO as usize][3] = h;
    if media_controls() {
        x += MS(6.0);
        m.part[MP_INFO as usize][2] = x;
        let bw = MS(30.0);
        let bh = min(MS(32.0), h - MS(4.0));
        for i in MP_PREV..=MP_NEXT {
            let p = &mut m.part[i as usize];
            p[0] = x;
            p[1] = ((h - bh) * 0.5).floor();
            p[2] = x + bw;
            p[3] = p[1] + bh;
            x += bw;
        }
        x += MS(2.0);
    } else {
        x += MS(10.0);
        m.part[MP_INFO as usize][2] = x;
        for i in MP_PREV..=MP_NEXT {
            m.part[i as usize] = [0.0; 4];
        }
    }
    m.w = x as i32;
}

fn sd_triangle(px: f32, py: f32, x0: f32, y0: f32, x1: f32, y1: f32, x2: f32, y2: f32) -> f32 {
    let (e0x, e0y, e1x, e1y, e2x, e2y) = (x1 - x0, y1 - y0, x2 - x1, y2 - y1, x0 - x2, y0 - y2);
    let (v0x, v0y, v1x, v1y, v2x, v2y) = (px - x0, py - y0, px - x1, py - y1, px - x2, py - y2);
    let t0 = clamp((v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y), 0.0, 1.0);
    let t1 = clamp((v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y), 0.0, 1.0);
    let t2 = clamp((v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y), 0.0, 1.0);
    let (q0x, q0y, q1x, q1y, q2x, q2y) = (v0x - e0x * t0, v0y - e0y * t0, v1x - e1x * t1, v1y - e1y * t1, v2x - e2x * t2, v2y - e2y * t2);
    let sg = if e0x * e2y - e0y * e2x > 0.0 { 1.0 } else { -1.0 };
    let dx = min(min(q0x * q0x + q0y * q0y, q1x * q1x + q1y * q1y), q2x * q2x + q2y * q2y);
    let dy = min(min(sg * (v0x * e0y - v0y * e0x), sg * (v1x * e1y - v1y * e1x)), sg * (v2x * e2y - v2y * e2x));
    -dx.sqrt() * if dy > 0.0 { 1.0 } else { -1.0 }
}

unsafe fn media_icon(part: i32, playing: bool, fx: f32, fy: f32) -> f32 {
    let u = M.s;
    let r = 1.1 * u;
    let d;
    match part {
        MP_TOGGLE => {
            if playing {
                let a = sd_box(fx + 3.0 * u, fy, 1.6 * u, 6.0 * u, 1.0 * u);
                let b = sd_box(fx - 3.0 * u, fy, 1.6 * u, 6.0 * u, 1.0 * u);
                d = min(a, b);
            } else {
                d = sd_triangle(fx, fy, -4.2 * u + r, -6.4 * u + r * 1.6, 6.4 * u - r * 1.8, 0.0, -4.2 * u + r, 6.4 * u - r * 1.6) - r;
            }
        }
        MP_PREV | MP_NEXT => {
            let fx = if part == MP_NEXT { -fx } else { fx };
            let bar = sd_box(fx + 4.6 * u, fy, 0.9 * u, 5.0 * u, 0.9 * u);
            let tri = sd_triangle(fx, fy, 4.6 * u - r, -5.0 * u + r * 1.6, -3.0 * u + r * 1.8, 0.0, 4.6 * u - r, 5.0 * u - r * 1.6) - r;
            d = min(bar, tri);
        }
        _ => return 0.0,
    }
    clamp(0.5 - d, 0.0, 1.0)
}

unsafe fn media_playing() -> bool {
    if M.toggle_local && time_now() < M.toggle_until {
        return M.toggle_status == MS_PLAYING;
    }
    M.info.status == MS_PLAYING
}

// How far a line that does not fit has to slide to show its end (0 when it fits).
unsafe fn media_marquee_range(t: &MText) -> f32 {
    if !t.cov.is_null() && t.adv as f32 > M.text_w + 0.5 { t.adv as f32 - M.text_w + MS(2.0) } else { 0.0 }
}

// A long line rides to its end, waits, rides back to the start, waits, and so on.
unsafe fn media_marquee_at(range: f32, t: f32) -> f32 {
    if range <= 0.0 {
        return 0.0;
    }
    let hold = 1.4;
    let travel = max(range / (13.0 * M.s), 0.8);
    let c = t % (2.0 * (hold + travel));
    let k = if c < hold {
        0.0
    } else if c < hold + travel {
        (c - hold) / travel
    } else if c < 2.0 * hold + travel {
        1.0
    } else {
        1.0 - (c - 2.0 * hold - travel) / travel
    };
    (range * (k * k * (3.0 - 2.0 * k)) * 4.0 + 0.5).floor() * 0.25
}

// Redraws the columns [xa, xb) of the widget bitmap (all of it after a resize) and shows it.
unsafe fn media_render_cols(xa: i32, xb: i32) {
    let m = mm();
    let w = m.w;
    let h = m.h;
    if w <= 0 || h <= 0 {
        return;
    }
    let (mut xa, mut xb) = (xa, xb);
    if m.layer.bmp.is_null() || m.layer.w != w || m.layer.h != h {
        xa = 0;
        xb = w;
        if !layer_fit(&mut m.layer, w, h) {
            return;
        }
    }

    let light = m.light;
    let playing = media_playing();
    let fg = [if light { 0.0 } else { 1.0 }; 3];
    let vis = m.vis_t;
    let content = vis * (0.35 + 0.65 * m.swap_t);
    let hov_base = if light { 0.055 } else { 0.06 };
    let hov_btn = if light { 0.06 } else { 0.08 };
    let bg_a = hov_base * m.hov_t * vis;
    let brad = MS(5.0);
    let s = m.s;

    let cover = media_cover();
    let cx0 = MS(4.0);
    let cy0 = ((h as f32 - cover) * 0.5).floor();
    let crad = MS(4.0);
    let art = !m.art.is_null() && m.art_n == cover as i32;
    let ccx = cx0 + cover * 0.5;
    let ccy = cy0 + cover * 0.5;
    let note_size = (16.0 * s + 0.5).floor();
    let note_base = (ccy + (font_ascent(FONT_ICON, note_size) - font_descent(FONT_ICON, note_size)) * 0.5 + 0.5).floor() as i32;
    let size = media_text_size();
    let cap = font_cap_height(FONT_TEXT, size);
    let two = !m.artist.cov.is_null();
    let c = h as f32 * 0.5;
    let base1 = (if two { c - MS(8.0) + cap * 0.5 + 0.5 } else { c + cap * 0.5 + 0.5 }).floor() as i32;
    let base2 = (c + MS(8.5) + cap * 0.5 + 0.5).floor() as i32;
    let tx0 = m.text_x;
    let tx1 = m.text_x + m.text_w;
    let fade = MS(12.0);
    let text_a = content * m.text_a;

    let px = m.layer.bits;
    for y in 0..h {
        for x in max(xa, 0)..min(xb, w) {
            let fx = x as f32 + 0.5;
            let fy = y as f32 + 0.5;
            let mut col = [0.0f32; 3];
            let mut a = 0.0f32;
            if bg_a > 0.0 {
                px_over(&mut col, &mut a, &fg, bg_a * clamp(0.5 - sd_box(fx - w as f32 * 0.5, fy - h as f32 * 0.5, w as f32 * 0.5, h as f32 * 0.5 - MS(1.0), brad), 0.0, 1.0));
            }

            if media_controls() {
                for p in MP_PREV..=MP_NEXT {
                    let r = m.part[p as usize];
                    if fx < r[0] || fx >= r[2] || fy < r[1] || fy >= r[3] {
                        continue;
                    }
                    let bx = (r[0] + r[2]) * 0.5;
                    let by = (r[1] + r[3]) * 0.5;
                    let ha = (hov_btn * m.hp_t[p as usize] + (if light { 0.03 } else { -0.02 }) * m.press_t[p as usize]) * vis;
                    if ha > 0.0 {
                        px_over(&mut col, &mut a, &fg, ha * clamp(0.5 - sd_box(fx - bx, fy - by, (r[2] - r[0]) * 0.5 - MS(1.0), (r[3] - r[1]) * 0.5 - MS(2.0), MS(4.0)), 0.0, 1.0));
                    }
                    let en = if p == MP_PREV {
                        m.info.can_prev
                    } else if p == MP_NEXT {
                        m.info.can_next
                    } else {
                        m.info.can_toggle
                    };
                    let zoom = 1.0 - 0.1 * m.press_t[p as usize];
                    let mk = media_icon(p, playing, (fx - bx) / zoom, (fy - by) / zoom);
                    if mk > 0.0 {
                        px_over(&mut col, &mut a, &fg, mk * content * if en { if light { 0.86 } else { 0.92 } } else { 0.3 });
                    }
                }
            }

            if fx >= cx0 - 1.0 && fx < cx0 + cover + 1.0 && fy >= cy0 - 1.0 && fy < cy0 + cover + 1.0 {
                let mk = clamp(0.5 - sd_box(fx - ccx, fy - ccy, cover * 0.5, cover * 0.5, crad), 0.0, 1.0) * content;
                if mk > 0.0 && art {
                    let ix = clamp(x - cx0 as i32, 0, m.art_n - 1);
                    let iy = clamp(y - cy0 as i32, 0, m.art_n - 1);
                    let p = *m.art.add((iy * m.art_n + ix) as usize);
                    let ia = (p >> 24) as f32 / 255.0 * mk;
                    col[0] = ((p >> 16) & 255) as f32 / 255.0 * mk + col[0] * (1.0 - ia);
                    col[1] = ((p >> 8) & 255) as f32 / 255.0 * mk + col[1] * (1.0 - ia);
                    col[2] = (p & 255) as f32 / 255.0 * mk + col[2] * (1.0 - ia);
                    a = ia + a * (1.0 - ia);
                } else if mk > 0.0 {
                    px_over(&mut col, &mut a, &fg, mk * if light { 0.07 } else { 0.09 });
                    if !m.note.cov.is_null() {
                        let v = mtext_at(&m.note, x as f32 - (ccx - m.note.adv as f32 * 0.5).floor(), y - note_base + m.note.base);
                        if v > 0.0 {
                            px_over(&mut col, &mut a, &fg, v * content * 0.55);
                        }
                    }
                }
            }

            if fx >= tx0 && fx < tx1 + 1.0 {
                for line in 0..2 {
                    let t = if line != 0 { &m.artist } else { &m.title };
                    if t.cov.is_null() {
                        continue;
                    }
                    let ty = y - if line != 0 { base2 } else { base1 } + t.base;
                    if ty < 0 || ty >= t.h {
                        continue;
                    }
                    let range = media_marquee_range(t);
                    let off = m.mq_off[line];
                    let lx = x as f32 - tx0 + off;
                    let v = mtext_at(t, lx, ty);
                    if v <= 0.0 || fx > tx1 {
                        continue;
                    }
                    // An edge fades only while more text hides behind it.
                    let mut mask = 1.0;
                    if range > 0.0 {
                        mask *= 1.0 - (1.0 - clamp((tx1 - fx) / fade, 0.0, 1.0)) * clamp((range - off) / fade, 0.0, 1.0);
                        mask *= 1.0 - (1.0 - clamp((fx - tx0) / fade, 0.0, 1.0)) * clamp(off / fade, 0.0, 1.0);
                    }
                    px_over(&mut col, &mut a, &fg, v * mask * text_a * if line != 0 { if light { 0.6 } else { 0.62 } } else if light { 0.9 } else { 1.0 });
                }
            }

            *px.add((y * w + x) as usize) = px_pack(&col, a);
        }
    }
    layer_show(&m.layer, m.wnd);
}

unsafe fn media_render() {
    media_render_cols(0, M.w);
}

unsafe fn media_kick() {
    if M.wnd.is_null() {
        return;
    }
    M.last_anim = time_now();
    SetTimer(M.wnd, MW_TIMER_ANIM, 15, None);
    media_animate(false);
}

unsafe fn media_animate(tick: bool) {
    let m = mm();
    let now = time_now();
    let dt = min(now - m.last_anim, 0.05) as f32;
    m.last_anim = now;
    let mut anim = false;
    anim |= approach(&mut m.vis_t, if m.want { 1.0 } else { 0.0 }, 16.0, dt);
    anim |= approach(&mut m.swap_t, 1.0, 14.0, dt);
    anim |= approach(&mut m.hov_t, if m.hover_any || m.press != MP_NONE { 1.0 } else { 0.0 }, 18.0, dt);
    for p in MP_PREV..=MP_NEXT {
        let pi = p as usize;
        anim |= approach(&mut m.hp_t[pi], if m.hover == p || m.press == p { 1.0 } else { 0.0 }, 20.0, dt);
        anim |= approach(&mut m.press_t[pi], if m.press == p { 1.0 } else { 0.0 }, 30.0, dt);
    }
    anim |= approach(&mut m.text_a, 1.0, 16.0, dt);
    // The marquee keeps the timer going but repaints only while a line moves.
    let mut marquee = false;
    let mut moved = false;
    let on = g_cfg.media_marquee && !g_cfg.reduced_motion;
    if !on {
        m.scroll_t = 0.0; // off: long lines stay at their start
    }
    if m.visible {
        // paused: lines freeze where they were
        let run = on && media_playing();
        if run {
            m.scroll_t = (m.scroll_t + dt) % 3600.0;
        }
        for line in 0..2 {
            let range = media_marquee_range(if line != 0 { &m.artist } else { &m.title });
            let off = media_marquee_at(range, m.scroll_t);
            marquee |= range > 0.0 && run;
            moved |= off != m.mq_off[line];
            m.mq_off[line] = off;
        }
    }
    let full = !tick || anim;
    anim |= marquee;
    if m.vis_t <= 0.0 && !m.want && m.visible {
        ShowWindow(m.wnd, SW_HIDE);
        m.visible = false;
    }
    // only the marquee moved: redraw just the text columns
    if m.visible && full {
        media_render();
    } else if m.visible && moved {
        media_render_cols(m.text_x.floor() as i32, (m.text_x + m.text_w).ceil() as i32 + 2);
    }
    if anim && (m.visible || m.want) {
        SetTimer(m.wnd, MW_TIMER_ANIM, 15, None);
    } else {
        KillTimer(m.wnd, MW_TIMER_ANIM);
    }
}

static mut WIN11: i32 = -1;

unsafe fn media_win11() -> bool {
    if WIN11 < 0 {
        let mut b = [0u16; 16];
        let mut sz = 32u32;
        RegGetValueW(
            HKEY_LOCAL_MACHINE,
            w!("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
            w!("CurrentBuildNumber"),
            RRF_RT_REG_SZ,
            null_mut(),
            b.as_mut_ptr() as *mut c_void,
            &mut sz,
        );
        WIN11 = (_wtoi(b.as_ptr()) >= 22000) as i32;
    }
    WIN11 != 0
}

unsafe fn media_on_left() -> bool {
    g_cfg.media_position == 0 && media_win11() && tb_read_centered_pub()
}

unsafe fn media_place(left: bool) -> bool {
    let m = mm();
    if m.wnd.is_null() || m.taskbar.is_null() {
        return false;
    }
    let mut tr_: RECT = zeroed();
    GetWindowRect(m.taskbar, &mut tr_);
    let tw = tr_.right - tr_.left;
    let th = tr_.bottom - tr_.top;
    if tw <= 0 || th <= 0 || th > tw {
        return false;
    }
    let x;
    if left {
        AcquireSRWLockShared(&mut m.lock);
        let wb = m.widgets_btn;
        ReleaseSRWLockShared(&mut m.lock);
        let mut xx = MS(12.0) as i32;
        if wb.right > wb.left && wb.right < (tr_.left + tr_.right) / 2 {
            xx = wb.right - tr_.left + MS(4.0) as i32;
        }
        x = xx;
    } else {
        let tray = FindWindowExW(m.taskbar, null_mut(), w!("TrayNotifyWnd"), null());
        let mut r: RECT = zeroed();
        if !tray.is_null() && GetWindowRect(tray, &mut r) != 0 && r.left > tr_.left {
            x = r.left - tr_.left - m.w - MS(8.0) as i32;
        } else {
            x = tw - m.w - MS(20.0) as i32;
        }
    }
    let y = (th - m.h) / 2;
    let mut p = POINT { x: tr_.left + x, y: tr_.top + y };
    ScreenToClient(m.taskbar, &mut p);
    let mut cur: RECT = zeroed();
    GetWindowRect(m.wnd, &mut cur);
    let mut cp = POINT { x: cur.left, y: cur.top };
    ScreenToClient(m.taskbar, &mut cp);
    let moved = cp.x != p.x || cp.y != p.y || cur.right - cur.left != m.w || cur.bottom - cur.top != m.h;
    let covered = tb_covered_pub(m.wnd);
    let shown = IsWindowVisible(m.wnd) == 0;
    if moved || covered || shown {
        SetWindowPos(m.wnd, HWND_TOP, p.x, p.y, m.w, m.h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    m.x = x;
    shown
}

// The taskbar or its tray moved or changed size (a game switching the resolution, a tray icon coming or going):
// the widget follows at once, not a second later with the timer.
unsafe extern "system" fn media_moved(_hook: HWINEVENTHOOK, _ev: u32, h: HWND, obj: i32, child: i32, _tid: u32, _time: u32) {
    if obj != OBJID_WINDOW || child != CHILDID_SELF as i32 || h.is_null() || !M.enabled || M.wnd.is_null() || M.taskbar.is_null() {
        return;
    }
    if h == M.taskbar || h == FindWindowExW(M.taskbar, null_mut(), w!("TrayNotifyWnd"), null()) {
        media_sync();
    }
}

unsafe fn media_watch_moves(taskbar: HWND) {
    if !M.moves.is_null() {
        UnhookWinEvent(M.moves);
    }
    M.moves = null_mut();
    let mut pid = 0u32;
    let tid = if taskbar.is_null() { 0 } else { GetWindowThreadProcessId(taskbar, &mut pid) };
    if tid != 0 {
        M.moves = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, null_mut(), Some(media_moved), pid, tid, WINEVENT_OUTOFCONTEXT);
    }
}

unsafe fn media_create() -> bool {
    let m = mm();
    let tb = FindWindowW(w!("Shell_TrayWnd"), null());
    if tb.is_null() {
        return false;
    }
    if !m.wnd.is_null() && IsWindow(m.wnd) != 0 && m.taskbar == tb {
        return true;
    }
    if !m.wnd.is_null() && IsWindow(m.wnd) != 0 {
        DestroyWindow(m.wnd);
    }
    m.wnd = null_mut();
    layer_free(&mut m.layer);
    m.taskbar = tb;
    m.visible = false;
    m.vis_t = 0.0;
    m.wnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        w!("MixLauncherMediaWidget"),
        w!(""),
        WS_CHILD | WS_CLIPSIBLINGS,
        0,
        0,
        1,
        1,
        tb,
        null_mut(),
        GetModuleHandleW(null()),
        null(),
    );
    if m.wnd.is_null() {
        log_msg!("media: could not create the widget window ({})", GetLastError());
        return false;
    }
    SetTimer(m.wnd, MW_TIMER_SYNC, 1000, None);
    media_watch_moves(tb);
    m.dpi = 0;
    true
}

pub unsafe fn media_sync() {
    let m = mm();
    if !m.enabled {
        return;
    }
    if !media_create() {
        return;
    }

    let mut art: *mut u32 = null_mut();
    let mut art_n = 0;
    AcquireSRWLockShared(&mut m.lock);
    let inp = m.pub_;
    let new_art = m.pub_art_gen != m.art_gen;
    if new_art && !m.pub_art.is_null() {
        art_n = m.pub_art_n;
        art = malloc((art_n * art_n * 4) as usize) as *mut u32;
        if !art.is_null() {
            memcpy(art, m.pub_art, (art_n * art_n * 4) as usize);
        }
    }
    m.art_gen = m.pub_art_gen;
    ReleaseSRWLockShared(&mut m.lock);
    if new_art {
        free(m.art as *mut c_void);
        m.art = art;
        m.art_n = if art.is_null() { 0 } else { art_n };
    }

    let track = inp.title != m.info.title || inp.artist != m.info.artist;
    let mut changed = new_art || inp != m.info;
    if m.toggle_local && (inp.status != m.info.status || time_now() >= m.toggle_until) {
        m.toggle_local = false;
        changed = true;
    }
    m.info = inp;

    let dpi = monitor_dpi(MonitorFromWindow(m.taskbar, MONITOR_DEFAULTTONEAREST));
    let mut tr_: RECT = zeroed();
    GetWindowRect(m.taskbar, &mut tr_);
    let h = min((40.0 * dpi as f32 / 96.0 + 0.5).floor() as i32, (tr_.bottom - tr_.top) - 2);
    let relayout = track || dpi != m.dpi || h != m.h;
    if dpi != m.dpi {
        m.dpi = dpi;
        m.s = dpi as f32 / 96.0;
    }
    let left = media_on_left();
    m.want_widgets_btn.store(left as i32, SeqCst);
    if relayout {
        changed = true;
        m.h = max(h, 16);
        let px = media_cover() as i32;
        if m.art_px.swap(px, SeqCst) != px {
            m.art_reload.store(1, SeqCst);
            SetEvent(m.wake);
        }
        media_layout();
        m.scroll_t = 0.0;
        m.mq_off = [0.0; 2];
        if track && m.visible {
            m.swap_t = 0.0;
        }
    }

    let active = inp.has && inp.title[0] != 0 && inp.status != MS_CLOSED;
    let playing = inp.status == MS_PLAYING || (m.toggle_local && m.toggle_status == MS_PLAYING);
    let now = time_now();
    if playing || m.idle_since == 0.0 {
        m.idle_since = if playing { 0.0 } else { now };
    }
    let want = active && (!g_cfg.media_hide_paused || playing || now - m.idle_since < 1.0);
    if want != m.want {
        m.want = want;
        changed = true;
    }
    if want {
        changed |= media_place(left);
        m.visible = true;
    }
    if m.visible && changed {
        media_kick();
    }
}

unsafe fn media_send(cmd: i32) {
    M.cmd.store(cmd, SeqCst);
    SetEvent(M.wake);
}

struct MediaFind {
    app: *const WCHAR,
    found: HWND,
}

static PKEY_AppUserModel_ID_M: windows::Win32::Foundation::PROPERTYKEY =
    windows::Win32::Foundation::PROPERTYKEY { fmtid: windows::core::GUID::from_u128(0x9f4c2855_9f79_4b39_a8d0_e1d42de1d5f3), pid: 5 };

unsafe extern "system" fn media_find_window(h: HWND, lp: LPARAM) -> BOOL {
    let mf = &mut *(lp as *mut MediaFind);
    if IsWindowVisible(h) == 0 || !GetWindow(h, GW_OWNER).is_null() || (GetWindowLongW(h, GWL_EXSTYLE) as u32 & WS_EX_TOOLWINDOW) != 0 || GetWindowTextLengthW(h) == 0 {
        return TRUE;
    }
    if let Ok(ps) = SHGetPropertyStoreForWindow::<IPropertyStore>(whwnd(h)) {
        let mut hit = false;
        if let Ok(v) = ps.GetValue(&PKEY_AppUserModel_ID_M) {
            let mut v: PROPVARIANT = v;
            let vt = (*v.Anonymous.Anonymous).vt;
            if vt == windows::Win32::System::Variant::VT_LPWSTR {
                let p = (*v.Anonymous.Anonymous).Anonymous.pwszVal.0;
                hit = !p.is_null() && _wcsicmp(p, mf.app) == 0;
            }
            let _ = PropVariantClear(&mut v);
            core::mem::forget(v);
        }
        if hit {
            mf.found = h;
            return FALSE;
        }
    }
    let mut pid = 0u32;
    GetWindowThreadProcessId(h, &mut pid);
    let proc_ = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if !proc_.is_null() {
        let mut path = [0u16; MAX_PATH];
        let mut n = MAX_PATH as u32;
        if QueryFullProcessImageNameW(proc_, 0, path.as_mut_ptr(), &mut n) != 0 {
            let name = PathFindFileNameW(path.as_ptr());
            let an = wlen(mf.app);
            let hit = _wcsicmp(name, mf.app) == 0 || (_wcsnicmp(name, mf.app, an as usize) == 0 && _wcsicmp(name.add(an as usize), w!(".exe")) == 0);
            if hit {
                mf.found = h;
            }
        }
        CloseHandle(proc_);
    }
    if mf.found.is_null() { TRUE } else { FALSE }
}

unsafe fn media_open_player() {
    if M.info.app[0] == 0 {
        return;
    }
    let mut mf = MediaFind { app: M.info.app.as_ptr(), found: null_mut() };
    EnumWindows(Some(media_find_window), &mut mf as *mut MediaFind as LPARAM);
    if !mf.found.is_null() {
        if IsIconic(mf.found) != 0 {
            ShowWindow(mf.found, SW_RESTORE);
        }
        if SetForegroundWindow(mf.found) == 0 {
            SwitchToThisWindow(mf.found, TRUE);
        }
        return;
    }
    let mut j: LaunchJob = zeroed();
    j.act = ACT_OPEN;
    j.is_app = true;
    j.target = wdup_heap(M.info.app.as_ptr());
    launch_submit(&j);
}

const MM_PLAYER: i32 = 1;
const MM_SETTINGS: i32 = 2;
const MM_HIDE: i32 = 3;

unsafe fn media_menu_pick(cmd: i32) {
    match cmd {
        MM_PLAYER => media_open_player(),
        MM_SETTINGS => settings_open_at(SID_MEDIA),
        MM_HIDE => {
            cfg_set(CF_MEDIA_WIDGET, 0);
            media_set_enabled(false);
            settings_invalidate();
        }
        _ => {}
    }
}

unsafe fn media_menu() {
    let mut items: Vec<PmItem> = Vec::with_capacity(4);
    if M.info.app[0] != 0 {
        items.push(PmItem { id: MM_PLAYER, label: tr!("Открыть плеер", "Open the player"), glyph: 0xE8D6 });
    }
    items.push(PmItem { id: MM_SETTINGS, label: tr!("Настройки", "Settings"), glyph: 0xE713 });
    items.push(PmItem::SEP);
    items.push(PmItem { id: MM_HIDE, label: tr!("Убрать с панели задач", "Remove from the taskbar"), glyph: 0xE711 });
    let mut pt = POINT { x: 0, y: 0 };
    GetCursorPos(&mut pt);
    pm_open(&items, pt, media_menu_pick);
}

unsafe fn media_part_at(x: i32, y: i32) -> i32 {
    for p in MP_INFO..=MP_NEXT {
        if in_box(&M.part[p as usize], x as f32, y as f32) {
            return p;
        }
    }
    MP_INFO
}

unsafe fn media_click(part: i32) {
    match part {
        MP_INFO => {
            AllowSetForegroundWindow(ASFW_ANY);
            media_open_player();
        }
        MP_PREV => {
            if M.info.can_prev {
                media_send(MC_PREV);
            }
        }
        MP_NEXT => {
            if M.info.can_next {
                media_send(MC_NEXT);
            }
        }
        MP_TOGGLE => {
            if M.info.can_toggle {
                M.toggle_status = if media_playing() { MS_PAUSED } else { MS_PLAYING };
                M.toggle_local = true;
                M.toggle_until = time_now() + 1.5;
                media_send(MC_TOGGLE);
            }
        }
        _ => {}
    }
}

unsafe extern "system" fn media_wndproc(h: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    match msg {
        WM_MOUSEACTIVATE => return MA_NOACTIVATE as LRESULT,
        WM_NCHITTEST => return HTCLIENT as LRESULT,
        WM_SETCURSOR => {
            SetCursor(LoadCursorW(null_mut(), IDC_ARROW));
            return TRUE as LRESULT;
        }
        WM_MOUSEMOVE => {
            let p = media_part_at(get_x_lparam(lp), get_y_lparam(lp));
            if !M.hover_any {
                M.hover_any = true;
                track_mouse_leave(h);
                media_kick();
            }
            if p != M.hover {
                M.hover = p;
                media_kick();
            }
            return 0;
        }
        WM_MOUSELEAVE => {
            M.hover_any = false;
            M.hover = MP_NONE;
            if GetCapture() != h {
                M.press = MP_NONE;
            }
            media_kick();
            return 0;
        }
        WM_LBUTTONDOWN | WM_LBUTTONDBLCLK => {
            M.press = media_part_at(get_x_lparam(lp), get_y_lparam(lp));
            SetCapture(h);
            media_kick();
            return 0;
        }
        WM_LBUTTONUP => {
            let was = M.press;
            M.press = MP_NONE;
            ReleaseCapture();
            let x = get_x_lparam(lp);
            let y = get_y_lparam(lp);
            let inside = x >= 0 && y >= 0 && x < M.w && y < M.h;
            if was != MP_NONE && inside && media_part_at(x, y) == was {
                media_click(was);
            }
            media_kick();
            return 0;
        }
        WM_RBUTTONUP => {
            if U.visible && !U.closing {
                ui_hide();
            }
            media_menu();
            return 0;
        }
        WM_TIMER => {
            if wp == MW_TIMER_ANIM {
                media_animate(true);
            } else if wp == MW_TIMER_SYNC {
                media_sync();
            }
            return 0;
        }
        WM_DESTROY => {
            if M.wnd == h {
                M.wnd = null_mut();
                M.visible = false;
            }
            return 0;
        }
        _ => {}
    }
    DefWindowProcW(h, msg, wp, lp)
}

pub unsafe fn media_theme_changed() {
    let l = taskbar_light_pub();
    if l == M.light {
        return;
    }
    M.light = l;
    if M.visible {
        media_render();
    }
}

pub unsafe fn media_explorer_restarted() {
    let m = mm();
    if !m.enabled {
        return;
    }
    if !m.wnd.is_null() && IsWindow(m.wnd) != 0 {
        DestroyWindow(m.wnd);
    }
    m.wnd = null_mut();
    layer_free(&mut m.layer);
    m.taskbar = null_mut();
    media_sync();
}

pub unsafe fn media_settings_changed() {
    if !M.enabled {
        return;
    }
    M.dpi = 0;
    media_sync();
}

pub unsafe fn media_set_enabled(on: bool) {
    let m = mm();
    m.enabled = on;
    if on {
        window_class(w!("MixLauncherMediaWidget"), media_wndproc, CS_DBLCLKS, null_mut(), null_mut());
        m.light = taskbar_light_pub();
        if !m.started {
            m.started = true;
            InitializeSRWLock(&mut m.lock);
            m.wake = CreateEventW(null(), FALSE, FALSE, null());
            thread_start(media_thread, null_mut());
        }
        m.epoch.fetch_add(1, SeqCst);
        SetEvent(m.wake);
        media_sync();
    } else {
        media_watch_moves(null_mut());
        if !m.wnd.is_null() && IsWindow(m.wnd) != 0 {
            DestroyWindow(m.wnd);
        }
        m.wnd = null_mut();
        m.want = false;
        m.vis_t = 0.0;
        layer_free(&mut m.layer);
        AcquireSRWLockExclusive(&mut m.lock);
        m.pub_ = zeroed();
        free(m.pub_art as *mut c_void);
        m.pub_art = null_mut();
        m.pub_art_gen = m.pub_art_gen.wrapping_add(1);
        ReleaseSRWLockExclusive(&mut m.lock);
        m.info = zeroed();
        if !m.wake.is_null() {
            SetEvent(m.wake);
        }
    }
}

// Diagnostics: --media-dump <out.bmp> [dpi] [light] [sample|demo][-alpha] draws the widget into a BMP.
pub unsafe fn media_dump(out: *const WCHAR, dpi: i32, light: bool, mode: *const WCHAR) -> i32 {
    CoInitializeEx(null(), COINIT_MULTITHREADED as _);
    if !font_init() {
        return 1;
    }
    let m = mm();
    m.s = dpi as f32 / 96.0;
    m.light = light;
    let mgr = media_manager();
    let ses = mgr.as_ref().and_then(|g| g.GetCurrentSession().ok());
    let mut thumb = None;
    if let Some(s) = ses.as_ref() {
        media_read(s, &mut m.info, &mut thumb);
    }
    let mut h = 0u64;
    m.h = (40.0 * m.s + 0.5).floor() as i32;
    m.art_n = media_cover() as i32;
    m.art = media_decode_art(thumb.as_ref(), m.art_n, &mut h);
    let sample = !mode.is_null() && !wcsstr(mode, w!("sample")).is_null();
    let demo = !mode.is_null() && !wcsstr(mode, w!("demo")).is_null();
    let alpha = !mode.is_null() && !wcsstr(mode, w!("alpha")).is_null();
    if sample || demo || m.info.title[0] == 0 {
        wcopy(m.info.title.as_mut_ptr(), 256, if demo { w!("Midnight Drive") } else { w!("A rather long track title that does not fit") });
        free(m.art as *mut c_void);
        m.art = null_mut();
    }
    if demo {
        wcopy(m.info.artist.as_mut_ptr(), 256, w!("Neon Coast"));
        m.info.status = MS_PLAYING;
        m.info.can_prev = true;
        m.info.can_toggle = true;
        m.info.can_next = true;
        let n = m.art_n;
        m.art = malloc((n * n * 4) as usize) as *mut u32;
        if !m.art.is_null() {
            for y in 0..n {
                for x in 0..n {
                    let u = (x as f32 + 0.5) / n as f32;
                    let v = (y as f32 + 0.5) / n as f32;
                    let t = clamp((u + v) * 0.5, 0.0, 1.0);
                    let mut c = [28.0 + 150.0 * t, 30.0 + 40.0 * t, 92.0 + 60.0 * t];
                    let dx = u - 0.66;
                    let dy = v - 0.38;
                    let sun = clamp((0.22 - (dx * dx + dy * dy).sqrt()) * n as f32 * 0.5, 0.0, 1.0);
                    let hill = clamp((v - (0.72 - 0.08 * (u * 6.2).sin())) * n as f32 * 0.5, 0.0, 1.0);
                    for k in 0..3 {
                        c[k] = c[k] * (1.0 - sun) + (if k == 0 { 255.0 } else if k == 1 { 196.0 } else { 140.0 }) * sun;
                        c[k] = c[k] * (1.0 - hill) + (if k == 0 { 18.0 } else if k == 1 { 16.0 } else { 40.0 }) * hill;
                    }
                    *m.art.add((y * n + x) as usize) = 0xFF000000 | ((c[0] as u32) << 16) | ((c[1] as u32) << 8) | c[2] as u32;
                }
            }
        }
    }
    media_layout();
    m.vis_t = 1.0;
    m.swap_t = 1.0;
    m.text_a = 1.0;
    m.hov_t = 0.0;
    media_render();
    let w = m.w;
    let hh = (48.0 * m.s + 0.5).floor() as i32;
    let y0 = (hh - m.h) / 2;
    if m.layer.bits.is_null() {
        return 1;
    }
    let mut img = vec![0u32; (w * hh) as usize];
    let bg: u32 = if light { 0xF3F3F3 } else { 0x1C1C1C };
    for y in 0..hh {
        for x in 0..w {
            let mut d = if alpha { 0 } else { bg };
            if y >= y0 && y < y0 + m.h {
                let p = *m.layer.bits.add(((y - y0) * w + x) as usize);
                let a = p >> 24;
                let mut r = 0u32;
                let mut k = 0;
                while k < 24 {
                    let ch = (p >> k) & 255;
                    let v = if alpha {
                        if a != 0 { min(255, ch * 255 / a) } else { 0 }
                    } else {
                        min(255, ch + ((bg >> k) & 255) * (255 - a) / 255)
                    };
                    r |= v << k;
                    k += 8;
                }
                d = if alpha { r | (a << 24) } else { r };
            }
            img[((hh - 1 - y) * w + x) as usize] = d;
        }
    }
    let mut fh: BITMAPFILEHEADER = zeroed();
    let mut ih: BITMAPINFOHEADER = zeroed();
    ih.biSize = size_of::<BITMAPINFOHEADER>() as u32;
    ih.biWidth = w;
    ih.biHeight = hh;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    fh.bfType = 0x4D42;
    fh.bfOffBits = (size_of::<BITMAPFILEHEADER>() + size_of::<BITMAPINFOHEADER>()) as u32;
    fh.bfSize = fh.bfOffBits + (w * hh * 4) as u32;
    let mut b = Buf::new();
    b.put(core::slice::from_raw_parts(&fh as *const _ as *const u8, size_of::<BITMAPFILEHEADER>()));
    b.put(core::slice::from_raw_parts(&ih as *const _ as *const u8, size_of::<BITMAPINFOHEADER>()));
    b.put(core::slice::from_raw_parts(img.as_ptr() as *const u8, (w * hh * 4) as usize));
    buf_save(&mut b, out);
    let mut t = Buf::new();
    buf_printf!(
        t,
        "has={} status={} prev={} toggle={} next={} art={}\n",
        m.info.has as i32,
        m.info.status,
        m.info.can_prev as i32,
        m.info.can_toggle as i32,
        m.info.can_next as i32,
        (!m.art.is_null()) as i32
    );
    t.put_w(m.info.title.as_ptr());
    t.put(b"\n");
    t.put_w(m.info.artist.as_ptr());
    t.put(b"\n");
    t.put_w(m.info.app.as_ptr());
    t.put(b"\n");
    let mut txt = [0u16; MAX_PATH + 8];
    wfmt!(txt.as_mut_ptr(), txt.len(), "{}.txt", Ws(out));
    buf_save(&mut t, txt.as_ptr());
    0
}
