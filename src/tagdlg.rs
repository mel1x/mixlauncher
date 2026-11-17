// "Add tag": a small dialog over the launcher, in the manner of the hotkey recorder in the settings.

use crate::*;

const TG_CAP: i32 = TAG_INPUT_MAX + 1;

unsafe fn tg_can_save() -> bool {
    for i in 0..U.tg.len as usize {
        let c = U.tg.buf[i];
        if c != ' ' as u16 && c != ',' as u16 && c != ';' as u16 {
            return true;
        }
    }
    false
}

pub unsafe fn tg_open() {
    let r = sel_row();
    let mut type_ = 0u8;
    let key = row_tag_key(r, &mut type_);
    if key.is_null() {
        return;
    }
    free(U.tg.key as *mut c_void);
    U.tg.key = wdup_heap(key);
    U.tg.type_ = type_;
    wcopy(U.tg.name.as_mut_ptr(), 128, if (*r).kind == ROW_FILE { (*(*r).file).name } else { (*(*r).app).name });
    U.tg.len = 0;
    U.tg.caret = 0;
    U.tg.buf[0] = 0;
    U.tg.scroll = 0.0;
    U.tg.hover = -1;
    U.tg.press = -1;
    U.tg.t = if anims_enabled() { 0.0 } else { 1.0 };
    U.tg.open = true;
    U.tg.closing = false;
    U.animating = true;
    caret_reset();
}

pub unsafe fn tg_close(save: bool) {
    if !U.tg.open || U.tg.closing {
        return;
    }
    if save && !U.tg.key.is_null() && tags_add(U.tg.type_, U.tg.key, U.tg.buf.as_ptr()) {
        tags_changed();
    }
    U.tg.press = -1;
    if anims_enabled() && U.visible && anim_sec(160) > 0.0 {
        U.tg.closing = true;
        U.animating = true;
    } else {
        U.tg.open = false;
    }
    ui_invalidate();
}

pub unsafe fn tg_animate(dt: f32) -> bool {
    if !U.tg.open {
        return false;
    }
    let dur = anim_sec(160);
    if U.tg.closing {
        U.tg.t = if dur > 0.0 { U.tg.t - dt / (dur * 0.7) } else { 0.0 };
        if U.tg.t <= 0.0 {
            U.tg.t = 0.0;
            U.tg.open = false;
            U.tg.closing = false;
        }
        return true;
    }
    if U.tg.t < 1.0 {
        U.tg.t = if dur > 0.0 { min(1.0, U.tg.t + dt / dur) } else { 1.0 };
        return true;
    }
    false
}

unsafe fn tg_edit(a: i32, b: i32, s: *const WCHAR, n: i32) {
    U.tg.caret = text_splice(U.tg.buf.as_mut_ptr(), &raw mut U.tg.len, TG_CAP, a, b, s, n);
    caret_reset();
}

fn tg_sep(c: WCHAR) -> bool {
    c == ' ' as u16 || c == ',' as u16 || c == ';' as u16
}

unsafe fn tg_word_left(mut i: i32) -> i32 {
    while i > 0 && tg_sep(U.tg.buf[i as usize - 1]) {
        i -= 1;
    }
    while i > 0 && !tg_sep(U.tg.buf[i as usize - 1]) {
        i -= 1;
    }
    i
}

unsafe fn tg_word_right(mut i: i32) -> i32 {
    while i < U.tg.len && !tg_sep(U.tg.buf[i as usize]) {
        i += 1;
    }
    while i < U.tg.len && tg_sep(U.tg.buf[i as usize]) {
        i += 1;
    }
    i
}

unsafe fn tg_paste() {
    let mut tmp = [0u16; TG_CAP as usize];
    let n = clipboard_get(tmp.as_mut_ptr(), TG_CAP);
    if n != 0 {
        tg_edit(U.tg.caret, U.tg.caret, tmp.as_ptr(), n);
    }
}

