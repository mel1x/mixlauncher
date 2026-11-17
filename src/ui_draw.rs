// Drawing the launcher: the rows, the query field, the footer and the frame around them.

use crate::*;

pub unsafe fn draw_placeholder_icon(x: f32, y: f32, sz: f32, name: *const WCHAR) {
    r_rect(x, y, sz, sz, TH.tile, SR(LS().tile_r));
    if !name.is_null() && *name != 0 {
        let c: [WCHAR; 2] = [towupper(*name), 0];
        let fs = sz * 0.5;
        let w = text_width(FONT_TEXT_SEMIBOLD, fs, c.as_ptr(), 1);
        text_draw(FONT_TEXT_SEMIBOLD, fs, (x + (sz - w) * 0.5).floor(), y + sz * 0.5 + font_cap_height(FONT_TEXT_SEMIBOLD, fs) * 0.5, c.as_ptr(), 1, TH.dim);
    }
}

#[inline]
pub unsafe fn APP_ICON_KEY(a: *const App) -> u64 {
    (*a).id_hash ^ 0xA11
}

// The app's icon; until it loads a blank tile, its first letter if it has none.
pub unsafe fn draw_app_icon(a: *const App, x: f32, y: f32, sz: f32) {
    let mut uv = [0.0f32; 4];
    if icon_get(APP_ICON_KEY(a), ICON_KIND_APP, (*a).id, &mut uv) {
        r_icon(x, y, sz, sz, uv[0], uv[1], uv[2], uv[3], 1.0);
    } else {
        draw_placeholder_icon(x, y, sz, if icon_failed(APP_ICON_KEY(a)) { (*a).name } else { null() });
    }
}

pub unsafe fn draw_file_icon(f: *const FileItem, x: f32, y: f32, sz: f32) {
    let mut uv = [0.0f32; 4];
    if icon_get((*f).icon_key, ICON_KIND_FILE, (*f).full, &mut uv) {
        r_icon(x, y, sz, sz, uv[0], uv[1], uv[2], uv[3], 1.0);
    } else if icon_failed((*f).icon_key) {
        draw_placeholder_icon(x, y, sz, (*f).name);
    }
}

pub unsafe fn row_note(r: *const Row, col: &mut u32) -> *const WCHAR {
    *col = TH.faint;
    if r.is_null() || (*r).kind != ROW_APP {
        return null();
    }
    if (*(*r).app).kind != APP_CMD {
        return tr!("Приложение", "Application");
    }
    if U.armed == (*r).app {
        *col = TH.danger;
        return tr!("Нажмите Enter ещё раз для подтверждения", "Press Enter again to confirm");
    }
    tr!("Команда", "Command")
}

// The capsule style names what Enter does at the right of the selected row, with the key.
pub unsafe fn row_action(r: *const Row, col: &mut u32) -> *const WCHAR {
    let note = row_note(r, col);
    if *col == TH.danger {
        return note;
    }
    *col = TH.dim;
    if (*r).kind == ROW_APP && (*(*r).app).kind == APP_CMD { tr!("Выполнить", "Run") } else { tr!("Открыть", "Open") }
}

const CAP_KEY_W: f32 = 22.0;

unsafe fn action_width(r: *const Row) -> f32 {
    let mut col = 0;
    text_width(FONT_TEXT, S(12.5), row_action(r, &mut col), -1) + SR(8.0 + CAP_KEY_W)
}

// The action ending at xr, centered on cy: its label and an Enter keycap.
unsafe fn draw_action(xr: f32, cy: f32, s: *const WCHAR, col: u32) {
    let kw = SR(CAP_KEY_W);
    let kh = SR(20.0);
    let kx = xr - kw;
    let ky = (cy - kh * 0.5).floor();
    let fs = S(12.5);
    r_rect(kx, ky, kw, kh, TH.key_bg, SR(LS().key_r));
    r_border(kx, ky, kw, kh, TH.key_border, SR(LS().key_r));
    text_draw_icon(0xE751, S(11.0), kx + kw * 0.5, cy, TH.dim);
    let w = text_width(FONT_TEXT, fs, s, -1);
    text_draw(FONT_TEXT, fs, (kx - SR(8.0) - w).floor(), text_baseline(FONT_TEXT, fs, cy), s, -1, col);
}

// How far the highlight is from a row at y, 0..1: the row's own note fades out as it comes (the selected row's
// note is drawn over the highlight, ui_draw).
unsafe fn note_away(y: f32, h: f32) -> f32 {
    if row_selectable(U.sel) { clamp((U.list_y0 - (U.scroll + 0.5).floor() + U.hl_y - y).abs() / h, 0.0, 1.0) } else { 1.0 }
}

pub unsafe fn hl_prepare() {
    U.hq_len = if g_cfg.match_highlight { query_norm(U.hq.as_mut_ptr()) } else { 0 };
    U.hq_has_alt = U.hq_len != 0 && layout_convert(U.hq.as_ptr(), U.hq_len, U.hq_alt.as_mut_ptr());
}

static mut DN_MASK: [u8; 512] = [0; 512];
static mut DN_WSB: [u8; 512] = [0; 512];
static mut DN_NB: [WCHAR; 512] = [0; 512];

