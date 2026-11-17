// Text through DirectWrite glyph runs, rasterized once per size and subpixel phase into the glyph atlas.
// Characters a face lacks come from the first fallback family that has them.

use crate::*;
use core::mem::ManuallyDrop;
use windows::Win32::Graphics::DirectWrite::*;
use windows::core::Interface;

pub const FONT_TEXT: i32 = 0;
pub const FONT_TEXT_SEMIBOLD: i32 = 1;
pub const FONT_DISPLAY: i32 = 2;
pub const FONT_ICON: i32 = 3;
pub const FONT__COUNT: i32 = 4;

#[repr(C)]
pub struct Face {
    pub face: Option<IDWriteFontFace>,
    pub upem: f32,
    pub ascent: f32,
    pub descent: f32,
    pub cap_height: f32,
}

static k_fallback_families: [&str; 18] = [
    "Segoe UI", "Segoe UI Symbol", "Segoe UI Emoji", "Microsoft YaHei UI", "Yu Gothic UI",
    "Malgun Gothic", "Microsoft JhengHei UI", "Nirmala UI", "Leelawadee UI", "Segoe UI Historic",
    "Ebrima", "Gadugi", "Myanmar Text", "Javanese Text", "Mongolian Baiti", "Microsoft Himalaya",
    "Sylfaen", "Segoe MDL2 Assets",
];
const FALLBACK_COUNT: usize = 18;

#[repr(C)]
pub struct Fonts {
    pub dw: Option<IDWriteFactory>,
    pub dw2: Option<IDWriteFactory2>,
    pub v2: bool,
    pub coll: Option<IDWriteFontCollection>,
    pub faces: [Face; FONT__COUNT as usize + FALLBACK_COUNT],
    pub fallback_tried: [bool; FALLBACK_COUNT],
    pub overflow: bool,
    pub shelf_x: i32,
    pub shelf_y: i32,
    pub shelf_h: i32,
}

pub static mut F: Fonts = unsafe { zeroed() };

#[inline]
unsafe fn ff() -> &'static mut Fonts {
    &mut *(&raw mut F)
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GlyphMap {
    pub key: u32,
    pub glyph: u16,
    pub face: u8,
    pub used: u8,
    pub adv: f32,
}

const GMAP_SIZE: usize = 16384;
static mut g_gmap: [GlyphMap; GMAP_SIZE] = unsafe { zeroed() };
static mut g_gmap_count: i32 = 0;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GlyphRaster {
    pub key: u64,
    pub x: u16,
    pub y: u16,
    pub w: u16,
    pub h: u16,
    pub ox: i16,
    pub oy: i16,
}

const GRAST_SIZE: usize = 16384;
static mut g_grast: [GlyphRaster; GRAST_SIZE] = unsafe { zeroed() };
static mut g_grast_count: i32 = 0;

unsafe fn font_load_face(family: &str, weight: i32) -> Option<IDWriteFontFace> {
    let coll = ff().coll.as_ref()?;
    let name = wstr(family);
    let mut idx = 0u32;
    let mut exists = windows::core::BOOL(0);
    if coll.FindFamilyName(windows::core::PCWSTR(name.as_ptr()), &mut idx, &mut exists).is_err() || !exists.as_bool() {
        return None;
    }
    let fam = coll.GetFontFamily(idx).ok()?;
    let font = fam.GetFirstMatchingFont(DWRITE_FONT_WEIGHT(weight), DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL).ok()?;
    font.CreateFontFace().ok()
}

unsafe fn face_setup(f: *mut Face, face: IDWriteFontFace) {
    let mut m: DWRITE_FONT_METRICS = zeroed();
    face.GetMetrics(&mut m);
    (*f).face = Some(face);
    (*f).upem = if m.designUnitsPerEm != 0 { m.designUnitsPerEm as f32 } else { 2048.0 };
    (*f).ascent = m.ascent as f32;
    (*f).descent = m.descent as f32;
    (*f).cap_height = if m.capHeight != 0 { m.capHeight as f32 } else { m.ascent as f32 * 0.7 };
}

