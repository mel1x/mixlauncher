// The settings window: page drawing, animation, hit testing, input and the window itself.

use crate::*;
use core::sync::atomic::Ordering::SeqCst;

unsafe fn sb_button_rects() {
    let x = sb_x();
    let w = sb_w();
    let bot = SW.H as f32 - sb_x() - SSR(8.0);
    let bh = SSR(36.0);
    box_set(&mut SW.sb_btn[1], x + SSR(8.0), bot - bh, w - SSR(16.0), bh);
    box_set(&mut SW.sb_btn[0], x + SSR(8.0), bot - bh * 2.0 - SSR(6.0), w - SSR(16.0), bh);
}

unsafe fn draw_sidebar_buttons(t: &Theme, c: &SColors) {
    sb_button_rects();
    let fs = SSC(14.0);
    let start = if upd_visible() { 0 } else { 1 };
    for i in start..2 {
        let b = SW.sb_btn[i];
        let bw = b[2] - b[0];
        let bh = b[3] - b[1];
        let cy = (b[1] + b[3]) * 0.5;
        let ix = b[0] + SSR(10.0);
        let isz = SSR(20.0);
        let hv = SW.sb_hov[i];
        let mut ub = [0u16; 64];
        let lbl;
        let glyph;
        let icol;
        let fg;
        if i == 0 {
            let st = upd_state();
            lbl = upd_label(ub.as_mut_ptr(), 64);
            r_rect(b[0], b[1], bw, bh, color_alpha(t.accent, 0.18 + 0.10 * hv), SSR(8.0));
            if st == UPD_DOWNLOADING {
                let pw = (bw * UPD.progress.load(SeqCst) as f32 / 1000.0).floor();
                r_set_clip(b[0], b[1], b[0] + pw, b[3]);
                r_rect(b[0], b[1], bw, bh, color_alpha(t.accent, 0.22), SSR(8.0));
                r_clip_all();
            }
            glyph = if st == UPD_ERROR { 0xE72C } else { 0xE896 };
            icol = if st == UPD_ERROR { t.danger } else { t.text };
            fg = icol;
        } else {
            lbl = tr!("Поддержать автора", "Support the author");
            if hv > 0.0 {
                r_rect(b[0], b[1], bw, bh, color_alpha(c.nav_hover, hv), SSR(8.0));
            }
            glyph = 0xEB51;
            icol = color_mix(t.faint, t.dim, hv);
            fg = color_mix(t.dim, t.text, hv);
        }
        text_draw_icon(glyph, SSC(15.0), ix + isz * 0.5, cy, icol);
        text_draw_fit(FONT_TEXT, fs, ix + isz + SSR(12.0), text_baseline(FONT_TEXT, fs, cy), b[2] - ix - isz - SSR(18.0), lbl, -1, fg, false);
    }
}

unsafe fn settings_draw() {
    let t = &*(&raw const TH);
    let c = scolors(t.dark);
    if !SW.mica {
        r_rect(0.0, 0.0, SW.W as f32, SW.H as f32, c.bg, 0.0);
    }
    SW.scroll = clamp(SW.scroll, 0.0, max_scroll_s());

    draw_sidebar(t, &c);
    draw_sidebar_buttons(t, &c);

    let pe = ease_out_cubic(SW.page_t);
    let rise = ((1.0 - pe) * SSR(10.0) + 0.5).floor();
    R.opacity = pe;

    let x0 = content_x0();
    text_draw_fit(FONT_TEXT_SEMIBOLD, SSC(20.0), x0 + SSR(2.0), SSR(40.0) + rise, content_x1() - x0 - SSR(100.0), ss(k_pages[SW.page as usize].name), -1, t.text, false);

    let off = top_h() - (SW.scroll + 0.5).floor() + rise;
    r_set_clip(0.0, top_h(), SW.W as f32, SW.H as f32);
    for i in 0..SW.ncards as usize {
        let b = SW.cards[i];
        if b.h < 1.0 {
            continue;
        }
        r_rect(b.x, b.y + off, b.w, b.h, c.card, SSR(10.0));
        r_border(b.x, b.y + off, b.w, b.h, c.card_border, SSR(10.0));
    }
    for i in 0..SW.nheads as usize {
        if SW.head_a[i] <= 0.0 {
            continue;
        }
        let hy = SW.heads[i].y + off - SSR(15.0);
        if SW.head_a[i] < 1.0 {
            r_set_clip(0.0, max(top_h(), hy), SW.W as f32, min(SW.H as f32, hy + SW.heads[i].h));
        }
        R.opacity = pe * SW.head_a[i];
        text_draw(FONT_TEXT_SEMIBOLD, SSC(13.0), SW.heads[i].x, SW.heads[i].y + off + SSR(5.0), ss(SW.head_text[i]), -1, color_mix(t.dim, t.text, 0.35));
        r_set_clip(0.0, top_h(), SW.W as f32, SW.H as f32);
    }
    for i in 0..SW.nrows {
        let r = SW.rows[i as usize];
        if r.h < 1.0 || r.y + off + r.h < top_h() || r.y + off > SW.H as f32 {
            continue;
        }
        // A row on its way in or out is revealed from the top and fades with it.
        let part = r.vis < 1.0;
        if part {
            r_set_clip(0.0, max(top_h(), r.y + off), SW.W as f32, min(SW.H as f32, r.y + off + r.h));
        }
        R.opacity = pe * r.vis;
        draw_srow(i, off, t, &c);
        if part {
            r_set_clip(0.0, top_h(), SW.W as f32, SW.H as f32);
        }
    }
    r_clip_all();
    R.opacity = 1.0;

    let sep_k = clamp(SW.scroll / SSR(16.0), 0.0, 1.0);
    if sep_k > 0.0 {
        r_rect(x0, top_h() - 1.0, content_x1() - x0, 1.0, color_alpha(t.sep, sep_k), 0.0);
    }

    let ms = max_scroll_s();
    if ms > 0.0 && SW.sb_t > 0.0 {
        let hot = sb_hot();
        let vh = SW.H as f32 - top_h() - SSR(8.0);
        let bar_h = max(SSR(32.0), vh * vh / SW.content_h);
        let by = top_h() + (vh - bar_h) * clamp(SW.scroll / ms, 0.0, 1.0);
        let bw = if hot { SSR(5.0) } else { SSR(3.0) };
        r_rect(SW.W as f32 - SSR(4.0) - bw, by.floor(), bw, bar_h.floor(), color_alpha(if hot { t.dim } else { t.faint }, SW.sb_t), bw * 0.5);
    }

    for i in 0..2 {
        let mut r = [0.0; 4];
        winbtn_rect(i, &mut r);
        let ht = if i != 0 { HT_WINCLOSE } else { HT_WINMIN };
        let hov = SW.hover.type_ == ht;
        let down = hov && SW.press.type_ == SW.hover.type_;
        let h = SW.win_hov[i as usize];
        let mut fg = if SW.active { t.text } else { t.faint };
        if i == 1 {
            r_rect(r[0], r[1], r[2] - r[0], r[3] - r[1], color_alpha(if down { RGBA(200, 64, 50, 255) } else { RGBA(196, 43, 28, 255) }, h), 0.0);
            fg = color_mix(fg, RGBA(255, 255, 255, 255), h);
        } else {
            r_rect(r[0], r[1], r[2] - r[0], r[3] - r[1], color_alpha(if down { c.ctl_hover } else { c.ctl }, h), 0.0);
        }
        text_draw_icon(if i != 0 { 0xE8BB } else { 0xE921 }, SSC(10.0), (r[0] + r[2]) * 0.5, (r[1] + r[3]) * 0.5, fg);
    }

    if SW.pop_open && SW.pop_row >= 0 && SW.pop_row < SW.nrows {
        R.opacity = ease_out_cubic(SW.pop_t);
        draw_popover(t, &c);
        R.opacity = 1.0;
    }
    if SW.rec.open {
        R.opacity = ease_out_cubic(SW.rec_t);
        draw_recorder(t, &c);
        R.opacity = 1.0;
    }
    if SW.don.open {
        draw_donate(t, &c);
    }
    confetti_draw();
    let mut tip_key = 0;
    let tip = if SW.tip_on { tip_at(&mut tip_key) } else { null() };
    if !tip.is_null() && tip_key == SW.tip_row {
        draw_tip(t, &c, tip);
    }
}

