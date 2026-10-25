// The launcher: its state (U), the rows it shows, the search behind them and the query field.
// Window and visibility are in ui_window.c, drawing in ui_draw.c, input in ui_input.c; the Start styles add
// start.c and start10.c.

enum { ROW_HEADER, ROW_APP, ROW_FILE, ROW_INFO, ROW_VIRTUAL };

// A full-width row, a Start grid tile, a Start section title, its hint, a Windows 10 tile.
enum { CELL_ROW, CELL_TILE, CELL_TITLE, CELL_HINT, CELL_W10 };
enum { TILE_SMALL, TILE_MEDIUM, TILE_WIDE, TILE_LARGE, TILE__COUNT };  // Windows 10 tile sizes

typedef struct Row {
    u8 kind, cell, tsize;
    App *app;
    FileItem *file;
    const WCHAR *text;
    f32 x, y, w, h;  // in the list's content, y from its top
} Row;

typedef struct AppHit {
    App *app;
    f32 score;
    int raw;   // the match alone, before frecency
    bool alt;  // matched in the other keyboard layout
} AppHit;

typedef struct TagHit {
    FileItem *file;
    f32 score;
} TagHit;

#define Q_MAX 500
#define TIMER_CARET 1
#define TIMER_REHOOK 2
#define TIMER_PRTSC 4

static struct {
    bool visible;
    bool closing;
    f32 vis, vis_target;  // 0..1, how far it has opened
    int alpha;
    HWND prev_fg;
    bool app_mode;
    bool slide;  // bottom positions: the window slides out from under the taskbar like Start
    HMONITOR mon;
    int win_x, win_y, slide_dy, slide_y;
    f32 slide_off, slide_v;  // offset from the resting place and its velocity, px
    bool no_focus_restore;
    int prtsc_ticks;  // PrtSc hand-off (hotkey.c): the foreground is taken back when the screenshot tool is done
    f32 settings_btn[4], update_btn[4];
    bool settings_hover, press_settings, update_hover, press_update;
    f32 update_hover_t;
    f64 hide_time;

    WCHAR q[Q_MAX + 1];
    int qlen, caret, anchor;
    bool search_mode, want_search;  // the Start styles opened from the taskbar's search: results even with no query
    f32 q_scroll;
    bool caret_on;
    bool text_drag;

    AppList *apps;
    AppHit *hits;
    int hit_count, hit_cap;
    TagHit thits[16];
    int thit_count;
    bool tags_first;
    bool files_alt;  // the file search goes in the other layout too
    FileResults *files;
    u32 files_req_id;
    bool files_pending;
    FileResults **fpages;  // pages fetched while scrolling, each owning its items
    int fpage_count, fpage_cap;
    FileItem **flist;      // every file so far, in order
    int flist_count, flist_cap;
    int files_shown;       // of flist; the rest of the total is a placeholder row (ROW_VIRTUAL)
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
    f32 hl_y, hl_h, hl_vy, hl_vh;  // the highlight on its springs
    f32 hl_x, hl_w, hl_vx, hl_vw;
    f64 open_time;
    f32 settings_hover_t;
    f32 sb_t, sb_prev;  // the scrollbar: shown while scrolling or hovered
    WCHAR hq[Q_MAX + 1], hq_alt[Q_MAX + 1];  // the query as highlighted in names
    int hq_len;
    bool hq_has_alt;
    f64 sb_seen;
    bool sb_hot;
    const WCHAR *sub_text;  // the note at the right of the selected row
    u32 sub_col;
    f32 sub_t;
    struct {  // the context menu, ctxmenu.c
        bool open, closing;
        f32 t;
        f32 x, y, w, h;
        int n, hover, press;
        f32 hl_y, hl_v;
        bool hl_init;
        struct { int cmd; u32 glyph; const WCHAR *label, *keys; bool sep, disabled; f32 y; } it[16];
        WCHAR tag_count[32], tag_list[96];
    } cm;
    struct {  // the tag dialog, tagdlg.c
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

    App *armed;  // a command that asks for a second Enter
    int mouse_x, mouse_y;
    bool menu_open;
    bool settings_after_hide;