pub unsafe fn font_init() -> bool {
    let f: IDWriteFactory = match DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED) {
        Ok(f) => f,
        Err(_) => return false,
    };
    if let Ok(f2) = f.cast::<IDWriteFactory2>() {
        F.v2 = true;
        ff().dw2 = Some(f2);
    }
    let mut coll: Option<IDWriteFontCollection> = None;
    if f.GetSystemFontCollection(&mut coll, false).is_err() {
        return false;
    }
    ff().dw = Some(f);
    ff().coll = coll;

    let want: [(i32, &str, i32, &str, i32); 4] = [
        (FONT_TEXT, "Segoe UI Variable Text", 400, "Segoe UI", 400),
        (FONT_TEXT_SEMIBOLD, "Segoe UI Variable Text", 600, "Segoe UI", 600),
        (FONT_DISPLAY, "Segoe UI Variable Display", 400, "Segoe UI", 400),
        (FONT_ICON, "Segoe Fluent Icons", 400, "Segoe MDL2 Assets", 400),
    ];
    for (slot, fam, w, alt, aw) in want {
        let face = font_load_face(fam, w).or_else(|| font_load_face(alt, aw)).or_else(|| font_load_face("Arial", 400));
        let Some(face) = face else { return false };
        face_setup(&mut ff().faces[slot as usize], face);
    }
    true
}

unsafe fn face_get(face_id: i32) -> *mut Face {
    if face_id < FONT__COUNT {
        return &mut ff().faces[face_id as usize];
    }
    let fb = (face_id - FONT__COUNT) as usize;
    if !F.fallback_tried[fb] {
        F.fallback_tried[fb] = true;
        if let Some(face) = font_load_face(k_fallback_families[fb], 400) {
            face_setup(&mut ff().faces[face_id as usize], face);
        }
    }
    if ff().faces[face_id as usize].face.is_some() { &mut ff().faces[face_id as usize] } else { null_mut() }
}

unsafe fn face_glyph_index(f: *mut Face, cp: u32) -> u16 {
    let mut gi = 0u16;
    if !f.is_null() {
        if let Some(face) = (*f).face.as_ref() {
            let _ = face.GetGlyphIndices(&cp, 1, &mut gi);
        }
    }
    gi
}

pub unsafe fn glyph_map(font: i32, cp: u32) -> *mut GlyphMap {
    let key = ((font as u32) << 24) | (cp & 0xFFFFFF);
    let h = hash_u64(key as u64) as u32 as usize & (GMAP_SIZE - 1);
    let gmap = &mut *(&raw mut g_gmap);
    for probe in 0..GMAP_SIZE {
        let e = &mut gmap[(h + probe) & (GMAP_SIZE - 1)];
        if e.used != 0 && e.key == key {
            return e;
        }
        if e.used == 0 {
            if g_gmap_count >= (GMAP_SIZE * 3 / 4) as i32 {
                break; // filling up: lookups would crawl, start over
            }
            g_gmap_count += 1;
            e.used = 1;
            e.key = key;
            let mut face_id = font;
            let mut gi = face_glyph_index(&mut ff().faces[font as usize], cp);
            if gi == 0 && cp >= 0x20 {
                for i in 0..FALLBACK_COUNT as i32 {
                    let fb = face_get(FONT__COUNT + i);
                    let g2 = if fb.is_null() { 0 } else { face_glyph_index(fb, cp) };
                    if g2 != 0 {
                        gi = g2;
                        face_id = FONT__COUNT + i;
                        break;
                    }
                }
            }
            e.glyph = gi;
            e.face = face_id as u8;
            let f = &ff().faces[face_id as usize];
            let mut gm: DWRITE_GLYPH_METRICS = zeroed();
            let _ = f.face.as_ref().unwrap().GetDesignGlyphMetrics(&gi, 1, &mut gm, false);
            e.adv = gm.advanceWidth as f32 / f.upem;
            return e;
        }
    }
    memset(gmap.as_mut_ptr(), 0, size_of::<[GlyphMap; GMAP_SIZE]>());
    g_gmap_count = 0;
    glyph_map(font, cp)
}

