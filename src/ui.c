typedef struct Theme {
    bool dark;
    u32 bg, bg_solid;
    u32 text, dim, faint;
    u32 sel, sel_border, sep, accent, text_sel;
    u32 key_bg, key_border, danger, tile;
} Theme;

enum { ROW_HEADER, ROW_APP, ROW_FILE, ROW_INFO, ROW_VIRTUAL };

typedef struct Row {
    u8 kind;
    App *app;
    FileItem *file;
    const WCHAR *text;
    f32 y, h;
} Row;

typedef struct AppHit {
    App *app;
    f32 score;
    int raw;
    bool alt;
} AppHit;

typedef struct TagHit {
    FileItem *file;
    f32 score;
} TagHit;

#define Q_MAX 500
#define TIMER_CARET 1
#define TIMER_REHOOK 2

static struct {
    bool visible;
    bool closing;
    f32 vis, vis_target;
    int alpha;
    HWND prev_fg;
    bool app_mode;
    bool no_focus_restore;
    int settings_btn[4], update_btn[4];
    bool settings_hover, press_settings, update_hover, press_update;
    f32 update_hover_t;
    f64 hide_time;

    WCHAR q[Q_MAX + 1];
    int qlen, caret, anchor;
    f32 q_scroll;
    bool caret_on;
    bool text_drag;

    AppList *apps;
    AppHit *hits;
    int hit_count, hit_cap;
    TagHit thits[16];
    int thit_count;
    bool tags_first;
    bool files_alt;
    FileResults *files;
    u32 files_req_id;
    bool files_pending;
    FileResults **fpages;
    int fpage_count, fpage_cap;
    FileItem **flist;
    int flist_count, flist_cap;
    int files_shown;
    u32 files_total, files_fetched;
    bool files_more_pending;
    f32 files_end_y;
    WCHAR files_header[64];

    App *frequent[8];
    int frequent_count;
    FileItem recent[6];
    int recent_count;
    Arena recent_arena;

    Row *rows;
    int row_count, row_cap;
    int sel;
    int press_row;
    f32 content_h;
    f32 scroll, scroll_target, scroll_v;
    f32 hl_y, hl_h, hl_vy, hl_vh;
    f64 open_time;
    f32 settings_hover_t;
    f32 sb_t, sb_prev;
    WCHAR hq[Q_MAX + 1], hq_alt[Q_MAX + 1];
    int hq_len;
    bool hq_has_alt;
    f64 sb_seen;
    bool sb_hot;
    const WCHAR *sub_text;
    u32 sub_col;
    f32 sub_t;
    struct {
        bool open, closing;
        f32 t;
        f32 x, y, w, h;
        int n, hover, press;
        f32 hl_y, hl_v;
        bool hl_init;
        struct { int cmd; u32 glyph; const WCHAR *label, *keys; bool sep, disabled; f32 y; } it[10];
        WCHAR tag_count[32], tag_list[96];
    } cm;
    struct {
        bool open, closing;
        f32 t;
        WCHAR buf[TAG_INPUT_MAX + 1];
        int len, caret;
        f32 scroll;
        u8 type;
        WCHAR *key;
        WCHAR name[128];
        f32 panel[4], field[4], btn[2][4];
        int hover, press;
    } tg;
    f64 last_frame;
    bool dirty, animating;

    App *armed;
    int mouse_x, mouse_y;
    bool menu_open;

    u32 dpi;
    f32 s;
    int W, H;
    f32 search_h, footer_h, list_y0, list_y1;
    Theme th;
    bool backdrop_ok;
    f64 last_reindex;
} U;

static bool g_pinned;
static bool g_other_monitor;

static void ui_invalidate(void) { U.dirty = true; }

static bool cm_animate(f32 dt);
static void cm_draw(void);
static void cm_close(bool animate);
static bool tg_animate(f32 dt);
static void tg_draw(void);
static bool tag_hit_has(const WCHAR *full);

static f32 S(f32 v) { return v * U.s; }
static f32 SR(f32 v) { return floorf(v * U.s + 0.5f); }

static bool reg_dword(HKEY root, const WCHAR *key, const WCHAR *name, DWORD *out)
{
    DWORD sz = sizeof(DWORD);
    return RegGetValueW(root, key, name, RRF_RT_REG_DWORD, NULL, out, &sz) == ERROR_SUCCESS;
}

static bool system_dark(void)
{
    DWORD light = 0;
    if (reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", &light)) return light == 0;
    return true;
}

static void theme_update(void)
{
    bool dark = g_cfg.theme == 1 ? true : g_cfg.theme == 2 ? false : system_dark();
    Theme *t = &U.th;
    t->dark = dark;

    u32 acc = RGBA(0, 120, 212, 255);
    BYTE pal[32];
    DWORD sz = sizeof pal;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette",
                     RRF_RT_REG_BINARY, NULL, pal, &sz) == ERROR_SUCCESS && sz >= 32) {
        int i = dark ? 1 : 4;
        acc = RGBA(pal[i * 4], pal[i * 4 + 1], pal[i * 4 + 2], 255);
    } else {
        DWORD c;
        if (reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", &c)) acc = RGBA(c & 255, (c >> 8) & 255, (c >> 16) & 255, 255);
    }
    t->accent = acc;
    if (dark) {
        t->bg = g_cfg.backdrop == BACKDROP_BLUR ? RGBA(24, 24, 27, 150) : RGBA(28, 28, 30, 90);
        t->bg_solid = RGBA(32, 32, 34, 250);
        t->text = RGBA(255, 255, 255, 236);
        t->dim = RGBA(255, 255, 255, 150);
        t->faint = RGBA(255, 255, 255, 92);
        t->sel = RGBA(255, 255, 255, 20);
        t->sel_border = RGBA(255, 255, 255, 12);
        t->sep = RGBA(255, 255, 255, 22);
        t->key_bg = RGBA(255, 255, 255, 24);
        t->key_border = RGBA(255, 255, 255, 26);
        t->danger = RGBA(255, 107, 107, 255);
        t->tile = RGBA(255, 255, 255, 30);
    } else {
        t->bg = g_cfg.backdrop == BACKDROP_BLUR ? RGBA(250, 250, 252, 150) : RGBA(252, 252, 253, 110);
        t->bg_solid = RGBA(249, 249, 251, 250);
        t->text = RGBA(0, 0, 0, 228);
        t->dim = RGBA(0, 0, 0, 140);
        t->faint = RGBA(0, 0, 0, 90);
        t->sel = RGBA(0, 0, 0, 13);
        t->sel_border = RGBA(0, 0, 0, 8);
        t->sep = RGBA(0, 0, 0, 20);
        t->key_bg = RGBA(0, 0, 0, 14);
        t->key_border = RGBA(0, 0, 0, 22);
        t->danger = RGBA(196, 43, 28, 255);
        t->tile = RGBA(0, 0, 0, 22);
    }
    t->text_sel = color_alpha(acc, dark ? 0.45f : 0.30f);
    if (g_hwnd) {
        BOOL d = dark;
        DwmSetWindowAttribute(g_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &d, sizeof d);
    }
}

enum { SEL_PILL, SEL_FILL, SEL_BAR };

typedef struct LStyle {
    f32 row_h, header_h, icon, icon_gap;
    f32 search_h, search_fs, search_icon, name_fs, footer_h;
    f32 inset, sel_r, tile_r, key_r;
    int corner;  // DWMWA_WINDOW_CORNER_PREFERENCE
    u8 sel;
    bool footer_tint, all_notes;
} LStyle;

static const LStyle k_lstyles[STYLE__COUNT] = {
    [STYLE_STANDARD] = { 42, 30, 28, 12, 60, 20, 17, 14, 40, 8, 8, 7, 5, 2, SEL_PILL, false, false },
    [STYLE_RAYCAST] = { 40, 30, 20, 12, 56, 18, 16, 14, 40, 8, 8, 5, 5, 2, SEL_FILL, true, true },
    [STYLE_WIN11] = { 40, 32, 24, 14, 56, 18, 16, 14, 42, 6, 4, 4, 4, 2, SEL_BAR, false, false },
    [STYLE_COMPACT] = { 32, 26, 20, 10, 48, 16, 15, 13, 36, 6, 6, 5, 4, 3, SEL_PILL, false, false },
};

static const LStyle *LS(void) { return &k_lstyles[CLAMP(g_cfg.style, 0, STYLE__COUNT - 1)]; }

static bool backdrop_apply(HWND h)
{
    int corner = LS()->corner;
    DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof corner);
    PFN_SetWindowCompositionAttribute swca = (PFN_SetWindowCompositionAttribute)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    int mode = g_cfg.backdrop;
    int none = 1;  // DWMSBT_NONE
    if (mode == BACKDROP_ACRYLIC) {
        MARGINS m = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(h, &m);
        int type = 3;  // DWMSBT_TRANSIENTWINDOW
        if (SUCCEEDED(DwmSetWindowAttribute(h, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &type, sizeof type))) return true;
        mode = BACKDROP_BLUR;
    }
    DwmSetWindowAttribute(h, 38, &none, sizeof none);
    MARGINS zero = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(h, &zero);
    ML_ACCENT_POLICY ap = { 0, 0, 0, 0 };
    if (mode == BACKDROP_BLUR) {
        ap.accent_state = 4;  // ACCENT_ENABLE_ACRYLICBLURBEHIND
        ap.accent_flags = 2;
        ap.gradient_color = U.th.dark ? 0x30201e1eu : 0x30f8f6f6u;
    }
    ML_WINCOMPATTR_DATA d = { 19 /*WCA_ACCENT_POLICY*/, &ap, sizeof ap };
    bool ok = swca && swca(h, &d);
    return mode == BACKDROP_BLUR && ok;
}

#define ROW_H (LS()->row_h)
#define HEADER_H (LS()->header_h)
#define LIST_PAD 6.f
#define ICON_LOGICAL (LS()->icon)

static int ui_icon_px(void) { return (int)(ICON_LOGICAL * U.s + 0.5f); }

static void ui_metrics(u32 dpi)
{
    U.dpi = dpi ? dpi : 96;
    U.s = (f32)U.dpi / 96.f;
    U.search_h = SR(LS()->search_h);
    U.footer_h = SR(LS()->footer_h);
    f32 list_h = SR(g_cfg.rows * ROW_H + 2 * LIST_PAD);
    U.W = (int)SR((f32)g_cfg.width);
    U.H = (int)(U.search_h + 1 + list_h + 1 + U.footer_h);
    U.list_y0 = U.search_h + 1;
    U.list_y1 = U.list_y0 + list_h;
}

static f32 list_height(void) { return U.list_y1 - U.list_y0; }

