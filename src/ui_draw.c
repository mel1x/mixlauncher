// Drawing the launcher: the rows, the query field, the footer and the frame around them.

static void draw_placeholder_icon(f32 x, f32 y, f32 sz, const WCHAR *name)
{
    r_rect(x, y, sz, sz, TH.tile, SR(LS()->tile_r));
    if (name && name[0]) {
        WCHAR c[2] = { (WCHAR)towupper(name[0]), 0 };
        f32 fs = sz * 0.5f;
        f32 w = text_width(FONT_TEXT_SEMIBOLD, fs, c, 1);
        text_draw(FONT_TEXT_SEMIBOLD, fs, floorf(x + (sz - w) * 0.5f), y + sz * 0.5f + font_cap_height(FONT_TEXT_SEMIBOLD, fs) * 0.5f, c, 1, TH.dim);
    }
}

#define APP_ICON_KEY(a) ((a)->id_hash ^ 0xA11)

// The app's icon; until it loads a blank tile, its first letter if it has none.
static void draw_app_icon(App *a, f32 x, f32 y, f32 sz)
{
    f32 uv[4];
    if (icon_get(APP_ICON_KEY(a), ICON_KIND_APP, a->id, uv)) r_icon(x, y, sz, sz, uv[0], uv[1], uv[2], uv[3], 1.f);
    else draw_placeholder_icon(x, y, sz, icon_failed(APP_ICON_KEY(a)) ? a->name : NULL);
}

static void draw_file_icon(FileItem *f, f32 x, f32 y, f32 sz)
{
    f32 uv[4];
    if (icon_get(f->icon_key, ICON_KIND_FILE, f->full, uv)) r_icon(x, y, sz, sz, uv[0], uv[1], uv[2], uv[3], 1.f);
    else if (icon_failed(f->icon_key)) draw_placeholder_icon(x, y, sz, f->name);
}

static const WCHAR *row_note(Row *r, u32 *col)
{
    *col = TH.faint;
    if (!r || r->kind != ROW_APP) return NULL;
    if (r->app->kind != APP_CMD) return TR("Приложение", "Application");
    if (U.armed == r->app) {
        *col = TH.danger;
        return TR("Нажмите Enter ещё раз для подтверждения", "Press Enter again to confirm");
    }
    return TR("Команда", "Command");
}

// The capsule style names what Enter does at the right of the selected row, with the key.
static const WCHAR *row_action(Row *r, u32 *col)
{
    const WCHAR *note = row_note(r, col);
    if (*col == TH.danger) return note;
    *col = TH.dim;
    return r->kind == ROW_APP && r->app->kind == APP_CMD ? TR("Выполнить", "Run") : TR("Открыть", "Open");
}

#define CAP_KEY_W 22.f

static f32 action_width(Row *r)
{
    u32 col;
    return text_width(FONT_TEXT, S(12.5f), row_action(r, &col), -1) + SR(8 + CAP_KEY_W);
}

// The action ending at xr, centered on cy: its label and an Enter keycap.
static void draw_action(f32 xr, f32 cy, const WCHAR *s, u32 col)
{
    f32 kw = SR(CAP_KEY_W), kh = SR(20), kx = xr - kw, ky = floorf(cy - kh * 0.5f), fs = S(12.5f);
    r_rect(kx, ky, kw, kh, TH.key_bg, SR(LS()->key_r));
    r_border(kx, ky, kw, kh, TH.key_border, SR(LS()->key_r));
    text_draw_icon(0xE751, S(11), kx + kw * 0.5f, cy, TH.dim);
    f32 w = text_width(FONT_TEXT, fs, s, -1);
    text_draw(FONT_TEXT, fs, floorf(kx - SR(8) - w), text_baseline(FONT_TEXT, fs, cy), s, -1, col);
}

// How far the highlight is from a row at y, 0..1: the row's own note fades out as it comes (the selected row's
// note is drawn over the highlight, ui_draw).
static f32 note_away(f32 y, f32 h)
{
    return row_selectable(U.sel) ? CLAMP(fabsf(U.list_y0 - floorf(U.scroll + 0.5f) + U.hl_y - y) / h, 0.f, 1.f) : 1.f;
}

static void hl_prepare(void)
{
    U.hq_len = g_cfg.match_highlight ? query_norm(U.hq) : 0;
    U.hq_has_alt = U.hq_len && layout_convert(U.hq, U.hq_len, U.hq_alt);
}