pub static mut g_atlas_gen: u32 = 0; // bumped on every reset: cached atlas regions know they are gone

pub unsafe fn font_reset_atlas() {
    g_atlas_gen = g_atlas_gen.wrapping_add(1);
    memset(&raw mut g_grast, 0, size_of::<[GlyphRaster; GRAST_SIZE]>());
    g_grast_count = 0;
    F.shelf_x = 0;
    F.shelf_y = 0;
    F.shelf_h = 0;
    if !R.glyph_cpu.is_null() {
        memset(R.glyph_cpu, 0, GLYPH_ATLAS as usize * GLYPH_ATLAS as usize);
    }
    R.dirty_x0 = 0;
    R.dirty_y0 = 0;
    R.dirty_x1 = GLYPH_ATLAS;
    R.dirty_y1 = GLYPH_ATLAS;
    F.overflow = false;
}

unsafe fn atlas_alloc(w: i32, h: i32, ox: &mut i32, oy: &mut i32) -> bool {
    let pad = 1;
    let w = w + pad;
    let h = h + pad;
    if F.shelf_x + w > GLYPH_ATLAS {
        F.shelf_y += F.shelf_h;
        F.shelf_x = 0;
        F.shelf_h = 0;
    }
    if F.shelf_y + h > GLYPH_ATLAS || w > GLYPH_ATLAS {
        return false;
    }
    *ox = F.shelf_x;
    *oy = F.shelf_y;
    F.shelf_x += w;
    F.shelf_h = max(F.shelf_h, h);
    true
}

// A glyph's coverage drawn at (ox, oy): grayscale, or ClearType averaged down where only that is offered.
// b gets its pixel box, out its w*h bytes (cap at most). False when it is empty or does not fit.
pub unsafe fn glyph_alpha(f: *mut Face, glyph: u16, size_px: f32, ox: f32, oy: f32, b: &mut RECT, out: *mut u8, cap: i32) -> bool {
    let adv = 0.0f32;
    let off = DWRITE_GLYPH_OFFSET { advanceOffset: 0.0, ascenderOffset: 0.0 };
    let run = DWRITE_GLYPH_RUN {
        fontFace: ManuallyDrop::new((*f).face.clone()),
        fontEmSize: size_px,
        glyphCount: 1,
        glyphIndices: &glyph,
        glyphAdvances: &adv,
        glyphOffsets: &off,
        isSideways: false.into(),
        bidiLevel: 0,
    };
    let mut ana: Option<IDWriteGlyphRunAnalysis> = None;
    let mut tex = DWRITE_TEXTURE_CLEARTYPE_3x1;
    if F.v2 {
        ana = ff()
            .dw2
            .as_ref()
            .unwrap()
            .CreateGlyphRunAnalysis(
                &run,
                None,
                DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                DWRITE_MEASURING_MODE_NATURAL,
                DWRITE_GRID_FIT_MODE_DEFAULT,
                DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                ox,
                oy,
            )
            .ok();
        tex = DWRITE_TEXTURE_ALIASED_1x1;
    }
    if ana.is_none() {
        ana = ff()
            .dw
            .as_ref()
            .unwrap()
            .CreateGlyphRunAnalysis(&run, 1.0, None, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, DWRITE_MEASURING_MODE_NATURAL, ox, oy)
            .ok();
        tex = DWRITE_TEXTURE_CLEARTYPE_3x1;
    }
    let _ = ManuallyDrop::into_inner(run.fontFace);
    let Some(ana) = ana else { return false };
    let mut wb = ana.GetAlphaTextureBounds(tex).unwrap_or_default();
    if wb.right <= wb.left && tex == DWRITE_TEXTURE_ALIASED_1x1 {
        tex = DWRITE_TEXTURE_CLEARTYPE_3x1;
        wb = ana.GetAlphaTextureBounds(tex).unwrap_or_default();
    }
    *b = RECT { left: wb.left, top: wb.top, right: wb.right, bottom: wb.bottom };
    let n = (b.right - b.left) * (b.bottom - b.top);
    let bpp = if tex == DWRITE_TEXTURE_CLEARTYPE_3x1 { 3 } else { 1 };
    let ok = b.right > b.left
        && b.bottom > b.top
        && n * bpp <= cap
        && ana.CreateAlphaTexture(tex, &wb, core::slice::from_raw_parts_mut(out, (n * bpp) as usize)).is_ok();
    if ok && bpp == 3 {
        for i in 0..n as usize {
            // in place: i <= 3i
            *out.add(i) = ((*out.add(i * 3) as i32 + *out.add(i * 3 + 1) as i32 + *out.add(i * 3 + 2) as i32) / 3) as u8;
        }
    }
    ok
}