static Row *row_add(u8 kind, f32 h)
{
    if (U.row_count == U.row_cap) {
        int nc = U.row_cap ? U.row_cap * 2 : 256;
        Row *nr = (Row *)realloc(U.rows, sizeof(Row) * nc);
        if (!nr) return NULL;
        U.rows = nr;
        U.row_cap = nc;
    }
    Row *r = &U.rows[U.row_count++];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->y = U.content_h;
    r->h = h;
    U.content_h += h;
    return r;
}

static void row_header(const WCHAR *text)
{
    Row *r = row_add(ROW_HEADER, SR(HEADER_H));
    if (r) r->text = text;
}

static void row_info(const WCHAR *text)
{
    Row *r = row_add(ROW_INFO, SR(ROW_H));
    if (r) r->text = text;
}

static void row_app(App *a)
{
    Row *r = row_add(ROW_APP, SR(ROW_H));
    if (r) r->app = a;
}

static void row_file(FileItem *f)
{
    Row *r = row_add(ROW_FILE, SR(ROW_H));
    if (r) r->file = f;
}

static int row_first_below(f32 y)
{
    int lo = 0, hi = U.row_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (U.rows[mid].y + U.rows[mid].h <= y) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static bool row_selectable(int i) { return i >= 0 && i < U.row_count && (U.rows[i].kind == ROW_APP || U.rows[i].kind == ROW_FILE); }

static int first_selectable(int from, int dir)
{
    for (int i = from; i >= 0 && i < U.row_count; i += dir)
        if (row_selectable(i)) return i;
    return -1;
}

#define FILES_STEP 40
#define FILES_VIRTUAL_MAX 100000

static int files_virtual_total(void)
{
    int n = U.flist_count;
    if (U.files_fetched < U.files_total) n = MAX(n, (int)MIN(U.files_total, (u32)FILES_VIRTUAL_MAX));
    return n;
}

static void rebuild_rows(bool keep_sel)
{
    int old = U.sel;
    U.row_count = 0;
    U.files_end_y = -1;
    U.content_h = SR(LIST_PAD);
    if (U.qlen == 0) {
        if (!U.apps) row_info(TR("Индексирую приложения…", "Indexing applications…"));
        if (U.frequent_count) {
            row_header(TR("Часто используемые", "Frequently used"));
            for (int i = 0; i < U.frequent_count; i++) row_app(U.frequent[i]);
        }
        if (U.recent_count) {
            row_header(TR("Недавние файлы", "Recent files"));
            for (int i = 0; i < U.recent_count; i++) row_file(&U.recent[i]);
        }
        if (U.apps && U.apps->shell_count) {
            row_header(TR("Все приложения", "All apps"));
            for (int i = 0; i < U.apps->shell_count; i++) row_app(&U.apps->apps[i]);
        }
    } else {
        for (int pass = 0; pass < 2; pass++) {
            if (U.thit_count && pass == (U.tags_first ? 0 : 1)) {
                row_header(TR("По тегу", "Tagged"));
                for (int i = 0; i < U.thit_count; i++) row_file(U.thits[i].file);
            }
            if (U.hit_count && pass == (U.tags_first ? 1 : 0)) {
                row_header(TR("Приложения", "Applications"));
                for (int i = 0; i < U.hit_count; i++) row_app(U.hits[i].app);
            }
        }
        bool want_files = U.qlen >= g_cfg.file_min_chars;
        if (want_files && U.files) {
            if (U.files->status == EV_NOT_RUNNING) {
                row_header(TR("Файлы", "Files"));
                row_info(TR("Everything не запущен - поиск файлов недоступен", "Everything is not running - file search is unavailable"));
            } else if (U.files->status == EV_ERROR) {
                row_header(TR("Файлы", "Files"));
                row_info(TR("Everything не ответил", "Everything did not respond"));
            } else if (U.flist_count) {
                int total = files_virtual_total();
                if (total > U.files_shown) _snwprintf(U.files_header, countof(U.files_header), L"%ls  ·  %d", TR("Файлы", "Files"), (int)U.files_total);
                else wcopy(U.files_header, countof(U.files_header), TR("Файлы", "Files"));
                U.files_header[countof(U.files_header) - 1] = 0;
                row_header(U.files_header);
                for (int i = 0; i < U.files_shown; i++)
                    if (!U.thit_count || !tag_hit_has(U.flist[i]->full)) row_file(U.flist[i]);
                U.files_end_y = U.content_h;
                if (total > U.files_shown) row_add(ROW_VIRTUAL, SR(ROW_H) * (f32)(total - U.files_shown));
            }
        }
        if (first_selectable(0, 1) < 0 && !(want_files && U.files_pending) && !(U.files && U.files->status != EV_OK))
            row_info(TR("Ничего не найдено", "No results"));
    }
    U.content_h += SR(LIST_PAD);

    if (keep_sel && row_selectable(old)) U.sel = old;
    else if (keep_sel && old >= U.row_count) U.sel = first_selectable(U.row_count - 1, -1);
    else U.sel = first_selectable(0, 1);
    if (!row_selectable(U.sel)) U.sel = first_selectable(0, 1);
}

static f32 max_scroll(void) { return MAX(0.f, U.content_h - list_height()); }

static void snap_highlight(void)
{
    if (row_selectable(U.sel)) {
        U.hl_y = U.rows[U.sel].y;
        U.hl_h = U.rows[U.sel].h;
    }
    U.hl_vy = U.hl_vh = 0;
}

static void ensure_visible(int idx, bool animate)
{
    if (!row_selectable(idx)) return;
    Row *r = &U.rows[idx];
    f32 top = r->y, bot = r->y + r->h, view = list_height();
    if (idx > 0 && U.rows[idx - 1].kind == ROW_HEADER) top = U.rows[idx - 1].y;
    if (first_selectable(0, 1) == idx) top = 0;
    if (top < U.scroll_target) U.scroll_target = top;
    else if (bot + SR(LIST_PAD) > U.scroll_target + view) U.scroll_target = bot + SR(LIST_PAD) - view;
    U.scroll_target = CLAMP(U.scroll_target, 0.f, max_scroll());
    if (!animate) {
        U.scroll = U.scroll_target;
        U.scroll_v = 0;
    }
    U.animating = true;
}

static int cmp_hits(const void *a, const void *b)
{
    const AppHit *x = (const AppHit *)a, *y = (const AppHit *)b;
    if (x->score != y->score) return x->score < y->score ? 1 : -1;
    return x->app < y->app ? -1 : 1;
}

static int query_norm(WCHAR *out)
{
    int b = 0, e = U.qlen;
    while (b < e && U.q[b] == ' ') b++;
    while (e > b && U.q[e - 1] == ' ') e--;
    norm_str(out, U.q + b, e - b);
    return e - b;
}

static void refresh_frecency(void)
{
    if (!U.apps) return;
    i64 now = unix_now();
    for (int i = 0; i < U.apps->count; i++) {
        App *a = &U.apps->apps[i];
        a->frec = frec_bonus(hist_frecency(a->kind == APP_CMD ? 'c' : 'a', a->id, now));
    }
}

static int cmp_frequent(const void *a, const void *b)
{
    App *x = *(App **)a, *y = *(App **)b;
    return x->frec < y->frec ? 1 : x->frec > y->frec ? -1 : 0;
}

static void refresh_empty_view(void)
{
    U.frequent_count = 0;
    if (U.apps) {
        App *tmp[512];
        int n = 0;
        for (int i = 0; i < U.apps->shell_count && n < countof(tmp); i++)
            if (U.apps->apps[i].frec > frec_bonus(0.3)) tmp[n++] = &U.apps->apps[i];
        qsort(tmp, n, sizeof(App *), cmp_frequent);
        U.frequent_count = MIN(n, countof(U.frequent));
        memcpy(U.frequent, tmp, sizeof(App *) * U.frequent_count);
    }
    RecentFile rf[countof(U.recent)];
    int n = history_top_files(rf, countof(rf));
    arena_reset(&U.recent_arena);
    U.recent_count = 0;
    for (int i = 0; i < n; i++) {
        FileItem *f = &U.recent[U.recent_count++];
        memset(f, 0, sizeof *f);
        f->full = wdup(&U.recent_arena, rf[i].path, -1);
        const WCHAR *slash = wcsrchr(f->full, '\\');
        bool root = slash && slash[1] == 0;
        if (slash && !root) {
            f->name = (WCHAR *)slash + 1;
            f->dir = wdup(&U.recent_arena, f->full, (int)(slash - f->full));
        } else {
            f->name = f->full;
            f->dir = wdup(&U.recent_arena, L"", 0);
        }
        f->flags = rf[i].folder ? FI_FOLDER : 0;
        f->icon_key = icon_key_for_file(f->full, rf[i].folder);
    }
}

static void search_apps(void)
{
    U.hit_count = 0;
    U.files_alt = false;
    if (!U.apps || !U.qlen) return;
    if (U.hit_cap < U.apps->count) {
        AppHit *nh = (AppHit *)realloc(U.hits, sizeof(AppHit) * U.apps->count);
        if (!nh) return;
        U.hits = nh;
        U.hit_cap = U.apps->count;
    }
    WCHAR qn[Q_MAX + 1], alt[Q_MAX + 1];
    int ql = query_norm(qn);
    if (!ql) return;
    bool has_alt = layout_convert(qn, ql, alt);
    i64 now = unix_now();
    for (int i = 0; i < U.apps->count; i++) {
        App *a = &U.apps->apps[i];
        int sc = score_query(a->norm, a->len, a->ws, qn, ql);
        bool is_alt = false;
        if (has_alt) {
            int sa = score_query(a->norm, a->len, a->ws, alt, ql) * 9 / 10;
            if (sa > sc) {
                sc = sa;
                is_alt = true;
            }
        }
        int kw_pen = 300 + MIN(a->len, 60) * 2;
        for (int k = 0; k < a->kw_count; k++) {
            int sk = score_query(a->kw[k], a->kw_len[k], a->kw_ws[k], qn, ql);
            if (sk >= SCORE_SUBSTRING - 400 && sk - kw_pen > sc) {
                sc = sk - kw_pen;
                is_alt = false;
            }
            if (has_alt) {
                int sa = score_query(a->kw[k], a->kw_len[k], a->kw_ws[k], alt, ql);
                if (sa >= SCORE_SUBSTRING - 400 && (sa - kw_pen) * 9 / 10 > sc) {
                    sc = (sa - kw_pen) * 9 / 10;
                    is_alt = true;
                }
            }
        }
        if (a->kind == APP_CMD) {
            if (sc < SCORE_SUBSTRING - 600 || (a->danger && ql < 3)) sc = 0;
            else sc -= 500;
        }
        u8 type = a->kind == APP_CMD ? 'c' : 'a';
        if (TG.n) {
            bool tag_alt;
            int st = tags_score(tags_find(type, a->id), qn, ql, alt, has_alt, &tag_alt);
            if (st && st + 400 > sc) {  // the user's own word for it beats a name match of the same kind
                sc = st + 400;
                is_alt = tag_alt;
            }
        }
        if (sc <= 0) continue;
        AppHit *h = &U.hits[U.hit_count++];
        h->app = a;
        h->alt = is_alt;
        h->raw = sc;
        h->score = (f32)sc + a->frec + qmem_bonus(type, a->id, qn, ql, now);
    }
    qsort(U.hits, U.hit_count, sizeof(AppHit), cmp_hits);
    // Search files in the other layout only when the query clearly is a name typed in the wrong one: a
    // loose fuzzy hit ("gdk" -> "пвл" in "Панель упраВЛения") must not take the file results away.
    if (U.hit_count && U.hits[0].alt) {
        bool any_direct = false, strong_alt = false;
        for (int i = 0; i < U.hit_count; i++) {
            if (!U.hits[i].alt) any_direct = true;
            else if (U.hits[i].raw > SCORE_FUZZY_MAX) strong_alt = true;
        }
        U.files_alt = !any_direct && strong_alt;
    }
    U.hit_count = MIN(U.hit_count, g_cfg.max_apps);
}

static int cmp_tag_hits(const void *a, const void *b)
{
    const TagHit *x = (const TagHit *)a, *y = (const TagHit *)b;
    return x->score < y->score ? 1 : x->score > y->score ? -1 : 0;
}

// Files and folders found by their tags. They get a section of their own: Everything finds them by name only.
static void search_tags(void)
{
    U.thit_count = 0;
    U.tags_first = false;
    if (!TG.n || !U.qlen) return;
    WCHAR qn[Q_MAX + 1], alt[Q_MAX + 1];
    int ql = query_norm(qn);
    if (!ql) return;
    bool has_alt = layout_convert(qn, ql, alt);
    i64 now = unix_now();
    TagHit tmp[256];
    int n = 0;
    for (int i = 0; i < TG.n && n < countof(tmp); i++) {
        TagItem *ti = TG.items[i];
        if (ti->type != 'f' && ti->type != 'd') continue;
        bool is_alt;
        int st = tags_score(ti, qn, ql, alt, has_alt, &is_alt);
        if (!st) continue;
        tmp[n].file = &ti->file;
        tmp[n].score = (f32)(st + 400) + frec_bonus(hist_frecency(ti->type, ti->key, now)) + qmem_bonus(ti->type, ti->key, qn, ql, now);
        n++;
    }
    qsort(tmp, n, sizeof(TagHit), cmp_tag_hits);
    U.thit_count = MIN(n, countof(U.thits));
    memcpy(U.thits, tmp, sizeof(TagHit) * U.thit_count);
    U.tags_first = U.thit_count && (!U.hit_count || U.thits[0].score > U.hits[0].score);
}

static bool tag_hit_has(const WCHAR *full)
{
    for (int i = 0; i < U.thit_count; i++)
        if (!_wcsicmp(U.thits[i].file->full, full)) return true;
    return false;
}

static void files_clear(void)
{
    fileresults_free(U.files);
    U.files = NULL;
    for (int i = 0; i < U.fpage_count; i++) fileresults_free(U.fpages[i]);
    U.fpage_count = 0;
    U.flist_count = 0;
    U.files_shown = 0;
    U.files_total = U.files_fetched = 0;
    U.files_more_pending = false;
}

static bool files_append(FileResults *r)
{
    if (U.flist_count + r->count > U.flist_cap) {
        int nc = MAX(U.flist_cap * 2, U.flist_count + r->count + 256);
        FileItem **nl = (FileItem **)realloc(U.flist, sizeof(FileItem *) * nc);
        if (!nl) return false;
        U.flist = nl;
        U.flist_cap = nc;
    }
    for (int i = 0; i < r->count; i++) U.flist[U.flist_count++] = &r->items[i];
    return true;
}

static void request_files(void)
{
    if (U.qlen >= g_cfg.file_min_chars) {
        WCHAR q[Q_MAX + 1];
        if (U.files_alt) {
            WCHAR qn[Q_MAX + 1];
            int ql = query_norm(qn);
            if (!layout_convert(qn, ql, q)) wcopy(q, countof(q), U.q);
        } else {
            wcopy(q, countof(q), U.q);
        }
        U.files_req_id++;
        U.files_pending = true;
        U.files_more_pending = false;
        ev_request(U.files_req_id, q);
    } else {
        U.files_pending = false;
        files_clear();
    }
}

static int cmp_file_score2(const void *a, const void *b)
{
    const FileItem *x = (const FileItem *)a, *y = (const FileItem *)b;
    return x->score < y->score ? 1 : x->score > y->score ? -1 : 0;
}

static void files_load_more(void)
{
    if (U.files_end_y < 0 || !U.files || U.files->status != EV_OK) return;
    f32 rh = SR(ROW_H), need = MAX(U.scroll, U.scroll_target) + list_height() * 2.f;
    if (need < U.files_end_y) return;
    int want = U.files_shown + (int)ceilf((need - U.files_end_y) / rh);
    if (U.files_shown < U.flist_count) {
        int n = MIN(U.flist_count, MAX(U.files_shown + FILES_STEP, want));
        if (n != U.files_shown) {
            U.files_shown = n;
            rebuild_rows(true);
            ui_invalidate();
        }
    }
    if (!U.files_more_pending && U.files_fetched < U.files_total && U.flist_count - U.files_shown < FILES_STEP &&
        U.flist_count < FILES_VIRTUAL_MAX) {
        U.files_more_pending = true;
        ev_request_more(U.files_req_id);
    }
}

static void on_files_ready(FileResults *r)
{
    if (!r) return;
    if (r->id != U.files_req_id || U.qlen < g_cfg.file_min_chars || (r->page && !U.files)) {
        fileresults_free(r);
        return;
    }
    i64 now = unix_now();
    for (int i = 0; i < r->count; i++) r->items[i].score += frec_bonus(hist_frecency((r->items[i].flags & FI_FOLDER) ? 'd' : 'f', r->items[i].full, now));
    qsort(r->items, r->count, sizeof(FileItem), cmp_file_score2);
    if (r->page) {
        U.files_more_pending = false;
        U.files_total = r->total;
        U.files_fetched = r->fetched;
        if (U.fpage_count == U.fpage_cap) {
            int nc = U.fpage_cap ? U.fpage_cap * 2 : 16;
            FileResults **np = (FileResults **)realloc(U.fpages, sizeof(FileResults *) * nc);
            if (!np) {
                fileresults_free(r);
                return;
            }
            U.fpages = np;
            U.fpage_cap = nc;
        }
        if (!files_append(r)) {
            fileresults_free(r);
            return;
        }
        U.fpages[U.fpage_count++] = r;
    } else {
        U.files_pending = false;
        files_clear();
        U.files = r;
        files_append(r);
        U.files_shown = MIN(U.flist_count, FILES_STEP);
        U.files_total = r->total;
        U.files_fetched = r->fetched;
    }
    rebuild_rows(true);
    if (U.scroll_target > max_scroll()) U.scroll_target = max_scroll();
    files_load_more();
    ui_invalidate();
}

static void on_query_changed(void)
{
    U.armed = NULL;
    cm_close(false);
    search_apps();
    search_tags();
    request_files();
    rebuild_rows(false);
    U.scroll = U.scroll_target = U.scroll_v = 0;
    snap_highlight();
    U.caret_on = true;
    if (U.visible) SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), NULL);
    ui_invalidate();
}