pub unsafe fn tg_key(vk: WPARAM) -> bool {
    if !U.tg.open {
        return false;
    }
    if U.tg.closing {
        return vk as u16 != VK_ESCAPE; // a second Esc goes on to the launcher
    }
    let ctrl = GetKeyState(VK_CONTROL as i32) < 0;
    let alt = GetKeyState(VK_MENU as i32) < 0;
    let c = U.tg.caret;
    match vk as u16 {
        VK_ESCAPE => {
            tg_close(false);
            return true;
        }
        VK_RETURN => {
            if tg_can_save() {
                tg_close(true);
            }
            return true;
        }
        VK_BACK => {
            if c > 0 {
                tg_edit(if ctrl { tg_word_left(c) } else { c - 1 }, c, null(), 0);
            }
            return true;
        }
        VK_DELETE => {
            if c < U.tg.len {
                tg_edit(c, if ctrl { tg_word_right(c) } else { c + 1 }, null(), 0);
            }
            return true;
        }
        VK_LEFT => U.tg.caret = if ctrl { tg_word_left(c) } else { max(0, c - 1) },
        VK_RIGHT => U.tg.caret = if ctrl { tg_word_right(c) } else { min(U.tg.len, c + 1) },
        VK_HOME => U.tg.caret = 0,
        VK_END => U.tg.caret = U.tg.len,
        0x56 /*'V'*/ => {
            if ctrl && !alt {
                tg_paste();
            }
            return true;
        }
        VK_F4 => return !alt, // Alt+F4 still closes the launcher
        _ => return true,
    }
    caret_reset();
    true
}

pub unsafe fn tg_char(c: WCHAR) {
    if c < 0x20 || c == 0x7F || U.tg.closing {
        return;
    }
    let cc = c;
    tg_edit(U.tg.caret, U.tg.caret, &cc, 1);
}

pub unsafe fn tg_btn_at(x: i32, y: i32) -> i32 {
    for i in 0..2 {
        if in_box(&U.tg.btn[i], x as f32, y as f32) {
            return i as i32;
        }
    }
    -1
}

static mut TG_OFFS: [f32; TG_CAP as usize + 1] = [0.0; TG_CAP as usize + 1];

pub unsafe fn tg_index_at(x: i32) -> i32 {
    let offs = (&raw mut TG_OFFS) as *mut f32;
    text_offsets(FONT_TEXT, S(14.5), U.tg.buf.as_ptr(), U.tg.len, offs);
    offset_at(offs, U.tg.len, x as f32 - (U.tg.field[0] + SR(12.0)) + U.tg.scroll.floor())
}

static mut TG_DOFFS: [f32; TG_CAP as usize + 1] = [0.0; TG_CAP as usize + 1];

