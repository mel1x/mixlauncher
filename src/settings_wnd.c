// The settings window: page drawing, animation, hit testing, input and the window itself.

static void sb_button_rects(void)
{
    f32 x = sb_x(), w = sb_w(), bot = (f32)SW.H - sb_x() - SSR(8), bh = SSR(36);
    box_set(SW.sb_btn[1], x + SSR(8), bot - bh, w - SSR(16), bh);
    box_set(SW.sb_btn[0], x + SSR(8), bot - bh * 2 - SSR(6), w - SSR(16), bh);
}

static void draw_sidebar_buttons(const Theme *t, const SColors *c)
{
    sb_button_rects();
    f32 fs = SSC(14);
    for (int i = upd_visible() ? 0 : 1; i < 2; i++) {
        f32 *b = SW.sb_btn[i], bw = b[2] - b[0], bh = b[3] - b[1], cy = (b[1] + b[3]) * 0.5f, ix = b[0] + SSR(10), isz = SSR(20);
        f32 hv = SW.sb_hov[i];
        WCHAR ub[64];
        const WCHAR *lbl;
        u32 glyph, icol, fg;
        if (i == 0) {
            int st = upd_state();
            lbl = upd_label(ub, countof(ub));
            r_rect(b[0], b[1], bw, bh, color_alpha(t->accent, 0.18f + 0.10f * hv), SSR(8));
            if (st == UPD_DOWNLOADING) {
                f32 pw = floorf(bw * (f32)UPD.progress / 1000.f);
                r_set_clip(b[0], b[1], b[0] + pw, b[3]);
                r_rect(b[0], b[1], bw, bh, color_alpha(t->accent, 0.22f), SSR(8));
                r_clip_all();
            }
            glyph = st == UPD_ERROR ? 0xE72C : 0xE896;
            icol = fg = st == UPD_ERROR ? t->danger : t->text;
        } else {
            lbl = TR("Поддержать автора", "Support the author");
            if (hv > 0) r_rect(b[0], b[1], bw, bh, color_alpha(c->nav_hover, hv), SSR(8));
            glyph = 0xEB51;
            icol = color_mix(t->faint, t->dim, hv);
            fg = color_mix(t->dim, t->text, hv);
        }
        text_draw_icon(glyph, SSC(15), ix + isz * 0.5f, cy, icol);
        text_draw_fit(FONT_TEXT, fs, ix + isz + SSR(12), text_baseline(FONT_TEXT, fs, cy), b[2] - ix - isz - SSR(18), lbl, -1, fg, false);
    }
}