// The name with the query matches in bold and the rest a little dimmer; plain (and elided) when it does not fit.
static f32 draw_name(f32 fs, f32 x, f32 base, f32 max_w, const WCHAR *s, int len, const WCHAR *norm, const u8 *ws, u32 col)
{
    static u8 mask[512], wsb[512];
    static WCHAR nb[512];
    if (len < 0) len = wlen(s);
    bool hl = U.hq_len && len > 0 && len < (int)countof(mask);
    if (hl) {
        if (!norm || !ws) {
            norm_str(nb, s, len);
            word_starts(s, len, wsb);
            norm = nb;
            ws = wsb;
        }
        int k = match_mask(norm, len, ws, U.hq, U.hq_len, mask);
        if (!k && U.hq_has_alt) k = match_mask(norm, len, ws, U.hq_alt, U.hq_len, mask);
        hl = k > 0;
    }
    if (hl) {
        f32 w = 0;
        for (int i = 0; i < len;) {
            int j = i;
            while (j < len && mask[j] == mask[i]) j++;
            w += text_width(mask[i] ? FONT_TEXT_SEMIBOLD : FONT_TEXT, fs, s + i, j - i);
            i = j;
        }
        hl = w <= max_w;
    }
    if (!hl) return text_draw_fit(FONT_TEXT, fs, x, base, max_w, s, len, col, false);
    u32 rest = color_mix(TH.dim, col, 0.6f);
    f32 pen = x;
    for (int i = 0; i < len;) {
        int j = i;
        while (j < len && mask[j] == mask[i]) j++;
        pen = text_draw(mask[i] ? FONT_TEXT_SEMIBOLD : FONT_TEXT, fs, pen, base, s + i, j - i, mask[i] ? col : rest);
        i = j;
    }
    return pen - x;
}

// Placeholders for files Everything counted but has not sent yet: pulsing bars, only where they are seen.
static void draw_virtual_rows(Row *r, f32 y)
{
    const LStyle *ls = LS();
    f32 rh = SR(ROW_H), isz = (f32)ui_icon_px(), x0 = r->x, x1 = r->x + r->w;
    f32 pulse = 0.75f + 0.25f * sinf((f32)fmod(time_now(), 100.0) * 5.f);
    int k0 = (int)MAX(0.f, floorf((U.list_y0 - y) / rh)), k1 = (int)ceilf((U.list_y1 - y) / rh);
    k1 = MIN(k1, (int)(r->h / rh + 0.5f));
    for (int k = k0; k < k1; k++) {
        f32 ry = y + rh * (f32)k, rcy = ry + rh * 0.5f;
        f32 ix = x0 + SR(12), iy = floorf(rcy - isz * 0.5f + 0.5f);
        u32 c = color_alpha(TH.tile, pulse);
        r_rect(ix, iy, isz, isz, c, SR(ls->tile_r));
        u32 hsh = (u32)(k + (int)(r->y / rh)) * 2654435761u;  // lengths that vary from row to row
        f32 w1 = SR(120) + (f32)(hsh % 160u) * U.s, w2 = SR(90) + (f32)((hsh >> 8) % 120u) * U.s;
        r_rect(ix + isz + SR(12), floorf(rcy - SR(5)), w1, SR(10), c, SR(5));
        r_rect(x1 - SR(12) - w2, floorf(rcy - SR(4)), w2, SR(8), color_alpha(c, 0.6f), SR(4));
    }
}