static void on_apps_ready(AppList *l)
{
    AppList *old = U.apps;
    U.apps = l;
    refresh_frecency();
    refresh_empty_view();
    search_apps();
    search_tags();
    rebuild_rows(true);
    applist_free(old);
    snap_highlight();
    ui_invalidate();
}

static bool is_word_char(WCHAR c) { return c != ' ' && !is_sep(c); }

static int word_left(int i)
{
    while (i > 0 && !is_word_char(U.q[i - 1])) i--;
    while (i > 0 && is_word_char(U.q[i - 1])) i--;
    return i;
}

static int word_right(int i)
{
    while (i < U.qlen && !is_word_char(U.q[i])) i++;
    while (i < U.qlen && is_word_char(U.q[i])) i++;
    return i;
}

static int char_left(int i)
{
    if (i <= 0) return 0;
    i--;
    if (i > 0 && U.q[i] >= 0xDC00 && U.q[i] <= 0xDFFF && U.q[i - 1] >= 0xD800 && U.q[i - 1] <= 0xDBFF) i--;
    return i;
}

static int char_right(int i)
{
    if (i >= U.qlen) return U.qlen;
    i++;
    if (i < U.qlen && U.q[i] >= 0xDC00 && U.q[i] <= 0xDFFF && U.q[i - 1] >= 0xD800 && U.q[i - 1] <= 0xDBFF) i++;
    return i;
}

static void edit_replace(int a, int b, const WCHAR *s, int n)
{
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    WCHAR clean[Q_MAX + 1];
    int cn = 0;
    for (int i = 0; i < n && cn < Q_MAX; i++) {
        WCHAR c = s[i];
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (c < 0x20 || c == 0x7F) continue;
        clean[cn++] = c;
    }
    int room = Q_MAX - (U.qlen - (b - a));
    if (cn > room) cn = MAX(room, 0);
    memmove(U.q + a + cn, U.q + b, (size_t)(U.qlen - b) * sizeof(WCHAR));
    memcpy(U.q + a, clean, (size_t)cn * sizeof(WCHAR));
    U.qlen = U.qlen - (b - a) + cn;
    U.q[U.qlen] = 0;
    U.caret = U.anchor = a + cn;
    on_query_changed();
}

static void edit_move(int pos, bool extend)
{
    U.caret = CLAMP(pos, 0, U.qlen);
    if (!extend) U.anchor = U.caret;
    U.caret_on = true;
    if (U.visible) SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), NULL);
    ui_invalidate();
}

static void clipboard_set(const WCHAR *s, int n)
{
    if (n < 0) n = wlen(s);
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (size_t)(n + 1) * sizeof(WCHAR));
    if (g) {
        WCHAR *p = (WCHAR *)GlobalLock(g);
        memcpy(p, s, (size_t)n * sizeof(WCHAR));
        p[n] = 0;
        GlobalUnlock(g);
        if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
    }
    CloseClipboard();
}

static void clipboard_paste(void)
{
    if (!OpenClipboard(g_hwnd)) return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const WCHAR *p = (const WCHAR *)GlobalLock(h);
        if (p) {
            int n = 0;
            while (p[n] && n < Q_MAX) n++;
            WCHAR tmp[Q_MAX + 1];
            memcpy(tmp, p, (size_t)n * sizeof(WCHAR));
            GlobalUnlock(h);
            CloseClipboard();
            edit_replace(U.anchor, U.caret, tmp, n);
            return;
        }
    }
    CloseClipboard();
}