    struct {  // the Windows 11 style, start.c
        bool all_view;      // "All apps" instead of the pinned grid
        App *tiles[36];     // pinned
        int tile_count;
        App *recs[18];      // recommended: the most launched of the rest
        int rec_count;
        f32 title_btn[4];   // the section title's button
        bool title_hover, title_press;
        f32 title_hover_t;
        f32 fb[5][4];       // footer: account, downloads, explorer, settings, power
        int fb_hover, fb_press;
        f32 fb_hover_t[5];
        WCHAR letters[72][2];
        int letter_count;
    } st;
    int tsel;  // Windows 10: the tile under the pointer (sel_row() then returns it), -1 none
    struct {  // the Windows 10 style, start10.c
        Row tiles[64];
        int tile_count;
        f32 tiles_h, tscroll, tscroll_target;
        int press_tile;
        bool rail_open;
        f32 rail_t;
        f32 rail[6][4];     // menu, account, explorer, downloads, settings, power
        int rail_hover, rail_press;
        f32 rail_hover_t[6];
        f32 act[4][4];      // search: the actions of the selected result
        int act_cmd[4], act_n, act_hover, act_press;
        App *tdrag;         // the tile being dragged to a new cell, and that cell
        int drag_cx, drag_cy;
        POINT press_pt;
        f32 grab_x, grab_y; // where in the tile it was taken
        int drag;           // resizing by an edge: EDGE_* bits
        POINT drag_pt;
        int drag_w, drag_h, drag_y;
        bool wide;          // on a monitor 2560 px wide or more: a wider search pane
    } w10;