static void draw_row(Row *r, f32 y, bool selected)
{
    Theme *t = &TH;
    const LStyle *ls = LS();
    f32 x0 = r->x, x1 = r->x + r->w;
    if (r->kind == ROW_HEADER) {
        if (st_search()) text_draw(FONT_TEXT_SEMIBOLD, S(14), x0 + SR(8), y + r->h - SR(10), r->text, -1, t->text);
        else text_draw(FONT_TEXT_SEMIBOLD, S(11.5f), x0 + SR(12), y + r->h - SR(10), r->text, -1, color_mix(t->faint, t->dim, 0.6f));
        return;
    }
    if (r->kind == ROW_VIRTUAL) {
        draw_virtual_rows(r, y);
        return;
    }
    f32 cy = y + r->h * 0.5f;
    f32 fs = S(ls->name_fs);
    f32 base = text_baseline(FONT_TEXT, fs, cy);
    if (r->kind == ROW_INFO) {
        if (U.row_count == 1) {
            // centered in the row: the Windows 10 search pane's results column, not the whole window
            f32 lcy = U.list_y0 + list_height() * 0.5f - SR(6), tw = text_width(FONT_TEXT, fs, r->text, -1), cx = (x0 + x1) * 0.5f;
            f32 tx = floorf(MAX(x0 + SR(16), cx - tw * 0.5f));
            text_draw_icon(0xE721, S(26), cx, lcy - SR(20), t->faint);
            text_draw_fit(FONT_TEXT, fs, tx, floorf(lcy + SR(22) + 0.5f), x1 - SR(16) - tx, r->text, -1, t->dim, false);
            return;
        }
        text_draw_fit(FONT_TEXT, fs, x0 + SR(12), base, x1 - x0 - SR(24), r->text, -1, t->dim, false);
        return;
    }
    f32 isz = (f32)ui_icon_px();
    f32 ix = x0 + SR(12), iy = floorf(cy - isz * 0.5f + 0.5f);
    f32 tx = ix + isz + SR(ls->icon_gap);
    f32 right = x1 - SR(12);
    f32 sub_fs = S(12.5f);
    f32 sub_base = text_baseline(FONT_TEXT, sub_fs, cy);
    // the Windows 11 search: every icon on a square plate, the name alone (the pane on the right tells the rest)
    bool plate = st_search();
    if (plate) {
        f32 pz = SR(40), px = x0 + SR(8), py = floorf(cy - pz * 0.5f + 0.5f);
        bool armed = r->kind == ROW_APP && U.armed == r->app;
        r_rect(px, py, pz, pz, armed ? color_alpha(t->danger, 0.16f) : t->dark ? RGBA(255, 255, 255, 14) : RGBA(0, 0, 0, 10), SR(4));
        r_border(px, py, pz, pz, t->dark ? RGBA(255, 255, 255, 12) : RGBA(0, 0, 0, 14), SR(4));
        isz = SR(24);
        ix = floorf(px + (pz - isz) * 0.5f);
        iy = floorf(py + (pz - isz) * 0.5f);
        tx = px + pz + SR(12);
    }

    if (r->kind == ROW_FILE) {
        FileItem *f = r->file;
        draw_file_icon(f, ix, iy, isz);
        f32 avail = right - tx, gap = SR(16);
        if (plate) {  // the name, and where it is under it
            draw_name(fs, tx, floorf(cy - SR(3) + 0.5f), avail, f->name, -1, NULL, NULL, t->text);
            text_draw_fit(FONT_TEXT, sub_fs, tx, floorf(cy + SR(15) + 0.5f), avail, f->dir, -1, t->faint, true);
            return;
        }
        // the folder at the right, as much of it as fits; the name gets at least half when both are long
        f32 nw = text_width(FONT_TEXT, fs, f->name, -1);
        f32 dw = text_width(FONT_TEXT, sub_fs, f->dir, -1);
        f32 name_max = nw + gap + dw <= avail ? nw : MAX(avail * 0.55f, avail - dw - gap);
        if (cap_on() && selected) name_max = MIN(name_max, avail - action_width(r) - gap);  // the action takes the folder's place
        f32 drawn = draw_name(fs, tx, base, name_max, f->name, -1, NULL, NULL, t->text);
        f32 dir_max = avail - drawn - gap;
        u32 dir_col = cap_on() ? color_alpha(t->faint, note_away(y, r->h)) : t->faint;
        if (dir_max > SR(40) && f->dir[0]) text_draw_fit(FONT_TEXT, sub_fs, right - MIN(dw, dir_max), sub_base, dir_max, f->dir, -1, dir_col, true);
        return;
    }
    App *a = r->app;
    bool armed = U.armed == a;
    u32 sub_col = t->faint;
    const WCHAR *sub = row_note(r, &sub_col);
    if (a->kind == APP_CMD && plate) {
        text_draw_icon(a->glyph, S(15), ix + isz * 0.5f, iy + isz * 0.5f, armed ? t->danger : t->text);
    } else if (a->kind == APP_CMD) {
        r_rect(ix, iy, isz, isz, armed ? color_alpha(t->danger, 0.16f) : t->tile, SR(ls->tile_r));
        r_border(ix, iy, isz, isz, t->key_border, SR(ls->tile_r));
        text_draw_icon(a->glyph, MIN(S(14), isz * 0.52f), ix + isz * 0.5f, iy + isz * 0.5f, armed ? t->danger : t->text);
    } else {
        draw_app_icon(a, ix, iy, isz);
    }
    f32 sw = text_width(FONT_TEXT, sub_fs, sub, -1);
    f32 avail = right - tx;
    bool show_sub = !plate && ((selected && !w10_on()) || a->kind == APP_CMD || ls->all_notes);
    f32 note_w = cap_on() && selected ? MAX(sw, action_width(r)) : sw;
    draw_name(fs, tx, base, show_sub ? avail - note_w - SR(16) : avail, a->name, a->len, a->norm, a->ws, t->text);
    if ((ls->all_notes && a->kind != APP_CMD) || (a->kind == APP_CMD && !plate)) {
        f32 away = note_away(y, r->h);
        text_draw(FONT_TEXT, sub_fs, right - sw, sub_base, sub, -1, color_alpha(a->kind == APP_CMD ? sub_col : t->faint, away));
    }
}

static f32 keycap_width(const WCHAR *k) { return MAX(text_width(FONT_TEXT, S(11.5f), k, -1) + SR(6) * 2, SR(20)); }