unsafe fn tip_hide() {
    SW.tip_row = -1;
    SW.tip_on = false;
    if !SW.hwnd.is_null() {
        KillTimer(SW.hwnd, SETTINGS_TIMER_TIP);
    }
}

// Arms the tooltip when the mouse comes to one; it shows after a pause, like a system tooltip.
unsafe fn tip_track() {
    let mut row_ = 0;
    tip_at(&mut row_);
    if row_ == SW.tip_row {
        return;
    }
    tip_hide();
    SW.tip_row = row_;
    if row_ >= 0 {
        SetTimer(SW.hwnd, SETTINGS_TIMER_TIP, 600, None);
    }
    settings_invalidate();
}

unsafe fn settings_snap_motion() {
    let s = sw();
    let mut r = [0.0; 4];
    nav_rect(s.page, &mut r);
    s.nav_y = r[1];
    s.nav_v = 0.0;
    for i in 0..SITEMS {
        s.tog[i] = if k_sitems[i].kind == SK_TOGGLE && sval(k_sitems[i].id) != 0 { 1.0 } else { 0.0 };
        s.seg[i] = if is_seg(&k_sitems[i]) { sval(k_sitems[i].id) as f32 } else { 0.0 };
        s.vis[i] = if item_shown(i) { 1.0 } else { 0.0 };
    }
    s.nav_hov = [0.0; PG__COUNT as usize];
    s.win_hov = [0.0; 2];
    for k in 0..STYLE__COUNT as usize {
        s.card_hov[k] = if s.hover.type_ == HT_ROW && s.hover.part == PART_CARD0 + k as u8 { 1.0 } else { 0.0 };
        s.card_sel[k] = if k as i32 == sval(SID_STYLE) { 1.0 } else { 0.0 };
    }
    s.scroll_target = s.scroll;
    s.scroll_v = 0.0;
    s.page_t = 1.0;
    s.pop_t = 1.0;
    s.rec_t = 1.0;
    if s.don.closing {
        s.don.open = false;
        s.don.closing = false;
    }
    s.don.t = 1.0;
    s.don.scroll_target = clamp(s.don.scroll_target, 0.0, max(0.0, s.don.content_h - s.don.view_h));
    s.don.scroll = s.don.scroll_target;
    s.don.scroll_v = 0.0;
    s.don.sb_t = if s.don.content_h > s.don.view_h { 1.0 } else { 0.0 };
    for i in 0..10 {
        let want = if i == DON_HOV_LINK {
            DON_LINK
        } else if i == DON_HOV_CLOSE {
            DON_CLOSE
        } else {
            i as i32
        };
        s.don.hov[i] = if s.hover.type_ == HT_DON && s.hover.idx == want { 1.0 } else { 0.0 };
    }
    s.sb_hov = [0.0; 2];
    s.sb_t = 0.0;
    s.sb_prev = s.scroll;
    s.row_hov = 0.0;
    s.row_hov_idx = -1;
}