// The name with the query matches in bold and the rest a little dimmer; plain (and elided) when it does not fit.
pub unsafe fn draw_name(fs: f32, x: f32, base: f32, max_w: f32, s: *const WCHAR, len: i32, norm: *const WCHAR, ws: *const u8, col: u32) -> f32 {
    let mask = (&raw mut DN_MASK) as *mut u8;
    let len = if len < 0 { wlen(s) } else { len };
    let mut hl = U.hq_len != 0 && len > 0 && len < 512;
    let mut norm = norm;
    let mut ws = ws;
    if hl {
        if norm.is_null() || ws.is_null() {
            norm_str((&raw mut DN_NB) as *mut WCHAR, s, len);
            word_starts(s, len, (&raw mut DN_WSB) as *mut u8);
            norm = (&raw const DN_NB) as *const WCHAR;
            ws = (&raw const DN_WSB) as *const u8;
        }
        let mut k = match_mask(norm, len, ws, U.hq.as_ptr(), U.hq_len, mask);
        if k == 0 && U.hq_has_alt {
            k = match_mask(norm, len, ws, U.hq_alt.as_ptr(), U.hq_len, mask);
        }
        hl = k > 0;
    }
    if hl {
        let mut w = 0.0;
        let mut i = 0;
        while i < len {
            let mut j = i;
            while j < len && *mask.add(j as usize) == *mask.add(i as usize) {
                j += 1;
            }
            w += text_width(if *mask.add(i as usize) != 0 { FONT_TEXT_SEMIBOLD } else { FONT_TEXT }, fs, s.add(i as usize), j - i);
            i = j;
        }
        hl = w <= max_w;
    }
    if !hl {
        return text_draw_fit(FONT_TEXT, fs, x, base, max_w, s, len, col, false);
    }
    let rest = color_mix(TH.dim, col, 0.6);
    let mut pen = x;
    let mut i = 0;
    while i < len {
        let mut j = i;
        while j < len && *mask.add(j as usize) == *mask.add(i as usize) {
            j += 1;
        }
        let m = *mask.add(i as usize) != 0;
        pen = text_draw(if m { FONT_TEXT_SEMIBOLD } else { FONT_TEXT }, fs, pen, base, s.add(i as usize), j - i, if m { col } else { rest });
        i = j;
    }
    pen - x
}

// Placeholders for files Everything counted but has not sent yet: pulsing bars, only where they are seen.
unsafe fn draw_virtual_rows(r: *const Row, y: f32) {
    let ls = LS();
    let r = &*r;
    let rh = SR(ROW_H());
    let isz = ui_icon_px() as f32;
    let x0 = r.x;
    let x1 = r.x + r.w;
    let pulse = 0.75 + 0.25 * ((time_now() % 100.0) as f32 * 5.0).sin();
    let k0 = max(0.0, ((U.list_y0 - y) / rh).floor()) as i32;
    let mut k1 = ((U.list_y1 - y) / rh).ceil() as i32;
    k1 = min(k1, (r.h / rh + 0.5) as i32);
    for k in k0..k1 {
        let ry = y + rh * k as f32;
        let rcy = ry + rh * 0.5;
        let ix = x0 + SR(12.0);
        let iy = (rcy - isz * 0.5 + 0.5).floor();
        let c = color_alpha(TH.tile, pulse);
        r_rect(ix, iy, isz, isz, c, SR(ls.tile_r));
        let hsh = ((k + (r.y / rh) as i32) as u32).wrapping_mul(2654435761); // lengths that vary from row to row
        let w1 = SR(120.0) + (hsh % 160) as f32 * U.s;
        let w2 = SR(90.0) + ((hsh >> 8) % 120) as f32 * U.s;
        r_rect(ix + isz + SR(12.0), (rcy - SR(5.0)).floor(), w1, SR(10.0), c, SR(5.0));
        r_rect(x1 - SR(12.0) - w2, (rcy - SR(4.0)).floor(), w2, SR(8.0), color_alpha(c, 0.6), SR(4.0));
    }
}