// A key chip (footer hints, dialog buttons) at x, centered on cy.
static void draw_keycap(f32 x, f32 cy, const WCHAR *k, u32 col)
{
    f32 kfs = S(11.5f), kh = SR(20), w = keycap_width(k), tw = text_width(FONT_TEXT, kfs, k, -1);
    r_rect(x, floorf(cy - kh * 0.5f), w, kh, TH.key_bg, SR(LS()->key_r));
    text_draw(FONT_TEXT, kfs, floorf(x + (w - tw) * 0.5f + 0.5f), text_baseline(FONT_TEXT, kfs, cy), k, -1, col);
}

// A footer hint ending at xr: the label, then its keys. Returns where the next one (to the left) ends.
static f32 draw_hint(f32 xr, f32 cy, const WCHAR *label, const WCHAR *const *keys, int nkeys, u32 label_col)
{
    f32 lfs = S(12.5f);
    for (int i = nkeys - 1; i >= 0; i--) {
        f32 kx = floorf(xr - keycap_width(keys[i]));
        draw_keycap(kx, cy, keys[i], TH.dim);
        xr = kx - SR(4);
    }
    f32 lw = text_width(FONT_TEXT, lfs, label, -1);
    xr -= SR(4);
    text_draw(FONT_TEXT, lfs, floorf(xr - lw), text_baseline(FONT_TEXT, lfs, cy), label, -1, label_col);
    return xr - lw - SR(18);
}

static f32 update_chip_width(void)
{
    WCHAR ub[64];
    return floorf(SR(31) + text_width(FONT_TEXT, S(12.5f), upd_label(ub, countof(ub)), -1) + SR(10));
}

// The "Update to ..." chip (with the download's progress) at x, h high, centered on cy. It is the update button.
static void draw_update_chip(f32 x, f32 cy, f32 h, f32 rad)
{
    Theme *t = &TH;
    WCHAR ub[64];
    const WCHAR *label = upd_label(ub, countof(ub));
    int st = upd_state();
    f32 w = update_chip_width(), y = floorf(cy - h * 0.5f), fs = S(12.5f);
    f32 hov = st == UPD_DOWNLOADING || st == UPD_INSTALLING ? 0.f : U.update_hover_t;
    r_rect(x, y, w, h, color_alpha(t->accent, 0.16f + 0.10f * hov), rad);
    f32 pw = st == UPD_DOWNLOADING ? floorf(w * (f32)UPD.progress / 1000.f) : 0.f;
    if (pw > 0) {
        r_set_clip(x, y, x + pw, y + h);
        r_rect(x, y, w, h, color_alpha(t->accent, 0.22f), rad);
        r_clip_all();
    }
    u32 col = st == UPD_ERROR ? t->danger : t->text;
    text_draw_icon(st == UPD_ERROR ? 0xE72C : 0xE896, S(14), x + SR(16), cy, col);
    text_draw(FONT_TEXT, fs, x + SR(31), text_baseline(FONT_TEXT, fs, cy), label, -1, col);
    box_set(U.update_btn, x, y, w, h);
}

static void draw_footer(f32 y0)
{
    Theme *t = &TH;
    f32 cy = y0 + U.footer_h * 0.5f;
    f32 m = cap_on() ? SR(CAP_MARGIN) : 0;  // the capsule's panel stops short of the window's edges
    f32 xr = (f32)U.W - m - SR(14);
    static const WCHAR *k_enter[] = { L"Enter" };
    static const WCHAR *k_ctrl_enter[] = { L"Ctrl", L"Enter" };
    static const WCHAR *k_admin[] = { L"Ctrl", L"Shift", L"Enter" };
    static const WCHAR *k_copy[] = { L"Ctrl", L"C" };
    static const WCHAR *k_esc[] = { L"Esc" };
    if (LS()->footer_tint) r_rect(0, y0, (f32)U.W, U.footer_h, t->dark ? RGBA(255, 255, 255, 10) : RGBA(0, 0, 0, 8), 0);
    const WCHAR *label = TR("Настройки", "Settings");
    f32 lfs = S(12.5f), lw = text_width(FONT_TEXT, lfs, label, -1);
    f32 bx = m + SR(8), bh = SR(28), by = floorf(cy - bh * 0.5f);
    f32 bw = floorf(SR(31) + lw + SR(10));
    f32 hov = U.settings_hover_t;
    r_rect(bx, by, bw, bh, color_alpha(t->sel, hov), SR(LS()->key_r + 1));
    u32 col = color_mix(t->dim, t->text, hov);
    text_draw_icon(0xE713, S(14), bx + SR(16), cy, col);
    text_draw(FONT_TEXT, lfs, bx + SR(31), text_baseline(FONT_TEXT, lfs, cy), label, -1, col);
    box_set(U.settings_btn, bx, by, bw, bh);
    f32 min_x = m + SR(150);
    memset(U.update_btn, 0, sizeof U.update_btn);
    if (upd_visible()) {
        f32 ux = bx + bw + SR(6);
        draw_update_chip(ux, cy, bh, SR(LS()->key_r + 1));
        min_x = MAX(min_x, U.update_btn[2] + SR(18));
    }
    // the hints that fit, from the right: what Enter does, then Ctrl+Enter, Ctrl+Shift+Enter or Ctrl+C
    Row *r = sel_row();
    if (!r) {
        draw_hint(xr, cy, TR("Закрыть", "Close"), k_esc, 1, t->dim);
    } else if (r->kind == ROW_APP && r->app->kind == APP_CMD) {
        if (cap_on()) return;
        bool armed = U.armed == r->app;
        draw_hint(xr, cy, armed ? TR("Подтвердить", "Confirm") : TR("Выполнить", "Run"), k_enter, 1, armed ? t->danger : t->text);
    } else {
        if (!cap_on()) xr = draw_hint(xr, cy, TR("Открыть", "Open"), k_enter, 1, t->text);  // the capsule shows it on the row
        if (row_path(r) && xr - SR(230) > min_x) xr = draw_hint(xr, cy, TR("Показать в папке", "Show in folder"), k_ctrl_enter, 2, t->dim);
        if (r->kind == ROW_APP) {
            if (xr - SR(250) > min_x) draw_hint(xr, cy, TR("Администратор", "Run as admin"), k_admin, 3, t->dim);
        } else if (xr - SR(200) > min_x) {
            draw_hint(xr, cy, TR("Копировать путь", "Copy path"), k_copy, 2, t->dim);
        }
    }
}