unsafe fn settings_animate(dt: f32) -> bool {
    let s = sw();
    if !anims_enabled() {
        s.scroll_target = clamp(s.scroll_target, 0.0, max_scroll_s());
        s.scroll = s.scroll_target;
        settings_snap_motion();
        s.sb_t = if max_scroll_s() > 0.0 { 1.0 } else { 0.0 };
        s.row_hov_idx = row_hover_target();
        s.row_hov = if s.row_hov_idx >= 0 { 1.0 } else { 0.0 };
        return false;
    }
    let mut anim = false;
    let mut r = [0.0; 4];
    nav_rect(s.page, &mut r);
    anim |= spring_step(&mut s.nav_y, &mut s.nav_v, r[1], anim_omega(g_cfg.anim_select_ms + 20), dt);
    for i in 0..PG__COUNT {
        let target = if s.hover.type_ == HT_NAV && s.hover.idx == i && i != s.page { 1.0 } else { 0.0 };
        anim |= approach(&mut s.nav_hov[i as usize], target, 20.0, dt);
    }
    for i in 0..2 {
        let target = if s.hover.type_ == if i != 0 { HT_WINCLOSE } else { HT_WINMIN } { 1.0 } else { 0.0 };
        anim |= approach(&mut s.win_hov[i], target, 24.0, dt);
    }
    for i in 0..SITEMS {
        if k_sitems[i].kind == SK_TOGGLE {
            let target = if sval(k_sitems[i].id) != 0 { 1.0 } else { 0.0 };
            anim |= approach(&mut s.tog[i], target, 20.0, dt);
        }
        if is_seg(&k_sitems[i]) {
            let target = sval(k_sitems[i].id) as f32;
            anim |= approach(&mut s.seg[i], target, 18.0, dt);
        }
        let want = if item_shown(i) { 1.0 } else { 0.0 };
        let vd = anim_sec(260);
        if s.vis[i] != want {
            s.vis[i] = if vd > 0.0 {
                if want > s.vis[i] { min(want, s.vis[i] + dt / vd) } else { max(want, s.vis[i] - dt / vd) }
            } else {
                want
            };
            anim = true;
        }
    }
    if s.pop_open && s.pop_row >= 0 && s.pop_row < s.nrows && !item_shown(s.rows[s.pop_row as usize].item as usize) {
        s.pop_open = false;
    }
    s.scroll_target = clamp(s.scroll_target, 0.0, max_scroll_s());
    let target = s.scroll_target;
    anim |= spring_step(&mut s.scroll, &mut s.scroll_v, target, anim_omega(g_cfg.anim_scroll_ms), dt);
    let now = time_now();
    if s.scroll != s.sb_prev {
        s.sb_seen = now;
    }
    s.sb_prev = s.scroll;
    let recent = now - s.sb_seen < 0.9;
    let hot = sb_hot();
    let sb_on = max_scroll_s() > 0.0 && (hot || recent);
    anim |= approach(&mut s.sb_t, if sb_on { 1.0 } else { 0.0 }, if sb_on { 20.0 } else { 7.0 }, dt);
    if recent && !hot && max_scroll_s() > 0.0 {
        anim = true;
    }
    let rh = row_hover_target();
    if rh >= 0 && rh != s.row_hov_idx {
        s.row_hov_idx = rh;
        s.row_hov = 0.0;
    }
    anim |= approach(&mut s.row_hov, if rh >= 0 { 1.0 } else { 0.0 }, 22.0, dt);
    let on_cards = s.hover.type_ == HT_ROW && s.hover.part >= PART_CARD0 && !s.pop_open;
    for k in 0..STYLE__COUNT as usize {
        let h = if on_cards && s.hover.part == PART_CARD0 + k as u8 { 1.0 } else { 0.0 };
        anim |= approach(&mut s.card_hov[k], h, 14.0, dt);
        let sl = if k as i32 == sval(SID_STYLE) { 1.0 } else { 0.0 };
        anim |= approach(&mut s.card_sel[k], sl, 11.0, dt);
    }
    for i in 0..2 {
        let target = if s.hover.type_ == if i != 0 { HT_DONATE } else { HT_SBUPDATE } { 1.0 } else { 0.0 };
        anim |= approach(&mut s.sb_hov[i], target, 22.0, dt);
    }
    anim |= don_animate(dt);
    anim |= confetti_animate(dt);
    let dur = [anim_sec(g_cfg.anim_page_ms), anim_sec(g_cfg.anim_menu_ms) * 0.85, anim_sec(160)];
    let entr: [&mut f32; 3] = [&mut s.page_t, &mut s.pop_t, &mut s.rec_t];
    for (i, e) in entr.into_iter().enumerate() {
        if *e < 1.0 {
            *e = if dur[i] > 0.0 { min(1.0, *e + dt / dur[i]) } else { 1.0 };
            anim = true;
        }
    }
    anim
}

pub unsafe fn settings_render() {
    if SW.hwnd.is_null() || !SW.target.ok || !R.ok {
        return;
    }
    r_use(&raw mut SW.target);
    r_wait_frame();
    let now = time_now();
    let mut dt = (now - SW.last_frame) as f32;
    if dt > 0.1 || dt <= 0.0 {
        dt = 1.0 / 60.0;
    }
    SW.last_frame = now;
    if SW.page == PG_DESIGN as i32 {
        settings_layout();
        for i in 0..SW.nrows as usize {
            if k_sitems[SW.rows[i].item as usize].kind == SK_GALLERY {
                let mut b = [0.0; 4];
                gallery_card(&SW.rows[i], 0, &mut b);
                pv_update((b[2] - b[0] - SSR(16.0)) as i32, gallery_preview_h(b[2] - b[0]) as i32);
            }
        }
        r_use(&raw mut SW.target);
    }
    settings_layout();
    SW.animating = settings_animate(dt);
    R.scale = 1.0;
    r_draw(settings_draw);
    r_end_and_present();
    r_use(&raw mut R.main);
    SW.dirty = false;
}

