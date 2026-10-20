// The launcher's context menu on a result row.

enum {
    CM_OPEN = 1, CM_REVEAL, CM_RUNAS, CM_COPY, CM_PROPS, CM_TAG_ADD, CM_TAG_CLEAR, CM_TAG_INFO, CM_PIN, CM_UNPIN, CM_PIN_TOP,
    // the Start style's account and power menus
    CM_ST_ACCOUNT, CM_ST_LOCK, CM_ST_SIGNOUT, CM_ST_MLSET, CM_ST_SLEEP, CM_ST_HIBERNATE, CM_ST_SHUTDOWN, CM_ST_RESTART,
    // Windows 10 tile sizes, in the order of TILE_*
    CM_SIZE_SMALL, CM_SIZE_MEDIUM, CM_SIZE_WIDE, CM_SIZE_LARGE
};
#define CM_ITEM_H 32.f
#define CM_PAD 5.f
#define CM_SEP_H 9.f

static void cm_close(bool animate)
{
    if (!U.cm.open) return;
    U.cm.press = -1;
    if (animate && anims_enabled() && anim_sec(g_cfg.anim_menu_ms) > 0.f) {
        U.cm.closing = true;
        U.animating = true;
    } else {
        U.cm.open = U.cm.closing = false;
    }
    ui_invalidate();
}

static void cm_add(int cmd, u32 glyph, const WCHAR *label, const WCHAR *keys, bool sep)
{
    if (U.cm.n >= countof(U.cm.it)) return;
    U.cm.it[U.cm.n].cmd = cmd;
    U.cm.it[U.cm.n].glyph = glyph;
    U.cm.it[U.cm.n].label = label;
    U.cm.it[U.cm.n].keys = keys;
    U.cm.it[U.cm.n].sep = sep;
    U.cm.it[U.cm.n].disabled = false;
    U.cm.n++;
}

// What a row's tags are stored under: the app id or the full path.
static const WCHAR *row_tag_key(Row *r, u8 *type)
{
    if (!r) return NULL;
    if (r->kind == ROW_FILE) {
        *type = file_item_type(r->file);
        return r->file->full;
    }
    if (r->kind == ROW_APP) {
        *type = app_item_type(r->app);
        return r->app->id;
    }
    return NULL;
}

// Tags changed: rows may point into what was freed, so everything is searched and laid out again at once.
static void tags_changed(void)
{
    search_apps();
    search_tags();
    rebuild_rows(true);
    snap_highlight();
    ui_invalidate();
}

static void cm_show(int x, int y, bool keyboard);

// The menu for the selected row, at x, y (keyboard: its first item already hovered).
static void ui_context_menu(int x, int y, bool keyboard)
{
    Row *r = sel_row();
    if (!r) return;
    bool is_cmd = r->kind == ROW_APP && r->app->kind == APP_CMD;
    const WCHAR *path = row_path(r);
    U.cm.n = 0;
    cm_add(CM_OPEN, is_cmd ? 0xE768 : 0xE8A7, is_cmd ? TR("Выполнить", "Run") : TR("Открыть", "Open"), L"Enter", false);
    if (!is_cmd) {
        if (path) cm_add(CM_REVEAL, 0xE838, TR("Показать в папке", "Show in folder"), L"Ctrl+Enter", false);
        cm_add(CM_RUNAS, 0xE7EF, TR("Запустить от имени администратора", "Run as administrator"), L"Ctrl+Shift+Enter", false);
        cm_add(CM_COPY, 0xE8C8, path ? TR("Копировать путь", "Copy path") : TR("Копировать имя", "Copy name"), L"Ctrl+C", true);
        if (path) cm_add(CM_PROPS, 0xE946, TR("Свойства", "Properties"), L"Alt+Enter", false);
    }
    if ((st_on() || w10_on()) && r->kind == ROW_APP) {
        int pi = pin_index(r->app->id);
        if (pi < 0) {
            cm_add(CM_PIN, 0xE718, TR("Закрепить на начальном экране", "Pin to Start"), L"", true);
        } else {
            cm_add(CM_UNPIN, 0xE77A, TR("Открепить от начального экрана", "Unpin from Start"), L"", true);
            if (pi > 0 && !w10_on()) cm_add(CM_PIN_TOP, 0xE74A, TR("Переместить в начало", "Move to front"), L"", false);  // Windows 10 tiles are dragged
            if (w10_on()) {
                const WCHAR *names[TILE__COUNT] = { TR("Мелкая плитка", "Small tile"), TR("Средняя плитка", "Medium tile"),
                                                    TR("Широкая плитка", "Wide tile"), TR("Крупная плитка", "Large tile") };
                int cur = pin_size(pi);
                for (int k = 0; k < TILE__COUNT; k++) cm_add(CM_SIZE_SMALL + k, k == cur ? 0xE73E : 0, names[k], L"", k == 0);
            }
        }
    }
    u8 ttype;
    const WCHAR *tkey = row_tag_key(r, &ttype);
    if (tkey) {
        cm_add(CM_TAG_ADD, 0xE8EC, TR("Добавить тег", "Add tag"), L"", true);
        TagItem *ti = tags_find(ttype, tkey, 0);
        if (ti && ti->ntags) {
            cm_add(CM_TAG_CLEAR, 0xE74D, TR("Удалить все теги", "Remove all tags"), L"", false);
            tags_count_label(U.cm.tag_count, countof(U.cm.tag_count), ti->ntags);
            // the tags themselves go to the shortcut column, cut to a sane width
            int n = 0, cap = 40;
            U.cm.tag_list[0] = 0;
            for (int i = 0; i < ti->ntags && n < cap; i++) {
                int add = wfmt(U.cm.tag_list + n, countof(U.cm.tag_list) - n, L"%ls%ls", i ? L", " : L"", ti->tags[i].text);
                if (add < 0) break;
                n += add;
            }
            U.cm.tag_list[countof(U.cm.tag_list) - 1] = 0;
            if (n > cap) wcopy(U.cm.tag_list + cap, countof(U.cm.tag_list) - cap, L"…");
            cm_add(CM_TAG_INFO, 0, U.cm.tag_count, U.cm.tag_list, false);
            U.cm.it[U.cm.n - 1].disabled = true;
        }
    }
    cm_show(x, y, keyboard);
}