static void ui_render(void);
static void settings_open(void);
static void settings_device_lost(void);
static void settings_device_restored(void);
static void open_settings_from_launcher(void);

static UINT monitor_dpi(HMONITOR m)
{
    typedef HRESULT (WINAPI *PFN_GetDpiForMonitor)(HMONITOR, int, UINT *, UINT *);
    static PFN_GetDpiForMonitor f;
    static bool tried;
    if (!tried) {
        tried = true;
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) f = (PFN_GetDpiForMonitor)(void *)GetProcAddress(shcore, "GetDpiForMonitor");
    }
    UINT x = 96, y = 96;
    if (!f || FAILED(f(m, 0, &x, &y))) {
        HDC dc = GetDC(NULL);
        x = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    return x;
}

static void force_foreground(HWND h)
{
    if (GetForegroundWindow() == h) return;
    SetForegroundWindow(h);
    if (GetForegroundWindow() != h) {
        HWND fg = GetForegroundWindow();
        DWORD fg_tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0, me = GetCurrentThreadId();
        if (fg_tid && fg_tid != me && AttachThreadInput(me, fg_tid, TRUE)) {
            BringWindowToTop(h);
            SetForegroundWindow(h);
            AttachThreadInput(me, fg_tid, FALSE);
        }
    }
    if (GetForegroundWindow() != h) {
        // Being the source of the last input event grants foreground rights.
        INPUT in[2];
        memset(in, 0, sizeof in);
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = 0xE8;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        inject_keys(in, 2);
        SetForegroundWindow(h);
    }
    SetFocus(h);
}

static BOOL CALLBACK find_other_monitor(HMONITOR m, HDC dc, LPRECT r, LPARAM lp)
{
    (void)dc;
    (void)r;
    HMONITOR *io = (HMONITOR *)lp;
    if (m != io[0]) {
        io[1] = m;
        return FALSE;
    }
    return TRUE;
}

static void ui_place_window(void)
{
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    if (g_pinned || g_other_monitor) {
        HMONITOR io[2] = { mon, NULL };
        EnumDisplayMonitors(NULL, NULL, find_other_monitor, (LPARAM)io);
        if (io[1]) mon = io[1];
    }
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    UINT dpi = monitor_dpi(mon);
    if (dpi != U.dpi) {
        ui_metrics(dpi);
        icons_reset(ui_icon_px());
    }
    RECT wa = mi.rcWork;
    int x = wa.left + ((wa.right - wa.left) - U.W) / 2;
    int y = wa.top + (int)((f32)((wa.bottom - wa.top) - U.H) * 0.28f);
    SetWindowPos(g_hwnd, U.app_mode ? HWND_NOTOPMOST : HWND_TOPMOST, x, y, U.W, U.H, SWP_NOACTIVATE);
    r_resize(U.W, U.H);
}

#define ROW_STAGGER_MAX 8

static bool anims_enabled(void)
{
    BOOL sys = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &sys, 0);
    return g_cfg.animations && sys;
}

static f32 anim_sec(int ms) { return (f32)ms / 1000.f * 100.f / (f32)MAX(g_cfg.anim_speed, 1); }

static f32 anim_omega(int ms)
{
    f32 t = anim_sec(ms);
    return t > 0.001f ? 4.7f / t : 0.f;
}

static f32 vis_curve(f32 v, bool opening)
{
    v = CLAMP(v, 0.f, 1.f);
    if (opening) return ease_out_quart(v);
    return v * v;
}

static f32 vis_eased(void) { return vis_curve(U.vis, U.vis_target > 0.5f); }

static void vis_set_direction(bool opening)
{
    f32 a = vis_eased();
    U.vis_target = opening ? 1.f : 0.f;
    U.vis = opening ? 1.f - sqrtf(sqrtf(1.f - a)) : sqrtf(a);
}

static f32 row_in(int k, f64 now)
{
    f32 dur = anim_sec(g_cfg.anim_row_ms);
    if (!g_cfg.anim_cascade || dur <= 0.f) return 1.f;
    f32 t = (f32)(now - U.open_time) - anim_sec(g_cfg.anim_stagger_ms) * (f32)MIN(k, ROW_STAGGER_MAX);
    return ease_out_cubic(t / dur);
}

static bool rows_cascading(f64 now)
{
    return g_cfg.anim_cascade && now - U.open_time < anim_sec(g_cfg.anim_row_ms) + anim_sec(g_cfg.anim_stagger_ms) * ROW_STAGGER_MAX;
}

static void apply_window_alpha(void)
{
    int a = (int)(vis_eased() * 255.f + 0.5f);
    if (a != U.alpha) {
        U.alpha = a;
        SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)a, LWA_ALPHA);
    }
}

static void set_click_through(bool on)
{
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    LONG_PTR nx = on ? (ex | WS_EX_TRANSPARENT) : (ex & ~(LONG_PTR)WS_EX_TRANSPARENT);
    if (nx != ex) SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, nx);
}

// Never hand focus to these: activating the taskbar while Win is down opens Start.
static bool is_shell_window(HWND h)
{
    WCHAR cls[64];
    if (!h || !GetClassNameW(h, cls, countof(cls))) return true;
    static const WCHAR *shell[] = { L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW",
                                    L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow",
                                    L"TopLevelWindowForOverflowXamlIsland", L"NotifyIconOverflowWindow" };
    for (int i = 0; i < countof(shell); i++)
        if (!wcscmp(cls, shell[i])) return true;
    return false;
}

static HWND taskbar_on_monitor(HMONITOR mon)
{
    HWND h = FindWindowW(L"Shell_TrayWnd", NULL);
    if (h && MonitorFromWindow(h, MONITOR_DEFAULTTONULL) == mon) return h;
    for (h = NULL; (h = FindWindowExW(NULL, h, L"Shell_SecondaryTrayWnd", NULL)) != NULL;)
        if (MonitorFromWindow(h, MONITOR_DEFAULTTONULL) == mon) return h;
    return NULL;
}

// Fullscreen app over the taskbar: open as a regular window so the shell brings the taskbar back.
static bool taskbar_covered(HMONITOR mon)
{
    HWND tray = taskbar_on_monitor(mon);
    if (!tray || !IsWindowVisible(tray)) return false;
    RECT r;
    GetWindowRect(tray, &r);
    POINT c = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
    HWND at = WindowFromPoint(c);
    HWND root = at ? GetAncestor(at, GA_ROOT) : NULL;
    if (!root || root == tray || root == g_hwnd) return false;
    return !(GetWindowLongW(root, GWL_EXSTYLE) & WS_EX_TOPMOST);
}

static void set_app_window_mode(bool app)
{
    U.app_mode = app;
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    LONG_PTR nx = app ? (ex & ~(LONG_PTR)WS_EX_TOOLWINDOW) : (ex | WS_EX_TOOLWINDOW);
    if (nx != ex) SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, nx);
    SetWindowPos(g_hwnd, app ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (nx != ex ? SWP_FRAMECHANGED : 0));
}

static void taskbar_button_remove(void)
{
    static ITaskbarList *tbl;
    static bool tried;
    static const GUID clsid = { 0x56fdf344, 0xfd6d, 0x11d0, { 0x95, 0x8a, 0x00, 0x60, 0x97, 0xc9, 0xa0, 0x90 } };
    static const GUID iid = { 0x56fdf342, 0xfd6d, 0x11d0, { 0x95, 0x8a, 0x00, 0x60, 0x97, 0xc9, 0xa0, 0x90 } };
    if (!tried) {
        tried = true;
        if (SUCCEEDED(CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &iid, (void **)&tbl)) && tbl) {
            if (FAILED(ITaskbarList_HrInit(tbl))) {
                ITaskbarList_Release(tbl);
                tbl = NULL;
            }
        }
    }
    if (tbl) ITaskbarList_DeleteTab(tbl, g_hwnd);
}

static void ui_hide_finish(void)
{
    U.cm.open = U.cm.closing = false;
    U.visible = false;
    U.closing = false;
    U.vis = U.vis_target = 0;
    ShowWindow(g_hwnd, SW_HIDE);
    set_click_through(false);
    set_app_window_mode(false);
    hook_watchdog_sync();
}

static void ui_show(void)
{
    if (U.visible && !U.closing) {
        force_foreground(g_hwnd);
        return;
    }
    if (U.visible && U.closing) {
        U.closing = false;
        vis_set_direction(true);
        U.open_time = 0;
        hook_set_visible(true);
        set_click_through(false);
        if (!g_pinned) force_foreground(g_hwnd);
        U.caret_on = true;
        SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), NULL);
        U.animating = true;
        return;
    }
    f64 now = time_now();
    hook_watchdog_off();
    hook_set_visible(true);
    HWND fg = GetForegroundWindow();
    U.prev_fg = fg != g_hwnd ? fg : NULL;

    bool was_dark = U.th.dark;
    theme_update();
    if (was_dark != U.th.dark) U.backdrop_ok = backdrop_apply(g_hwnd);
    ui_place_window();
    set_app_window_mode(taskbar_covered(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST)));
    // Take the keyboard while still transparent, so keys typed right after Win land here.
    SetLayeredWindowAttributes(g_hwnd, 0, 0, LWA_ALPHA);
    SendMessageW(g_hwnd, WM_NCACTIVATE, TRUE, -1);  // acrylic from the first frame, see WM_NCACTIVATE
    U.alpha = 0;
    set_click_through(false);
    if (g_pinned) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    } else {
        ShowWindow(g_hwnd, SW_SHOW);
        force_foreground(g_hwnd);
        if (GetForegroundWindow() != g_hwnd) log_msg("show: could not take foreground");
    }
    if (U.app_mode) taskbar_button_remove();

    bool keep = U.qlen && (now - U.hide_time) < (f64)g_cfg.keep_query_seconds;
    if (!keep) {
        U.qlen = 0;
        U.q[0] = 0;
        U.caret = U.anchor = 0;
    } else {
        U.anchor = 0;
        U.caret = U.qlen;
    }
    refresh_frecency();
    refresh_empty_view();
    U.q_scroll = 0;
    if (U.qlen) {
        search_apps();
        request_files();
    }
    rebuild_rows(false);
    U.scroll = U.scroll_target = U.scroll_v = 0;
    snap_highlight();
    U.armed = NULL;
    U.cm.open = U.cm.closing = false;
    U.cm.press = -1;
    U.sub_text = NULL;
    U.sub_t = 0;
    U.caret_on = true;
    U.visible = true;
    U.closing = false;
    U.vis = anims_enabled() ? 0.f : 1.f;
    U.open_time = anims_enabled() ? now : 0;
    U.settings_hover_t = 0;
    U.sb_t = U.sb_prev = 0;
    U.sb_seen = 0;
    U.sb_hot = false;
    U.vis_target = 1;
    U.press_row = -1;
    POINT cur;
    GetCursorPos(&cur);
    ScreenToClient(g_hwnd, &cur);
    U.mouse_x = cur.x;
    U.mouse_y = cur.y;
    U.dirty = true;
    ui_render();
    SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), NULL);
    SetTimer(g_hwnd, TIMER_REHOOK, 60, NULL);
    if (now - U.last_reindex > 180.0) {
        U.last_reindex = now;
        apps_request_reindex();
    }
}