static void settings_draw(void)
{
    const Theme *t = &TH;
    SColors c = scolors(t->dark);
    if (!SW.mica) r_rect(0, 0, (f32)SW.W, (f32)SW.H, c.bg, 0);
    SW.scroll = CLAMP(SW.scroll, 0.f, max_scroll_s());

    draw_sidebar(t, &c);
    draw_sidebar_buttons(t, &c);

    f32 pe = ease_out_cubic(SW.page_t), rise = floorf((1.f - pe) * SSR(10) + 0.5f);
    R.opacity = pe;

    f32 x0 = content_x0();
    text_draw_fit(FONT_TEXT_SEMIBOLD, SSC(20), x0 + SSR(2), SSR(40) + rise, content_x1() - x0 - SSR(100), ss(k_pages[SW.page].name), -1, t->text,
                  false);

    f32 off = top_h() - floorf(SW.scroll + 0.5f) + rise;
    r_set_clip(0, top_h(), (f32)SW.W, (f32)SW.H);
    for (int i = 0; i < SW.ncards; i++) {
        SBox *b = &SW.cards[i];
        if (b->h < 1) continue;
        r_rect(b->x, b->y + off, b->w, b->h, c.card, SSR(10));
        r_border(b->x, b->y + off, b->w, b->h, c.card_border, SSR(10));
    }
    for (int i = 0; i < SW.nheads; i++) {
        if (SW.head_a[i] <= 0.f) continue;
        f32 hy = SW.heads[i].y + off - SSR(15);
        if (SW.head_a[i] < 1.f) r_set_clip(0, MAX(top_h(), hy), (f32)SW.W, MIN((f32)SW.H, hy + SW.heads[i].h));
        R.opacity = pe * SW.head_a[i];
        text_draw(FONT_TEXT_SEMIBOLD, SSC(13), SW.heads[i].x, SW.heads[i].y + off + SSR(5), ss(SW.head_text[i]), -1, color_mix(t->dim, t->text, 0.35f));
        r_set_clip(0, top_h(), (f32)SW.W, (f32)SW.H);
    }
    for (int i = 0; i < SW.nrows; i++) {
        SRow *r = &SW.rows[i];
        if (r->h < 1 || r->y + off + r->h < top_h() || r->y + off > SW.H) continue;
        // A row on its way in or out is revealed from the top and fades with it.
        bool part = r->vis < 1.f;
        if (part) r_set_clip(0, MAX(top_h(), r->y + off), (f32)SW.W, MIN((f32)SW.H, r->y + off + r->h));
        R.opacity = pe * r->vis;
        draw_srow(r, off, t, &c);
        if (part) r_set_clip(0, top_h(), (f32)SW.W, (f32)SW.H);
    }
    r_clip_all();
    R.opacity = 1.f;

    f32 sep_k = CLAMP(SW.scroll / SSR(16), 0.f, 1.f);
    if (sep_k > 0.f) r_rect(x0, top_h() - 1, content_x1() - x0, 1, color_alpha(t->sep, sep_k), 0);

    f32 ms = max_scroll_s();
    if (ms > 0 && SW.sb_t > 0.f) {
        bool hot = sb_hot();
        f32 vh = (f32)SW.H - top_h() - SSR(8), bar_h = MAX(SSR(32), vh * vh / SW.content_h);
        f32 by = top_h() + (vh - bar_h) * CLAMP(SW.scroll / ms, 0.f, 1.f), bw = hot ? SSR(5) : SSR(3);
        r_rect((f32)SW.W - SSR(4) - bw, floorf(by), bw, floorf(bar_h), color_alpha(hot ? t->dim : t->faint, SW.sb_t), bw * 0.5f);
    }

    for (int i = 0; i < 2; i++) {
        f32 r[4];
        winbtn_rect(i, r);
        bool hov = SW.hover.type == (i ? HT_WINCLOSE : HT_WINMIN);
        bool down = hov && SW.press.type == SW.hover.type;
        f32 h = SW.win_hov[i];
        u32 fg = SW.active ? t->text : t->faint;
        if (i == 1) {
            r_rect(r[0], r[1], r[2] - r[0], r[3] - r[1], color_alpha(down ? RGBA(200, 64, 50, 255) : RGBA(196, 43, 28, 255), h), 0);
            fg = color_mix(fg, RGBA(255, 255, 255, 255), h);
        } else {
            r_rect(r[0], r[1], r[2] - r[0], r[3] - r[1], color_alpha(down ? c.ctl_hover : c.ctl, h), 0);
        }
        text_draw_icon(i ? 0xE8BB : 0xE921, SSC(10), (r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f, fg);
    }

    if (SW.pop_open && SW.pop_row >= 0 && SW.pop_row < SW.nrows) {
        R.opacity = ease_out_cubic(SW.pop_t);
        draw_popover(t, &c);
        R.opacity = 1.f;
    }
    if (SW.rec.open) {
        R.opacity = ease_out_cubic(SW.rec_t);
        draw_recorder(t, &c);
        R.opacity = 1.f;
    }
    if (SW.don.open) draw_donate(t, &c);
    confetti_draw();
    int tip_key;
    const WCHAR *tip = SW.tip_on ? tip_at(&tip_key) : NULL;
    if (tip && tip_key == SW.tip_row) draw_tip(t, &c, tip);
}

static void tip_hide(void)
{
    SW.tip_row = -1;
    SW.tip_on = false;
    if (SW.hwnd) KillTimer(SW.hwnd, SETTINGS_TIMER_TIP);
}

// Arms the tooltip when the mouse comes to one; it shows after a pause, like a system tooltip.
static void tip_track(void)
{
    int row;
    tip_at(&row);
    if (row == SW.tip_row) return;
    tip_hide();
    SW.tip_row = row;
    if (row >= 0) SetTimer(SW.hwnd, SETTINGS_TIMER_TIP, 600, NULL);
    settings_invalidate();
}

static void settings_snap_motion(void)
{
    f32 r[4];
    nav_rect(SW.page, r);
    SW.nav_y = r[1];
    SW.nav_v = 0;
    for (int i = 0; i < (int)SITEMS; i++) {
        SW.tog[i] = k_sitems[i].kind == SK_TOGGLE && sval(k_sitems[i].id) ? 1.f : 0.f;
        SW.seg[i] = is_seg(&k_sitems[i]) ? (f32)sval(k_sitems[i].id) : 0.f;
        SW.vis[i] = item_shown(i) ? 1.f : 0.f;
    }
    memset(SW.nav_hov, 0, sizeof SW.nav_hov);
    memset(SW.win_hov, 0, sizeof SW.win_hov);
    SW.scroll_target = SW.scroll;
    SW.scroll_v = 0;
    SW.page_t = SW.pop_t = SW.rec_t = 1.f;
    if (SW.don.closing) SW.don.open = SW.don.closing = false;
    SW.don.t = 1.f;
    SW.don.scroll = SW.don.scroll_target = CLAMP(SW.don.scroll_target, 0.f, MAX(0.f, SW.don.content_h - SW.don.view_h));
    SW.don.scroll_v = 0;
    SW.don.sb_t = SW.don.content_h > SW.don.view_h ? 1.f : 0.f;
    for (int i = 0; i < 10; i++) SW.don.hov[i] = SW.hover.type == HT_DON && SW.hover.idx == (i == DON_HOV_LINK ? DON_LINK : i == DON_HOV_CLOSE ? DON_CLOSE : i) ? 1.f : 0.f;
    SW.sb_hov[0] = SW.sb_hov[1] = 0;
    SW.sb_t = 0;
    SW.sb_prev = SW.scroll;
    SW.row_hov = 0;
    SW.row_hov_idx = -1;
}

static bool settings_animate(f32 dt)
{
    if (!anims_enabled()) {
        SW.scroll = SW.scroll_target = CLAMP(SW.scroll_target, 0.f, max_scroll_s());
        settings_snap_motion();
        SW.sb_t = max_scroll_s() > 0 ? 1.f : 0.f;
        SW.row_hov_idx = row_hover_target();
        SW.row_hov = SW.row_hov_idx >= 0 ? 1.f : 0.f;
        return false;
    }
    bool anim = false;
    f32 r[4];
    nav_rect(SW.page, r);
    anim |= spring_step(&SW.nav_y, &SW.nav_v, r[1], anim_omega(g_cfg.anim_select_ms + 20), dt);
    for (int i = 0; i < PG__COUNT; i++)
        anim |= approach(&SW.nav_hov[i], SW.hover.type == HT_NAV && SW.hover.idx == i && i != SW.page ? 1.f : 0.f, 20.f, dt);
    for (int i = 0; i < 2; i++) anim |= approach(&SW.win_hov[i], SW.hover.type == (i ? HT_WINCLOSE : HT_WINMIN) ? 1.f : 0.f, 24.f, dt);
    for (int i = 0; i < (int)SITEMS; i++) {
        if (k_sitems[i].kind == SK_TOGGLE) anim |= approach(&SW.tog[i], sval(k_sitems[i].id) ? 1.f : 0.f, 20.f, dt);
        if (is_seg(&k_sitems[i])) anim |= approach(&SW.seg[i], (f32)sval(k_sitems[i].id), 18.f, dt);
        f32 want = item_shown(i) ? 1.f : 0.f, vd = anim_sec(260);
        if (SW.vis[i] != want) {
            SW.vis[i] = vd > 0.f ? (want > SW.vis[i] ? MIN(want, SW.vis[i] + dt / vd) : MAX(want, SW.vis[i] - dt / vd)) : want;
            anim = true;
        }
    }
    if (SW.pop_open && SW.pop_row >= 0 && SW.pop_row < SW.nrows && !item_shown(SW.rows[SW.pop_row].item)) SW.pop_open = false;
    SW.scroll_target = CLAMP(SW.scroll_target, 0.f, max_scroll_s());
    anim |= spring_step(&SW.scroll, &SW.scroll_v, SW.scroll_target, anim_omega(g_cfg.anim_scroll_ms), dt);
    f64 now = time_now();
    if (SW.scroll != SW.sb_prev) SW.sb_seen = now;
    SW.sb_prev = SW.scroll;
    bool recent = now - SW.sb_seen < 0.9, hot = sb_hot();
    bool sb_on = max_scroll_s() > 0 && (hot || recent);
    anim |= approach(&SW.sb_t, sb_on ? 1.f : 0.f, sb_on ? 20.f : 7.f, dt);
    if (recent && !hot && max_scroll_s() > 0) anim = true;
    int rh = row_hover_target();
    if (rh >= 0 && rh != SW.row_hov_idx) {
        SW.row_hov_idx = rh;
        SW.row_hov = 0;
    }
    anim |= approach(&SW.row_hov, rh >= 0 ? 1.f : 0.f, 22.f, dt);
    for (int i = 0; i < 2; i++)
        anim |= approach(&SW.sb_hov[i], SW.hover.type == (i ? HT_DONATE : HT_SBUPDATE) ? 1.f : 0.f, 22.f, dt);
    anim |= don_animate(dt);
    anim |= confetti_animate(dt);
    f32 *entr[3] = { &SW.page_t, &SW.pop_t, &SW.rec_t };
    f32 dur[3] = { anim_sec(g_cfg.anim_page_ms), anim_sec(g_cfg.anim_menu_ms) * 0.85f, anim_sec(160) };
    for (int i = 0; i < 3; i++)
        if (*entr[i] < 1.f) {
            *entr[i] = dur[i] > 0.f ? MIN(1.f, *entr[i] + dt / dur[i]) : 1.f;
            anim = true;
        }
    return anim;
}

static void settings_render(void)
{
    if (!SW.hwnd || !SW.target.ok || !R.ok) return;
    r_use(&SW.target);
    r_wait_frame();
    f64 now = time_now();
    f32 dt = (f32)(now - SW.last_frame);
    if (dt > 0.1f || dt <= 0) dt = 1.f / 60.f;
    SW.last_frame = now;
    settings_layout();
    SW.animating = settings_animate(dt);
    R.scale = 1.f;
    r_draw(settings_draw);
    r_end_and_present();
    r_use(&R.main);
    SW.dirty = false;
}

static SHit settings_hit(int mx, int my)
{
    SHit h = { HT_NONE, PART_ROW, -1 };
    f32 x = (f32)mx, y = (f32)my;
    if (SW.rec.open) {
        h.type = in_box(SW.rec_btn[1], x, y) ? HT_RECSAVE : in_box(SW.rec_btn[0], x, y) ? HT_RECCANCEL : in_box(SW.rec_panel, x, y) ? HT_RECPANEL : HT_RECOUT;
        return h;
    }
    if (don_active()) {
        h.type = HT_DON;
        h.idx = in_box(SW.don.panel, x, y) ? DON_PANEL : DON_OUT;
        if (h.idx == DON_PANEL) {
            if (in_box(SW.don.close, x, y)) h.idx = DON_CLOSE;
            else if (in_box(SW.don.link, x, y)) h.idx = DON_LINK;
            else
                for (int i = 0; i < 8; i++)
                    if (in_box(SW.don.copy[i], x, y)) h.idx = i;
        }
        return h;
    }
    if (SW.pop_open) {
        h.type = HT_POPOUT;
        if (x >= SW.pop_x && x < SW.pop_x + SW.pop_w && y >= SW.pop_y && y < SW.pop_y + SW.pop_h) {
            int k = (int)floorf((y - SW.pop_y - SSR(5)) / SSR(32));
            if (k >= 0 && k < choice_count(&k_sitems[SW.rows[SW.pop_row].item])) {
                h.type = HT_POPITEM;
                h.idx = k;
            }
        }
        return h;
    }
    for (int i = 0; i < 2; i++) {
        f32 r[4];
        winbtn_rect(i, r);
        if (in_box(r, x, y)) {
            h.type = i ? HT_WINCLOSE : HT_WINMIN;
            return h;
        }
    }
    sb_button_rects();
    if (in_box(SW.sb_btn[1], x, y)) {
        h.type = HT_DONATE;
        return h;
    }
    if (upd_visible() && in_box(SW.sb_btn[0], x, y)) {
        h.type = HT_SBUPDATE;
        return h;
    }
    for (int i = 0; i < PG__COUNT; i++) {
        f32 r[4];
        nav_rect(i, r);
        if (in_box(r, x, y)) {
            h.type = HT_NAV;
            h.idx = i;
            return h;
        }
    }
    if (y < top_h()) return h;
    f32 cy = y - top_h() + floorf(SW.scroll + 0.5f);
    for (int i = 0; i < SW.nrows; i++) {
        SRow *r = &SW.rows[i];
        if (x < r->x || x >= r->x + r->w || cy < r->y || cy >= r->y + r->h) continue;
        h.type = HT_ROW;
        h.idx = i;
        f32 g = SSR(4);
        if (x >= r->cx - g && x < r->cx + r->cw + g && cy >= r->cy - g && cy < r->cy + r->ch + g) {
            h.part = PART_CONTROL;
            if (k_sitems[r->item].kind == SK_STEPPER) {
                if (x < r->cx + SSR(34)) h.part = PART_MINUS;
                else if (x >= r->cx + r->cw - SSR(34)) h.part = PART_PLUS;
            }
        }
        return h;
    }
    return h;
}

static void ensure_focus_visible(void)
{
    if (!row_focusable(SW.focus)) return;
    SRow *r = &SW.rows[SW.focus];
    f32 vh = (f32)SW.H - top_h();
    f32 top = r->y - SSR(40), bot = r->y + r->h + SSR(12);
    if (top < SW.scroll_target) SW.scroll_target = top;
    else if (bot > SW.scroll_target + vh) SW.scroll_target = bot - vh;
    SW.scroll_target = CLAMP(SW.scroll_target, 0.f, max_scroll_s());
}

static void pop_open(int row)
{
    const SItem *it = &k_sitems[SW.rows[row].item];
    SRow *r = &SW.rows[row];
    f32 fs = SSC(13.5f), w = 0;
    int nopt = choice_count(it);
    for (int k = 0; k < nopt; k++) w = MAX(w, text_width(FONT_TEXT, fs, ss(it->opt[k]), -1));
    SW.pop_w = MAX(r->cw, floorf(w + SSR(5) + SSR(32) + SSR(24)));
    SW.pop_h = SSR(32) * (f32)nopt + SSR(10);
    f32 off = top_h() - floorf(SW.scroll + 0.5f);
    SW.pop_x = floorf(r->cx + r->cw - SW.pop_w);
    SW.pop_y = r->cy + off + r->ch + SSR(4);
    if (SW.pop_y + SW.pop_h > (f32)SW.H - SSR(8)) SW.pop_y = r->cy + off - SSR(4) - SW.pop_h;
    SW.pop_open = true;
    SW.pop_t = 0;
    SW.pop_row = row;
    SW.pop_hover = sval(it->id);
    settings_invalidate();
}

static void pop_close(void)
{
    SW.pop_open = false;
    settings_invalidate();
}

static void set_page(int page)
{
    page = (page + PG__COUNT) % PG__COUNT;
    if (page == SW.page) return;
    end_edit(true);
    SW.pop_open = false;
    SW.armed = false;
    SW.page = page;
    SW.scroll = SW.scroll_target = SW.scroll_v = 0;
    SW.page_t = 0;
    SW.focus = -1;
    SW.hover.type = HT_NONE;
    tip_hide();
    if (page == PG_SEARCH) refresh_status();  // the Everything status
    settings_invalidate();
}

static void activate_row(int ri, int part)
{
    const SItem *it = &k_sitems[SW.rows[ri].item];
    if (item_locked(it)) return;
    if (it->id != SID_CLEARHIST) SW.armed = false;
    switch (it->kind) {
    case SK_TOGGLE: sset(it, !sval(it->id)); break;
    case SK_CHOICE:
        if (is_seg(it)) sset(it, SW.seg_click >= 0 ? SW.seg_click : (sval(it->id) + 1) % choice_count(it));
        else pop_open(ri);
        SW.seg_click = -1;
        break;
    case SK_STEPPER:
        if (part == PART_MINUS) sset(it, sval(it->id) - it->step);
        else if (part == PART_PLUS) sset(it, sval(it->id) + it->step);
        break;
    case SK_TEXT: begin_edit(ri); break;
    case SK_HOTKEY: rec_open(); break;
    case SK_BUTTON: button_action(it); break;
    }
    settings_invalidate();
}

static void move_focus(int dir)
{
    int i = SW.focus;
    if (!row_focusable(i)) i = dir > 0 ? -1 : SW.nrows;
    for (int k = 0; k < SW.nrows; k++) {
        i += dir;
        if (i < 0 || i >= SW.nrows) break;
        if (row_focusable(i)) {
            SW.focus = i;
            break;
        }
    }
    ensure_focus_visible();
    settings_invalidate();
}

static void settings_key(UINT vk)
{
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (SW.don.open) {
        if (vk == VK_ESCAPE) don_close();
        else if (vk == VK_DOWN || vk == VK_NEXT) SW.don.scroll_target += SSR(vk == VK_NEXT ? 240.f : 48.f);
        else if (vk == VK_UP || vk == VK_PRIOR) SW.don.scroll_target -= SSR(vk == VK_PRIOR ? 240.f : 48.f);
        settings_invalidate();
        return;
    }
    if (SW.pop_open) {
        const SItem *it = &k_sitems[SW.rows[SW.pop_row].item];
        switch (vk) {
        case VK_UP: SW.pop_hover = (SW.pop_hover + choice_count(it) - 1) % choice_count(it); break;
        case VK_DOWN: SW.pop_hover = (SW.pop_hover + 1) % choice_count(it); break;
        case VK_RETURN:
        case VK_SPACE:
            sset(it, SW.pop_hover);
            pop_close();
            break;
        case VK_ESCAPE:
        case VK_TAB: pop_close(); break;
        }
        settings_invalidate();
        return;
    }
    if (SW.editing >= 0) {
        if (vk == VK_TAB) {
            end_edit(true);
            move_focus(shift ? -1 : 1);
            return;
        }
        edit_key(vk, ctrl);
        return;
    }
    if (ctrl && (vk == VK_TAB || vk == VK_NEXT || vk == VK_PRIOR)) {
        set_page(SW.page + ((vk == VK_TAB && shift) || vk == VK_PRIOR ? -1 : 1));
        return;
    }
    switch (vk) {
    case VK_ESCAPE:
        if (SW.armed) {
            SW.armed = false;
            settings_invalidate();
        } else {
            DestroyWindow(SW.hwnd);
        }
        return;
    case VK_DOWN: move_focus(1); return;
    case VK_UP: move_focus(-1); return;
    case VK_TAB: move_focus(shift ? -1 : 1); return;
    case VK_NEXT: SW.scroll_target += ((f32)SW.H - top_h()) * 0.8f; settings_invalidate(); return;
    case VK_PRIOR: SW.scroll_target -= ((f32)SW.H - top_h()) * 0.8f; settings_invalidate(); return;
    case 'W':
        if (ctrl) DestroyWindow(SW.hwnd);
        return;
    }
    if (!row_focusable(SW.focus)) return;
    const SItem *it = &k_sitems[SW.rows[SW.focus].item];
    switch (vk) {
    case VK_LEFT:
    case VK_RIGHT: {
        int d = vk == VK_LEFT ? -1 : 1;
        if (it->kind == SK_STEPPER) sset(it, sval(it->id) + d * it->step);
        else if (it->kind == SK_CHOICE) sset(it, sval(it->id) + d);
        else if (it->kind == SK_TOGGLE) sset(it, d > 0);
        break;
    }
    case VK_SPACE:
    case VK_RETURN:
        activate_row(SW.focus, it->kind == SK_STEPPER ? PART_ROW : PART_CONTROL);
        break;
    }
    settings_invalidate();
}

static void settings_mouse_down(int mx, int my)
{
    SHit h = settings_hit(mx, my);
    SW.press = h;
    tip_hide();
    if (SW.rec.open) {
        if (h.type == HT_RECSAVE) rec_close(true);
        else if (h.type == HT_RECCANCEL || h.type == HT_RECOUT) rec_close(false);
        return;
    }
    if (SW.don.open) {
        if (SW.don.closing) return;
        if (h.idx == DON_OUT || h.idx == DON_CLOSE) {
            don_close();
        } else if (h.idx == DON_LINK) {
            launch_open(DON_LINK_URL, OPEN_PATH);
            confetti_burst();
        } else if (h.idx >= 0) {
            don_copy(h.idx);
            confetti_burst();
        }
        return;
    }
    if (h.type == HT_DONATE) {
        don_open();
        return;
    }
    if (h.type == HT_SBUPDATE) {
        upd_download();
        settings_invalidate();
        return;
    }
    if (SW.pop_open) {
        if (h.type == HT_POPITEM) sset(&k_sitems[SW.rows[SW.pop_row].item], h.idx);
        pop_close();
        return;
    }
    if (SW.editing >= 0 && !(h.type == HT_ROW && h.idx == SW.editing && h.part == PART_CONTROL)) end_edit(true);
    if (h.type == HT_NAV) {
        set_page(h.idx);
    } else if (h.type == HT_ROW) {
        SW.focus = h.idx;
        const SItem *it = &k_sitems[SW.rows[h.idx].item];
        if (is_seg(it) && h.part == PART_CONTROL) SW.seg_click = seg_index_at(it, SW.rows[h.idx].cx, (f32)mx);
        if ((it->kind == SK_TOGGLE || h.part != PART_ROW) && !(it->kind == SK_TEXT && SW.editing == h.idx)) activate_row(h.idx, h.part);
        else if (SW.armed) SW.armed = false;
    } else if (SW.armed) {
        SW.armed = false;
    }
    settings_invalidate();
}

static void settings_mouse_up(int mx, int my)
{
    SHit h = settings_hit(mx, my);
    if (SW.press.type == HT_WINCLOSE && h.type == HT_WINCLOSE) DestroyWindow(SW.hwnd);
    else if (SW.press.type == HT_WINMIN && h.type == HT_WINMIN) ShowWindow(SW.hwnd, SW_MINIMIZE);
    SW.press.type = HT_NONE;
    settings_invalidate();
}

static void settings_apply_theme(void)
{
    if (!SW.hwnd) return;
    dwm_set(SW.hwnd, DWA_DARK, TH.dark);
    settings_invalidate();
}

static LRESULT settings_nchittest(HWND h, LPARAM lp)
{
    POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    ScreenToClient(h, &p);
    f32 x = (f32)p.x, y = (f32)p.y;
    if (!IsZoomed(h)) {
        f32 b = SSR(6);
        bool L = x < b, R = x >= SW.W - b, T = y < b, B = y >= SW.H - b;
        if (T && L) return HTTOPLEFT;
        if (T && R) return HTTOPRIGHT;
        if (B && L) return HTBOTTOMLEFT;
        if (B && R) return HTBOTTOMRIGHT;
        if (T) return HTTOP;
        if (B) return HTBOTTOM;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
    }
    if (SW.rec.open || SW.pop_open || SW.don.open) return HTCLIENT;
    for (int i = 0; i < 2; i++) {
        f32 r[4];
        winbtn_rect(i, r);
        if (in_box(r, x, y)) return HTCLIENT;
    }
    if (y < top_h() && x >= sb_x() + sb_w()) return HTCAPTION;
    if (x < sb_x() + sb_w() && y < nav_y0() - SSR(6)) return HTCAPTION;
    return HTCLIENT;
}

static LRESULT CALLBACK settings_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp) {
            if (IsZoomed(h)) {
                NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
                int f = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                InflateRect(&p->rgrc[0], -f, -f);
            }
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return settings_nchittest(h, lp);
    case WM_NCACTIVATE:
        return DefWindowProcW(h, msg, wp, -1);  // no classic caption repaint
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        if (SW.s > 0) {
            mm->ptMinTrackSize.x = (LONG)SSR(720);
            mm->ptMinTrackSize.y = (LONG)SSR(460);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        settings_invalidate();
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(h, &rc);
        SW.W = MAX(1, rc.right - rc.left);
        SW.H = MAX(1, rc.bottom - rc.top);
        r_target_resize(&SW.target, SW.W, SW.H);
        SW.pop_open = false;
        settings_invalidate();
        settings_render();
        return 0;
    }
    case WM_DPICHANGED: {
        SW.dpi = LOWORD(wp);
        SW.s = (f32)SW.dpi / 96.f;
        RECT *r = (RECT *)lp;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        settings_invalidate();
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && SW.hover.type == HT_ROW && SW.hover.part == PART_CONTROL &&
            k_sitems[SW.rows[SW.hover.idx].item].kind == SK_TEXT) {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_IBEAM));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        bool was_hot = sb_hot();
        SW.mx = GET_X_LPARAM(lp);
        SW.my = GET_Y_LPARAM(lp);
        if (sb_hot() != was_hot || (SW.hover.type == HT_ROW && SW.hover.part == PART_CONTROL)) settings_invalidate();
        SHit hit = settings_hit(SW.mx, SW.my);
        if (hit.type != SW.hover.type || hit.idx != SW.hover.idx || hit.part != SW.hover.part) {
            SW.hover = hit;
            if (SW.pop_open && hit.type == HT_POPITEM) SW.pop_hover = hit.idx;
            settings_invalidate();
        }
        tip_track();  // also from one option of a control to the next
        track_mouse_leave(h);
        return 0;
    }
    case WM_MOUSELEAVE:
        SW.hover.type = HT_NONE;
        tip_hide();
        SW.mx = SW.my = -1;
        settings_invalidate();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetFocus(h);
        SetCapture(h);
        settings_mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        settings_mouse_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL:
        if (SW.rec.open) return 0;
        if (SW.don.open) {
            SW.don.scroll_target = CLAMP(SW.don.scroll_target - (f32)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * SSR(64), 0.f,
                                         MAX(0.f, SW.don.content_h - SW.don.view_h));
            settings_invalidate();
            return 0;
        }
        SW.pop_open = false;
        tip_hide();
        SW.scroll_target = CLAMP(SW.scroll_target - (f32)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * SSR(64), 0.f, max_scroll_s());
        settings_invalidate();
        return 0;
    case WM_APP_KEYCAP:
        if (SW.rec.open) rec_key((UINT)wp, lp != 0);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        if (SW.rec.open) {
            rec_key(sided_vk(wp, lp), down);
            return 0;
        }
        if ((wp == VK_LWIN || wp == VK_RWIN) && hotkey_is_win_tap()) return 0;
        if (!down) break;
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;
        settings_key((UINT)wp);
        return 0;
    }
    case WM_SYSCHAR:
        if (SW.rec.open) return 0;
        if (wp == ' ') break;
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_TASKLIST && hotkey_is_win_tap()) return 0;
        break;
    case WM_CHAR:
        if (SW.editing >= 0) {
            WCHAR c = (WCHAR)wp;
            edit_splice(SW.edit_caret, SW.edit_caret, &c, 1);
            SW.caret_on = true;
            settings_invalidate();
        }
        return 0;
    case WM_TIMER:
        if (wp == SETTINGS_TIMER_CARET) SW.caret_on = !SW.caret_on;
        if (wp == SETTINGS_TIMER_FLASH) KillTimer(h, SETTINGS_TIMER_FLASH);
        if (wp == SETTINGS_TIMER_APPLY) pend_flush(false);
        if (wp == SETTINGS_TIMER_TIP) {
            KillTimer(h, SETTINGS_TIMER_TIP);
            SW.tip_on = SW.tip_row >= 0;
            SW.tip_x = SW.mx;
            SW.tip_y = SW.my;
        }
        settings_invalidate();
        return 0;
    case WM_ACTIVATE:
        SW.active = LOWORD(wp) != WA_INACTIVE;
        if (SW.active) hook_watchdog_off();
        if (!SW.active) {
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
    case WM_SETTINGCHANGE:
        if (lp && !wcscmp((const WCHAR *)lp, L"ImmersiveColorSet")) {
            theme_update();
            settings_apply_theme();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        pend_flush(true);
        rec_close(false);
        r_target_release(&SW.target);
        SW.hwnd = NULL;
        SW.active = false;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void settings_open(void)
{
    if (SW.hwnd) {
        hook_watchdog_off();
        if (IsIconic(SW.hwnd)) ShowWindow(SW.hwnd, SW_RESTORE);
        SetForegroundWindow(SW.hwnd);
        return;
    }
    window_class(SETTINGS_CLASS, settings_wndproc, CS_DBLCLKS, g_icon_big, g_icon_small);
    theme_update();
    refresh_status();
    SW.page = PG_GENERAL;
    SW.scroll = 0;
    SW.focus = -1;
    SW.hover.type = SW.press.type = HT_NONE;
    SW.editing = -1;
    SW.armed = false;
    SW.pop_open = false;
    SW.don.open = false;
    SW.tip_row = -1;
    SW.tip_on = false;
    memset(&SW.rec, 0, sizeof SW.rec);

    HMONITOR mon = open_monitor();
    SW.dpi = monitor_dpi(mon);
    SW.s = (f32)SW.dpi / 96.f;
    // WS_CAPTION: system open/minimize animations; no WS_SYSMENU, so DWM draws no caption buttons.
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX;
    DWORD ex = WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW;
    RECT wa = monitor_work(mon);
    int w = MIN((int)SSR(900), (int)(wa.right - wa.left) - 40), h = MIN((int)SSR(640), (int)(wa.bottom - wa.top) - 40);
    int x = wa.left + ((wa.right - wa.left) - w) / 2, y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    SW.hwnd = CreateWindowExW(ex, SETTINGS_CLASS, TR("Настройки MixLauncher", "MixLauncher Settings"), style, x, y, w, h, NULL, NULL,
                              GetModuleHandleW(NULL), NULL);
    if (!SW.hwnd) return;
    window_icons(SW.hwnd);
    dwm_set(SW.hwnd, DWA_DARK, TH.dark);
    dwm_set(SW.hwnd, DWA_CORNER, DWC_ROUND);
    dwm_set(SW.hwnd, DWA_NO_TRANSITIONS, g_cfg.reduced_motion);  // the system open and minimize animations
    SW.mica = dwm_backdrop(SW.hwnd, DWB_MICA);
    SetWindowPos(SW.hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RECT cr;
    GetClientRect(SW.hwnd, &cr);
    SW.W = MAX(1, cr.right);
    SW.H = MAX(1, cr.bottom);
    if (!r_target_init(&SW.target, SW.hwnd, SW.W, SW.H)) {
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
static void settings_open_at(int id)
{
    settings_open();
    const SItem *it = sitem(id);
    if (!SW.hwnd || !it) return;
    set_page(it->page);
    settings_layout();
    for (int i = 0; i < SW.nrows; i++)
        if (k_sitems[SW.rows[i].item].id == id) {
            SW.scroll_target = CLAMP(SW.rows[i].y - SSR(36), 0.f, max_scroll_s());  // the group header just below the top
            break;
        }
    settings_invalidate();
}

static void settings_device_lost(void) { r_target_release(&SW.target); }

static void settings_device_restored(void)
{
    if (SW.hwnd && r_target_init(&SW.target, SW.hwnd, SW.W, SW.H)) settings_invalidate();
}