static mut GR_TMP: [u8; 512 * 512 * 3] = [0; 512 * 512 * 3];

unsafe fn glyph_rasterize(out: *mut GlyphRaster, f: *mut Face, glyph: u16, size_px: f32, phase: i32) {
    (*out).w = 0;
    (*out).h = 0;
    let tmp = (&raw mut GR_TMP) as *mut u8;
    let mut b = RECT { left: 0, top: 0, right: 0, bottom: 0 };
    if !glyph_alpha(f, glyph, size_px, phase as f32 * 0.25, 0.0, &mut b, tmp, (512 * 512 * 3) as i32) {
        return;
    }
    let w = b.right - b.left;
    let h = b.bottom - b.top;
    if w > 512 || h > 512 {
        return;
    }
    let (mut ax, mut ay) = (0, 0);
    if !atlas_alloc(w, h, &mut ax, &mut ay) {
        F.overflow = true;
        return;
    }
    for y in 0..h {
        memcpy(R.glyph_cpu.add((ay + y) as usize * GLYPH_ATLAS as usize + ax as usize), tmp.add((y * w) as usize), w as usize);
    }
    r_glyph_dirty(ax, ay, w, h);
    (*out).x = ax as u16;
    (*out).y = ay as u16;
    (*out).w = w as u16;
    (*out).h = h as u16;
    (*out).ox = b.left as i16;
    (*out).oy = b.top as i16;
}

unsafe fn glyph_raster(face_id: i32, glyph: u16, size_px: f32, phase: i32) -> *mut GlyphRaster {
    let size_q = (size_px * 16.0 + 0.5) as u32;
    let key = ((face_id as u64) << 48) | (((size_q & 0xFFFF) as u64) << 32) | ((glyph as u64) << 16) | (phase & 3) as u64 | 0x8000;
    let h = hash_u64(key) as u32 as usize & (GRAST_SIZE - 1);
    let grast = &mut *(&raw mut g_grast);
    for probe in 0..GRAST_SIZE {
        let e = &mut grast[(h + probe) & (GRAST_SIZE - 1)] as *mut GlyphRaster;
        if (*e).key == key {
            return e;
        }
        if (*e).key == 0 {
            if g_grast_count > (GRAST_SIZE * 3 / 4) as i32 {
                F.overflow = true;
                return null_mut();
            }
            let f = face_get(face_id);
            if f.is_null() {
                return null_mut();
            }
            glyph_rasterize(e, f, glyph, size_px, phase);
            if F.overflow {
                return null_mut();
            }
            (*e).key = key;
            g_grast_count += 1;
            return e;
        }
    }
    F.overflow = true;
    null_mut()
}