// Lays out the items added since U.cm.n = 0 and opens the menu at x, y (above it when there is no room below).
static void cm_show(int x, int y, bool keyboard)
{
    bool reopen = U.cm.open && !U.cm.closing;
    f32 fs = S(13.5f), kfs = S(12);
    f32 lw = 0, kw = 0;
    for (int i = 0; i < U.cm.n; i++) {
        lw = MAX(lw, text_width(FONT_TEXT, fs, U.cm.it[i].label, -1));
        kw = MAX(kw, text_width(FONT_TEXT, kfs, U.cm.it[i].keys, -1));
    }
    f32 h = SR(CM_PAD), w = floorf(SR(CM_PAD) + SR(12) + SR(16) + SR(12) + lw + SR(32) + kw + SR(12) + SR(CM_PAD));
    for (int i = 0; i < U.cm.n; i++) {
        if (U.cm.it[i].sep) h += SR(CM_SEP_H);
        U.cm.it[i].y = h;
        h += SR(CM_ITEM_H);
    }
    h += SR(CM_PAD);
    f32 m = SR(8);
    w = MIN(w, (f32)U.W - 2 * m);
    f32 px = CLAMP((f32)x, m, (f32)U.W - w - m);
    f32 py = (f32)y + SR(4);
    if (py + h > (f32)U.H - m) py = MAX(m, (f32)y - SR(4) - h);
    U.cm.x = floorf(px);
    U.cm.y = floorf(py);
    U.cm.w = w;
    U.cm.h = h;
    U.cm.hover = keyboard ? 0 : -1;
    U.cm.press = -1;
    U.cm.hl_init = false;
    if (!reopen || U.cm.t <= 0.f) U.cm.t = anims_enabled() ? 0.f : 1.f;
    if (reopen) U.cm.t = MIN(U.cm.t, 0.35f);
    U.cm.open = true;
    U.cm.closing = false;
    U.animating = true;
    ui_invalidate();
}

static int cm_item_at(int x, int y)
{
    if (!U.cm.open || U.cm.closing) return -1;
    f32 fx = (f32)x - U.cm.x, fy = (f32)y - U.cm.y;
    if (fx < 0 || fx >= U.cm.w || fy < 0 || fy >= U.cm.h) return -1;
    for (int i = 0; i < U.cm.n; i++)
        if (fy >= U.cm.it[i].y && fy < U.cm.it[i].y + SR(CM_ITEM_H)) return i;
    return -1;
}

static bool cm_inside(int x, int y)
{
    return U.cm.open && (f32)x >= U.cm.x && (f32)x < U.cm.x + U.cm.w && (f32)y >= U.cm.y && (f32)y < U.cm.y + U.cm.h;
}

static void cm_run(int i)
{
    if (i < 0 || i >= U.cm.n || U.cm.it[i].disabled) return;
    int cmd = U.cm.it[i].cmd;
    U.cm.open = U.cm.closing = false;
    ui_invalidate();
    switch (cmd) {
    case CM_OPEN: ui_activate(ACT_OPEN); break;
    case CM_REVEAL: ui_activate(ACT_REVEAL); break;
    case CM_RUNAS: ui_activate(ACT_RUNAS); break;
    case CM_COPY: ui_copy_path(); break;
    case CM_PROPS: ui_activate(ACT_PROPERTIES); break;
    case CM_TAG_ADD: tg_open(); break;
    case CM_TAG_CLEAR: {
        u8 type;
        const WCHAR *key = row_tag_key(sel_row(), &type);
        if (key) {
            tags_clear(type, key);  // frees what tagged rows point to: rebuild right away
            tags_changed();
        }
        break;
    }
    case CM_PIN:
    case CM_UNPIN:
    case CM_PIN_TOP: {
        Row *r = sel_row();
        if (!r || r->kind != ROW_APP) break;
        if (cmd == CM_PIN_TOP) pin_to_top(r->app->id);
        else pin_set(r->app->id, cmd == CM_PIN);
        st_pins_changed();
        break;
    }
    case CM_SIZE_SMALL:
    case CM_SIZE_MEDIUM:
    case CM_SIZE_WIDE:
    case CM_SIZE_LARGE: {
        Row *r = sel_row();
        if (r && r->kind == ROW_APP) pin_set_size(r->app->id, cmd - CM_SIZE_SMALL);
        st_pins_changed();
        break;
    }
    default: st_menu_pick(cmd); break;
    }
}