unsafe fn draw_row(rp: *const Row, y: f32, selected: bool) {
    let t = &*(&raw const TH);
    let ls = LS();
    let r = &*rp;
    let x0 = r.x;
    let x1 = r.x + r.w;
    if r.kind == ROW_HEADER {
        if st_search() {
            text_draw(FONT_TEXT_SEMIBOLD, S(14.0), x0 + SR(8.0), y + r.h - SR(10.0), r.text, -1, t.text);
        } else {
            text_draw(FONT_TEXT_SEMIBOLD, S(11.5), x0 + SR(12.0), y + r.h - SR(10.0), r.text, -1, color_mix(t.faint, t.dim, 0.6));
        }
        return;
    }
    if r.kind == ROW_VIRTUAL {
        draw_virtual_rows(rp, y);
        return;
    }
    let cy = y + r.h * 0.5;
    let fs = S(ls.name_fs);
    let base = text_baseline(FONT_TEXT, fs, cy);
    if r.kind == ROW_INFO {
        if U.row_count == 1 {
            // centered in the row: the Windows 10 search pane's results column, not the whole window
            let lcy = U.list_y0 + list_height() * 0.5 - SR(6.0);
            let tw = text_width(FONT_TEXT, fs, r.text, -1);
            let cx = (x0 + x1) * 0.5;
            let tx = max(x0 + SR(16.0), cx - tw * 0.5).floor();
            text_draw_icon(0xE721, S(26.0), cx, lcy - SR(20.0), t.faint);
            text_draw_fit(FONT_TEXT, fs, tx, (lcy + SR(22.0) + 0.5).floor(), x1 - SR(16.0) - tx, r.text, -1, t.dim, false);
            return;
        }
        text_draw_fit(FONT_TEXT, fs, x0 + SR(12.0), base, x1 - x0 - SR(24.0), r.text, -1, t.dim, false);
        return;
    }
    let mut isz = ui_icon_px() as f32;
    let mut ix = x0 + SR(12.0);
    let mut iy = (cy - isz * 0.5 + 0.5).floor();
    let mut tx = ix + isz + SR(ls.icon_gap);
    let right = x1 - SR(12.0);
    let sub_fs = S(12.5);
    let sub_base = text_baseline(FONT_TEXT, sub_fs, cy);
    // the Windows 11 search: every icon on a square plate, the name alone (the pane on the right tells the rest)
    let plate = st_search();
    if plate {
        let pz = SR(40.0);
        let px = x0 + SR(8.0);
        let py = (cy - pz * 0.5 + 0.5).floor();
        let armed = r.kind == ROW_APP && U.armed == r.app;
        r_rect(
            px,
            py,
            pz,
            pz,
            if armed {
                color_alpha(t.danger, 0.16)
            } else if t.dark {
                RGBA(255, 255, 255, 14)
            } else {
                RGBA(0, 0, 0, 10)
            },
            SR(4.0),
        );
        r_border(px, py, pz, pz, if t.dark { RGBA(255, 255, 255, 12) } else { RGBA(0, 0, 0, 14) }, SR(4.0));
        isz = SR(24.0);
        ix = (px + (pz - isz) * 0.5).floor();
        iy = (py + (pz - isz) * 0.5).floor();
        tx = px + pz + SR(12.0);
    }

    if r.kind == ROW_FILE {
        let f = &*r.file;
        draw_file_icon(f, ix, iy, isz);
        let avail = right - tx;
        let gap = SR(16.0);
        if plate {
            // the name, and where it is under it
            draw_name(fs, tx, (cy - SR(3.0) + 0.5).floor(), avail, f.name, -1, null(), null(), t.text);
            text_draw_fit(FONT_TEXT, sub_fs, tx, (cy + SR(15.0) + 0.5).floor(), avail, f.dir, -1, t.faint, true);
            return;
        }
        // the folder at the right, as much of it as fits; the name gets at least half when both are long
        let nw = text_width(FONT_TEXT, fs, f.name, -1);
        let dw = text_width(FONT_TEXT, sub_fs, f.dir, -1);
        let mut name_max = if nw + gap + dw <= avail { nw } else { max(avail * 0.55, avail - dw - gap) };
        if cap_on() && selected {
            name_max = min(name_max, avail - action_width(rp) - gap); // the action takes the folder's place
        }
        let drawn = draw_name(fs, tx, base, name_max, f.name, -1, null(), null(), t.text);
        let dir_max = avail - drawn - gap;
        let dir_col = if cap_on() { color_alpha(t.faint, note_away(y, r.h)) } else { t.faint };
        if dir_max > SR(40.0) && *f.dir != 0 {
            text_draw_fit(FONT_TEXT, sub_fs, right - min(dw, dir_max), sub_base, dir_max, f.dir, -1, dir_col, true);
        }
        return;
    }
    let a = &*r.app;
    let armed = U.armed == r.app;
    let mut sub_col = t.faint;
    let sub = row_note(rp, &mut sub_col);
    if a.kind == APP_CMD && plate {
        text_draw_icon(a.glyph as u32, S(15.0), ix + isz * 0.5, iy + isz * 0.5, if armed { t.danger } else { t.text });
    } else if a.kind == APP_CMD {
        r_rect(ix, iy, isz, isz, if armed { color_alpha(t.danger, 0.16) } else { t.tile }, SR(ls.tile_r));
        r_border(ix, iy, isz, isz, t.key_border, SR(ls.tile_r));
        text_draw_icon(a.glyph as u32, min(S(14.0), isz * 0.52), ix + isz * 0.5, iy + isz * 0.5, if armed { t.danger } else { t.text });
    } else {
        draw_app_icon(a, ix, iy, isz);
    }
    let sw = text_width(FONT_TEXT, sub_fs, sub, -1);
    let avail = right - tx;
    let show_sub = !plate && ((selected && !w10_on()) || a.kind == APP_CMD || ls.all_notes);
    let note_w = if cap_on() && selected { max(sw, action_width(rp)) } else { sw };
    draw_name(fs, tx, base, if show_sub { avail - note_w - SR(16.0) } else { avail }, a.name, a.len, a.norm, a.ws, t.text);
    if (ls.all_notes && a.kind != APP_CMD) || (a.kind == APP_CMD && !plate) {
        let away = note_away(y, r.h);
        text_draw(FONT_TEXT, sub_fs, right - sw, sub_base, sub, -1, color_alpha(if a.kind == APP_CMD { sub_col } else { t.faint }, away));
    }
}