unsafe fn settings_hit(mx: i32, my: i32) -> SHit {
    let mut h = SHit { type_: HT_NONE, part: PART_ROW, idx: -1 };
    let x = mx as f32;
    let y = my as f32;
    if SW.rec.open {
        h.type_ = if in_box(&SW.rec_btn[1], x, y) {
            HT_RECSAVE
        } else if in_box(&SW.rec_btn[0], x, y) {
            HT_RECCANCEL
        } else if in_box(&SW.rec_panel, x, y) {
            HT_RECPANEL
        } else {
            HT_RECOUT
        };
        return h;
    }
    if don_active() {
        h.type_ = HT_DON;
        h.idx = if in_box(&SW.don.panel, x, y) { DON_PANEL } else { DON_OUT };
        if h.idx == DON_PANEL {
            if in_box(&SW.don.close, x, y) {
                h.idx = DON_CLOSE;
            } else if in_box(&SW.don.link, x, y) {
                h.idx = DON_LINK;
            } else {
                for i in 0..8 {
                    if in_box(&SW.don.copy[i], x, y) {
                        h.idx = i as i32;
                    }
                }
            }
        }
        return h;
    }
    if SW.pop_open {
        h.type_ = HT_POPOUT;
        if x >= SW.pop_x && x < SW.pop_x + SW.pop_w && y >= SW.pop_y && y < SW.pop_y + SW.pop_h {
            let k = ((y - SW.pop_y - SSR(5.0)) / SSR(32.0)).floor() as i32;
            if k >= 0 && k < choice_count(&k_sitems[SW.rows[SW.pop_row as usize].item as usize]) {
                h.type_ = HT_POPITEM;
                h.idx = k;
            }
        }
        return h;
    }
    for i in 0..2 {
        let mut r = [0.0; 4];
        winbtn_rect(i, &mut r);
        if in_box(&r, x, y) {
            h.type_ = if i != 0 { HT_WINCLOSE } else { HT_WINMIN };
            return h;
        }
    }
    sb_button_rects();
    if in_box(&SW.sb_btn[1], x, y) {
        h.type_ = HT_DONATE;
        return h;
    }
    if upd_visible() && in_box(&SW.sb_btn[0], x, y) {
        h.type_ = HT_SBUPDATE;
        return h;
    }
    for i in 0..PG__COUNT {
        let mut r = [0.0; 4];
        nav_rect(i, &mut r);
        if in_box(&r, x, y) {
            h.type_ = HT_NAV;
            h.idx = i;
            return h;
        }
    }
    if y < top_h() {
        return h;
    }
    let cy = y - top_h() + (SW.scroll + 0.5).floor();
    for i in 0..SW.nrows {
        let r = &SW.rows[i as usize];
        if x < r.x || x >= r.x + r.w || cy < r.y || cy >= r.y + r.h {
            continue;
        }
        h.type_ = HT_ROW;
        h.idx = i;
        if k_sitems[r.item as usize].kind == SK_GALLERY {
            let k = gallery_at(r, x, cy);
            if k >= 0 {
                h.part = PART_CARD0 + k as u8;
            }
            return h;
        }
        let g = SSR(4.0);
        if x >= r.cx - g && x < r.cx + r.cw + g && cy >= r.cy - g && cy < r.cy + r.ch + g {
            h.part = PART_CONTROL;
            if k_sitems[r.item as usize].kind == SK_STEPPER {
                if x < r.cx + SSR(34.0) {
                    h.part = PART_MINUS;
                } else if x >= r.cx + r.cw - SSR(34.0) {
                    h.part = PART_PLUS;
                }
            }
        }
        return h;
    }
    h
}

unsafe fn ensure_focus_visible() {
    if !row_focusable(SW.focus) {
        return;
    }
    let r = SW.rows[SW.focus as usize];
    let vh = SW.H as f32 - top_h();
    let top = r.y - SSR(40.0);
    let bot = r.y + r.h + SSR(12.0);
    if top < SW.scroll_target {
        SW.scroll_target = top;
    } else if bot > SW.scroll_target + vh {
        SW.scroll_target = bot - vh;
    }
    SW.scroll_target = clamp(SW.scroll_target, 0.0, max_scroll_s());
}

unsafe fn pop_open(row_: i32) {
    let r = SW.rows[row_ as usize];
    let it = &k_sitems[r.item as usize];
    let fs = SSC(13.5);
    let mut w: f32 = 0.0;
    let nopt = choice_count(it);
    for k in 0..nopt as usize {
        w = max(w, text_width(FONT_TEXT, fs, ss(it.opt[k]), -1));
    }
    SW.pop_w = max(r.cw, (w + SSR(5.0) + SSR(32.0) + SSR(24.0)).floor());
    SW.pop_h = SSR(32.0) * nopt as f32 + SSR(10.0);
    let off = top_h() - (SW.scroll + 0.5).floor();
    SW.pop_x = (r.cx + r.cw - SW.pop_w).floor();
    SW.pop_y = r.cy + off + r.ch + SSR(4.0);
    if SW.pop_y + SW.pop_h > SW.H as f32 - SSR(8.0) {
        SW.pop_y = r.cy + off - SSR(4.0) - SW.pop_h;
    }
    SW.pop_open = true;
    SW.pop_t = 0.0;
    SW.pop_row = row_;
    SW.pop_hover = sval(it.id);
    settings_invalidate();
}

unsafe fn pop_close() {
    SW.pop_open = false;
    settings_invalidate();
}