// Where the query field sits: a band across the top, or across the bottom (Windows 10, like the taskbar box).
static void search_band(f32 *y0, f32 *y1)
{
    *y0 = cap_on() ? SR(CAP_MARGIN) : 0;
    *y1 = w10_on() && !q_active() ? 0 : *y0 + U.search_h;  // Windows 10 shows the query only once typing starts
}

// The query field: text from x0 to x1, centered on cy. The Start style puts it in a rounded box.
static void search_field(f32 *x0, f32 *x1, f32 *cy)
{
    if (w10_on()) {
        *x0 = SR(48);
        *x1 = (f32)U.W - SR(16) - (upd_visible() ? SR(200) : 0);
        *cy = floorf(U.search_h * 0.5f);
        return;
    }
    if (st_on()) {
        f32 m = SR(32);
        *x0 = m + SR(42);
        *x1 = (f32)U.W - m - SR(16);
        *cy = floorf(U.search_h * 0.5f + SR(4));
        return;
    }
    if (cap_on()) {
        f32 m = SR(CAP_MARGIN);
        *x0 = m + SR(52);
        *x1 = (f32)U.W - m - SR(28);
        *cy = floorf(m + U.search_h * 0.5f);
        return;
    }
    *x0 = SR(LS()->inset + 42);
    *x1 = (f32)U.W - SR(20);
    *cy = U.search_h * 0.5f;
}

static void draw_search(void)
{
    Theme *t = &TH;
    const LStyle *ls = LS();
    f32 fx0, fx1, cy;
    search_field(&fx0, &fx1, &cy);
    text_draw_icon(0xE721, S(ls->search_icon), fx0 - SR(cap_on() ? 26.f : 23.f), cy, U.qlen ? t->text : t->dim);
    f32 fs = S(ls->search_fs);
    f32 base = text_baseline(FONT_DISPLAY, fs, cy);
    f32 sel_top = floorf(cy - fs * 0.62f), sel_h = floorf(fs * 1.24f);
    if (!U.qlen) {
        text_draw_fit(FONT_DISPLAY, fs, fx0, base, fx1 - fx0, TR("Поиск приложений и файлов", "Search apps and files"), -1, t->faint, false);
        if (U.caret_on) r_rect(fx0, sel_top, MAX(1.f, floorf(S(1.5f))), sel_h, t->accent, SR(1));
        return;
    }
    static f32 offs[Q_MAX + 2];
    text_offsets(FONT_DISPLAY, fs, U.q, U.qlen, offs);
    field_scroll(&U.q_scroll, offs, U.caret, U.qlen, fx1 - fx0 - SR(4));
    f32 cx = offs[U.caret], ox = fx0 - floorf(U.q_scroll);
    f32 by0, by1;
    search_band(&by0, &by1);
    r_set_clip(fx0 - SR(2), by0, fx1, by1);
    if (U.anchor != U.caret) {
        int a = MIN(U.anchor, U.caret), b = MAX(U.anchor, U.caret);
        r_rect(floorf(ox + offs[a]), sel_top, floorf(offs[b] - offs[a] + 0.5f), sel_h, t->text_sel, SR(3));
    }
    text_draw(FONT_DISPLAY, fs, ox, base, U.q, U.qlen, t->text);
    if (U.caret_on) r_rect(floorf(ox + cx), sel_top, MAX(1.f, floorf(S(1.5f))), sel_h, t->accent, SR(1));
    r_clip_all();
}