static bool cm_animate(f32 dt)
{
    if (!U.cm.open) return false;
    bool anim = false;
    f32 dur = anim_sec(g_cfg.anim_menu_ms);
    if (U.cm.closing) {
        U.cm.t = dur > 0.f ? U.cm.t - dt / (dur * 0.6f) : 0.f;
        if (U.cm.t <= 0.f) {
            U.cm.t = 0.f;
            U.cm.open = U.cm.closing = false;
            return true;
        }
        anim = true;
    } else if (U.cm.t < 1.f) {
        U.cm.t = dur > 0.f ? MIN(1.f, U.cm.t + dt / dur) : 1.f;
        anim = true;
    }
    if (U.cm.hover >= 0) {
        f32 ty = U.cm.it[U.cm.hover].y;
        if (!U.cm.hl_init) {
            U.cm.hl_y = ty;
            U.cm.hl_v = 0;
            U.cm.hl_init = true;
        }
        anim |= spring_step(&U.cm.hl_y, &U.cm.hl_v, ty, anim_omega(g_cfg.anim_select_ms), dt);
    }
    return anim;
}

static void cm_draw(void)
{
    if (!U.cm.open) return;
    Theme *t = &TH;
    f32 e = U.cm.closing ? U.cm.t : ease_out_cubic(U.cm.t);
    f32 x = U.cm.x, y = U.cm.y - floorf((1.f - e) * SR(6) + 0.5f), w = U.cm.w, h = U.cm.h, rad = SR(10);
    R.opacity = e;
    r_clip_all();
    r_panel(x, y, w, h, rad, t->dark ? RGBA(40, 40, 43, 252) : RGBA(252, 252, 253, 252), t->dark ? RGBA(255, 255, 255, 20) : RGBA(0, 0, 0, 22),
            t->dark ? RGBA(0, 0, 0, 110) : RGBA(0, 0, 0, 40), SR(8), SR(18));
    f32 ix = x + SR(CM_PAD), iw = w - SR(CM_PAD) * 2;
    if (U.cm.hover >= 0 && U.cm.hl_init) {
        f32 hy = floorf(y + U.cm.hl_y + 0.5f);
        r_rect(ix, hy, iw, SR(CM_ITEM_H), t->sel, SR(6));
    }
    f32 fs = S(13.5f), kfs = S(12);
    for (int i = 0; i < U.cm.n; i++) {
        f32 iy = y + U.cm.it[i].y, cy = iy + SR(CM_ITEM_H) * 0.5f;
        if (U.cm.it[i].sep) r_rect(ix + SR(8), floorf(iy - SR(CM_SEP_H) * 0.5f), iw - SR(16), 1, t->sep, 0);
        bool hot = i == U.cm.hover, off = U.cm.it[i].disabled;
        if (U.cm.it[i].glyph) text_draw_icon(U.cm.it[i].glyph, S(14), ix + SR(12) + SR(8), cy, hot ? t->text : t->dim);
        f32 lx = ix + SR(12) + SR(16) + SR(12);
        text_draw(FONT_TEXT, fs, lx, text_baseline(FONT_TEXT, fs, cy), U.cm.it[i].label, -1, off ? t->faint : t->text);
        f32 kw = text_width(FONT_TEXT, kfs, U.cm.it[i].keys, -1);
        text_draw(FONT_TEXT, kfs, floorf(ix + iw - SR(12) - kw), text_baseline(FONT_TEXT, kfs, cy), U.cm.it[i].keys, -1, t->faint);
    }
    R.opacity = 1.f;
}

// Keys while the menu is open; any other key closes it and goes on to the launcher.
static bool cm_key(WPARAM vk)
{
    if (!U.cm.open || U.cm.closing) return false;
    switch (vk) {
    case VK_UP:
    case VK_DOWN: {
        int d = vk == VK_DOWN ? 1 : -1, h = U.cm.hover;
        for (int k = 0; k < U.cm.n; k++) {
            h = h < 0 ? (d > 0 ? 0 : U.cm.n - 1) : (h + d + U.cm.n) % U.cm.n;
            if (!U.cm.it[h].disabled) break;
        }
        U.cm.hover = h;
        U.animating = true;
        ui_invalidate();
        return true;
    }
    case VK_RETURN:
    case VK_SPACE:
        if (U.cm.hover >= 0) cm_run(U.cm.hover);
        else cm_close(true);
        return true;
    case VK_ESCAPE:
    case VK_APPS:
    case VK_LEFT:
        cm_close(true);
        return true;
    case VK_TAB:
        return true;
    }
    cm_close(true);
    return false;
}