pub unsafe fn utf16_next(s: *const WCHAR, len: i32, i: &mut i32) -> u32 {
    let mut c = *s.add(*i as usize) as u32;
    *i += 1;
    if (0xD800..=0xDBFF).contains(&c) && *i < len {
        let d = *s.add(*i as usize) as u32;
        if (0xDC00..=0xDFFF).contains(&d) {
            *i += 1;
            c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
        }
    }
    c
}

pub unsafe fn font_cap_height(font: i32, size: f32) -> f32 {
    let f = &ff().faces[font as usize];
    f.cap_height / f.upem * size
}
pub unsafe fn font_ascent(font: i32, size: f32) -> f32 {
    let f = &ff().faces[font as usize];
    f.ascent / f.upem * size
}
pub unsafe fn font_descent(font: i32, size: f32) -> f32 {
    let f = &ff().faces[font as usize];
    f.descent / f.upem * size
}

// The baseline that centers capitals on cy.
pub unsafe fn text_baseline(font: i32, size: f32, cy: f32) -> f32 {
    (cy + font_cap_height(font, size) * 0.5 + 0.5).floor()
}

pub unsafe fn text_width(font: i32, size: f32, s: *const WCHAR, len: i32) -> f32 {
    let len = if len < 0 { wlen(s) } else { len };
    let mut w = 0.0;
    let mut i = 0;
    while i < len {
        let mut cp = utf16_next(s, len, &mut i);
        if cp == '\t' as u32 {
            cp = ' ' as u32;
        }
        w += (*glyph_map(font, cp)).adv * size;
    }
    w
}

pub unsafe fn text_offsets(font: i32, size: f32, s: *const WCHAR, len: i32, out: *mut f32) {
    let mut w = 0.0;
    let mut i = 0;
    while i < len {
        let start = i;
        let mut cp = utf16_next(s, len, &mut i);
        if cp == '\t' as u32 {
            cp = ' ' as u32;
        }
        for k in start..i {
            *out.add(k as usize) = w;
        }
        w += (*glyph_map(font, cp)).adv * size;
    }
    *out.add(len as usize) = w;
}

// The character boundary nearest to x (offs from text_offsets).
pub unsafe fn offset_at(offs: *const f32, len: i32, x: f32) -> i32 {
    let mut best = 0;
    for i in 0..=len {
        if (*offs.add(i as usize) - x).abs() < (*offs.add(best as usize) - x).abs() {
            best = i;
        }
    }
    best
}

// Keeps a one-line field's caret in view: scroll is how far its text is shifted left, width the room it has.
pub unsafe fn field_scroll(scroll: &mut f32, offs: *const f32, caret: i32, len: i32, width: f32) {
    let cx = *offs.add(caret as usize);
    if cx - *scroll > width {
        *scroll = cx - width;
    }
    if cx - *scroll < 0.0 {
        *scroll = cx;
    }
    let end = *offs.add(len as usize);
    if end - *scroll < width {
        *scroll = max(0.0, end - width);
    }
}

pub unsafe fn text_draw(font: i32, size: f32, x: f32, baseline: f32, s: *const WCHAR, len: i32, color: u32) -> f32 {
    let len = if len < 0 { wlen(s) } else { len };
    let mut pen = x;
    let by = (baseline + 0.5).floor();
    let inv = 1.0 / GLYPH_ATLAS as f32;
    let mut i = 0;
    while i < len {
        let mut cp = utf16_next(s, len, &mut i);
        if cp == '\t' as u32 {
            cp = ' ' as u32;
        }
        let g = glyph_map(font, cp);
        if cp != ' ' as u32 {
            let mut px = pen.floor();
            let mut phase = ((pen - px) * 4.0 + 0.5) as i32;
            if phase == 4 {
                phase = 0;
                px += 1.0;
            }
            let gr = glyph_raster((*g).face as i32, (*g).glyph, size, phase);
            if !gr.is_null() && (*gr).w != 0 {
                let gr = &*gr;
                r_glyph(
                    px + gr.ox as f32,
                    by + gr.oy as f32,
                    gr.w as f32,
                    gr.h as f32,
                    gr.x as f32 * inv,
                    gr.y as f32 * inv,
                    (gr.x + gr.w) as f32 * inv,
                    (gr.y + gr.h) as f32 * inv,
                    color,
                );
            }
        }
        pen += (*g).adv * size;
    }
    pen
}