pub unsafe fn keycap_width(k: *const WCHAR) -> f32 {
    max(text_width(FONT_TEXT, S(11.5), k, -1) + SR(6.0) * 2.0, SR(20.0))
}

// A key chip (footer hints, dialog buttons) at x, centered on cy.
pub unsafe fn draw_keycap(x: f32, cy: f32, k: *const WCHAR, col: u32) {
    let kfs = S(11.5);
    let kh = SR(20.0);
    let w = keycap_width(k);
    let tw = text_width(FONT_TEXT, kfs, k, -1);
    r_rect(x, (cy - kh * 0.5).floor(), w, kh, TH.key_bg, SR(LS().key_r));
    text_draw(FONT_TEXT, kfs, (x + (w - tw) * 0.5 + 0.5).floor(), text_baseline(FONT_TEXT, kfs, cy), k, -1, col);
}

// A footer hint ending at xr: the label, then its keys. Returns where the next one (to the left) ends.
pub unsafe fn draw_hint(xr: f32, cy: f32, label: *const WCHAR, keys: &[*const WCHAR], label_col: u32) -> f32 {
    let lfs = S(12.5);
    let mut xr = xr;
    for i in (0..keys.len()).rev() {
        let kx = (xr - keycap_width(keys[i])).floor();
        draw_keycap(kx, cy, keys[i], TH.dim);
        xr = kx - SR(4.0);
    }
    let lw = text_width(FONT_TEXT, lfs, label, -1);
    xr -= SR(4.0);
    text_draw(FONT_TEXT, lfs, (xr - lw).floor(), text_baseline(FONT_TEXT, lfs, cy), label, -1, label_col);
    xr - lw - SR(18.0)
}

pub unsafe fn update_chip_width() -> f32 {
    let mut ub = [0u16; 64];
    (SR(31.0) + text_width(FONT_TEXT, S(12.5), upd_label(ub.as_mut_ptr(), 64), -1) + SR(10.0)).floor()
}

// The "Update to ..." chip (with the download's progress) at x, h high, centered on cy. It is the update button.
pub unsafe fn draw_update_chip(x: f32, cy: f32, h: f32, rad: f32) {
    let t = &*(&raw const TH);
    let mut ub = [0u16; 64];
    let label = upd_label(ub.as_mut_ptr(), 64);
    let st = upd_state();
    let w = update_chip_width();
    let y = (cy - h * 0.5).floor();
    let fs = S(12.5);
    let hov = if st == UPD_DOWNLOADING || st == UPD_INSTALLING { 0.0 } else { U.update_hover_t };
    r_rect(x, y, w, h, color_alpha(t.accent, 0.16 + 0.10 * hov), rad);
    let pw = if st == UPD_DOWNLOADING { (w * UPD.progress.load(core::sync::atomic::Ordering::SeqCst) as f32 / 1000.0).floor() } else { 0.0 };
    if pw > 0.0 {
        r_set_clip(x, y, x + pw, y + h);
        r_rect(x, y, w, h, color_alpha(t.accent, 0.22), rad);
        r_clip_all();
    }
    let col = if st == UPD_ERROR { t.danger } else { t.text };
    text_draw_icon(if st == UPD_ERROR { 0xE72C } else { 0xE896 }, S(14.0), x + SR(16.0), cy, col);
    text_draw(FONT_TEXT, fs, x + SR(31.0), text_baseline(FONT_TEXT, fs, cy), label, -1, col);
    box_set(&mut U.update_btn, x, y, w, h);
}

unsafe fn draw_footer(y0: f32) {
    let t = &*(&raw const TH);
    let cy = y0 + U.footer_h * 0.5;
    let m = if cap_on() { SR(CAP_MARGIN) } else { 0.0 }; // the capsule's panel stops short of the window's edges
    let mut xr = U.W as f32 - m - SR(14.0);
    let k_enter = [w!("Enter")];
    let k_ctrl_enter = [w!("Ctrl"), w!("Enter")];
    let k_admin = [w!("Ctrl"), w!("Shift"), w!("Enter")];
    let k_copy = [w!("Ctrl"), w!("C")];
    let k_esc = [w!("Esc")];
    if LS().footer_tint {
        r_rect(0.0, y0, U.W as f32, U.footer_h, if t.dark { RGBA(255, 255, 255, 10) } else { RGBA(0, 0, 0, 8) }, 0.0);
    }
    let label = tr!("Настройки", "Settings");
    let lfs = S(12.5);
    let lw = text_width(FONT_TEXT, lfs, label, -1);
    let bx = m + SR(8.0);
    let bh = SR(28.0);
    let by = (cy - bh * 0.5).floor();
    let bw = (SR(31.0) + lw + SR(10.0)).floor();
    let hov = U.settings_hover_t;
    r_rect(bx, by, bw, bh, color_alpha(t.sel, hov), SR(LS().key_r + 1.0));
    let col = color_mix(t.dim, t.text, hov);
    text_draw_icon(0xE713, S(14.0), bx + SR(16.0), cy, col);
    text_draw(FONT_TEXT, lfs, bx + SR(31.0), text_baseline(FONT_TEXT, lfs, cy), label, -1, col);
    box_set(&mut U.settings_btn, bx, by, bw, bh);
    let mut min_x = m + SR(150.0);
    U.update_btn = [0.0; 4];
    if upd_visible() {
        let ux = bx + bw + SR(6.0);
        draw_update_chip(ux, cy, bh, SR(LS().key_r + 1.0));
        min_x = max(min_x, U.update_btn[2] + SR(18.0));
    }
    // the hints that fit, from the right: what Enter does, then Ctrl+Enter, Ctrl+Shift+Enter or Ctrl+C
    let r = sel_row();
    if r.is_null() {
        draw_hint(xr, cy, tr!("Закрыть", "Close"), &k_esc, t.dim);
    } else if (*r).kind == ROW_APP && (*(*r).app).kind == APP_CMD {
        if cap_on() {
            return;
        }
        let armed = U.armed == (*r).app;
        draw_hint(xr, cy, if armed { tr!("Подтвердить", "Confirm") } else { tr!("Выполнить", "Run") }, &k_enter, if armed { t.danger } else { t.text });
    } else {
        if !cap_on() {
            xr = draw_hint(xr, cy, tr!("Открыть", "Open"), &k_enter, t.text); // the capsule shows it on the row
        }
        if !row_path(r).is_null() && xr - SR(230.0) > min_x {
            xr = draw_hint(xr, cy, tr!("Показать в папке", "Show in folder"), &k_ctrl_enter, t.dim);
        }
        if (*r).kind == ROW_APP {
            if xr - SR(250.0) > min_x {
                draw_hint(xr, cy, tr!("Администратор", "Run as admin"), &k_admin, t.dim);
            }
        } else if xr - SR(200.0) > min_x {
            draw_hint(xr, cy, tr!("Копировать путь", "Copy path"), &k_copy, t.dim);
        }
    }
}