pub unsafe fn tg_draw() {
    if !U.tg.open {
        return;
    }
    let t = &*(&raw const TH);
    let e = if U.tg.closing { U.tg.t } else { ease_out_cubic(U.tg.t) };
    let W = U.W as f32;
    let HH = U.H as f32;
    r_clip_all();
    R.opacity = e;
    r_rect(0.0, 0.0, W, HH, if t.dark { RGBA(0, 0, 0, 110) } else { RGBA(0, 0, 0, 36) }, 0.0);
    let m = SR(16.0);
    let pw = min(SR(420.0), W - m * 2.0);
    let ph = SR(200.0);
    let px = ((W - pw) * 0.5).floor();
    let py = (max(m, (HH - ph) * 0.5) + (1.0 - e) * SR(10.0)).floor();
    let rad = SR(14.0);
    box_set(&mut U.tg.panel, px, py, pw, ph);
    r_panel(
        px,
        py,
        pw,
        ph,
        rad,
        if t.dark { RGBA(40, 40, 43, 255) } else { RGBA(252, 252, 253, 255) },
        if t.dark { RGBA(255, 255, 255, 20) } else { RGBA(0, 0, 0, 22) },
        if t.dark { RGBA(0, 0, 0, 120) } else { RGBA(0, 0, 0, 45) },
        SR(10.0),
        SR(24.0),
    );

    let x0 = px + SR(20.0);
    let x1 = px + pw - SR(20.0);
    text_draw(FONT_TEXT_SEMIBOLD, S(15.0), x0, (py + SR(34.0)).floor(), tr!("Добавить тег", "Add tag"), -1, t.text);
    text_draw_fit(FONT_TEXT, S(12.5), x0, (py + SR(54.0)).floor(), x1 - x0, U.tg.name.as_ptr(), -1, t.faint, false);

    let fy = py + SR(68.0);
    let fh = SR(40.0);
    let fw = x1 - x0;
    box_set(&mut U.tg.field, x0, fy, fw, fh);
    r_rect(x0, fy, fw, fh, t.key_bg, SR(8.0));
    r_border(x0, fy, fw, fh, color_alpha(t.accent, 0.85), SR(8.0));
    let fs = S(14.5);
    let cy = fy + fh * 0.5;
    let tx0 = x0 + SR(12.0);
    let tw = fw - SR(24.0);
    let base = text_baseline(FONT_TEXT, fs, cy);
    let car_top = (cy - fs * 0.62).floor();
    let car_h = (fs * 1.24).floor();
    let offs = (&raw mut TG_DOFFS) as *mut f32;
    text_offsets(FONT_TEXT, fs, U.tg.buf.as_ptr(), U.tg.len, offs);
    field_scroll(&mut U.tg.scroll, offs, U.tg.caret, U.tg.len, tw - SR(2.0));
    let cx = *offs.add(U.tg.caret as usize);
    let ox = tx0 - U.tg.scroll.floor();
    r_set_clip(tx0 - SR(2.0), fy, x1 - SR(10.0), fy + fh);
    if U.tg.len == 0 {
        text_draw_fit(FONT_TEXT, fs, tx0, base, tw, tr!("например: работа, отчёты", "e.g. work, reports"), -1, t.faint, false);
    } else {
        text_draw(FONT_TEXT, fs, ox, base, U.tg.buf.as_ptr(), U.tg.len, t.text);
    }
    if U.caret_on && !U.tg.closing {
        r_rect((ox + cx).floor(), car_top, max(1.0, S(1.5).floor()), car_h, t.accent, SR(1.0));
    }
    r_clip_all();
    text_draw_fit(
        FONT_TEXT,
        S(12.5),
        x0,
        (fy + fh + SR(22.0)).floor(),
        fw,
        tr!("Несколько тегов - через запятую", "Separate several tags with commas"),
        -1,
        t.faint,
        false,
    );

    let by = py + ph - SR(52.0);
    let bcy = by + SR(26.0);
    r_rect(px, by, pw, 1.0, t.sep, 0.0);
    let can_save = tg_can_save();
    let bfs = S(13.5);
    let mut xr = px + pw - SR(12.0);
    for b in (0..2).rev() {
        let lbl = if b != 0 { tr!("Сохранить", "Save") } else { tr!("Отмена", "Cancel") };
        let key = if b != 0 { w!("Enter") } else { w!("Esc") };
        let kw = keycap_width(key);
        let lw = text_width(FONT_TEXT, bfs, lbl, -1);
        let bw = SR(10.0) + lw + SR(8.0) + kw + SR(6.0);
        let bh = SR(32.0);
        let bx = (xr - bw).floor();
        let byy = (bcy - bh * 0.5).floor();
        box_set(&mut U.tg.btn[b], bx, byy, bw, bh);
        let enabled = b == 0 || can_save;
        let hov = U.tg.hover == b as i32 && enabled;
        if b != 0 && can_save {
            r_rect(bx, byy, bw, bh, color_alpha(t.accent, if hov { 0.34 } else { 0.24 }), SR(8.0));
        } else if hov {
            r_rect(bx, byy, bw, bh, t.sel, SR(8.0));
        }
        let col = if enabled { t.text } else { t.faint };
        text_draw(FONT_TEXT, bfs, bx + SR(10.0), text_baseline(FONT_TEXT, bfs, bcy), lbl, -1, col);
        draw_keycap(bx + SR(10.0) + lw + SR(8.0), bcy, key, if enabled { t.dim } else { t.faint });
        xr = bx - SR(6.0);
    }
    R.opacity = 1.0;
}

pub unsafe fn tg_can_save_pub() -> bool {
    tg_can_save()
}