static ELL: [WCHAR; 2] = [0x2026, 0];

pub unsafe fn text_draw_fit(font: i32, size: f32, x: f32, baseline: f32, max_w: f32, s: *const WCHAR, len: i32, color: u32, elide_start: bool) -> f32 {
    let len = if len < 0 { wlen(s) } else { len };
    if max_w <= 0.0 || len <= 0 {
        return 0.0;
    }
    let full = text_width(font, size, s, len);
    if full <= max_w {
        text_draw(font, size, x, baseline, s, len, color);
        return full;
    }
    let ell = ELL.as_ptr();
    let ew = text_width(font, size, ell, 1);
    let avail = max_w - ew;
    if avail <= 0.0 {
        return 0.0;
    }
    if !elide_start {
        let mut w = 0.0;
        let mut cut = 0;
        let mut i = 0;
        while i < len {
            let cp = utf16_next(s, len, &mut i);
            let a = (*glyph_map(font, if cp == '\t' as u32 { ' ' as u32 } else { cp })).adv * size;
            if w + a > avail {
                break;
            }
            w += a;
            cut = i;
        }
        while cut > 0 && *s.add(cut as usize - 1) == ' ' as u16 {
            cut -= 1;
            w -= (*glyph_map(font, ' ' as u32)).adv * size;
        }
        let end = text_draw(font, size, x, baseline, s, cut, color);
        text_draw(font, size, end, baseline, ell, 1, color);
        w + ew
    } else {
        let mut w = 0.0;
        let mut cut = len;
        while cut > 0 {
            let mut i = cut - 1;
            let si = *s.add(i as usize);
            if i > 0 && (0xDC00..=0xDFFF).contains(&si) && (0xD800..=0xDBFF).contains(&*s.add(i as usize - 1)) {
                i -= 1;
            }
            let mut j = i;
            let cp = utf16_next(s, len, &mut j);
            let a = (*glyph_map(font, if cp == '\t' as u32 { ' ' as u32 } else { cp })).adv * size;
            if w + a > avail {
                break;
            }
            w += a;
            cut = i;
        }
        let end = text_draw(font, size, x, baseline, ell, 1, color);
        text_draw(font, size, end, baseline, s.add(cut as usize), len - cut, color);
        w + ew
    }
}

pub unsafe fn text_draw_icon(codepoint: u32, size: f32, cx: f32, cy: f32, color: u32) {
    let s: [WCHAR; 2] = [codepoint as u16, 0];
    let w = text_width(FONT_ICON, size, s.as_ptr(), 1);
    let asc = font_ascent(FONT_ICON, size);
    let desc = font_descent(FONT_ICON, size);
    let baseline = cy + (asc - desc) * 0.5;
    text_draw(FONT_ICON, size, (cx - w * 0.5 + 0.5).floor(), baseline, s.as_ptr(), 1, color);
}

// Starts a frame and draws it; when the glyph atlas ran full meanwhile, clears it and draws the frame again.
pub unsafe fn r_draw(draw: unsafe fn()) {
    r_begin();
    draw();
    if !F.overflow {
        return;
    }
    font_reset_atlas();
    r_begin();
    draw();
    F.overflow = false;
}

pub unsafe fn atlas_alloc_pub(w: i32, h: i32, ox: &mut i32, oy: &mut i32) -> bool {
    atlas_alloc(w, h, ox, oy)
}

pub unsafe fn face_get_pub(face_id: i32) -> *mut Face {
    face_get(face_id)
}