// Where the query field sits: a band across the top, or across the bottom (Windows 10, like the taskbar box).
pub unsafe fn search_band(y0: &mut f32, y1: &mut f32) {
    *y0 = if cap_on() { SR(CAP_MARGIN) } else { 0.0 };
    *y1 = if w10_on() && !q_active() { 0.0 } else { *y0 + U.search_h }; // Windows 10 shows the query only once typing starts
}

// The query field: text from x0 to x1, centered on cy. The Start style puts it in a rounded box.
pub unsafe fn search_field(x0: &mut f32, x1: &mut f32, cy: &mut f32) {
    if w10_on() {
        *x0 = SR(48.0);
        *x1 = U.W as f32 - SR(16.0) - if upd_visible() { SR(200.0) } else { 0.0 };
        *cy = (U.search_h * 0.5).floor();
        return;
    }
    if st_on() {
        let m = SR(32.0);
        *x0 = m + SR(42.0);
        *x1 = U.W as f32 - m - SR(16.0);
        *cy = (U.search_h * 0.5 + SR(4.0)).floor();
        return;
    }
    if cap_on() {
        let m = SR(CAP_MARGIN);
        *x0 = m + SR(52.0);
        *x1 = U.W as f32 - m - SR(28.0);
        *cy = (m + U.search_h * 0.5).floor();
        return;
    }
    *x0 = SR(LS().inset + 42.0);
    *x1 = U.W as f32 - SR(20.0);
    *cy = U.search_h * 0.5;
}

static mut DS_OFFS: [f32; Q_MAX + 2] = [0.0; Q_MAX + 2];

pub unsafe fn draw_search() {
    let t = &*(&raw const TH);
    let ls = LS();
    let (mut fx0, mut fx1, mut cy) = (0.0, 0.0, 0.0);
    search_field(&mut fx0, &mut fx1, &mut cy);
    text_draw_icon(0xE721, S(ls.search_icon), fx0 - SR(if cap_on() { 26.0 } else { 23.0 }), cy, if U.qlen != 0 { t.text } else { t.dim });
    let fs = S(ls.search_fs);
    let base = text_baseline(FONT_DISPLAY, fs, cy);
    let sel_top = (cy - fs * 0.62).floor();
    let sel_h = (fs * 1.24).floor();
    if U.qlen == 0 {
        text_draw_fit(FONT_DISPLAY, fs, fx0, base, fx1 - fx0, tr!("Поиск приложений и файлов", "Search apps and files"), -1, t.faint, false);
        if U.caret_on {
            r_rect(fx0, sel_top, max(1.0, S(1.5).floor()), sel_h, t.accent, SR(1.0));
        }
        return;
    }
    let offs = (&raw mut DS_OFFS) as *mut f32;
    text_offsets(FONT_DISPLAY, fs, U.q.as_ptr(), U.qlen, offs);
    field_scroll(&mut U.q_scroll, offs, U.caret, U.qlen, fx1 - fx0 - SR(4.0));
    let cx = *offs.add(U.caret as usize);
    let ox = fx0 - U.q_scroll.floor();
    let (mut by0, mut by1) = (0.0, 0.0);
    search_band(&mut by0, &mut by1);
    r_set_clip(fx0 - SR(2.0), by0, fx1, by1);
    if U.anchor != U.caret {
        let a = min(U.anchor, U.caret) as usize;
        let b = max(U.anchor, U.caret) as usize;
        r_rect((ox + *offs.add(a)).floor(), sel_top, (*offs.add(b) - *offs.add(a) + 0.5).floor(), sel_h, t.text_sel, SR(3.0));
    }
    text_draw(FONT_DISPLAY, fs, ox, base, U.q.as_ptr(), U.qlen, t.text);
    if U.caret_on {
        r_rect((ox + cx).floor(), sel_top, max(1.0, S(1.5).floor()), sel_h, t.accent, SR(1.0));
    }
    r_clip_all();
}