// A capsule style panel: a soft shadow, the glass or the solid fill, a hairline outline.
static void cap_panel(f32 x, f32 y, f32 w, f32 h, f32 rad, bool glass)
{
    Theme *t = &TH;
    r_rect_ex(x, y + SR(4), w, h, t->dark ? RGBA(0, 0, 0, 110) : RGBA(0, 0, 0, 38), rad, 0, SR(12));
    if (glass) r_backdrop(x, y, w, h, rad);
    r_rect(x, y, w, h, glass ? t->bg : t->bg_solid, rad);
    r_border(x, y, w, h, t->dark ? RGBA(255, 255, 255, 20) : RGBA(0, 0, 0, 18), rad);
}

// The capsule style's query field (round ends) and the results panel under it.
static void cap_boxes(f32 *search, f32 *panel)
{
    f32 m = SR(CAP_MARGIN), w = (f32)U.W - m * 2;
    box_set(search, m, m, w, U.search_h);
    box_set(panel, m, U.list_y0, w, U.list_y1 + U.footer_h - U.list_y0);
}

static void cap_draw_chrome(bool glass)
{
    f32 sb[4], pb[4];
    cap_boxes(sb, pb);
    cap_panel(sb[0], sb[1], sb[2] - sb[0], U.search_h, floorf(U.search_h * 0.5f), glass);
    cap_panel(pb[0], pb[1], pb[2] - pb[0], pb[3] - pb[1], SR(18), glass);
    draw_search();
}

// The see-through margin and gap around the capsule's panels.
static bool cap_outside(int x, int y)
{
    f32 sb[4], pb[4];
    cap_boxes(sb, pb);
    return !in_box(sb, (f32)x, (f32)y) && !in_box(pb, (f32)x, (f32)y);
}

