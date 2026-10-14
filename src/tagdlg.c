// "Add tag": a small dialog over the launcher, in the manner of the hotkey recorder in the settings.
static bool tg_can_save(void)
{
    for (int i = 0; i < U.tg.len; i++)
        if (U.tg.buf[i] != ' ' && U.tg.buf[i] != ',' && U.tg.buf[i] != ';') return true;
    return false;
}

static void tg_open(void)
{
    Row *r = sel_row();
    u8 type;
    const WCHAR *key = row_tag_key(r, &type);
    if (!key) return;
    free(U.tg.key);
    U.tg.key = wdup_heap(key);
    U.tg.type = type;
    wcopy(U.tg.name, countof(U.tg.name), r->kind == ROW_FILE ? r->file->name : r->app->name);
    U.tg.len = U.tg.caret = 0;
    U.tg.buf[0] = 0;
    U.tg.scroll = 0;
    U.tg.hover = U.tg.press = -1;
    U.tg.t = anims_enabled() ? 0.f : 1.f;
    U.tg.open = true;
    U.tg.closing = false;
    U.animating = true;
    caret_reset();
}

static void tg_close(bool save)
{
    if (!U.tg.open || U.tg.closing) return;
    if (save && U.tg.key && tags_add(U.tg.type, U.tg.key, U.tg.buf)) tags_changed();
    U.tg.press = -1;
    if (anims_enabled() && U.visible && anim_sec(160) > 0.f) {
        U.tg.closing = true;
        U.animating = true;
    } else {
        U.tg.open = false;
    }
    ui_invalidate();
}

static bool tg_animate(f32 dt)
{
    if (!U.tg.open) return false;
    f32 dur = anim_sec(160);
    if (U.tg.closing) {
        U.tg.t = dur > 0.f ? U.tg.t - dt / (dur * 0.7f) : 0.f;
        if (U.tg.t <= 0.f) {
            U.tg.t = 0.f;
            U.tg.open = U.tg.closing = false;
        }
        return true;
    }
    if (U.tg.t < 1.f) {
        U.tg.t = dur > 0.f ? MIN(1.f, U.tg.t + dt / dur) : 1.f;
        return true;
    }
    return false;
}

static void tg_edit(int a, int b, const WCHAR *s, int n)
{
    WCHAR ins[TAG_INPUT_MAX];
    int k = 0;
    for (int i = 0; i < n && k < countof(ins); i++) {
        WCHAR c = s[i];
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (c < 0x20 || c == 0x7F) continue;
        ins[k++] = c;
    }
    k = MIN(k, TAG_INPUT_MAX - (U.tg.len - (b - a)));
    if (k < 0) return;
    memmove(U.tg.buf + a + k, U.tg.buf + b, (size_t)(U.tg.len - b) * sizeof(WCHAR));
    memcpy(U.tg.buf + a, ins, (size_t)k * sizeof(WCHAR));
    U.tg.len += k - (b - a);
    U.tg.buf[U.tg.len] = 0;
    U.tg.caret = a + k;
    caret_reset();
}

static bool tg_sep(WCHAR c) { return c == ' ' || c == ',' || c == ';'; }

static int tg_word_left(int i)
{
    while (i > 0 && tg_sep(U.tg.buf[i - 1])) i--;
    while (i > 0 && !tg_sep(U.tg.buf[i - 1])) i--;
    return i;
}

static int tg_word_right(int i)
{
    while (i < U.tg.len && !tg_sep(U.tg.buf[i])) i++;
    while (i < U.tg.len && tg_sep(U.tg.buf[i])) i++;
    return i;
}

static void tg_paste(void)
{
    if (!OpenClipboard(g_hwnd)) return;
    HANDLE hd = GetClipboardData(CF_UNICODETEXT);
    const WCHAR *p = hd ? (const WCHAR *)GlobalLock(hd) : NULL;
    if (p) {
        tg_edit(U.tg.caret, U.tg.caret, p, MIN(wlen(p), TAG_INPUT_MAX));
        GlobalUnlock(hd);
    }
    CloseClipboard();
}