// A capsule style panel: a soft shadow, the glass or the solid fill, a hairline outline.
unsafe fn cap_panel(x: f32, y: f32, w: f32, h: f32, rad: f32, glass: bool) {
    let t = &*(&raw const TH);
    r_rect_ex(x, y + SR(4.0), w, h, if t.dark { RGBA(0, 0, 0, 110) } else { RGBA(0, 0, 0, 38) }, rad, 0.0, SR(12.0));
    if glass {
        r_backdrop(x, y, w, h, rad);
    }
    r_rect(x, y, w, h, if glass { t.bg } else { t.bg_solid }, rad);
    r_border(x, y, w, h, if t.dark { RGBA(255, 255, 255, 20) } else { RGBA(0, 0, 0, 18) }, rad);
}

// The capsule style's query field (round ends) and the results panel under it.
unsafe fn cap_boxes(search: &mut [f32; 4], panel: &mut [f32; 4]) {
    let m = SR(CAP_MARGIN);
    let w = U.W as f32 - m * 2.0;
    box_set(search, m, m, w, U.search_h);
    box_set(panel, m, U.list_y0, w, U.list_y1 + U.footer_h - U.list_y0);
}

unsafe fn cap_draw_chrome(glass: bool) {
    let mut sb = [0.0; 4];
    let mut pb = [0.0; 4];
    cap_boxes(&mut sb, &mut pb);
    cap_panel(sb[0], sb[1], sb[2] - sb[0], U.search_h, (U.search_h * 0.5).floor(), glass);
    cap_panel(pb[0], pb[1], pb[2] - pb[0], pb[3] - pb[1], SR(18.0), glass);
    draw_search();
}

// The see-through margin and gap around the capsule's panels.
pub unsafe fn cap_outside(x: i32, y: i32) -> bool {
    let mut sb = [0.0; 4];
    let mut pb = [0.0; 4];
    cap_boxes(&mut sb, &mut pb);
    !in_box(&sb, x as f32, y as f32) && !in_box(&pb, x as f32, y as f32)
}