static void ui_hide(void)
{
    if (!U.visible || U.closing) return;
    U.hide_time = time_now();
    U.armed = NULL;
    U.text_drag = false;
    U.tg.open = U.tg.closing = false;
    KillTimer(g_hwnd, TIMER_CARET);
    bool restore = !U.no_focus_restore;
    U.no_focus_restore = false;
    hook_set_visible(false);
    if (restore && !g_pinned && GetForegroundWindow() == g_hwnd && U.prev_fg && IsWindow(U.prev_fg) &&
        IsWindowVisible(U.prev_fg) && !is_shell_window(U.prev_fg))
        SetForegroundWindow(U.prev_fg);
    history_save();
    if (!anims_enabled() || U.vis <= 0.f) {
        ui_hide_finish();
        return;
    }
    U.closing = true;
    vis_set_direction(false);
    U.animating = true;
    set_click_through(true);
    ui_invalidate();
}

static void ui_toggle(void)
{
    if (U.visible && !U.closing) ui_hide();
    else ui_show();
}

static Row *sel_row(void) { return row_selectable(U.sel) ? &U.rows[U.sel] : NULL; }

static const WCHAR *row_path(Row *r)
{
    if (!r) return NULL;
    if (r->kind == ROW_FILE) return r->file->full;
    if (r->kind == ROW_APP && r->app->kind == APP_SHELL) return r->app->path;
    return NULL;
}

static void ui_activate(int act)
{
    Row *r = sel_row();
    if (!r) return;
    LaunchJob j;
    memset(&j, 0, sizeof j);
    j.act = act;
    WCHAR qn[Q_MAX + 1];
    int ql = query_norm(qn);
    if (r->kind == ROW_APP) {
        App *a = r->app;
        if (a->kind == APP_CMD) {
            if (act != ACT_OPEN) return;
            if (a->danger && U.armed != a) {
                U.armed = a;
                ui_invalidate();
                return;
            }
            j.cmd = a->cmd;
            history_record('c', a->id, qn, ql);
        } else {
            if ((act == ACT_REVEAL || act == ACT_PROPERTIES) && !a->path) return;
            j.is_app = true;
            j.target = wdup_heap(a->id);
            j.path = wdup_heap(a->path);
            if (act != ACT_PROPERTIES) history_record('a', a->id, qn, ql);
        }
    } else {
        FileItem *f = r->file;
        j.target = wdup_heap(f->full);
        j.dir = wdup_heap(f->dir);
        j.hist_type = (f->flags & FI_FOLDER) ? 'd' : 'f';
        j.hist_key = wdup_heap(f->full);
        if (act != ACT_PROPERTIES && !(f->full[0] == '\\' && f->full[1] == '\\')) history_record(j.hist_type, f->full, qn, ql);
    }
    AllowSetForegroundWindow(ASFW_ANY);
    ui_hide();
    launch_submit(&j);
}

static void ui_copy_path(void)
{
    Row *r = sel_row();
    if (!r) return;
    const WCHAR *p = row_path(r);
    clipboard_set(p ? p : r->app->name, -1);
}

static void move_sel(int delta)
{
    if (U.sel < 0) return;
    int dir = delta > 0 ? 1 : -1, steps = abs(delta), i = U.sel;
    while (steps-- > 0) {
        int n = first_selectable(i + dir, dir);
        if (n < 0) break;
        i = n;
    }
    if (i != U.sel) {
        U.sel = i;
        U.armed = NULL;
        ensure_visible(i, true);
        ui_invalidate();
    }
}

static void jump_section(int dir)
{
    int i = U.sel;
    if (i < 0) return;
    if (dir > 0) {
        for (int k = i + 1; k < U.row_count; k++)
            if (U.rows[k].kind == ROW_HEADER) {
                int n = first_selectable(k + 1, 1);
                if (n >= 0) {
                    U.sel = n;
                    ensure_visible(n, true);
                }
                break;
            }
    } else {
        int h = -1;
        for (int k = i; k >= 0; k--)
            if (U.rows[k].kind == ROW_HEADER) {
                h = k;
                break;
            }
        for (int k = h - 1; k >= 0; k--)
            if (U.rows[k].kind == ROW_HEADER) {
                int n = first_selectable(k + 1, 1);
                if (n >= 0) {
                    U.sel = n;
                    ensure_visible(n, true);
                }
                break;
            }
    }
    U.armed = NULL;
    ui_invalidate();
}

static void draw_placeholder_icon(f32 x, f32 y, f32 sz, const WCHAR *name)
{
    r_rect(x, y, sz, sz, U.th.tile, SR(LS()->tile_r));
    if (name && name[0]) {
        WCHAR c[2] = { (WCHAR)towupper(name[0]), 0 };
        f32 fs = sz * 0.5f;
        f32 w = text_width(FONT_TEXT_SEMIBOLD, fs, c, 1);
        text_draw(FONT_TEXT_SEMIBOLD, fs, floorf(x + (sz - w) * 0.5f), y + sz * 0.5f + font_cap_height(FONT_TEXT_SEMIBOLD, fs) * 0.5f, c, 1, U.th.dim);
    }
}

static const WCHAR *row_note(Row *r, u32 *col)
{
    *col = U.th.faint;
    if (!r || r->kind != ROW_APP) return NULL;
    if (r->app->kind != APP_CMD) return TR("Приложение", "Application");
    if (U.armed == r->app) {
        *col = U.th.danger;
        return TR("Нажмите Enter ещё раз для подтверждения", "Press Enter again to confirm");
    }
    return TR("Команда", "Command");
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
    u32 rest = color_mix(U.th.dim, col, 0.6f);
    f32 pen = x;
    for (int i = 0; i < len;) {
        int j = i;
        while (j < len && mask[j] == mask[i]) j++;
        pen = text_draw(mask[i] ? FONT_TEXT_SEMIBOLD : FONT_TEXT, fs, pen, base, s + i, j - i, mask[i] ? col : rest);
        i = j;
    }
    return pen - x;
}