static bool tg_key(WPARAM vk)
{
    if (!U.tg.open) return false;
    if (U.tg.closing) return vk != VK_ESCAPE;  // a second Esc goes on to the launcher
    bool ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0;
    int c = U.tg.caret;
    switch (vk) {
    case VK_ESCAPE: tg_close(false); return true;
    case VK_RETURN:
        if (tg_can_save()) tg_close(true);
        return true;
    case VK_BACK:
        if (c > 0) tg_edit(ctrl ? tg_word_left(c) : c - 1, c, NULL, 0);
        return true;
    case VK_DELETE:
        if (c < U.tg.len) tg_edit(c, ctrl ? tg_word_right(c) : c + 1, NULL, 0);
        return true;
    case VK_LEFT: U.tg.caret = ctrl ? tg_word_left(c) : MAX(0, c - 1); break;
    case VK_RIGHT: U.tg.caret = ctrl ? tg_word_right(c) : MIN(U.tg.len, c + 1); break;
    case VK_HOME: U.tg.caret = 0; break;
    case VK_END: U.tg.caret = U.tg.len; break;
    case 'V':
        if (ctrl && !alt) tg_paste();
        return true;
    case VK_F4: return !alt;  // Alt+F4 still closes the launcher
    default: return true;
    }
    caret_reset();
    return true;
}

static void tg_char(WCHAR c)
{
    if (c < 0x20 || c == 0x7F || U.tg.closing) return;
    tg_edit(U.tg.caret, U.tg.caret, &c, 1);
}

static int tg_btn_at(int x, int y)
{
    for (int i = 0; i < 2; i++)
        if (in_box(U.tg.btn[i], (f32)x, (f32)y)) return i;
    return -1;
}

static int tg_index_at(int x)
{
    static f32 offs[TAG_INPUT_MAX + 2];
    text_offsets(FONT_TEXT, S(14.5f), U.tg.buf, U.tg.len, offs);
    f32 lx = (f32)x - (U.tg.field[0] + SR(12)) + floorf(U.tg.scroll);
    int best = 0;
    for (int i = 0; i <= U.tg.len; i++)
        if (fabsf(offs[i] - lx) < fabsf(offs[best] - lx)) best = i;
    return best;
}