unsafe fn set_page(page: i32) {
    let page = (page + PG__COUNT) % PG__COUNT;
    if page == SW.page {
        return;
    }
    end_edit(true);
    SW.pop_open = false;
    SW.armed = false;
    SW.page = page;
    SW.scroll = 0.0;
    SW.scroll_target = 0.0;
    SW.scroll_v = 0.0;
    SW.page_t = 0.0;
    SW.focus = -1;
    SW.hover.type_ = HT_NONE;
    tip_hide();
    if page == PG_SEARCH as i32 {
        refresh_status(); // the Everything status
    }
    if page == PG_DESIGN as i32 {
        PV.dirty = true; // the wallpaper or the apps may have changed
    }
    settings_invalidate();
}

unsafe fn activate_row(ri: i32, part: u8) {
    let it = &k_sitems[SW.rows[ri as usize].item as usize];
    if item_locked(it) {
        return;
    }
    if it.id != SID_CLEARHIST {
        SW.armed = false;
    }
    match it.kind {
        SK_TOGGLE => sset(it, (sval(it.id) == 0) as i32),
        SK_CHOICE => {
            if is_seg(it) {
                sset(it, if SW.seg_click >= 0 { SW.seg_click } else { (sval(it.id) + 1) % choice_count(it) });
            } else {
                pop_open(ri);
            }
            SW.seg_click = -1;
        }
        SK_STEPPER => {
            if part == PART_MINUS {
                sset(it, sval(it.id) - it.step);
            } else if part == PART_PLUS {
                sset(it, sval(it.id) + it.step);
            }
        }
        SK_GALLERY => {
            if part >= PART_CARD0 {
                sset(it, (part - PART_CARD0) as i32);
            }
        }
        SK_SLIDER => {
            if part == PART_CONTROL {
                sset(it, slider_value_at(&SW.rows[ri as usize], SW.mx as f32)); // dragging goes on in WM_MOUSEMOVE
            }
        }
        SK_TEXT => begin_edit(ri),
        SK_HOTKEY => rec_open(),
        SK_BUTTON => button_action(it),
        _ => {}
    }
    settings_invalidate();
}

unsafe fn move_focus(dir: i32) {
    let mut i = SW.focus;
    if !row_focusable(i) {
        i = if dir > 0 { -1 } else { SW.nrows };
    }
    for _ in 0..SW.nrows {
        i += dir;
        if i < 0 || i >= SW.nrows {
            break;
        }
        if row_focusable(i) {
            SW.focus = i;
            break;
        }
    }
    ensure_focus_visible();
    settings_invalidate();
}

unsafe fn settings_key(vk: u32) {
    let ctrl = GetKeyState(VK_CONTROL as i32) < 0;
    let shift = GetKeyState(VK_SHIFT as i32) < 0;
    let v = vk as u16;
    if SW.don.open {
        if v == VK_ESCAPE {
            don_close();
        } else if v == VK_DOWN || v == VK_NEXT {
            SW.don.scroll_target += SSR(if v == VK_NEXT { 240.0 } else { 48.0 });
        } else if v == VK_UP || v == VK_PRIOR {
            SW.don.scroll_target -= SSR(if v == VK_PRIOR { 240.0 } else { 48.0 });
        }
        settings_invalidate();
        return;
    }
    if SW.pop_open {
        let it = &k_sitems[SW.rows[SW.pop_row as usize].item as usize];
        let cc = choice_count(it);
        match v {
            VK_UP => SW.pop_hover = (SW.pop_hover + cc - 1) % cc,
            VK_DOWN => SW.pop_hover = (SW.pop_hover + 1) % cc,
            VK_RETURN | VK_SPACE => {
                sset(it, SW.pop_hover);
                pop_close();
            }
            VK_ESCAPE | VK_TAB => pop_close(),
            _ => {}
        }
        settings_invalidate();
        return;
    }
    if SW.editing >= 0 {
        if v == VK_TAB {
            end_edit(true);
            move_focus(if shift { -1 } else { 1 });
            return;
        }
        edit_key(vk, ctrl);
        return;
    }
    if ctrl && (v == VK_TAB || v == VK_NEXT || v == VK_PRIOR) {
        set_page(SW.page + if (v == VK_TAB && shift) || v == VK_PRIOR { -1 } else { 1 });
        return;
    }
    match v {
        VK_ESCAPE => {
            if SW.armed {
                SW.armed = false;
                settings_invalidate();
            } else {
                DestroyWindow(SW.hwnd);
            }
            return;
        }
        VK_DOWN => {
            move_focus(1);
            return;
        }
        VK_UP => {
            move_focus(-1);
            return;
        }
        VK_TAB => {
            move_focus(if shift { -1 } else { 1 });
            return;
        }
        VK_NEXT => {
            SW.scroll_target += (SW.H as f32 - top_h()) * 0.8;
            settings_invalidate();
            return;
        }
        VK_PRIOR => {
            SW.scroll_target -= (SW.H as f32 - top_h()) * 0.8;
            settings_invalidate();
            return;
        }
        0x57 /*W*/ => {
            if ctrl {
                DestroyWindow(SW.hwnd);
            }
            return;
        }
        _ => {}
    }
    if !row_focusable(SW.focus) {
        return;
    }
    let it = &k_sitems[SW.rows[SW.focus as usize].item as usize];
    match v {
        VK_LEFT | VK_RIGHT => {
            let d = if v == VK_LEFT { -1 } else { 1 };
            if it.kind == SK_STEPPER || it.kind == SK_SLIDER {
                sset(it, sval(it.id) + d * it.step);
            } else if it.kind == SK_CHOICE {
                sset(it, sval(it.id) + d);
            } else if it.kind == SK_GALLERY {
                sset(it, k_gal_order[clamp(gallery_pos(sval(it.id)) + d, 0, STYLE__COUNT - 1) as usize] as i32);
            } else if it.kind == SK_TOGGLE {
                sset(it, (d > 0) as i32);
            }
        }
        VK_SPACE | VK_RETURN => activate_row(SW.focus, if it.kind == SK_STEPPER || it.kind == SK_SLIDER { PART_ROW } else { PART_CONTROL }),
        _ => {}
    }
    settings_invalidate();
}