static void draw_row(Row *r, f32 y, bool selected)
{
    Theme *t = &U.th;
    const LStyle *ls = LS();
    f32 x0 = SR(ls->inset), x1 = (f32)U.W - SR(ls->inset);
    if (r->kind == ROW_HEADER) {
        f32 fs = S(11.5f);
        text_draw(FONT_TEXT_SEMIBOLD, fs, x0 + SR(12), y + r->h - SR(10), r->text, -1, color_mix(t->faint, t->dim, 0.6f));
        return;
    }
    f32 cy = y + r->h * 0.5f;
    f32 fs = S(ls->name_fs);
    f32 base = floorf(cy + font_cap_height(FONT_TEXT, fs) * 0.5f + 0.5f);
    if (r->kind == ROW_VIRTUAL) {
        f32 rh = SR(ROW_H), isz = (f32)ui_icon_px();
        f32 pulse = 0.75f + 0.25f * sinf((f32)fmod(time_now(), 100.0) * 5.f);
        int k0 = (int)MAX(0.f, floorf((U.list_y0 - y) / rh)), k1 = (int)ceilf((U.list_y1 - y) / rh);
        k1 = MIN(k1, (int)(r->h / rh + 0.5f));
        for (int k = k0; k < k1; k++) {
            f32 ry = y + rh * (f32)k, rcy = ry + rh * 0.5f;
            f32 ix = x0 + SR(12), iy = floorf(rcy - isz * 0.5f + 0.5f);
            u32 c = color_alpha(t->tile, pulse);
            r_rect(ix, iy, isz, isz, c, SR(ls->tile_r));
            u32 hsh = (u32)(k + (int)(r->y / rh)) * 2654435761u;
            f32 w1 = SR(120) + (f32)(hsh % 160u) * U.s, w2 = SR(90) + (f32)((hsh >> 8) % 120u) * U.s;
            r_rect(ix + isz + SR(12), floorf(rcy - SR(5)), w1, SR(10), c, SR(5));
            r_rect(x1 - SR(12) - w2, floorf(rcy - SR(4)), w2, SR(8), color_alpha(c, 0.6f), SR(4));
        }
        return;
    }
    if (r->kind == ROW_INFO) {
        if (U.row_count == 1) {
            f32 lcy = U.list_y0 + list_height() * 0.5f - SR(6), tw = text_width(FONT_TEXT, fs, r->text, -1);
            text_draw_icon(0xE721, S(26), (f32)U.W * 0.5f, lcy - SR(20), t->faint);
            text_draw_fit(FONT_TEXT, fs, floorf(MAX(SR(20), ((f32)U.W - tw) * 0.5f)), floorf(lcy + SR(22) + 0.5f), x1 - SR(20), r->text, -1,
                          t->dim, false);
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
    f32 sub_base = floorf(cy + font_cap_height(FONT_TEXT, sub_fs) * 0.5f + 0.5f);
    f32 uv[4];

    if (r->kind == ROW_APP) {
        App *a = r->app;
        u32 sub_col = t->faint;
        const WCHAR *sub = row_note(r, &sub_col);
        if (a->kind == APP_CMD) {
            bool armed = U.armed == a;
            r_rect(ix, iy, isz, isz, armed ? color_alpha(t->danger, 0.16f) : t->tile, SR(ls->tile_r));
            r_rect_ex(ix, iy, isz, isz, t->key_border, SR(ls->tile_r), 1.f, 0);
            text_draw_icon(a->glyph, MIN(S(14), isz * 0.52f), ix + isz * 0.5f, iy + isz * 0.5f, armed ? t->danger : t->text);
        } else {
            if (icon_get(a->id_hash ^ 0xA11, ICON_KIND_APP, a->id, uv)) r_icon(ix, iy, isz, isz, uv[0], uv[1], uv[2], uv[3], 1.f);
            else draw_placeholder_icon(ix, iy, isz, icon_failed(a->id_hash ^ 0xA11) ? a->name : NULL);
        }
        f32 sw = text_width(FONT_TEXT, sub_fs, sub, -1);
        f32 avail = right - tx;
        bool show_sub = selected || a->kind == APP_CMD || ls->all_notes;
        draw_name(fs, tx, base, show_sub ? avail - sw - SR(16) : avail, a->name, a->len, a->norm, a->ws, t->text);
        if (ls->all_notes && a->kind != APP_CMD) {
            f32 away = CLAMP(fabsf(U.list_y0 - floorf(U.scroll + 0.5f) + U.hl_y - y) / r->h, 0.f, 1.f);
            if (!row_selectable(U.sel)) away = 1.f;
            text_draw(FONT_TEXT, sub_fs, right - sw, sub_base, sub, -1, color_alpha(t->faint, away));
        } else if (a->kind == APP_CMD) {
            f32 away = CLAMP(fabsf(U.list_y0 - floorf(U.scroll + 0.5f) + U.hl_y - y) / r->h, 0.f, 1.f);
            if (!row_selectable(U.sel)) away = 1.f;
            text_draw(FONT_TEXT, sub_fs, right - sw, sub_base, sub, -1, color_alpha(sub_col, away));
        }
    } else {
        FileItem *f = r->file;
        if (icon_get(f->icon_key, ICON_KIND_FILE, f->full, uv)) r_icon(ix, iy, isz, isz, uv[0], uv[1], uv[2], uv[3], 1.f);
        else if (icon_failed(f->icon_key)) draw_placeholder_icon(ix, iy, isz, f->name);
        f32 avail = right - tx, gap = SR(16);
        f32 nw = text_width(FONT_TEXT, fs, f->name, -1);
        f32 dw = text_width(FONT_TEXT, sub_fs, f->dir, -1);
        f32 name_max = nw + gap + dw <= avail ? nw : MAX(avail * 0.55f, avail - dw - gap);
        f32 drawn = draw_name(fs, tx, base, name_max, f->name, -1, NULL, NULL, t->text);
        f32 dir_max = avail - drawn - gap;
        if (dir_max > SR(40) && f->dir[0]) {
            f32 dwn = MIN(dw, dir_max);
            text_draw_fit(FONT_TEXT, sub_fs, right - dwn, sub_base, dir_max, f->dir, -1, t->faint, true);
        }
    }
}

static f32 draw_hint(f32 xr, f32 cy, const WCHAR *label, const WCHAR *const *keys, int nkeys, u32 label_col)
{
    Theme *t = &U.th;
    f32 kfs = S(11.5f), lfs = S(12.5f);
    f32 kh = SR(20), kpad = SR(6);
    for (int i = nkeys - 1; i >= 0; i--) {
        f32 w = MAX(text_width(FONT_TEXT, kfs, keys[i], -1) + kpad * 2, kh);
        f32 kx = floorf(xr - w), ky = floorf(cy - kh * 0.5f);
        r_rect(kx, ky, w, kh, t->key_bg, SR(LS()->key_r));
        f32 tw = text_width(FONT_TEXT, kfs, keys[i], -1);
        text_draw(FONT_TEXT, kfs, floorf(kx + (w - tw) * 0.5f + 0.5f), floorf(cy + font_cap_height(FONT_TEXT, kfs) * 0.5f + 0.5f), keys[i], -1, t->dim);
        xr = kx - SR(4);
    }
    f32 lw = text_width(FONT_TEXT, lfs, label, -1);
    xr -= SR(4);
    text_draw(FONT_TEXT, lfs, floorf(xr - lw), floorf(cy + font_cap_height(FONT_TEXT, lfs) * 0.5f + 0.5f), label, -1, label_col);
    return xr - lw - SR(18);
}

static void draw_footer(f32 y0)
{
    Theme *t = &U.th;
    f32 cy = y0 + U.footer_h * 0.5f;
    f32 xr = (f32)U.W - SR(14);
    static const WCHAR *k_enter[] = { L"Enter" };
    static const WCHAR *k_ctrl_enter[] = { L"Ctrl", L"Enter" };
    static const WCHAR *k_admin[] = { L"Ctrl", L"Shift", L"Enter" };
    static const WCHAR *k_copy[] = { L"Ctrl", L"C" };
    static const WCHAR *k_esc[] = { L"Esc" };
    if (LS()->footer_tint) r_rect(0, y0, (f32)U.W, U.footer_h, t->dark ? RGBA(255, 255, 255, 10) : RGBA(0, 0, 0, 8), 0);
    const WCHAR *label = TR("Настройки", "Settings");
    f32 lfs = S(12.5f), lw = text_width(FONT_TEXT, lfs, label, -1);
    f32 bx = SR(8), bh = SR(28), by = floorf(cy - bh * 0.5f);
    f32 bw = floorf(SR(8) + SR(16) + SR(7) + lw + SR(10));
    f32 hov = U.settings_hover_t;
    r_rect(bx, by, bw, bh, color_alpha(t->sel, hov), SR(LS()->key_r + 1));
    u32 col = color_mix(t->dim, t->text, hov);
    text_draw_icon(0xE713, S(14), bx + SR(8) + SR(8), cy, col);
    text_draw(FONT_TEXT, lfs, bx + SR(8) + SR(16) + SR(7), floorf(cy + font_cap_height(FONT_TEXT, lfs) * 0.5f + 0.5f), label, -1, col);
    U.settings_btn[0] = (int)bx;
    U.settings_btn[1] = (int)by;
    U.settings_btn[2] = (int)(bx + bw);
    U.settings_btn[3] = (int)(by + bh);
    f32 min_x = SR(150);
    memset(U.update_btn, 0, sizeof U.update_btn);
    if (upd_visible()) {
        WCHAR ub[64];
        const WCHAR *ul = upd_label(ub, countof(ub));
        int st = upd_state();
        bool busy = st == UPD_DOWNLOADING || st == UPD_INSTALLING;
        f32 ulw = text_width(FONT_TEXT, lfs, ul, -1), ux = bx + bw + SR(6);
        f32 uw = floorf(SR(8) + SR(16) + SR(7) + ulw + SR(10)), uh = U.update_hover_t * (busy ? 0.f : 1.f), rad = SR(LS()->key_r + 1);
        r_rect(ux, by, uw, bh, color_alpha(t->accent, 0.16f + 0.10f * uh), rad);
        if (st == UPD_DOWNLOADING) {
            f32 pw = floorf(uw * (f32)UPD.progress / 1000.f);
            if (pw > 0) {
                r_set_clip(ux, by, ux + pw, by + bh);
                r_rect(ux, by, uw, bh, color_alpha(t->accent, 0.22f), rad);
                r_set_clip(0, 0, (f32)U.W, (f32)U.H);
            }
        }
        u32 ucol = st == UPD_ERROR ? t->danger : t->text;
        text_draw_icon(st == UPD_ERROR ? 0xE72C : 0xE896, S(14), ux + SR(8) + SR(8), cy, ucol);
        text_draw(FONT_TEXT, lfs, ux + SR(8) + SR(16) + SR(7), floorf(cy + font_cap_height(FONT_TEXT, lfs) * 0.5f + 0.5f), ul, -1, ucol);
        U.update_btn[0] = (int)ux;
        U.update_btn[1] = (int)by;
        U.update_btn[2] = (int)(ux + uw);
        U.update_btn[3] = (int)(by + bh);
        min_x = MAX(min_x, ux + uw + SR(18));
    }
    Row *r = sel_row();
    if (!r) {
        draw_hint(xr, cy, TR("Закрыть", "Close"), k_esc, 1, t->dim);
    } else if (r->kind == ROW_APP && r->app->kind == APP_CMD) {
        bool armed = U.armed == r->app;
        draw_hint(xr, cy, armed ? TR("Подтвердить", "Confirm") : TR("Выполнить", "Run"), k_enter, 1, armed ? t->danger : t->text);
    } else {
        bool app = r->kind == ROW_APP;
        xr = draw_hint(xr, cy, TR("Открыть", "Open"), k_enter, 1, t->text);
        if (row_path(r)) {
            f32 need = SR(230);
            if (xr - need > min_x) xr = draw_hint(xr, cy, TR("Показать в папке", "Show in folder"), k_ctrl_enter, 2, t->dim);
        }
        if (app) {
            if (xr - SR(250) > min_x) xr = draw_hint(xr, cy, TR("Администратор", "Run as admin"), k_admin, 3, t->dim);
        } else {
            if (xr - SR(200) > min_x) xr = draw_hint(xr, cy, TR("Копировать путь", "Copy path"), k_copy, 2, t->dim);
        }
    }
}

static void draw_search(void)
{
    Theme *t = &U.th;
    f32 cy = U.search_h * 0.5f;
    const LStyle *ls = LS();
    text_draw_icon(0xE721, S(ls->search_icon), SR(ls->inset + 19), cy, U.qlen ? t->text : t->dim);
    f32 fx0 = SR(ls->inset + 42), fx1 = (f32)U.W - SR(20);
    f32 fs = S(ls->search_fs);
    f32 base = floorf(cy + font_cap_height(FONT_DISPLAY, fs) * 0.5f + 0.5f);
    f32 sel_top = floorf(cy - fs * 0.62f), sel_h = floorf(fs * 1.24f);
    if (!U.qlen) {
        text_draw_fit(FONT_DISPLAY, fs, fx0, base, fx1 - fx0, TR("Поиск приложений и файлов", "Search apps and files"), -1, t->faint, false);
        if (U.caret_on) r_rect(fx0, sel_top, MAX(1.f, floorf(S(1.5f))), sel_h, t->accent, SR(1));
        return;
    }
    static f32 offs[Q_MAX + 2];
    text_offsets(FONT_DISPLAY, fs, U.q, U.qlen, offs);
    f32 width = fx1 - fx0;
    f32 cx = offs[U.caret];
    if (cx - U.q_scroll > width - SR(4)) U.q_scroll = cx - width + SR(4);
    if (cx - U.q_scroll < 0) U.q_scroll = cx;
    if (offs[U.qlen] - U.q_scroll < width - SR(4)) U.q_scroll = MAX(0.f, offs[U.qlen] - width + SR(4));
    f32 ox = fx0 - floorf(U.q_scroll);
    r_set_clip(fx0 - SR(2), 0, fx1, U.search_h);
    if (U.anchor != U.caret) {
        int a = MIN(U.anchor, U.caret), b = MAX(U.anchor, U.caret);
        r_rect(floorf(ox + offs[a]), sel_top, floorf(offs[b] - offs[a] + 0.5f), sel_h, t->text_sel, SR(3));
    }
    text_draw(FONT_DISPLAY, fs, ox, base, U.q, U.qlen, t->text);
    if (U.caret_on) r_rect(floorf(ox + cx), sel_top, MAX(1.f, floorf(S(1.5f))), sel_h, t->accent, SR(1));
    r_set_clip(0, 0, (f32)U.W, (f32)U.H);
}

static void ui_draw(void)
{
    Theme *t = &U.th;
    icons_begin_frame();
    hl_prepare();
    r_begin();
    r_rect(0, 0, (f32)U.W, (f32)U.H, U.backdrop_ok ? t->bg : t->bg_solid, 0);
    draw_search();
    r_rect(0, U.search_h, (f32)U.W, 1, t->sep, 0);

    r_set_clip(0, U.list_y0, (f32)U.W, U.list_y1);
    f32 off = U.list_y0 - floorf(U.scroll + 0.5f);
    f64 now = time_now();
    bool cascade = rows_cascading(now);
    f32 hl_op = 1.f;
    if (row_selectable(U.sel)) {
        int k = 0;
        for (int i = 0; cascade && i < U.sel; i++)
            if (U.rows[i].y + U.rows[i].h >= U.scroll) k++;
        hl_op = R.opacity = cascade ? row_in(k, now) : 1.f;
        const LStyle *ls = LS();
        f32 hy = floorf(off + U.hl_y + 0.5f), hh = floorf(U.hl_h + 0.5f);
        f32 hx = SR(ls->inset), hw = (f32)U.W - hx * 2, hr = SR(ls->sel_r);
        if (ls->sel == SEL_PILL) {
            r_rect(hx, hy, hw, hh, t->sel, hr);
            r_rect_ex(hx, hy, hw, hh, t->sel_border, hr, 1.f, 0);
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
        draw_row(r, off + r->y + dy, i == U.sel);
    }
    u32 note_col;
    const WCHAR *note = row_note(sel_row(), &note_col);
    if (note) {
        U.sub_text = note;
        U.sub_col = note_col;
    }
    if (U.sub_text && U.sub_t > 0.f) {
        f32 nfs = S(12.5f), cy = off + U.hl_y + U.hl_h * 0.5f;
        f32 nw = text_width(FONT_TEXT, nfs, U.sub_text, -1);
        R.opacity = hl_op * U.sub_t;
        text_draw(FONT_TEXT, nfs, floorf((f32)U.W - SR(20) - nw), floorf(cy + font_cap_height(FONT_TEXT, nfs) * 0.5f + 0.5f), U.sub_text, -1,
                  U.sub_col);
    }
    R.opacity = 1.f;
    r_set_clip(0, 0, (f32)U.W, (f32)U.H);

    f32 ms = max_scroll();
    if (ms > 0 && U.sb_t > 0.f) {
        f32 lh = list_height() - SR(8), bar_h = MAX(SR(24), lh * lh / U.content_h);
        f32 by = U.list_y0 + SR(4) + (lh - bar_h) * CLAMP(U.scroll / ms, 0.f, 1.f), bw = U.sb_hot ? SR(5) : SR(3);
        r_rect((f32)U.W - SR(3) - bw, floorf(by), bw, floorf(bar_h), color_alpha(U.sb_hot ? t->dim : t->faint, U.sb_t), bw * 0.5f);
    }

    f32 fy = U.list_y1;
    r_rect(0, fy, (f32)U.W, 1, t->sep, 0);
    draw_footer(fy + 1);
    cm_draw();
    tg_draw();
}

static bool renderer_recover(void)
{
    settings_device_lost();
    r_shutdown();
    if (!r_init(g_hwnd, U.W, U.H)) return false;
    font_reset_atlas();
    icons_reset(ui_icon_px());
    settings_device_restored();
    return true;
}

static void animate(f32 dt)
{
    bool anim = false;
    U.scroll_target = CLAMP(U.scroll_target, 0.f, max_scroll());
    anim |= spring_step(&U.scroll, &U.scroll_v, U.scroll_target, anim_omega(g_cfg.anim_scroll_ms), dt);
    if (row_selectable(U.sel)) {
        f32 ty = U.rows[U.sel].y, th = U.rows[U.sel].h;
        if (fabsf(ty - U.hl_y) > list_height()) {
            U.hl_y = ty;
            U.hl_vy = 0;
        }
        anim |= spring_step(&U.hl_y, &U.hl_vy, ty, anim_omega(g_cfg.anim_select_ms), dt);
        anim |= spring_step(&U.hl_h, &U.hl_vh, th, anim_omega(g_cfg.anim_select_ms), dt);
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
    anim |= approach(&U.sub_t, row_selectable(U.sel) && U.rows[U.sel].kind == ROW_APP ? 1.f : 0.f, 24.f, dt);
    anim |= cm_animate(dt);
    anim |= tg_animate(dt);
    if (U.vis != U.vis_target) {
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
    R.scale = (U.vis >= 1.f && U.vis_target >= 1.f) ? 1.f : U.closing ? 0.985f + 0.015f * v : s0 + (1.f - s0) * v;
    ui_draw();
    if (F.overflow) {
        font_reset_atlas();
        ui_draw();
        F.overflow = false;
    }
    apply_window_alpha();
    r_end_and_present();
    U.dirty = false;
    if (U.closing && U.vis <= 0.f) ui_hide_finish();
}

static int row_at(int y)
{
    if (y < U.list_y0 || y >= U.list_y1) return -1;
    f32 cy = (f32)y - U.list_y0 + U.scroll;
    int i = row_first_below(cy);
    return i < U.row_count && cy >= U.rows[i].y ? i : -1;
}

static int text_index_at(int x)
{
    if (!U.qlen) return 0;
    static f32 offs[Q_MAX + 2];
    f32 fs = S(LS()->search_fs);
    text_offsets(FONT_DISPLAY, fs, U.q, U.qlen, offs);
    f32 lx = (f32)x - SR(LS()->inset + 42) + floorf(U.q_scroll);
    int best = 0;
    for (int i = 0; i <= U.qlen; i++)
        if (fabsf(offs[i] - lx) < fabsf(offs[best] - lx)) best = i;
    return best;
}

enum { CM_OPEN = 1, CM_REVEAL, CM_RUNAS, CM_COPY, CM_PROPS, CM_TAG_ADD, CM_TAG_CLEAR, CM_TAG_INFO };
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
        *type = (r->file->flags & FI_FOLDER) ? 'd' : 'f';
        return r->file->full;
    }
    if (r->kind == ROW_APP) {
        *type = r->app->kind == APP_CMD ? 'c' : 'a';
        return r->app->id;
    }
    return NULL;
}

static void tg_open(void);

static void tags_changed(void)
{
    search_apps();
    search_tags();
    rebuild_rows(true);
    snap_highlight();
    ui_invalidate();
}

static void ui_context_menu(int x, int y, bool keyboard)
{
    Row *r = sel_row();
    if (!r) return;
    bool is_cmd = r->kind == ROW_APP && r->app->kind == APP_CMD;
    const WCHAR *path = row_path(r);
    bool reopen = U.cm.open && !U.cm.closing;
    U.cm.n = 0;
    cm_add(CM_OPEN, is_cmd ? 0xE768 : 0xE8A7, is_cmd ? TR("Выполнить", "Run") : TR("Открыть", "Open"), L"Enter", false);
    if (!is_cmd) {
        if (path) cm_add(CM_REVEAL, 0xE838, TR("Показать в папке", "Show in folder"), L"Ctrl+Enter", false);
        cm_add(CM_RUNAS, 0xE7EF, TR("Запустить от имени администратора", "Run as administrator"), L"Ctrl+Shift+Enter", false);
        cm_add(CM_COPY, 0xE8C8, path ? TR("Копировать путь", "Copy path") : TR("Копировать имя", "Copy name"), L"Ctrl+C", true);
        if (path) cm_add(CM_PROPS, 0xE946, TR("Свойства", "Properties"), L"Alt+Enter", false);
    }
    u8 ttype;
    const WCHAR *tkey = row_tag_key(r, &ttype);
    if (tkey) {
        cm_add(CM_TAG_ADD, 0xE8EC, TR("Добавить тег", "Add tag"), L"", true);
        TagItem *ti = tags_find(ttype, tkey);
        if (ti && ti->ntags) {
            cm_add(CM_TAG_CLEAR, 0xE74D, TR("Удалить все теги", "Remove all tags"), L"", false);
            tags_count_label(U.cm.tag_count, countof(U.cm.tag_count), ti->ntags);
            // the tags themselves go to the shortcut column, cut to a sane width
            int n = 0, cap = 40;
            U.cm.tag_list[0] = 0;
            for (int i = 0; i < ti->ntags && n < cap; i++) {
                int add = _snwprintf(U.cm.tag_list + n, countof(U.cm.tag_list) - n, L"%ls%ls", i ? L", " : L"", ti->tags[i].text);
                if (add < 0) break;
                n += add;
            }
            U.cm.tag_list[countof(U.cm.tag_list) - 1] = 0;
            if (n > cap) wcopy(U.cm.tag_list + cap, countof(U.cm.tag_list) - cap, L"…");
            cm_add(CM_TAG_INFO, 0, U.cm.tag_count, U.cm.tag_list, false);
            U.cm.it[U.cm.n - 1].disabled = true;
        }
    }
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
    Theme *t = &U.th;
    f32 e = U.cm.closing ? U.cm.t : ease_out_cubic(U.cm.t);
    f32 x = U.cm.x, y = U.cm.y - floorf((1.f - e) * SR(6) + 0.5f), w = U.cm.w, h = U.cm.h, rad = SR(10);
    R.opacity = e;
    r_set_clip(0, 0, (f32)U.W, (f32)U.H);
    r_rect_ex(x, y + SR(8), w, h, t->dark ? RGBA(0, 0, 0, 110) : RGBA(0, 0, 0, 40), rad, 0, SR(18));
    r_rect(x, y, w, h, t->dark ? RGBA(40, 40, 43, 252) : RGBA(252, 252, 253, 252), rad);
    r_rect_ex(x, y, w, h, t->dark ? RGBA(255, 255, 255, 20) : RGBA(0, 0, 0, 22), rad, 1.f, 0);
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
        text_draw(FONT_TEXT, fs, lx, floorf(cy + font_cap_height(FONT_TEXT, fs) * 0.5f + 0.5f), U.cm.it[i].label, -1, off ? t->faint : t->text);
        f32 kw = text_width(FONT_TEXT, kfs, U.cm.it[i].keys, -1);
        text_draw(FONT_TEXT, kfs, floorf(ix + iw - SR(12) - kw), floorf(cy + font_cap_height(FONT_TEXT, kfs) * 0.5f + 0.5f), U.cm.it[i].keys, -1,
                  t->faint);
    }
    R.opacity = 1.f;
}

// "Add tag": a small dialog over the launcher, in the manner of the hotkey recorder in the settings.
static bool tg_can_save(void)
{
    for (int i = 0; i < U.tg.len; i++)
        if (U.tg.buf[i] != ' ' && U.tg.buf[i] != ',' && U.tg.buf[i] != ';') return true;
    return false;
}

static void tg_caret_reset(void)
{
    U.caret_on = true;
    if (U.visible) SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), NULL);
    ui_invalidate();
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
    tg_caret_reset();
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
    tg_caret_reset();
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
    tg_caret_reset();
    return true;
}