static void ui_draw(void)
{
    Theme *t = &TH;
    icons_begin_frame();
    hl_prepare();
    // rounded as DWM rounds the window, so the zoom keeps the corners
    bool glass = U.backdrop_ok && bd_ready();
    f32 rad = backdrop_radius();
    if (glass && !cap_on()) r_backdrop(0, 0, (f32)U.W, (f32)U.H, rad);
    // the Start styles tint the glass with their own fill (TH.veil) instead
    if (!cap_on()) r_rect(0, 0, (f32)U.W, (f32)U.H, !glass ? t->bg_solid : st_on() || w10_on() ? 0 : t->bg, rad);
    if (cap_on()) {
        cap_draw_chrome(glass);
    } else if (st_on()) {
        st_draw_chrome();
    } else if (w10_on()) {
        w10_draw_chrome();
    } else {
        draw_search();
        r_rect(0, U.search_h, (f32)U.W, 1, t->sep, 0);
    }

    r_set_clip(list_x0(), U.list_y0, list_x1(), U.list_y1);
    f32 off = U.list_y0 - floorf(U.scroll + 0.5f);
    f64 now = time_now();
    bool cascade = rows_cascading(now);
    f32 hl_op = 1.f;
    // Windows 10: no list highlight while a tile is pointed at, held or dragged
    if (row_selectable(U.sel) && !(w10_on() && (U.tsel >= 0 || U.w10.press_tile >= 0 || U.w10.tdrag))) {
        int k = 0;
        for (int i = 0; cascade && i < U.sel; i++)
            if (U.rows[i].y + U.rows[i].h >= U.scroll) k++;
        hl_op = R.opacity = cascade ? row_in(k, now) : 1.f;
        const LStyle *ls = LS();
        f32 hy = floorf(off + U.hl_y + 0.5f), hh = floorf(U.hl_h + 0.5f);
        f32 hx = SR(ls->inset), hw = (f32)U.W - hx * 2, hr = SR(ls->sel_r);
        if (st_on() || w10_on()) {
            st_draw_highlight(off);
        } else if (ls->sel == SEL_PILL) {
            r_rect(hx, hy, hw, hh, t->sel, hr);
        } else if (ls->sel == SEL_FILL) {
            r_rect(hx, hy, hw, hh, t->dark ? RGBA(255, 255, 255, 32) : RGBA(0, 0, 0, 20), hr);
        } else {
            r_rect(hx, hy, hw, hh, t->sel, hr);
            f32 bh = floorf(MIN(hh * 0.4f + fabsf(U.hl_vy) * 0.012f, hh * 0.75f) + 0.5f), bw = SR(3);
            r_rect(hx + SR(2), floorf(hy + (hh - bh) * 0.5f + 0.5f), bw, bh, t->accent, bw * 0.5f);
        }
        R.opacity = 1.f;
    }
    f32 top = U.scroll, bottom = U.scroll + list_height();
    for (int i = row_first_below(top), k = 0; i < U.row_count; i++) {
        Row *r = &U.rows[i];
        if (r->y > bottom) break;
        f32 dy = 0;
        if (cascade) {
            f32 e = row_in(k++, now);
            R.opacity = e;
            dy = floorf((1.f - e) * S(8) + 0.5f);
        }
        if (r->cell) st_draw_cell(r, off + r->y + dy, i == U.sel);
        else draw_row(r, off + r->y + dy, i == U.sel);
    }
    // the selected row's note rides on the highlight
    u32 note_col;
    Row *nr_row = sel_row();
    bool quiet = nr_row && (nr_row->cell || st_search() || (w10_on() && nr_row->kind == ROW_APP && nr_row->app->kind != APP_CMD));  // Windows 10 lists no "Application"
    const WCHAR *note = quiet ? NULL : cap_on() && nr_row ? row_action(nr_row, &note_col) : row_note(nr_row, &note_col);
    if (note) {
        U.sub_text = note;
        U.sub_col = note_col;
    }
    if (U.sub_text && U.sub_t > 0.f) {
        f32 nfs = S(12.5f), cy = off + U.hl_y + U.hl_h * 0.5f;
        f32 nw = text_width(FONT_TEXT, nfs, U.sub_text, -1);
        R.opacity = hl_op * U.sub_t;
        f32 nr = st_on() || w10_on() ? U.hl_x + U.hl_w - SR(12) : (f32)U.W - SR(20);
        if (cap_on()) draw_action((f32)U.W - SR(LS()->inset + 8), cy, U.sub_text, U.sub_col);
        else text_draw(FONT_TEXT, nfs, floorf(nr - nw), text_baseline(FONT_TEXT, nfs, cy), U.sub_text, -1, U.sub_col);
    }
    R.opacity = 1.f;
    r_clip_all();

    f32 ms = max_scroll();
    if (ms > 0 && U.sb_t > 0.f) {
        f32 lh = list_height() - SR(8), bar_h = MAX(SR(24), lh * lh / U.content_h);
        f32 by = U.list_y0 + SR(4) + (lh - bar_h) * CLAMP(U.scroll / ms, 0.f, 1.f), bw = U.sb_hot ? SR(5) : SR(3);
        r_rect(list_x1() - SR(3) - bw, floorf(by), bw, floorf(bar_h), color_alpha(U.sb_hot ? t->dim : t->faint, U.sb_t), bw * 0.5f);
    }

    f32 fy = U.list_y1;
    if (st_on()) {
        if (st_search()) w10_draw_preview();
        st_draw_footer();
    } else if (w10_on()) {
        w10_draw_overlay();
    } else if (cap_on()) {
        f32 sx = SR(LS()->inset);
        r_rect(sx, fy, (f32)U.W - sx * 2, 1, t->sep, 0);
        draw_footer(fy);
    } else {
        r_rect(0, fy, (f32)U.W, 1, t->sep, 0);
        draw_footer(fy + 1);
    }
    cm_draw();
    tg_draw();
}

// The GPU was reset (driver update, sleep): everything on it is made again.
static bool renderer_recover(void)
{
    settings_device_lost();
    pm_close();
    bd_close();  // its screen duplication belongs to the old device
    r_shutdown();
    if (!r_init(g_hwnd, U.W, U.H)) return false;
    font_reset_atlas();
    icons_reset(ui_icon_px());
    settings_device_restored();
    bd_refresh();
    return true;
}