static void tg_draw(void)
{
    if (!U.tg.open) return;
    Theme *t = &U.th;
    f32 e = U.tg.closing ? U.tg.t : ease_out_cubic(U.tg.t);
    f32 W = (f32)U.W, H = (f32)U.H;
    r_set_clip(0, 0, W, H);
    R.opacity = e;
    r_rect(0, 0, W, H, t->dark ? RGBA(0, 0, 0, 110) : RGBA(0, 0, 0, 36), 0);
    f32 m = SR(16), pw = MIN(SR(420), W - m * 2), ph = SR(200);
    f32 px = floorf((W - pw) * 0.5f), py = floorf(MAX(m, (H - ph) * 0.5f) + (1.f - e) * SR(10)), rad = SR(14);
    U.tg.panel[0] = px, U.tg.panel[1] = py, U.tg.panel[2] = px + pw, U.tg.panel[3] = py + ph;
    r_rect_ex(px, py + SR(10), pw, ph, t->dark ? RGBA(0, 0, 0, 120) : RGBA(0, 0, 0, 45), rad, 0, SR(24));
    r_rect(px, py, pw, ph, t->dark ? RGBA(40, 40, 43, 255) : RGBA(252, 252, 253, 255), rad);
    r_rect_ex(px, py, pw, ph, t->dark ? RGBA(255, 255, 255, 20) : RGBA(0, 0, 0, 22), rad, 1.f, 0);

    f32 x0 = px + SR(20), x1 = px + pw - SR(20);
    text_draw(FONT_TEXT_SEMIBOLD, S(15), x0, floorf(py + SR(34)), TR("Добавить тег", "Add tag"), -1, t->text);
    text_draw_fit(FONT_TEXT, S(12.5f), x0, floorf(py + SR(54)), x1 - x0, U.tg.name, -1, t->faint, false);

    f32 fy = py + SR(68), fh = SR(40), fw = x1 - x0;
    U.tg.field[0] = x0, U.tg.field[1] = fy, U.tg.field[2] = x1, U.tg.field[3] = fy + fh;
    r_rect(x0, fy, fw, fh, t->key_bg, SR(8));
    r_rect_ex(x0, fy, fw, fh, color_alpha(t->accent, 0.85f), SR(8), 1.f, 0);
    f32 fs = S(14.5f), cy = fy + fh * 0.5f, tx0 = x0 + SR(12), tw = fw - SR(24);
    f32 base = floorf(cy + font_cap_height(FONT_TEXT, fs) * 0.5f + 0.5f);
    f32 car_top = floorf(cy - fs * 0.62f), car_h = floorf(fs * 1.24f);
    static f32 offs[TAG_INPUT_MAX + 2];
    text_offsets(FONT_TEXT, fs, U.tg.buf, U.tg.len, offs);
    f32 cx = offs[U.tg.caret];
    if (cx - U.tg.scroll > tw - SR(2)) U.tg.scroll = cx - tw + SR(2);
    if (cx - U.tg.scroll < 0) U.tg.scroll = cx;
    if (offs[U.tg.len] - U.tg.scroll < tw - SR(2)) U.tg.scroll = MAX(0.f, offs[U.tg.len] - tw + SR(2));
    f32 ox = tx0 - floorf(U.tg.scroll);
    r_set_clip(tx0 - SR(2), fy, x1 - SR(10), fy + fh);
    if (!U.tg.len) text_draw_fit(FONT_TEXT, fs, tx0, base, tw, TR("например: работа, отчёты", "e.g. work, reports"), -1, t->faint, false);
    else text_draw(FONT_TEXT, fs, ox, base, U.tg.buf, U.tg.len, t->text);
    if (U.caret_on && !U.tg.closing) r_rect(floorf(ox + cx), car_top, MAX(1.f, floorf(S(1.5f))), car_h, t->accent, SR(1));
    r_set_clip(0, 0, W, H);
    text_draw_fit(FONT_TEXT, S(12.5f), x0, floorf(fy + fh + SR(22)), fw,
                  TR("Несколько тегов - через запятую", "Separate several tags with commas"),
                  -1, t->faint, false);

    f32 by = py + ph - SR(52), bcy = by + SR(26);
    r_rect(px, by, pw, 1, t->sep, 0);
    bool can_save = tg_can_save();
    f32 bfs = S(13.5f), xr = px + pw - SR(12);
    for (int b = 1; b >= 0; b--) {
        const WCHAR *lbl = b ? TR("Сохранить", "Save") : TR("Отмена", "Cancel");
        const WCHAR *key = b ? L"Enter" : L"Esc";
        f32 kw = keycap_width(key);
        f32 lw = text_width(FONT_TEXT, bfs, lbl, -1);
        f32 bw = SR(10) + lw + SR(8) + kw + SR(6), bh = SR(32);
        f32 bx = floorf(xr - bw), byy = floorf(bcy - bh * 0.5f);
        f32 *rb = U.tg.btn[b];
        rb[0] = bx, rb[1] = byy, rb[2] = bx + bw, rb[3] = byy + bh;
        bool enabled = !b || can_save, hov = U.tg.hover == b && enabled;
        if (b && can_save) r_rect(bx, byy, bw, bh, color_alpha(t->accent, hov ? 0.34f : 0.24f), SR(8));
        else if (hov) r_rect(bx, byy, bw, bh, t->sel, SR(8));
        u32 col = enabled ? t->text : t->faint;
        text_draw(FONT_TEXT, bfs, bx + SR(10), floorf(bcy + font_cap_height(FONT_TEXT, bfs) * 0.5f + 0.5f), lbl, -1, col);
        draw_keycap(bx + SR(10) + lw + SR(8), bcy, key, enabled ? t->dim : t->faint);
        xr = bx - SR(6);
    }
    R.opacity = 1.f;
}