unsafe fn settings_mouse_down(mx: i32, my: i32) {
    let h = settings_hit(mx, my);
    SW.press = h;
    tip_hide();
    if SW.rec.open {
        if h.type_ == HT_RECSAVE {
            rec_close(true);
        } else if h.type_ == HT_RECCANCEL || h.type_ == HT_RECOUT {
            rec_close(false);
        }
        return;
    }
    if SW.don.open {
        if SW.don.closing {
            return;
        }
        if h.idx == DON_OUT || h.idx == DON_CLOSE {
            don_close();
        } else if h.idx == DON_LINK {
            let url = wstr(DON_LINK_URL);
            launch_open(url.as_ptr(), OPEN_PATH);
            confetti_burst();
        } else if h.idx >= 0 {
            don_copy(h.idx);
            confetti_burst();
        }
        return;
    }
    if h.type_ == HT_DONATE {
        don_open();
        return;
    }
    if h.type_ == HT_SBUPDATE {
        upd_download();
        settings_invalidate();
        return;
    }
    if SW.pop_open {
        if h.type_ == HT_POPITEM {
            sset(&k_sitems[SW.rows[SW.pop_row as usize].item as usize], h.idx);
        }
        pop_close();
        return;
    }
    if SW.editing >= 0 && !(h.type_ == HT_ROW && h.idx == SW.editing && h.part == PART_CONTROL) {
        end_edit(true);
    }
    if h.type_ == HT_NAV {
        set_page(h.idx);
    } else if h.type_ == HT_ROW {
        SW.focus = h.idx;
        let it = &k_sitems[SW.rows[h.idx as usize].item as usize];
        if is_seg(it) && h.part == PART_CONTROL {
            SW.seg_click = seg_index_at(it, SW.rows[h.idx as usize].cx, mx as f32);
        }
        if (it.kind == SK_TOGGLE || h.part != PART_ROW) && !(it.kind == SK_TEXT && SW.editing == h.idx) {
            activate_row(h.idx, h.part);
        } else if SW.armed {
            SW.armed = false;
        }
    } else if SW.armed {
        SW.armed = false;
    }
    settings_invalidate();
}

unsafe fn settings_mouse_up(mx: i32, my: i32) {
    let h = settings_hit(mx, my);
    if SW.press.type_ == HT_WINCLOSE && h.type_ == HT_WINCLOSE {
        DestroyWindow(SW.hwnd);
    } else if SW.press.type_ == HT_WINMIN && h.type_ == HT_WINMIN {
        ShowWindow(SW.hwnd, SW_MINIMIZE);
    }
    SW.press.type_ = HT_NONE;
    settings_invalidate();
}

pub unsafe fn settings_apply_theme() {
    if SW.hwnd.is_null() {
        return;
    }
    dwm_set(SW.hwnd, DWA_DARK, TH.dark as u32);
    settings_invalidate();
}

unsafe fn settings_nchittest(h: HWND, lp: LPARAM) -> LRESULT {
    let mut p = POINT { x: get_x_lparam(lp), y: get_y_lparam(lp) };
    ScreenToClient(h, &mut p);
    let x = p.x as f32;
    let y = p.y as f32;
    if IsZoomed(h) == 0 {
        let b = SSR(6.0);
        let l = x < b;
        let r = x >= SW.W as f32 - b;
        let t = y < b;
        let bt = y >= SW.H as f32 - b;
        if t && l {
            return HTTOPLEFT as LRESULT;
        }
        if t && r {
            return HTTOPRIGHT as LRESULT;
        }
        if bt && l {
            return HTBOTTOMLEFT as LRESULT;
        }
        if bt && r {
            return HTBOTTOMRIGHT as LRESULT;
        }
        if t {
            return HTTOP as LRESULT;
        }
        if bt {
            return HTBOTTOM as LRESULT;
        }
        if l {
            return HTLEFT as LRESULT;
        }
        if r {
            return HTRIGHT as LRESULT;
        }
    }
    if SW.rec.open || SW.pop_open || SW.don.open {
        return HTCLIENT as LRESULT;
    }
    for i in 0..2 {
        let mut r = [0.0; 4];
        winbtn_rect(i, &mut r);
        if in_box(&r, x, y) {
            return HTCLIENT as LRESULT;
        }
    }
    if y < top_h() && x >= sb_x() + sb_w() {
        return HTCAPTION as LRESULT;
    }
    if x < sb_x() + sb_w() && y < nav_y0() - SSR(6.0) {
        return HTCAPTION as LRESULT;
    }
    HTCLIENT as LRESULT
}