pub unsafe fn ui_draw() {
    let t = &*(&raw const TH);
    icons_begin_frame();
    hl_prepare();
    // rounded as DWM rounds the window, so the zoom keeps the corners
    let glass = U.backdrop_ok && bd_ready();
    let rad = backdrop_radius();
    if glass && !cap_on() {
        r_backdrop(0.0, 0.0, U.W as f32, U.H as f32, rad);
    }
    // the Start styles tint the glass with their own fill (TH.veil) instead
    if !cap_on() {
        r_rect(
            0.0,
            0.0,
            U.W as f32,
            U.H as f32,
            if !glass {
                t.bg_solid
            } else if st_on() || w10_on() {
                0
            } else {
                t.bg
            },
            rad,
        );
    }
    if cap_on() {
        cap_draw_chrome(glass);
    } else if st_on() {
        st_draw_chrome();
    } else if w10_on() {
        w10_draw_chrome();
    } else {
        draw_search();
        r_rect(0.0, U.search_h, U.W as f32, 1.0, t.sep, 0.0);
    }

    r_set_clip(list_x0(), U.list_y0, list_x1(), U.list_y1);
    let off = U.list_y0 - (U.scroll + 0.5).floor();
    let now = time_now();
    let cascade = rows_cascading(now);
    let mut hl_op = 1.0;
    // Windows 10: no list highlight while a tile is pointed at, held or dragged
    if row_selectable(U.sel) && !(w10_on() && (U.tsel >= 0 || U.w10.press_tile >= 0 || !U.w10.tdrag.is_null())) {
        let mut k = 0;
        if cascade {
            for i in 0..U.sel {
                if row(i).y + row(i).h >= U.scroll {
                    k += 1;
                }
            }
        }
        hl_op = if cascade { row_in(k, now) } else { 1.0 };
        R.opacity = hl_op;
        let ls = LS();
        let hy = (off + U.hl_y + 0.5).floor();
        let hh = (U.hl_h + 0.5).floor();
        let hx = SR(ls.inset);
        let hw = U.W as f32 - hx * 2.0;
        let hr = SR(ls.sel_r);
        if st_on() || w10_on() {
            st_draw_highlight(off);
        } else if ls.sel == SEL_PILL {
            r_rect(hx, hy, hw, hh, t.sel, hr);
        } else if ls.sel == SEL_FILL {
            r_rect(hx, hy, hw, hh, if t.dark { RGBA(255, 255, 255, 32) } else { RGBA(0, 0, 0, 20) }, hr);
        } else {
            r_rect(hx, hy, hw, hh, t.sel, hr);
            let bh = (min(hh * 0.4 + U.hl_vy.abs() * 0.012, hh * 0.75) + 0.5).floor();
            let bw = SR(3.0);
            r_rect(hx + SR(2.0), (hy + (hh - bh) * 0.5 + 0.5).floor(), bw, bh, t.accent, bw * 0.5);
        }
        R.opacity = 1.0;
    }
    let top = U.scroll;
    let bottom = U.scroll + list_height();
    let mut k = 0;
    let mut i = row_first_below(top);
    while i < U.row_count {
        let r = U.rows.add(i as usize);
        if (*r).y > bottom {
            break;
        }
        let mut dy = 0.0;
        if cascade {
            let e = row_in(k, now);
            k += 1;
            R.opacity = e;
            dy = ((1.0 - e) * S(8.0) + 0.5).floor();
        }
        if (*r).cell != 0 {
            st_draw_cell(r, off + (*r).y + dy, i == U.sel);
        } else {
            draw_row(r, off + (*r).y + dy, i == U.sel);
        }
        i += 1;
    }
    // the selected row's note rides on the highlight
    let mut note_col = 0u32;
    let nr_row = sel_row();
    let quiet = !nr_row.is_null()
        && ((*nr_row).cell != 0 || st_search() || (w10_on() && (*nr_row).kind == ROW_APP && (*(*nr_row).app).kind != APP_CMD)); // Windows 10 lists no "Application"
    let note = if quiet {
        null()
    } else if cap_on() && !nr_row.is_null() {
        row_action(nr_row, &mut note_col)
    } else {
        row_note(nr_row, &mut note_col)
    };
    if !note.is_null() {
        U.sub_text = note;
        U.sub_col = note_col;
    }
    if !U.sub_text.is_null() && U.sub_t > 0.0 {
        let nfs = S(12.5);
        let cy = off + U.hl_y + U.hl_h * 0.5;
        let nw = text_width(FONT_TEXT, nfs, U.sub_text, -1);
        R.opacity = hl_op * U.sub_t;
        let nr = if st_on() || w10_on() { U.hl_x + U.hl_w - SR(12.0) } else { U.W as f32 - SR(20.0) };
        if cap_on() {
            draw_action(U.W as f32 - SR(LS().inset + 8.0), cy, U.sub_text, U.sub_col);
        } else {
            text_draw(FONT_TEXT, nfs, (nr - nw).floor(), text_baseline(FONT_TEXT, nfs, cy), U.sub_text, -1, U.sub_col);
        }
    }
    R.opacity = 1.0;
    r_clip_all();

    let ms = max_scroll();
    if ms > 0.0 && U.sb_t > 0.0 {
        let lh = list_height() - SR(8.0);
        let bar_h = max(SR(24.0), lh * lh / U.content_h);
        let by = U.list_y0 + SR(4.0) + (lh - bar_h) * clamp(U.scroll / ms, 0.0, 1.0);
        let bw = if U.sb_hot { SR(5.0) } else { SR(3.0) };
        r_rect(list_x1() - SR(3.0) - bw, by.floor(), bw, bar_h.floor(), color_alpha(if U.sb_hot { t.dim } else { t.faint }, U.sb_t), bw * 0.5);
    }

    let fy = U.list_y1;
    if st_on() {
        if st_search() {
            w10_draw_preview();
        }
        st_draw_footer();
    } else if w10_on() {
        w10_draw_overlay();
    } else if cap_on() {
        let sx = SR(LS().inset);
        r_rect(sx, fy, U.W as f32 - sx * 2.0, 1.0, t.sep, 0.0);
        draw_footer(fy);
    } else {
        r_rect(0.0, fy, U.W as f32, 1.0, t.sep, 0.0);
        draw_footer(fy + 1.0);
    }
    cm_draw();
    tg_draw();
}

// The GPU was reset (driver update, sleep): everything on it is made again.
pub unsafe fn renderer_recover() -> bool {
    settings_device_lost();
    pm_close();
    bd_close(); // its screen duplication belongs to the old device
    r_shutdown();
    if !r_init(g_hwnd, U.W, U.H) {
        return false;
    }
    font_reset_atlas();
    icons_reset(ui_icon_px());
    settings_device_restored();
    bd_refresh();
    true
}

unsafe fn files_virtual_total_d() -> i32 {
    let mut n = U.flist_count;
    if U.files_fetched < U.files_total {
        n = max(n, min(U.files_total, FILES_VIRTUAL_MAX as u32) as i32);
    }
    n
}