// Moves everything one frame on; U.animating says whether another frame is needed.
static void animate(f32 dt)
{
    bool anim = false;
    U.scroll_target = CLAMP(U.scroll_target, 0.f, max_scroll());
    anim |= spring_step(&U.scroll, &U.scroll_v, U.scroll_target, anim_omega(g_cfg.anim_scroll_ms), dt);
    if (row_selectable(U.sel)) {
        f32 ty = U.rows[U.sel].y, th = U.rows[U.sel].h;
        f32 tx = U.rows[U.sel].x, tw = U.rows[U.sel].w;
        if (fabsf(ty - U.hl_y) > list_height()) {
            U.hl_y = ty;
            U.hl_vy = 0;
            U.hl_x = tx;
            U.hl_vx = 0;
        }
        anim |= spring_step(&U.hl_y, &U.hl_vy, ty, anim_omega(g_cfg.anim_select_ms), dt);
        anim |= spring_step(&U.hl_h, &U.hl_vh, th, anim_omega(g_cfg.anim_select_ms), dt);
        anim |= spring_step(&U.hl_x, &U.hl_vx, tx, anim_omega(g_cfg.anim_select_ms), dt);
        anim |= spring_step(&U.hl_w, &U.hl_vw, tw, anim_omega(g_cfg.anim_select_ms), dt);
    }
    anim |= approach(&U.settings_hover_t, U.settings_hover ? 1.f : 0.f, 22.f, dt);
    anim |= approach(&U.update_hover_t, U.update_hover ? 1.f : 0.f, 22.f, dt);
    f64 now = time_now();
    if (U.scroll != U.sb_prev) U.sb_seen = now;
    U.sb_prev = U.scroll;
    bool recent = now - U.sb_seen < 0.9;
    bool sb_on = max_scroll() > 0 && (U.sb_hot || recent);
    anim |= approach(&U.sb_t, sb_on ? 1.f : 0.f, sb_on ? 20.f : 7.f, dt);
    if (recent && !U.sb_hot && max_scroll() > 0) anim = true;
    anim |= approach(&U.sub_t, row_selectable(U.sel) && (U.rows[U.sel].kind == ROW_APP || cap_on()) && !U.rows[U.sel].cell &&
                                       !(w10_on() && U.rows[U.sel].app->kind != APP_CMD) ? 1.f : 0.f, 24.f, dt);
    if (st_on()) anim |= st_animate(dt);
    if (w10_on()) anim |= w10_animate(dt);
    anim |= cm_animate(dt);
    anim |= tg_animate(dt);
    if (slide_spring()) {
        // One speed both ways, so a reopen while it closes (or the other way) turns it around mid-flight.
        f32 target = U.vis_target > 0.5f ? 0.f : (f32)U.slide_dy;
        anim |= spring_step(&U.slide_off, &U.slide_v, target, slide_omega(), dt);
        if (U.vis_target < 0.5f && fabsf(target - U.slide_off) < fabsf(target) * 0.04f) {  // faded out already, skip the spring's tail
            U.slide_off = target;
            U.slide_v = 0;
        }
        U.vis = U.slide_off == target ? U.vis_target : 1.f - U.slide_off / (f32)U.slide_dy;
    } else if (U.vis != U.vis_target) {
        f32 open = anim_sec(g_cfg.anim_open_ms), close = anim_sec(g_cfg.anim_close_ms);
        if (U.vis_target > U.vis) U.vis = open > 0.f ? MIN(U.vis + dt / open, U.vis_target) : U.vis_target;
        else U.vis = close > 0.f ? MAX(U.vis - dt / close, U.vis_target) : U.vis_target;
        anim |= U.vis != U.vis_target;
    }
    if (!U.closing && rows_cascading(now)) anim = true;
    if (U.files_end_y >= 0 && U.files_end_y < U.scroll + list_height() && files_virtual_total() > U.files_shown) anim = true;
    U.animating = anim;
}

static void ui_render(void)
{
    if (!R.ok && !renderer_recover()) {
        U.dirty = false;
        U.animating = false;
        return;
    }
    r_use(&R.main);
    r_wait_frame();
    f64 now = time_now();
    f32 dt = (f32)(now - U.last_frame);
    if (dt > 0.1f || dt <= 0) dt = 1.f / 60.f;
    U.last_frame = now;
    animate(dt);
    files_load_more();
    f32 v = vis_eased();
    f32 s0 = (f32)g_cfg.anim_open_scale / 100.f;
    R.scale = (U.slide || (U.vis >= 1.f && U.vis_target >= 1.f)) ? 1.f : U.closing ? 0.985f + 0.015f * v : s0 + (1.f - s0) * v;
    r_draw(ui_draw);
    apply_window_alpha();
    apply_window_slide();
    bd_place();
    r_end_and_present();
    U.dirty = false;
    if (U.closing && U.vis <= 0.f) ui_hide_finish();
}

// ---- hit testing

static int row_at(int x, int y)
{
    if (y < U.list_y0 || y >= U.list_y1 || (f32)x < list_x0() || (f32)x >= list_x1()) return -1;
    f32 cy = (f32)y - U.list_y0 + U.scroll;
    for (int i = row_first_below(cy); i < U.row_count && U.rows[i].y <= cy; i++) {
        Row *r = &U.rows[i];
        if (cy < r->y + r->h && (r->cell != CELL_TILE || ((f32)x >= r->x && (f32)x < r->x + r->w))) return i;
    }
    return -1;
}

static int text_index_at(int x)
{
    if (!U.qlen) return 0;
    static f32 offs[Q_MAX + 2];
    f32 fs = S(LS()->search_fs);
    text_offsets(FONT_DISPLAY, fs, U.q, U.qlen, offs);
    f32 fx0, fx1, cy;
    search_field(&fx0, &fx1, &cy);
    return offset_at(offs, U.qlen, (f32)x - fx0 + floorf(U.q_scroll));
}