    u32 dpi;
    f32 s;
    int W, H;
    f32 search_h, footer_h, list_y0, list_y1;
    bool backdrop_ok;
    f64 last_reindex;
} U;

// Showing results: a query is typed, or the search was opened empty.
static bool q_active(void) { return U.qlen || U.search_mode; }

static void ui_invalidate(void) { U.dirty = true; }

static f32 S(f32 v) { return v * U.s; }
static f32 SR(f32 v) { return floorf(v * U.s + 0.5f); }

#define ROW_H (st_on() && q_active() ? 56.f : LS()->row_h)  // the Windows 11 search: room for the icon plates
#define HEADER_H (LS()->header_h)
#define LIST_PAD 6.f
#define ICON_LOGICAL (LS()->icon)

static int ui_icon_px(void) { return (int)(ICON_LOGICAL * U.s + 0.5f); }

static void ui_metrics(u32 dpi)
{
    U.dpi = dpi ? dpi : 96;
    U.s = (f32)U.dpi / 96.f * (f32)g_cfg.scale / 100.f;
    U.search_h = SR(LS()->search_h);
    U.footer_h = SR(LS()->footer_h);
    if (st_on()) {
        st_metrics();
        return;
    }
    if (w10_on()) {
        w10_metrics();
        return;
    }
    f32 list_h = SR(g_cfg.rows * ROW_H + 2 * LIST_PAD);
    U.W = (int)SR((f32)g_cfg.width);
    if (cap_on()) {  // the query capsule, a gap, then one panel for the list and the footer
        f32 m = SR(CAP_MARGIN);
        U.W += (int)(m * 2);
        U.list_y0 = m + U.search_h + SR(CAP_GAP);
        U.list_y1 = U.list_y0 + list_h;
        U.H = (int)(U.list_y1 + U.footer_h + m);
        return;
    }
    U.H = (int)(U.search_h + 1 + list_h + 1 + U.footer_h);
    U.list_y0 = U.search_h + 1;
    U.list_y1 = U.list_y0 + list_h;
}

static f32 list_height(void) { return U.list_y1 - U.list_y0; }

// ---- rows

static Row *row_add(u8 kind, f32 h)
{
    if (!GROW(U.rows, U.row_cap, U.row_count + 1, 256)) return NULL;
    Row *r = &U.rows[U.row_count++];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->x = SR(LS()->inset);
    r->w = (f32)U.W - r->x * 2;
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

// The Design page's previews leave the AI apps out (pv_update sets this while it draws them).
static bool g_hide_ai;

static bool app_hidden(const App *a)
{
    static const WCHAR *const k[] = { L"ChatGPT", L"Claude", L"Codex", L"Gemini", L"Copilot", L"Perplexity", L"Antigravity", L"OpenAI",
                                      L"Anthropic", L"Grok", L"DeepSeek", L"Ollama", L"LM Studio", L"Cursor", L"Windsurf", L"Mistral",
                                      L"Qwen", L"Kimi", L"Kiro", L"Midjourney", L"Character.AI" };
    if (!g_hide_ai || !a || !a->name) return false;
    for (int i = 0; i < countof(k); i++)
        if (StrStrIW(a->name, k[i])) return true;
    return false;
}

static void row_app(App *a)
{
    if (app_hidden(a)) return;
    Row *r = row_add(ROW_APP, SR(ROW_H));
    if (r) r->app = a;
}

static void row_file(FileItem *f)
{
    Row *r = row_add(ROW_FILE, SR(ROW_H));
    if (r) r->file = f;
}

// The first row that ends below y.
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

// Rows the file list takes: the ones loaded, or the total Everything reported while more pages are to come.
static int files_virtual_total(void)
{
    int n = U.flist_count;
    if (U.files_fetched < U.files_total) n = MAX(n, (int)MIN(U.files_total, (u32)FILES_VIRTUAL_MAX));
    return n;
}

static bool tag_hit_has(const WCHAR *full);

// Lays out what the launcher shows now: a Start home, the empty view or the results. keep_sel keeps the
// selected row where it can.
static void rebuild_rows(bool keep_sel)
{
    int old = U.sel;
    U.row_count = 0;
    U.files_end_y = -1;
    U.content_h = SR(LIST_PAD);
    if (!q_active() && st_on()) {
        st_build_home();
    } else if (!q_active() && w10_on()) {
        w10_build_home();
    } else if (U.qlen == 0 && U.search_mode) {
        row_info(TR("Введите запрос", "Type to search"));
    } else if (U.qlen == 0) {
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
                if (total > U.files_shown) wfmt(U.files_header, countof(U.files_header), L"%ls  ·  %d", TR("Файлы", "Files"), (int)U.files_total);
                else wcopy(U.files_header, countof(U.files_header), TR("Файлы", "Files"));
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
    if (w10_on()) w10_place_rows();
    else if (st_search()) st_place_rows();

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
        U.hl_x = U.rows[U.sel].x;
        U.hl_w = U.rows[U.sel].w;
    }
    U.hl_vy = U.hl_vh = U.hl_vx = U.hl_vw = 0;
}

// Scrolls a row into view, with its section header (and from the very top for the first one).
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

// ---- search

static int cmp_hits(const void *a, const void *b)
{
    const AppHit *x = (const AppHit *)a, *y = (const AppHit *)b;
    if (x->score != y->score) return x->score < y->score ? 1 : -1;
    return x->app < y->app ? -1 : 1;
}

// The query trimmed and normalized for matching.
static int query_norm(WCHAR *out)
{
    int b = 0, e = U.qlen;
    while (b < e && U.q[b] == ' ') b++;
    while (e > b && U.q[e - 1] == ' ') e--;
    norm_str(out, U.q + b, e - b);
    return e - b;
}

static u64 app_hist_hash(const App *a) { return item_hash_of(app_item_type(a), a->id_hash); }

static void refresh_frecency(void)
{
    if (!U.apps) return;
    i64 now = unix_now();
    for (int i = 0; i < U.apps->count; i++) {
        App *a = &U.apps->apps[i];
        a->frec = frec_bonus(hist_frecency(app_item_type(a), a->id, app_hist_hash(a), now));
    }
}

static int cmp_frequent(const void *a, const void *b)
{
    App *x = *(App **)a, *y = *(App **)b;
    return x->frec < y->frec ? 1 : x->frec > y->frec ? -1 : 0;
}

// The empty launcher: the apps used most and the files opened lately.
static void refresh_empty_view(void)
{
    U.frequent_count = 0;
    if (U.apps) {
        App *tmp[512];
        int n = 0;
        for (int i = 0; i < U.apps->shell_count && n < countof(tmp); i++)
            if (U.apps->apps[i].frec > frec_bonus(0.3) && !app_hidden(&U.apps->apps[i])) tmp[n++] = &U.apps->apps[i];
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
        const WCHAR *name;
        int dl = path_split(f->full, &name);
        f->name = (WCHAR *)name;
        f->dir = wdup(&U.recent_arena, f->full, dl);
        f->flags = rf[i].folder ? FI_FOLDER : 0;
        f->icon_key = icon_key_for_file(f->full, rf[i].folder);
    }
    if (st_on()) st_refresh_tiles();
    if (w10_on()) w10_refresh_tiles();
}

#define KW_MIN (SCORE_SUBSTRING - 400)  // a keyword counts from a substring match up, a fuzzy one is too loose

// Scores every app against the query (and the query in the other layout): its name, its hidden keywords and
// its tags, then the launch history on top.
static void search_apps(void)
{
    U.hit_count = 0;
    U.files_alt = false;
    if (!U.apps || !U.qlen) return;
    if (!GROW(U.hits, U.hit_cap, U.apps->count, 256)) return;
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
            if (sk >= KW_MIN && sk - kw_pen > sc) {
                sc = sk - kw_pen;
                is_alt = false;
            }
            if (has_alt) {
                int sa = score_query(a->kw[k], a->kw_len[k], a->kw_ws[k], alt, ql);
                if (sa >= KW_MIN && (sa - kw_pen) * 9 / 10 > sc) {
                    sc = (sa - kw_pen) * 9 / 10;
                    is_alt = true;
                }
            }
        }
        if (a->kind == APP_CMD) {  // commands only for a clear match, the risky ones from three letters
            if (sc < SCORE_SUBSTRING - 600 || (a->danger && ql < 3)) sc = 0;
            else sc -= 500;
        }
        u8 type = app_item_type(a);
        u64 h = app_hist_hash(a);
        if (TG.n) {
            bool tag_alt;
            int st = tags_score(tags_find(type, a->id, h), qn, ql, alt, has_alt, &tag_alt);
            if (st && st + 400 > sc) {  // the user's own word for it beats a name match of the same kind
                sc = st + 400;
                is_alt = tag_alt;
            }
        }
        if (sc <= 0) continue;
        AppHit *hit = &U.hits[U.hit_count++];
        hit->app = a;
        hit->alt = is_alt;
        hit->raw = sc;
        hit->score = (f32)sc + a->frec + qmem_bonus(h, qn, ql, now);
    }
    qsort(U.hits, U.hit_count, sizeof(AppHit), cmp_hits);
    // Other-layout file search only for a clear name match: a loose fuzzy hit must not hide the file results.
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
        if (ti->type != IT_FILE && ti->type != IT_DIR) continue;
        bool is_alt;
        int st = tags_score(ti, qn, ql, alt, has_alt, &is_alt);
        if (!st) continue;
        tmp[n].file = &ti->file;
        tmp[n].score = (f32)(st + 400) + frec_bonus(hist_frecency(ti->type, ti->key, ti->hash, now)) + qmem_bonus(ti->hash, qn, ql, now);
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

// ---- files (everything.c answers through on_files_ready)

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
    if (!GROW(U.flist, U.flist_cap, U.flist_count + r->count, 256)) return false;
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

// Shows more of the loaded files as the list scrolls toward their end, and asks for the next page in time.
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
    for (int i = 0; i < r->count; i++) {
        FileItem *f = &r->items[i];
        u8 type = file_item_type(f);
        f->score += frec_bonus(hist_frecency(type, f->full, item_hash(type, f->full), now));
    }
    qsort(r->items, r->count, sizeof(FileItem), cmp_file_score);
    if (r->page) {
        U.files_more_pending = false;
        U.files_total = r->total;
        U.files_fetched = r->fetched;
        if (!GROW(U.fpages, U.fpage_cap, U.fpage_count + 1, 16) || !files_append(r)) {
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

// The caret shows at once and blinks from now.
static void caret_reset(void)
{
    U.caret_on = true;
    if (U.visible) SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), NULL);
    ui_invalidate();
}

static void on_query_changed(void)
{
    U.armed = NULL;
    cm_close(false);
    if (w10_on()) w10_fit_size();
    search_apps();
    search_tags();
    request_files();
    rebuild_rows(false);
    U.scroll = U.scroll_target = U.scroll_v = 0;
    snap_highlight();
    caret_reset();
}

// A new app list from the indexer (or the cache): the old one goes once nothing points into it.
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

// ---- the query field

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

static bool surrogate_pair_at(int i) { return i > 0 && U.q[i] >= 0xDC00 && U.q[i] <= 0xDFFF && U.q[i - 1] >= 0xD800 && U.q[i - 1] <= 0xDBFF; }

static int char_left(int i)
{
    if (i <= 0) return 0;
    i--;
    return surrogate_pair_at(i) ? i - 1 : i;
}

static int char_right(int i)
{
    if (i >= U.qlen) return U.qlen;
    i++;
    return i < U.qlen && surrogate_pair_at(i) ? i + 1 : i;
}

static void edit_replace(int a, int b, const WCHAR *s, int n)
{
    U.caret = U.anchor = text_splice(U.q, &U.qlen, countof(U.q), a, b, s, n);
    on_query_changed();
}

static void edit_move(int pos, bool extend)
{
    U.caret = CLAMP(pos, 0, U.qlen);
    if (!extend) U.anchor = U.caret;
    caret_reset();
}

static void clipboard_paste(void)
{
    WCHAR tmp[Q_MAX + 1];
    int n = clipboard_get(tmp, countof(tmp));
    if (n) edit_replace(U.anchor, U.caret, tmp, n);
}
