// Icons: the shell renders them on two worker threads, the UI thread packs them into the icon atlas (one slot each,
// the least recently drawn one is reused) and draws them from there.

use crate::*;
use windows::Win32::Graphics::Imaging::*;
use windows::Win32::System::Com::{IStream, STATSTG, STATFLAG_NONAME, STREAM_SEEK_SET, CLSCTX_INPROC_SERVER as W_CLSCTX_INPROC_SERVER};

use windows::Win32::UI::Shell as wsh;
use windows::core::Interface;

pub const ICON_KIND_APP: u8 = 0;
pub const ICON_KIND_FILE: u8 = 1;
pub const ICON_KIND_AVATAR: u8 = 2; // avatar: a picture file, cut to a circle
pub const ICON_EMPTY: u8 = 0;
pub const ICON_PENDING: u8 = 1;
pub const ICON_READY: u8 = 2;
pub const ICON_FAILED: u8 = 3;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IconEntry {
    pub hash: u64,
    pub state: u8,
    pub slot: u16,
    pub last_used: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IconReq {
    pub hash: u64,
    pub generation: u32,
    pub px: i32,
    pub kind: u8,
    pub path: *mut WCHAR,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IconRes {
    pub hash: u64,
    pub generation: u32,
    pub px: i32,
    pub pixels: *mut u32,
}

const ICON_HASH: usize = 4096;
const ICON_MAX_SLOTS: usize = 1024;

#[repr(C)]
pub struct Icons {
    pub table: [IconEntry; ICON_HASH],
    pub px: i32,
    pub slot_px: i32,
    pub per_row: i32,
    pub slot_count: i32,
    pub slot_owner: [u64; ICON_MAX_SLOTS],
    pub slot_used: [u32; ICON_MAX_SLOTS],
    pub frame: u32,
    pub used: i32,
    pub generation: core::sync::atomic::AtomicI32,

    pub lock: SRWLOCK,
    pub cv: CONDITION_VARIABLE,
    pub reqs: *mut IconReq,
    pub req_count: i32,
    pub req_cap: i32,
    pub res: *mut IconRes,
    pub res_count: i32,
    pub res_cap: i32,
    pub posted: bool,
}

pub static mut I: Icons = unsafe { zeroed() };

use core::sync::atomic::Ordering::SeqCst;

#[inline]
unsafe fn ii() -> &'static mut Icons {
    &mut *(&raw mut I)
}

// The entry for an icon key, or a new one; emptied entries are reused as tombstones.
unsafe fn icon_entry(h: u64, create: bool) -> *mut IconEntry {
    let i = hash_u64(h) as u32 as usize & (ICON_HASH - 1);
    let mut tomb: *mut IconEntry = null_mut();
    let t = &mut ii().table;
    for p in 0..ICON_HASH {
        let e: *mut IconEntry = &mut t[(i + p) & (ICON_HASH - 1)];
        if (*e).hash == h {
            return e;
        }
        if (*e).hash == 0 {
            if !create {
                return null_mut();
            }
            if tomb.is_null() {
                I.used += 1;
            }
            let e = if tomb.is_null() { e } else { tomb };
            zero(e);
            (*e).hash = h;
            return e;
        }
        if (*e).state == ICON_EMPTY && tomb.is_null() {
            tomb = e;
        }
    }
    if !tomb.is_null() && create {
        zero(tomb);
        (*tomb).hash = h;
        return tomb;
    }
    null_mut()
}

pub unsafe fn icons_reset(px: i32) {
    let s = ii();
    s.generation.fetch_add(1, SeqCst);
    memset(s.table.as_mut_ptr(), 0, size_of::<[IconEntry; ICON_HASH]>());
    s.used = 0;
    s.slot_owner = [0; ICON_MAX_SLOTS];
    s.slot_used = [0; ICON_MAX_SLOTS];
    s.px = px;
    s.slot_px = px + 2;
    s.per_row = ICON_ATLAS / s.slot_px;
    s.slot_count = min(s.per_row * s.per_row, ICON_MAX_SLOTS as i32);
    AcquireSRWLockExclusive(&mut s.lock);
    for i in 0..s.req_count as usize {
        free((*s.reqs.add(i)).path as *mut c_void);
    }
    s.req_count = 0;
    ReleaseSRWLockExclusive(&mut s.lock);
}

pub unsafe fn icons_begin_frame() {
    I.frame = I.frame.wrapping_add(1);
    if I.used > (ICON_HASH * 3 / 4) as i32 {
        icons_reset(I.px);
    }
}

// Where a slot's icon sits in the atlas (inside its 1 px border).
unsafe fn icon_slot_xy(slot: i32, x: &mut i32, y: &mut i32) {
    *x = (slot % I.per_row) * I.slot_px + 1;
    *y = (slot / I.per_row) * I.slot_px + 1;
}

// The icon's atlas coordinates when it is ready; otherwise it is queued for the workers and false returned.
pub unsafe fn icon_get(h: u64, kind: u8, path: *const WCHAR, uv: &mut [f32; 4]) -> bool {
    if h == 0 || path.is_null() {
        return false;
    }
    let e = icon_entry(h, true);
    if e.is_null() {
        return false;
    }
    if (*e).state == ICON_READY {
        (*e).last_used = I.frame;
        ii().slot_used[(*e).slot as usize] = I.frame;
        let (mut sx, mut sy) = (0, 0);
        icon_slot_xy((*e).slot as i32, &mut sx, &mut sy);
        uv[0] = sx as f32 / ICON_ATLAS as f32;
        uv[1] = sy as f32 / ICON_ATLAS as f32;
        uv[2] = (sx + I.px) as f32 / ICON_ATLAS as f32;
        uv[3] = (sy + I.px) as f32 / ICON_ATLAS as f32;
        return true;
    }
    if (*e).state == ICON_EMPTY {
        (*e).state = ICON_PENDING;
        let s = ii();
        AcquireSRWLockExclusive(&mut s.lock);
        if grow(&mut s.reqs, &mut s.req_cap, s.req_count + 1, 128) {
            let r = &mut *s.reqs.add(s.req_count as usize);
            s.req_count += 1;
            r.hash = h;
            r.generation = s.generation.load(SeqCst) as u32;
            r.px = s.px;
            r.kind = kind;
            r.path = wdup_heap(path);
        } else {
            (*e).state = ICON_EMPTY;
        }
        ReleaseSRWLockExclusive(&mut s.lock);
        WakeConditionVariable(&mut s.cv);
    }
    false
}

pub unsafe fn icon_failed(h: u64) -> bool {
    let e = icon_entry(h, false);
    !e.is_null() && (*e).state == ICON_FAILED
}

unsafe fn icon_alloc_slot() -> i32 {
    let s = ii();
    let mut best = -1;
    let mut best_used = u32::MAX;
    for i in 0..s.slot_count as usize {
        if s.slot_owner[i] == 0 {
            return i as i32;
        }
        if s.slot_used[i] != s.frame && s.slot_used[i] < best_used {
            best_used = s.slot_used[i];
            best = i as i32;
        }
    }
    if best >= 0 {
        let old = icon_entry(s.slot_owner[best as usize], false);
        if !old.is_null() {
            (*old).state = ICON_EMPTY;
        }
    }
    best
}

pub unsafe fn icons_process_results() -> bool {
    let mut local: [IconRes; 64] = zeroed();
    let mut any = false;
    let s = ii();
    loop {
        AcquireSRWLockExclusive(&mut s.lock);
        let n = min(s.res_count, 64);
        memcpy(local.as_mut_ptr(), s.res, size_of::<IconRes>() * n as usize);
        memmove(s.res, s.res.add(n as usize), size_of::<IconRes>() * (s.res_count - n) as usize);
        s.res_count -= n;
        if s.res_count == 0 {
            s.posted = false;
        }
        ReleaseSRWLockExclusive(&mut s.lock);
        if n == 0 {
            break;
        }
        for r in &local[..n as usize] {
            let e = if r.generation == s.generation.load(SeqCst) as u32 && r.px == s.px { icon_entry(r.hash, false) } else { null_mut() };
            if !e.is_null() && (*e).state == ICON_PENDING {
                if r.pixels.is_null() {
                    (*e).state = ICON_FAILED;
                } else {
                    let slot = icon_alloc_slot();
                    if slot < 0 {
                        (*e).state = ICON_EMPTY;
                    } else {
                        s.slot_owner[slot as usize] = r.hash;
                        s.slot_used[slot as usize] = s.frame;
                        (*e).slot = slot as u16;
                        (*e).state = ICON_READY;
                        (*e).last_used = s.frame;
                        let (mut sx, mut sy) = (0, 0);
                        icon_slot_xy(slot, &mut sx, &mut sy);
                        r_upload_icon(sx, sy, s.px, r.pixels);
                    }
                }
                any = true;
            }
            free(r.pixels as *mut c_void);
        }
    }
    any
}

// The bitmap as premultiplied BGRA, px by px: fitted and centered, box-filtered when it is bigger.
pub unsafe fn hbitmap_to_pixels(bmp: HBITMAP, px: i32) -> *mut u32 {
    let mut bm: BITMAP = zeroed();
    if GetObjectW(bmp, size_of::<BITMAP>() as i32, &mut bm as *mut BITMAP as *mut c_void) == 0 || bm.bmWidth <= 0 || bm.bmHeight <= 0 {
        return null_mut();
    }
    let w = bm.bmWidth;
    let h = bm.bmHeight;
    let src = malloc(w as usize * h as usize * 4) as *mut u32;
    if src.is_null() {
        return null_mut();
    }
    let mut bi = dib32(w, h);
    let dc = GetDC(null_mut());
    let got = GetDIBits(dc, bmp, 0, h as u32, src as *mut c_void, &mut bi, DIB_RGB_COLORS);
    ReleaseDC(null_mut(), dc);
    if got != h {
        free(src as *mut c_void);
        return null_mut();
    }
    let n = (w * h) as usize;
    let sp = core::slice::from_raw_parts_mut(src, n);
    // no alpha at all: opaque; color above alpha somewhere: straight alpha, premultiply it
    let mut any_alpha = false;
    let mut straight = false;
    for &p in sp.iter() {
        let a = p >> 24;
        if a != 0 {
            any_alpha = true;
        }
        if ((p >> 16) & 255) > a || ((p >> 8) & 255) > a || (p & 255) > a {
            straight = true;
        }
    }
    if !any_alpha {
        for p in sp.iter_mut() {
            *p |= 0xFF000000;
        }
    } else if straight {
        for p in sp.iter_mut() {
            let a = *p >> 24;
            let r = ((*p >> 16) & 255) * a / 255;
            let g = ((*p >> 8) & 255) * a / 255;
            let b = (*p & 255) * a / 255;
            *p = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    let out = calloc(px as usize * px as usize, 4) as *mut u32;
    if out.is_null() {
        free(src as *mut c_void);
        return null_mut();
    }
    if w == px && h == px {
        memcpy(out, src, px as usize * px as usize * 4);
    } else {
        let scale = px as f32 / max(w, h) as f32;
        let dw = max(1, (w as f32 * scale + 0.5) as i32);
        let dh = max(1, (h as f32 * scale + 0.5) as i32);
        let ox = (px - dw) / 2;
        let oy = (px - dh) / 2;
        for y in 0..dh {
            let sy0 = y * h / dh;
            let sy1 = max(sy0 + 1, (y + 1) * h / dh);
            for x in 0..dw {
                let sx0 = x * w / dw;
                let sx1 = max(sx0 + 1, (x + 1) * w / dw);
                let mut acc = [0u32; 4];
                let mut cnt = 0u32;
                for sy in sy0..sy1 {
                    for sx in sx0..sx1 {
                        let p = sp[(sy * w + sx) as usize];
                        acc[0] += p & 255;
                        acc[1] += (p >> 8) & 255;
                        acc[2] += (p >> 16) & 255;
                        acc[3] += p >> 24;
                        cnt += 1;
                    }
                }
                *out.add(((oy + y) * px + ox + x) as usize) = (acc[0] / cnt) | ((acc[1] / cnt) << 8) | ((acc[2] / cnt) << 16) | ((acc[3] / cnt) << 24);
            }
        }
    }
    free(src as *mut c_void);
    out
}

unsafe fn icon_load(r: &IconReq) -> *mut u32 {
    let mut buf = [0u16; 1024];
    let mut path: *const WCHAR = r.path;
    if r.kind == ICON_KIND_APP {
        wjoin(buf.as_mut_ptr(), 1024, &[(w!("shell:AppsFolder\\"), -1), (r.path, -1)]);
        path = buf.as_ptr();
    }
    let item: wsh::IShellItem = match wsh::SHCreateItemFromParsingName(windows::core::PCWSTR(path), None) {
        Ok(i) => i,
        Err(_) => return null_mut(),
    };
    let mut px: *mut u32 = null_mut();
    if let Ok(f) = item.cast::<wsh::IShellItemImageFactory>() {
        let sz = windows::Win32::Foundation::SIZE { cx: r.px, cy: r.px };
        let mut hr = if r.kind == ICON_KIND_AVATAR { Err(windows::core::Error::from_hresult(windows::core::HRESULT(E_FAIL))) } else { f.GetImage(sz, wsh::SIIGBF_ICONONLY) };
        if hr.is_err() {
            hr = f.GetImage(sz, wsh::SIIGBF_RESIZETOFIT);
        }
        if let Ok(bmp) = hr {
            if !bmp.0.is_null() {
                px = hbitmap_to_pixels(bmp.0 as HBITMAP, r.px);
                DeleteObject(bmp.0 as HBITMAP);
            }
        }
    }
    drop(item);
    if !px.is_null() && r.kind == ICON_KIND_AVATAR {
        let c = r.px as f32 * 0.5;
        for y in 0..r.px {
            for x in 0..r.px {
                let dx = x as f32 + 0.5 - c;
                let dy = y as f32 + 0.5 - c;
                let cov = clamp(c - (dx * dx + dy * dy).sqrt() + 0.5, 0.0, 1.0);
                let p = *px.add((y * r.px + x) as usize);
                let mut o = 0u32;
                let mut k = 0;
                while k < 32 {
                    o |= ((((p >> k) & 255) as f32 * cov + 0.5) as u32) << k; // premultiplied: all four scale
                    k += 8;
                }
                *px.add((y * r.px + x) as usize) = o;
            }
        }
    }
    px
}

unsafe extern "system" fn icon_worker(_param: *mut c_void) -> u32 {
    windows_sys::Win32::System::Com::CoInitializeEx(null(), COINIT_APARTMENTTHREADED as _);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    let s = &raw mut I;
    loop {
        AcquireSRWLockExclusive(&raw mut (*s).lock);
        while (*s).req_count == 0 {
            SleepConditionVariableSRW(&raw mut (*s).cv, &raw mut (*s).lock, INFINITE, 0);
        }
        (*s).req_count -= 1;
        let req = *(*s).reqs.add((*s).req_count as usize);
        ReleaseSRWLockExclusive(&raw mut (*s).lock);

        let mut res = IconRes { hash: req.hash, generation: req.generation, px: req.px, pixels: null_mut() };
        if req.generation == (*s).generation.load(SeqCst) as u32 {
            res.pixels = icon_load(&req);
        }
        free(req.path as *mut c_void);

        AcquireSRWLockExclusive(&raw mut (*s).lock);
        let mut post = false;
        if grow(&raw mut (*s).res, &raw mut (*s).res_cap, (*s).res_count + 1, 64) {
            *(*s).res.add((*s).res_count as usize) = res;
            (*s).res_count += 1;
            if !(*s).posted {
                (*s).posted = true;
                post = true;
            }
        } else {
            free(res.pixels as *mut c_void);
        }
        ReleaseSRWLockExclusive(&raw mut (*s).lock);
        if post {
            PostMessageW(g_hwnd, WM_APP_ICONS_READY, 0, 0);
        }
    }
}

pub unsafe fn icons_start(px: i32) {
    InitializeSRWLock(&raw mut I.lock);
    InitializeConditionVariable(&raw mut I.cv);
    icons_reset(px);
    for _ in 0..2 {
        thread_start(icon_worker, null_mut());
    }
}

// ---- WIC: pictures from files and streams (the custom Start button icon, media covers), and PNG for our icon

pub unsafe fn wic_factory() -> Option<IWICImagingFactory> {
    windows::Win32::System::Com::CoCreateInstance(&CLSID_WICImagingFactory, None, W_CLSCTX_INPROC_SERVER).ok()
}

// An image scaled to w x h, as premultiplied BGRA (malloc'd).
pub unsafe fn wic_pixels(f: &IWICImagingFactory, src: &IWICBitmapSource, w: u32, h: u32) -> *mut u32 {
    let r = (|| -> windows::core::Result<IWICFormatConverter> {
        let sc = f.CreateBitmapScaler()?;
        sc.Initialize(src, w, h, WICBitmapInterpolationModeFant)?;
        let cv = f.CreateFormatConverter()?;
        cv.Initialize(&sc, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, None, 0.0, WICBitmapPaletteTypeCustom)?;
        Ok(cv)
    })();
    let Ok(cv) = r else { return null_mut() };
    let out = malloc(w as usize * h as usize * 4) as *mut u32;
    if !out.is_null() && cv.CopyPixels(core::ptr::null(), w * 4, core::slice::from_raw_parts_mut(out as *mut u8, (w * h * 4) as usize)).is_err() {
        free(out as *mut c_void);
        return null_mut();
    }
    out
}

// Straight-alpha BGRA as a PNG file, appended to b.
pub unsafe fn wic_png(bgra: *const u32, w: i32, h: i32, b: &mut Buf) -> bool {
    let Some(f) = wic_factory() else { return false };
    let Some(stm) = wsh::SHCreateMemStream(None) else { return false };
    let mut fmt = GUID_WICPixelFormat32bppBGRA;
    let ok = (|| -> windows::core::Result<bool> {
        let enc = f.CreateEncoder(&GUID_ContainerFormatPng, core::ptr::null())?;
        enc.Initialize(&stm, WICBitmapEncoderNoCache)?;
        let mut frame: Option<IWICBitmapFrameEncode> = None;
        enc.CreateNewFrame(&mut frame, core::ptr::null_mut())?;
        let frame = frame.unwrap();
        frame.Initialize(None)?;
        frame.SetSize(w as u32, h as u32)?;
        frame.SetPixelFormat(&mut fmt)?;
        if fmt != GUID_WICPixelFormat32bppBGRA {
            return Ok(false);
        }
        frame.WritePixels(h as u32, w as u32 * 4, core::slice::from_raw_parts(bgra as *const u8, (w * h * 4) as usize))?;
        frame.Commit()?;
        enc.Commit()?;
        Ok(true)
    })()
    .unwrap_or(false);
    if !ok {
        return false;
    }
    let mut st: STATSTG = zeroed();
    if stm.Stat(&mut st, STATFLAG_NONAME).is_err() || stm.Seek(0, STREAM_SEEK_SET, None).is_err() {
        return false;
    }
    let mut left = st.cbSize as u32;
    let mut chunk = vec![0u8; 16 * 1024];
    while left > 0 {
        let mut got = 0u32;
        let hr = stm.Read(chunk.as_mut_ptr() as *mut c_void, min(left, chunk.len() as u32), Some(&mut got));
        if hr.is_err() || got == 0 {
            return false;
        }
        b.put(&chunk[..got as usize]);
        left -= got;
    }
    true
}

// Files share their type's icon, except those with icons of their own (programs, shortcuts, icons).
pub unsafe fn icon_key_for_file(full: *const WCHAR, folder: bool) -> u64 {
    if folder {
        return hash_wstr_i(full, -1) ^ 0x1111;
    }
    let ext = PathFindExtensionW(full);
    let own = [
        w!(".exe"), w!(".lnk"), w!(".ico"), w!(".url"), w!(".cur"), w!(".ani"), w!(".appref-ms"), w!(".msc"), w!(".cpl"), w!(".scr"), w!(".website"),
        w!(".msi"), w!(""),
    ];
    for o in own {
        if _wcsicmp(ext, o) == 0 {
            return hash_wstr_i(full, -1) ^ 0x2222;
        }
    }
    hash_wstr_i(ext, -1) ^ 0x3333
}