static void tg_char(WCHAR c)
{
    if (c < 0x20 || c == 0x7F || U.tg.closing) return;
    tg_edit(U.tg.caret, U.tg.caret, &c, 1);
}

static bool tg_in(const f32 *b, int x, int y) { return (f32)x >= b[0] && (f32)x < b[2] && (f32)y >= b[1] && (f32)y < b[3]; }

static int tg_btn_at(int x, int y)
{
    for (int i = 0; i < 2; i++)
        if (tg_in(U.tg.btn[i], x, y)) return i;
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

static void tg_keycap(f32 x, f32 cy, const WCHAR *k, u32 col)
{
    Theme *t = &U.th;
    f32 kfs = S(11.5f), kh = SR(20), kpad = SR(6);
    f32 tw = text_width(FONT_TEXT, kfs, k, -1), w = MAX(tw + kpad * 2, kh);
    r_rect(x, floorf(cy - kh * 0.5f), w, kh, t->key_bg, SR(LS()->key_r));
    text_draw(FONT_TEXT, kfs, floorf(x + (w - tw) * 0.5f + 0.5f), floorf(cy + font_cap_height(FONT_TEXT, kfs) * 0.5f + 0.5f), k, -1, col);
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
        f32 kw = MAX(text_width(FONT_TEXT, S(11.5f), key, -1) + SR(12), SR(20));
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
        tg_keycap(bx + SR(10) + lw + SR(8), bcy, key, enabled ? t->dim : t->faint);
        xr = bx - SR(6);
    }
    R.opacity = 1.f;
}

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