unsafe extern "system" fn settings_wndproc(h: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    match msg {
        WM_NCCALCSIZE => {
            if wp != 0 {
                if IsZoomed(h) != 0 {
                    let p = lp as *mut NCCALCSIZE_PARAMS;
                    let f = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    InflateRect(&mut (*p).rgrc[0], -f, -f);
                }
                return 0;
            }
        }
        WM_NCHITTEST => return settings_nchittest(h, lp),
        WM_NCACTIVATE => return DefWindowProcW(h, msg, wp, -1), // no classic caption repaint
        WM_GETMINMAXINFO => {
            let mm = lp as *mut MINMAXINFO;
            if SW.s > 0.0 {
                (*mm).ptMinTrackSize.x = SSR(720.0) as i32;
                (*mm).ptMinTrackSize.y = SSR(460.0) as i32;
            }
            return 0;
        }
        WM_ERASEBKGND => return 1,
        WM_PAINT => {
            let mut ps: PAINTSTRUCT = zeroed();
            BeginPaint(h, &mut ps);
            EndPaint(h, &ps);
            settings_invalidate();
            return 0;
        }
        WM_SIZE => {
            let mut rc: RECT = zeroed();
            GetClientRect(h, &mut rc);
            SW.W = max(1, rc.right - rc.left);
            SW.H = max(1, rc.bottom - rc.top);
            r_target_resize(&raw mut SW.target, SW.W, SW.H);
            SW.pop_open = false;
            settings_invalidate();
            settings_render();
            return 0;
        }
        WM_DPICHANGED => {
            SW.dpi = loword(wp);
            SW.s = SW.dpi as f32 / 96.0;
            let r = &*(lp as *const RECT);
            SetWindowPos(h, null_mut(), r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
            settings_invalidate();
            return 0;
        }
        WM_SETCURSOR => {
            if loword(lp as usize) == HTCLIENT && SW.hover.type_ == HT_ROW && SW.hover.part == PART_CONTROL && k_sitems[SW.rows[SW.hover.idx as usize].item as usize].kind == SK_TEXT {
                SetCursor(LoadCursorW(null_mut(), IDC_IBEAM));
                return TRUE as LRESULT;
            }
            if loword(lp as usize) == HTCLIENT && SW.hover.type_ == HT_ROW && SW.hover.part >= PART_CARD0 {
                SetCursor(LoadCursorW(null_mut(), IDC_HAND));
                return TRUE as LRESULT;
            }
        }
        WM_MOUSEMOVE => {
            let was_hot = sb_hot();
            SW.mx = get_x_lparam(lp);
            SW.my = get_y_lparam(lp);
            if SW.press.type_ == HT_ROW && SW.press.part == PART_CONTROL && SW.press.idx < SW.nrows && k_sitems[SW.rows[SW.press.idx as usize].item as usize].kind == SK_SLIDER {
                let it = &k_sitems[SW.rows[SW.press.idx as usize].item as usize];
                if !item_locked(it) {
                    sset(it, slider_value_at(&SW.rows[SW.press.idx as usize], SW.mx as f32));
                }
            }
            if sb_hot() != was_hot || (SW.hover.type_ == HT_ROW && SW.hover.part == PART_CONTROL) {
                settings_invalidate();
            }
            let hit = settings_hit(SW.mx, SW.my);
            if hit.type_ != SW.hover.type_ || hit.idx != SW.hover.idx || hit.part != SW.hover.part {
                SW.hover = hit;
                if SW.pop_open && hit.type_ == HT_POPITEM {
                    SW.pop_hover = hit.idx;
                }
                settings_invalidate();
            }
            tip_track(); // also from one option of a control to the next
            track_mouse_leave(h);
            return 0;
        }
        WM_MOUSELEAVE => {
            SW.hover.type_ = HT_NONE;
            tip_hide();
            SW.mx = -1;
            SW.my = -1;
            settings_invalidate();
            return 0;
        }
        WM_LBUTTONDOWN | WM_LBUTTONDBLCLK => {
            SetFocus(h);
            SetCapture(h);
            SW.mx = get_x_lparam(lp);
            SW.my = get_y_lparam(lp);
            settings_mouse_down(get_x_lparam(lp), get_y_lparam(lp));
            return 0;
        }
        WM_LBUTTONUP => {
            ReleaseCapture();
            settings_mouse_up(get_x_lparam(lp), get_y_lparam(lp));
            return 0;
        }
        WM_MOUSEWHEEL => {
            if SW.rec.open {
                return 0;
            }
            let delta = hiword(wp) as u16 as i16 as f32;
            if SW.don.open {
                SW.don.scroll_target = clamp(SW.don.scroll_target - delta / WHEEL_DELTA as f32 * SSR(64.0), 0.0, max(0.0, SW.don.content_h - SW.don.view_h));
                settings_invalidate();
                return 0;
            }
            SW.pop_open = false;
            tip_hide();
            SW.scroll_target = clamp(SW.scroll_target - delta / WHEEL_DELTA as f32 * SSR(64.0), 0.0, max_scroll_s());
            settings_invalidate();
            return 0;
        }
        WM_APP_KEYCAP => {
            if SW.rec.open {
                rec_key(wp as u32, lp != 0);
            }
            return 0;
        }
        WM_KEYDOWN | WM_SYSKEYDOWN | WM_KEYUP | WM_SYSKEYUP => {
            let down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            if SW.rec.open {
                rec_key(sided_vk(wp, lp), down);
                return 0;
            }
            if (wp as u16 == VK_LWIN || wp as u16 == VK_RWIN) && hotkey_is_win_tap() {
                return 0;
            }
            if down && !(msg == WM_SYSKEYDOWN && wp as u16 == VK_F4) {
                settings_key(wp as u32);
                return 0;
            }
        }
        WM_SYSCHAR => {
            if SW.rec.open {
                return 0;
            }
            if wp != ' ' as usize {
                return 0;
            }
        }
        WM_SYSCOMMAND => {
            if (wp & 0xFFF0) == SC_TASKLIST as usize && hotkey_is_win_tap() {
                return 0;
            }
        }
        WM_CHAR => {
            if SW.editing >= 0 {
                let c = wp as u16;
                edit_splice(SW.edit_caret, SW.edit_caret, &c, 1);
                SW.caret_on = true;
                settings_invalidate();
            }
            return 0;
        }
        WM_TIMER => {
            if wp == SETTINGS_TIMER_CARET {
                SW.caret_on = !SW.caret_on;
            }
            if wp == SETTINGS_TIMER_FLASH {
                KillTimer(h, SETTINGS_TIMER_FLASH);
            }
            if wp == SETTINGS_TIMER_APPLY {
                pend_flush(false);
            }
            if wp == SETTINGS_TIMER_PV {
                KillTimer(h, SETTINGS_TIMER_PV);
                PV.dirty = true;
            }
            if wp == SETTINGS_TIMER_TIP {
                KillTimer(h, SETTINGS_TIMER_TIP);
                SW.tip_on = SW.tip_row >= 0;
                SW.tip_x = SW.mx;
                SW.tip_y = SW.my;
            }
            settings_invalidate();
            return 0;
        }
        WM_ACTIVATE => {
            SW.active = loword(wp) != WA_INACTIVE;
            if SW.active {
                hook_watchdog_off();
            }
            if !SW.active {
                end_edit(true);
                rec_close(false);
                SW.armed = false;
                SW.pop_open = false;
            } else {
                refresh_status();
                theme_update();
            }
            settings_invalidate();
            return 0;
        }
        WM_SETTINGCHANGE => {
            if lp != 0 && wcscmp(lp as *const u16, w!("ImmersiveColorSet")) == 0 {
                theme_update();
                settings_apply_theme();
            }
            return 0;
        }
        WM_CLOSE => {
            DestroyWindow(h);
            return 0;
        }
        WM_DESTROY => {
            pend_flush(true);
            rec_close(false);
            r_target_release(&raw mut SW.target);
            SW.hwnd = null_mut();
            SW.active = false;
            return 0;
        }
        _ => {}
    }
    DefWindowProcW(h, msg, wp, lp)
}

pub unsafe fn settings_open() {
    if !SW.hwnd.is_null() {
        hook_watchdog_off();
        if IsIconic(SW.hwnd) != 0 {
            ShowWindow(SW.hwnd, SW_RESTORE);
        }
        SetForegroundWindow(SW.hwnd);
        return;
    }
    window_class(w!("MixLauncherSettings"), settings_wndproc, CS_DBLCLKS, g_icon_big, g_icon_small);
    theme_update();
    refresh_status();
    SW.page = PG_GENERAL as i32;
    SW.scroll = 0.0;
    SW.focus = -1;
    SW.hover.type_ = HT_NONE;
    SW.press.type_ = HT_NONE;
    SW.editing = -1;
    SW.armed = false;
    SW.pop_open = false;
    SW.don.open = false;
    SW.tip_row = -1;
    SW.tip_on = false;
    SW.rec = zeroed();

    let mon = open_monitor();
    SW.dpi = monitor_dpi(mon);
    SW.s = SW.dpi as f32 / 96.0;
    // WS_CAPTION: system open/minimize animations; no WS_SYSMENU, so DWM draws no caption buttons.
    let style = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX;
    let ex = WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW;
    let wa = monitor_work(mon);
    let w = min(SSR(900.0) as i32, (wa.right - wa.left) - 40);
    let h = min(SSR(640.0) as i32, (wa.bottom - wa.top) - 40);
    let x = wa.left + ((wa.right - wa.left) - w) / 2;
    let y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    SW.hwnd = CreateWindowExW(
        ex,
        w!("MixLauncherSettings"),
        tr!("Настройки MixLauncher", "MixLauncher Settings"),
        style,
        x,
        y,
        w,
        h,
        null_mut(),
        null_mut(),
        GetModuleHandleW(null()),
        null(),
    );
    if SW.hwnd.is_null() {
        return;
    }
    window_icons(SW.hwnd);
    dwm_set(SW.hwnd, DWA_DARK, TH.dark as u32);
    dwm_set(SW.hwnd, DWA_CORNER, DWC_ROUND);
    dwm_set(SW.hwnd, DWA_NO_TRANSITIONS, g_cfg.reduced_motion as u32); // the system open and minimize animations
    SW.mica = dwm_backdrop(SW.hwnd, DWB_MICA);
    SetWindowPos(SW.hwnd, null_mut(), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    let mut cr: RECT = zeroed();
    GetClientRect(SW.hwnd, &mut cr);
    SW.W = max(1, cr.right);
    SW.H = max(1, cr.bottom);
    if !r_target_init(&raw mut SW.target, SW.hwnd, SW.W, SW.H) {
        DestroyWindow(SW.hwnd);
        return;
    }
    settings_snap_motion();
    SW.last_frame = time_now();
    SW.dirty = true;
    settings_render();
    hook_watchdog_off();
    ShowWindow(SW.hwnd, SW_SHOW);
    SetForegroundWindow(SW.hwnd);
}

// Opens the settings (or brings them up) and scrolls smoothly to the group of a setting.
pub unsafe fn settings_open_at(id: u8) {
    settings_open();
    let Some(it) = sitem(id) else { return };
    if SW.hwnd.is_null() {
        return;
    }
    set_page(it.page as i32);
    settings_layout();
    for i in 0..SW.nrows as usize {
        if k_sitems[SW.rows[i].item as usize].id == id {
            SW.scroll_target = clamp(SW.rows[i].y - SSR(36.0), 0.0, max_scroll_s()); // the group header just below the top
            break;
        }
    }
    settings_invalidate();
}

pub unsafe fn settings_device_lost() {
    r_target_release(&raw mut SW.target);
}

pub unsafe fn settings_device_restored() {
    PV.ok = false;
    if !SW.hwnd.is_null() && r_target_init(&raw mut SW.target, SW.hwnd, SW.W, SW.H) {
        settings_invalidate();
    }
}