// Moves everything one frame on; U.animating says whether another frame is needed.
unsafe fn animate(dt: f32) {
    let u = uu();
    let mut anim = false;
    u.scroll_target = clamp(u.scroll_target, 0.0, max_scroll());
    anim |= spring_step(&mut u.scroll, &mut u.scroll_v, u.scroll_target, anim_omega(g_cfg.anim_scroll_ms), dt);
    if row_selectable(u.sel) {
        let r = *row(u.sel);
        if (r.y - u.hl_y).abs() > list_height() {
            u.hl_y = r.y;
            u.hl_vy = 0.0;
            u.hl_x = r.x;
            u.hl_vx = 0.0;
        }
        let om = anim_omega(g_cfg.anim_select_ms);
        anim |= spring_step(&mut u.hl_y, &mut u.hl_vy, r.y, om, dt);
        anim |= spring_step(&mut u.hl_h, &mut u.hl_vh, r.h, om, dt);
        anim |= spring_step(&mut u.hl_x, &mut u.hl_vx, r.x, om, dt);
        anim |= spring_step(&mut u.hl_w, &mut u.hl_vw, r.w, om, dt);
    }
    anim |= approach(&mut u.settings_hover_t, if u.settings_hover { 1.0 } else { 0.0 }, 22.0, dt);
    anim |= approach(&mut u.update_hover_t, if u.update_hover { 1.0 } else { 0.0 }, 22.0, dt);
    let now = time_now();
    if u.scroll != u.sb_prev {
        u.sb_seen = now;
    }
    u.sb_prev = u.scroll;
    let recent = now - u.sb_seen < 0.9;
    let sb_on = max_scroll() > 0.0 && (u.sb_hot || recent);
    anim |= approach(&mut u.sb_t, if sb_on { 1.0 } else { 0.0 }, if sb_on { 20.0 } else { 7.0 }, dt);
    if recent && !u.sb_hot && max_scroll() > 0.0 {
        anim = true;
    }
    let sub_on = row_selectable(u.sel) && {
        let r = row(u.sel);
        (r.kind == ROW_APP || cap_on()) && r.cell == 0 && !(w10_on() && (*r.app).kind != APP_CMD)
    };
    anim |= approach(&mut u.sub_t, if sub_on { 1.0 } else { 0.0 }, 24.0, dt);
    if st_on() {
        anim |= st_animate(dt);
    }
    if w10_on() {
        anim |= w10_animate(dt);
    }
    anim |= cm_animate(dt);
    anim |= tg_animate(dt);
    if slide_spring() {
        // One speed both ways, so a reopen while it closes (or the other way) turns it around mid-flight.
        let target = if u.vis_target > 0.5 { 0.0 } else { u.slide_dy as f32 };
        anim |= spring_step(&mut u.slide_off, &mut u.slide_v, target, slide_omega(), dt);
        if u.vis_target < 0.5 && (target - u.slide_off).abs() < target.abs() * 0.04 {
            // faded out already, skip the spring's tail
            u.slide_off = target;
            u.slide_v = 0.0;
        }
        u.vis = if u.slide_off == target { u.vis_target } else { 1.0 - u.slide_off / u.slide_dy as f32 };
    } else if u.vis != u.vis_target {
        let open = anim_sec(g_cfg.anim_open_ms);
        let close = anim_sec(g_cfg.anim_close_ms);
        if u.vis_target > u.vis {
            u.vis = if open > 0.0 { min(u.vis + dt / open, u.vis_target) } else { u.vis_target };
        } else {
            u.vis = if close > 0.0 { max(u.vis - dt / close, u.vis_target) } else { u.vis_target };
        }
        anim |= u.vis != u.vis_target;
    }
    if !u.closing && rows_cascading(now) {
        anim = true;
    }
    if u.files_end_y >= 0.0 && u.files_end_y < u.scroll + list_height() && files_virtual_total_d() > u.files_shown {
        anim = true;
    }
    u.animating = anim;
}

pub unsafe fn ui_render() {
    if !R.ok && !renderer_recover() {
        U.dirty = false;
        U.animating = false;
        return;
    }
    r_use(&raw mut R.main);
    r_wait_frame();
    let now = time_now();
    let mut dt = (now - U.last_frame) as f32;
    if dt > 0.1 || dt <= 0.0 {
        dt = 1.0 / 60.0;
    }
    U.last_frame = now;
    animate(dt);
    files_load_more();
    let v = vis_eased();
    let s0 = g_cfg.anim_open_scale as f32 / 100.0;
    R.scale = if U.slide || (U.vis >= 1.0 && U.vis_target >= 1.0) {
        1.0
    } else if U.closing {
        0.985 + 0.015 * v
    } else {
        s0 + (1.0 - s0) * v
    };
    r_draw(ui_draw);
    apply_window_alpha();
    apply_window_slide();
    bd_place();
    r_end_and_present();
    U.dirty = false;
    if U.closing && U.vis <= 0.0 {
        ui_hide_finish_pub();
    }
}

// ---- hit testing

pub unsafe fn row_at(x: i32, y: i32) -> i32 {
    if (y as f32) < U.list_y0 || y as f32 >= U.list_y1 || (x as f32) < list_x0() || x as f32 >= list_x1() {
        return -1;
    }
    let cy = y as f32 - U.list_y0 + U.scroll;
    let mut i = row_first_below(cy);
    while i < U.row_count && row(i).y <= cy {
        let r = row(i);
        if cy < r.y + r.h && (r.cell != CELL_TILE || (x as f32 >= r.x && (x as f32) < r.x + r.w)) {
            return i;
        }
        i += 1;
    }
    -1
}

static mut TI_OFFS: [f32; Q_MAX + 2] = [0.0; Q_MAX + 2];

pub unsafe fn text_index_at(x: i32) -> i32 {
    if U.qlen == 0 {
        return 0;
    }
    let offs = (&raw mut TI_OFFS) as *mut f32;
    let fs = S(LS().search_fs);
    text_offsets(FONT_DISPLAY, fs, U.q.as_ptr(), U.qlen, offs);
    let (mut fx0, mut fx1, mut cy) = (0.0, 0.0, 0.0);
    search_field(&mut fx0, &mut fx1, &mut cy);
    offset_at(offs, U.qlen, x as f32 - fx0 + U.q_scroll.floor())
}