static bool ui_keydown(WPARAM vk)
{
    if (tg_key(vk)) return true;
    if (cm_key(vk)) return true;
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0;
    int page = MAX(1, g_cfg.rows - 1);
    switch (vk) {
    case VK_ESCAPE:
        if (U.armed) {
            U.armed = NULL;
            ui_invalidate();
        } else if (U.qlen) {
            edit_replace(0, U.qlen, L"", 0);
        } else {
            ui_hide();
        }
        return true;
    case VK_RETURN:
        ui_activate(alt ? ACT_PROPERTIES : (ctrl && shift) ? ACT_RUNAS : ctrl ? ACT_REVEAL : ACT_OPEN);
        return true;
    case VK_DOWN: move_sel(1); return true;
    case VK_UP: move_sel(-1); return true;
    case VK_NEXT: move_sel(page); return true;
    case VK_PRIOR: move_sel(-page); return true;
    case VK_TAB: jump_section(shift ? -1 : 1); return true;
    case VK_HOME:
        if (ctrl) move_sel(-100000);
        else edit_move(0, shift);
        return true;
    case VK_END:
        if (ctrl) move_sel(100000);
        else edit_move(U.qlen, shift);
        return true;
    case VK_LEFT:
        if (!shift && U.anchor != U.caret) edit_move(MIN(U.anchor, U.caret), false);
        else edit_move(ctrl ? word_left(U.caret) : char_left(U.caret), shift);
        return true;
    case VK_RIGHT:
        if (!shift && U.anchor != U.caret) edit_move(MAX(U.anchor, U.caret), false);
        else edit_move(ctrl ? word_right(U.caret) : char_right(U.caret), shift);
        return true;
    case VK_BACK:
        if (U.anchor != U.caret) edit_replace(U.anchor, U.caret, L"", 0);
        else if (U.caret > 0) edit_replace(ctrl ? word_left(U.caret) : char_left(U.caret), U.caret, L"", 0);
        return true;
    case VK_DELETE:
        if (U.anchor != U.caret) edit_replace(U.anchor, U.caret, L"", 0);
        else if (U.caret < U.qlen) edit_replace(U.caret, ctrl ? word_right(U.caret) : char_right(U.caret), L"", 0);
        return true;
    case VK_F4:
        if (alt) {
            ui_hide();
            return true;
        }
        break;
    case VK_APPS: {
        Row *r = sel_row();
        if (r) {
            f32 off = U.list_y0 - U.scroll;
            ui_context_menu((int)SR(56), (int)(off + r->y + r->h), true);
        }
        return true;
    }
    }
    if (ctrl && !alt) {
        switch (vk) {
        case 'A': U.anchor = 0; U.caret = U.qlen; ui_invalidate(); return true;
        case 'C':
            if (U.anchor != U.caret) clipboard_set(U.q + MIN(U.anchor, U.caret), abs(U.caret - U.anchor));
            else ui_copy_path();
            return true;
        case 'X':
            if (U.anchor != U.caret) {
                clipboard_set(U.q + MIN(U.anchor, U.caret), abs(U.caret - U.anchor));
                edit_replace(U.anchor, U.caret, L"", 0);
            }
            return true;
        case 'V': clipboard_paste(); return true;
        case 'N': case 'J': move_sel(1); return true;
        case 'P': case 'K': move_sel(-1); return true;
        case VK_OEM_COMMA: open_settings_from_launcher(); return true;
        }
    }
    return false;
}

static void ui_char(WCHAR c)
{
    if (U.tg.open) {
        tg_char(c);
        return;
    }
    if (c < 0x20 || c == 0x7F) return;
    if (U.cm.open && !U.cm.closing) return;
    edit_replace(U.anchor, U.caret, &c, 1);
}

static bool in_update_button(int x, int y)
{
    return U.update_btn[2] > U.update_btn[0] && x >= U.update_btn[0] && x < U.update_btn[2] && y >= U.update_btn[1] && y < U.update_btn[3];
}

static bool in_settings_button(int x, int y)
{
    return x >= U.settings_btn[0] && x < U.settings_btn[2] && y >= U.settings_btn[1] && y < U.settings_btn[3];
}

static void open_settings_from_launcher(void)
{
    U.no_focus_restore = true;
    ui_hide();
    settings_open();
}

static void ui_mouse_move(int x, int y)
{
    if (x == U.mouse_x && y == U.mouse_y) return;
    U.mouse_x = x;
    U.mouse_y = y;
    if (U.tg.open) {
        int h = U.tg.closing ? -1 : tg_btn_at(x, y);
        if (h != U.tg.hover) {
            U.tg.hover = h;
            ui_invalidate();
        }
        if (U.tg.press == 2) {
            U.tg.caret = tg_index_at(x);
            tg_caret_reset();
        }
        return;
    }
    if (U.cm.open && !U.cm.closing) {
        int h = cm_item_at(x, y);
        if (h >= 0 && h != U.cm.hover && !U.cm.it[h].disabled) {
            U.cm.hover = h;
            U.animating = true;
            ui_invalidate();
        }
        return;
    }
    bool sb_hot = x >= U.W - (int)SR(14) && y >= U.list_y0 && y < U.list_y1;
    if (sb_hot != U.sb_hot) {
        U.sb_hot = sb_hot;
        U.animating = true;
        ui_invalidate();
    }
    bool hover = in_settings_button(x, y);
    if (hover != U.settings_hover) {
        U.settings_hover = hover;
        U.animating = true;
        ui_invalidate();
    }
    hover = in_update_button(x, y);
    if (hover != U.update_hover) {
        U.update_hover = hover;
        U.animating = true;
        ui_invalidate();
    }
    if (U.text_drag) {
        edit_move(text_index_at(x), true);
        return;
    }
    int r = row_at(y);
    if (row_selectable(r) && r != U.sel) {
        U.sel = r;
        U.armed = NULL;
        ui_invalidate();
    }
}

static void ui_mouse_down(int x, int y, bool dbl)
{
    if (U.tg.open) {
        if (U.tg.closing) return;
        U.tg.press = tg_btn_at(x, y);
        if (U.tg.press < 0 && tg_in(U.tg.field, x, y)) {
            U.tg.press = 2;  // placing the caret, and dragging it
            U.tg.caret = tg_index_at(x);
            SetCapture(g_hwnd);
            tg_caret_reset();
        } else if (U.tg.press < 0 && !tg_in(U.tg.panel, x, y)) {
            tg_close(false);
        }
        return;
    }
    if (U.cm.open) {
        U.cm.press = cm_item_at(x, y);
        if (!cm_inside(x, y)) cm_close(true);
        return;
    }
    if (y < U.search_h) {
        if (dbl) {
            U.anchor = 0;
            U.caret = U.qlen;
            ui_invalidate();
            return;
        }
        edit_move(text_index_at(x), GetKeyState(VK_SHIFT) < 0);
        U.text_drag = true;
        SetCapture(g_hwnd);
        return;
    }
    U.press_settings = in_settings_button(x, y);
    if (U.press_settings) return;
    U.press_update = in_update_button(x, y);
    if (U.press_update) return;
    int r = row_at(y);
    U.press_row = row_selectable(r) ? r : -1;
    if (U.press_row >= 0) {
        U.sel = r;
        ui_invalidate();
    }
}

static void ui_mouse_up(int x, int y)
{
    if (U.tg.open || U.tg.press >= 0) {
        int p = U.tg.press, i = tg_btn_at(x, y);
        U.tg.press = -1;
        if (p == 2) ReleaseCapture();
        if (U.tg.open && !U.tg.closing && i >= 0 && i == p) {
            if (i == 0) tg_close(false);
            else if (tg_can_save()) tg_close(true);
        }
        return;
    }
    if (U.cm.open || U.cm.press >= 0) {
        int i = cm_item_at(x, y);
        if (i >= 0 && i == U.cm.press) cm_run(i);
        U.cm.press = -1;
        U.press_row = -1;
        return;
    }
    if (U.text_drag) {
        U.text_drag = false;
        ReleaseCapture();
        return;
    }
    if (U.press_settings) {
        U.press_settings = false;
        if (in_settings_button(x, y)) open_settings_from_launcher();
        return;
    }
    if (U.press_update) {
        U.press_update = false;
        if (in_update_button(x, y)) upd_download();
        ui_invalidate();
        return;
    }
    int r = row_at(y);
    if (r >= 0 && r == U.press_row) {
        U.sel = r;
        ui_activate(ACT_OPEN);
    }
    U.press_row = -1;
}

static void ui_wheel(int delta)
{
    if (U.tg.open) return;
    if (U.cm.open) {
        cm_close(true);
        return;
    }
    U.scroll_target -= (f32)delta / WHEEL_DELTA * SR(ROW_H) * 3.f;
    U.scroll_target = CLAMP(U.scroll_target, 0.f, max_scroll());
    U.animating = true;
    ui_invalidate();
}
