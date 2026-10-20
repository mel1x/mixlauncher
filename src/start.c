// The Windows 11 style: the launcher laid out like the Start menu. A grid of pinned and frequent apps
// (or all apps), the search box on top, the account and power buttons at the bottom.
// Search results reuse the regular list.

#include <sddl.h>

#define ST_W 654.f
#define ST_H 701.f
#define ST_COLS 6
#define ST_TILE_W 96.f
#define ST_TILE_H 100.f  // room for a name on two lines
#define ST_TITLE_H 60.f  // the title sits 34 below its top, the rest is room above the grid
#define PINS_MAX 64

enum { FB_ACCOUNT, FB_DOWNLOADS, FB_EXPLORER, FB_SETTINGS, FB_POWER, FB__COUNT };

static bool st_on(void) { return g_cfg.style == STYLE_START; }

#define ST_RESULTS 304.f  // the results column of the search; the chosen one is shown on the right

// Searching, with results to show: the list on the left and the pane about the chosen one.
// An empty query or no results take the whole width, the hint centered.
static bool st_search(void) { return st_on() && q_active() && first_selectable(0, 1) >= 0; }

static void st_place_rows(void)
{
    f32 x = SR(16), w = SR(ST_RESULTS) - x - SR(8);
    for (int i = 0; i < U.row_count; i++) {
        U.rows[i].x = x;
        U.rows[i].w = w;
    }
}

static f32 st_grid_x(void) { return floorf(((f32)U.W - SR(ST_TILE_W) * ST_COLS) * 0.5f); }

static void st_metrics(void)
{
    U.W = (int)SR(ST_W);
    U.H = (int)SR(ST_H);
    U.list_y0 = U.search_h;
    U.list_y1 = (f32)U.H - U.footer_h;
}

// ---- pins (pins.txt): app ids in the order they were pinned or moved, each with its Windows 10 tile size and
// cell (column and row, -1 until it has one). Shared with the Windows 10 style.

typedef struct Pin {
    WCHAR *id;
    u8 size;  // TILE_*
    i16 col, row;
} Pin;

static struct {
    Pin p[PINS_MAX];
    int n;
    bool loaded;
} PIN;

// <id> [<size> <col> <row>], tab-separated
static void pins_load(void)
{
    if (PIN.loaded) return;
    PIN.loaded = true;
    char *text = data_read(L"pins.txt");
    if (!text) return;
    char *cur = text, *line;
    WCHAR id[1024];
    while ((line = next_line(&cur)) != NULL && PIN.n < PINS_MAX) {
        if (!*line || *line == '#') continue;
        char *tab = strchr(line, '\t');
        int size = TILE_MEDIUM, col = -1, row = -1;
        if (tab) {
            *tab = 0;
            char *f;
            size = CLAMP((int)strtol(tab + 1, &f, 10), 0, TILE__COUNT - 1);
            if (*f == '\t') col = (int)strtol(f + 1, &f, 10);
            if (*f == '\t') row = (int)strtol(f + 1, &f, 10);
            else col = -1;
        }
        utf8_to_w(line, -1, id, countof(id));
        if (!id[0]) continue;
        Pin *p = &PIN.p[PIN.n++];
        bool cell = col >= 0 && row >= 0;
        p->size = (u8)size;
        p->col = (i16)(cell ? MIN(col, 63) : -1);
        p->row = (i16)(cell ? MIN(row, 250) : -1);
        p->id = wdup_heap(id);
    }
    free(text);
}

static void pins_save(void)
{
    Buf b = { 0 };
    buf_str(&b, "# MixLauncher pinned apps\n");
    for (int i = 0; i < PIN.n; i++) {
        buf_put_w(&b, PIN.p[i].id);
        buf_printf(&b, "\t%d\t%d\t%d\n", PIN.p[i].size, PIN.p[i].col, PIN.p[i].row);
    }
    buf_save_data(&b, L"pins.txt");
}

static int pin_index(const WCHAR *id)
{
    pins_load();
    for (int i = 0; i < PIN.n; i++)
        if (!wcscmp(PIN.p[i].id, id)) return i;
    return -1;
}

static int pin_size(int i) { return i >= 0 && i < PIN.n ? PIN.p[i].size : TILE_MEDIUM; }

static void pin_set_size(const WCHAR *id, int size)
{
    int i = pin_index(id);
    if (i < 0 || PIN.p[i].size == size) return;
    PIN.p[i].size = (u8)size;
    pins_save();
}

static void pin_set(const WCHAR *id, bool on)
{
    int i = pin_index(id);
    if (on && i < 0 && PIN.n < PINS_MAX) {
        Pin *p = &PIN.p[PIN.n++];
        p->id = wdup_heap(id);
        p->size = TILE_MEDIUM;
        p->col = p->row = -1;
    } else if (!on && i >= 0) {
        free(PIN.p[i].id);
        memmove(&PIN.p[i], &PIN.p[i + 1], sizeof(Pin) * (PIN.n - i - 1));
        PIN.n--;
    } else {
        return;
    }
    pins_save();
}

static void pin_to_top(const WCHAR *id)
{
    int i = pin_index(id);
    if (i <= 0) return;
    Pin p = PIN.p[i];
    memmove(&PIN.p[1], &PIN.p[0], sizeof(Pin) * i);
    PIN.p[0] = p;
    pins_save();
}

// ---- the grid

static App *app_by_id(const WCHAR *id)
{
    for (int i = 0; U.apps && i < U.apps->count; i++)
        if (!wcscmp(U.apps->apps[i].id, id)) return &U.apps->apps[i];
    return NULL;
}

// The pinned grid, and the recommended one: the most launched apps that are not pinned.
static void st_refresh_tiles(void)
{
    pins_load();
    U.st.tile_count = U.st.rec_count = 0;
    if (!U.apps) return;
    int cap = countof(U.st.tiles);
    for (int k = 0; k < PIN.n && U.st.tile_count < cap; k++) {
        App *a = app_by_id(PIN.p[k].id);
        if (a) U.st.tiles[U.st.tile_count++] = a;
    }
    App *tmp[512];
    int n = 0;
    for (int i = 0; i < U.apps->shell_count && n < countof(tmp); i++) {
        App *a = &U.apps->apps[i];
        if (a->frec > 0.f && pin_index(a->id) < 0) tmp[n++] = a;
    }
    qsort(tmp, n, sizeof(App *), cmp_frequent);
    for (int i = 0; i < n && U.st.rec_count < countof(U.st.recs); i++) U.st.recs[U.st.rec_count++] = tmp[i];
}

static void st_title(const WCHAR *text)
{
    Row *t = row_add(ROW_HEADER, SR(ST_TITLE_H));
    if (t) {
        t->cell = CELL_TITLE;
        t->text = text;
    }
}

static void st_hint(const WCHAR *text)
{
    Row *r = row_add(ROW_INFO, SR(36));
    if (r) {
        r->cell = CELL_HINT;
        r->text = text;
    }
}

static void st_grid(App **apps, int n)
{
    f32 tw = SR(ST_TILE_W), th = SR(ST_TILE_H), x0 = st_grid_x(), y0 = U.content_h;
    for (int i = 0; i < n; i++) {
        Row *r = row_add(ROW_APP, 0);
        if (!r) break;
        r->app = apps[i];
        r->cell = CELL_TILE;
        r->x = x0 + tw * (f32)(i % ST_COLS);
        r->w = tw;
        r->y = y0 + th * (f32)(i / ST_COLS);
        r->h = th;
    }
    U.content_h = y0 + th * (f32)((n + ST_COLS - 1) / ST_COLS);
}

// All apps by name, under a header for each first letter.
static void st_all_apps_rows(void)
{
    U.st.letter_count = 0;
    WCHAR last = 0;
    for (int i = 0; i < U.apps->shell_count; i++) {
        App *a = &U.apps->apps[i];
        WCHAR c = (WCHAR)towupper(a->name[0]);
        if (!iswalpha(c)) c = '#';
        if (c != last && U.st.letter_count < countof(U.st.letters)) {
            WCHAR *l = U.st.letters[U.st.letter_count++];
            l[0] = c;
            l[1] = 0;
            row_header(l);
            last = c;
        }
        row_app(a);
    }
}

static void st_build_home(void)
{
    U.content_h = 0;
    if (U.st.all_view) st_title(TR("Все приложения", "All apps"));
    else if (U.st.tile_count || !U.st.rec_count) st_title(TR("Закреплено", "Pinned"));  // hidden until something is pinned
    if (!U.apps) {
        row_info(TR("Индексирую приложения…", "Indexing applications…"));
        return;
    }
    if (U.st.all_view) {
        st_all_apps_rows();
        return;
    }
    if (U.st.tile_count) st_grid(U.st.tiles, U.st.tile_count);
    else if (!U.st.rec_count) st_hint(TR("Закрепляйте приложения через правый клик", "Pin apps with a right click"));
    if (U.st.rec_count) {
        st_title(TR("Рекомендуем", "Recommended"));
        st_grid(U.st.recs, U.st.rec_count);
    }
}

static void st_set_all_view(bool on)
{
    U.st.all_view = on;
    rebuild_rows(false);
    U.scroll = U.scroll_target = U.scroll_v = 0;
    snap_highlight();
    U.open_time = anims_enabled() ? time_now() : 0;  // the rows cascade in, as the pages of Start slide
    U.animating = true;
    ui_invalidate();
}

static void st_pins_changed(void)
{
    if (w10_on()) {
        w10_refresh_tiles();
        w10_pins_freeze();  // a new, resized or bumped tile keeps the cell it got
    } else {
        st_refresh_tiles();
    }
    rebuild_rows(true);
    snap_highlight();
    ui_invalidate();
}

// ---- the account

static struct {
    bool init;
    WCHAR name[128];
    WCHAR pic[MAX_PATH];
    u64 pic_key;
} STU;

static void st_user_init(void)
{
    if (STU.init) return;
    STU.init = true;
    typedef BOOLEAN (WINAPI *PFN_GetUserNameExW)(int, LPWSTR, PULONG);
    HMODULE m = LoadLibraryW(L"secur32.dll");
    PFN_GetUserNameExW gun = m ? (PFN_GetUserNameExW)(void *)GetProcAddress(m, "GetUserNameExW") : NULL;
    ULONG n = countof(STU.name);
    if (!gun || !gun(3 /*NameDisplay*/, STU.name, &n) || !STU.name[0]) {
        DWORD k = countof(STU.name);
        if (!GetUserNameW(STU.name, &k)) STU.name[0] = 0;
    }
    // The account picture, where Settings keeps it for this user's SID.
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return;
    BYTE buf[256];
    DWORD got = 0;
    WCHAR *sid = NULL;
    if (GetTokenInformation(tok, TokenUser, buf, sizeof buf, &got)) ConvertSidToStringSidW(((TOKEN_USER *)buf)->User.Sid, &sid);
    CloseHandle(tok);
    if (!sid) return;
    WCHAR key[256];
    wfmt(key, countof(key), L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AccountPicture\\Users\\%s", sid);
    LocalFree(sid);
    static const WCHAR *vals[] = { L"Image192", L"Image96", L"Image208", L"Image448" };
    for (int i = 0; i < countof(vals) && !STU.pic[0]; i++) {
        DWORD sz = sizeof STU.pic;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, vals[i], RRF_RT_REG_SZ, NULL, STU.pic, &sz) != ERROR_SUCCESS ||
            GetFileAttributesW(STU.pic) == INVALID_FILE_ATTRIBUTES)
            STU.pic[0] = 0;
    }
    if (STU.pic[0]) STU.pic_key = hash_wstr_i(STU.pic, -1) ^ 0xA7A7;
}

// ---- actions

static void st_open(const WCHAR *uri)
{
    AllowSetForegroundWindow(ASFW_ANY);
    ui_hide();
    launch_open(uri, OPEN_URI);
}

static void st_run(u8 cmd)
{
    ui_hide();
    launch_command(cmd);
}

// Hibernation is turned on (powercfg /h on) and the machine supports it.
static bool hibernate_allowed(void)
{
    typedef BOOLEAN (WINAPI *PFN_IsPwrHibernateAllowed)(void);
    HMODULE m = LoadLibraryW(L"powrprof.dll");
    PFN_IsPwrHibernateAllowed f = m ? (PFN_IsPwrHibernateAllowed)(void *)GetProcAddress(m, "IsPwrHibernateAllowed") : NULL;
    return f && f();
}

// The account or the power menu, at a button's top left.
static void st_menu_open(bool account, int x, int y)
{
    U.cm.n = 0;
    if (account) {
        cm_add(CM_ST_ACCOUNT, 0xE77B, TR("Изменить параметры учётной записи", "Change account settings"), L"", false);
        cm_add(CM_ST_LOCK, 0xE72E, TR("Заблокировать", "Lock"), L"", false);
        cm_add(CM_ST_SIGNOUT, 0xF3B1, TR("Выйти", "Sign out"), L"", false);
        cm_add(CM_ST_MLSET, 0xE713, TR("Настройки MixLauncher", "MixLauncher settings"), L"Ctrl+,", true);
    } else {
        cm_add(CM_ST_LOCK, 0xE72E, TR("Заблокировать", "Lock"), L"", false);
        cm_add(CM_ST_SLEEP, 0xE708, TR("Спящий режим", "Sleep"), L"", true);
        if (hibernate_allowed()) cm_add(CM_ST_HIBERNATE, 0xE823, TR("Гибернация", "Hibernate"), L"", false);
        cm_add(CM_ST_SHUTDOWN, 0xE7E8, TR("Завершение работы", "Shut down"), L"", false);
        cm_add(CM_ST_RESTART, 0xE777, TR("Перезагрузка", "Restart"), L"", false);
    }
    cm_show(x, y, false);
}

static void st_footer_click(int b)
{
    switch (b) {
    case FB_ACCOUNT:
    case FB_POWER: st_menu_open(b == FB_ACCOUNT, (int)U.st.fb[b][0], (int)U.st.fb[b][1]); break;
    case FB_DOWNLOADS: st_open(L"shell:Downloads"); break;
    case FB_EXPLORER: st_open(L"explorer.exe"); break;
    case FB_SETTINGS: st_open(L"ms-settings:"); break;
    }
}

// Commands of the footer menus, from cm_run.
static void st_menu_pick(int cmd)
{
    switch (cmd) {
    case CM_ST_ACCOUNT: st_open(L"ms-settings:yourinfo"); break;
    case CM_ST_LOCK: st_run(CMD_LOCK); break;
    case CM_ST_SIGNOUT: st_run(CMD_SIGNOUT); break;
    case CM_ST_MLSET: open_settings_from_launcher(); break;
    case CM_ST_SLEEP: st_run(CMD_SLEEP); break;
    case CM_ST_HIBERNATE: st_run(CMD_HIBERNATE); break;
    case CM_ST_SHUTDOWN: st_run(CMD_SHUTDOWN); break;
    case CM_ST_RESTART: st_run(CMD_RESTART); break;
    }
}

// ---- input

static int st_footer_at(int x, int y)
{
    for (int i = 0; i < FB__COUNT; i++)
        if (in_box(U.st.fb[i], (f32)x, (f32)y)) return i;
    return -1;
}

static bool st_title_at(int x, int y) { return y >= U.list_y0 && y < U.list_y1 && in_box(U.st.title_btn, (f32)x, (f32)y); }

// Hover over the footer buttons and the section button; true when the pointer is on one.
static bool st_mouse_move(int x, int y)
{
    if (st_search()) {
        int a = w10_act_at(x, y);
        if (a != U.w10.act_hover) {
            U.w10.act_hover = a;
            ui_invalidate();
        }
    }
    int f = st_footer_at(x, y);
    bool t = st_title_at(x, y);
    if (f != U.st.fb_hover || t != U.st.title_hover) {
        U.st.fb_hover = f;
        U.st.title_hover = t;
        U.animating = true;
        ui_invalidate();
    }
    return f >= 0 || t;
}

static bool st_mouse_down(int x, int y)
{
    if (st_search()) {
        U.w10.act_press = w10_act_at(x, y);
        if ((f32)x >= list_x1() && (f32)y >= U.list_y0 && (f32)y < U.list_y1) return true;
    }
    U.st.fb_press = st_footer_at(x, y);
    U.st.title_press = st_title_at(x, y);
    return U.st.fb_press >= 0 || U.st.title_press;
}

static bool st_mouse_up(int x, int y)
{
    int f = U.st.fb_press, ap = U.w10.act_press;
    bool t = U.st.title_press;
    U.st.fb_press = U.w10.act_press = -1;
    U.st.title_press = false;
    if (ap >= 0) {
        if (w10_act_at(x, y) == ap) w10_act_run(ap);
        return true;
    }
    if (f >= 0) {
        if (st_footer_at(x, y) == f) st_footer_click(f);
        return true;
    }
    if (t) {
        if (st_title_at(x, y)) st_set_all_view(!U.st.all_view);
        return true;
    }
    return false;
}

// Up and down in the grid go to the tile above or below, not the next one in the list.
static void st_move_vertical(int dir)
{
    Row *r = &U.rows[U.sel];
    f32 cx = r->x + r->w * 0.5f, line = 0, bd = 1e9f;
    int best = -1;
    for (int i = U.sel + dir; i >= 0 && i < U.row_count; i += dir) {
        Row *c = &U.rows[i];
        if (!row_selectable(i) || c->y == r->y) continue;
        if (best >= 0 && c->y != line) break;
        line = c->y;
        f32 d = fabsf(c->x + c->w * 0.5f - cx);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    if (best < 0) return;
    U.sel = best;
    U.armed = NULL;
    ensure_visible(best, true);
    ui_invalidate();
}

static bool st_key(WPARAM vk)
{
    if (vk == VK_ESCAPE && U.st.all_view && !q_active() && !U.armed) {
        st_set_all_view(false);
        return true;
    }
    Row *r = sel_row();
    if (!r || r->cell != CELL_TILE || q_active()) return false;
    switch (vk) {
    case VK_LEFT:
    case VK_RIGHT: move_sel(vk == VK_RIGHT ? 1 : -1); return true;
    case VK_UP:
    case VK_DOWN: st_move_vertical(vk == VK_DOWN ? 1 : -1); return true;
    }
    return false;
}

static bool st_animate(f32 dt)
{
    bool anim = approach(&U.st.title_hover_t, U.st.title_hover ? 1.f : 0.f, 22.f, dt);
    for (int i = 0; i < FB__COUNT; i++) anim |= approach(&U.st.fb_hover_t[i], U.st.fb_hover == i ? 1.f : 0.f, 22.f, dt);
    return anim;
}

// ---- drawing

static void st_draw_chrome(void)
{
    Theme *t = &TH;
    memset(U.st.title_btn, 0, sizeof U.st.title_btn);  // set again when the title is drawn, so not once it scrolls away
    // a dense acrylic close to the taskbar's color, as Start has; the page a shade lighter
    r_rect(0, 0, (f32)U.W, (f32)U.H, color_alpha(t->dark ? RGBA(38, 38, 39, 236) : RGBA(243, 243, 243, 236), t->veil), 0);
    r_rect(0, U.list_y0, (f32)U.W, U.list_y1 - U.list_y0, t->dark ? RGBA(255, 255, 255, 6) : RGBA(255, 255, 255, 60), 0);
    f32 fx0, fx1, cy;
    search_field(&fx0, &fx1, &cy);
    f32 bx = SR(32), bh = SR(34), by = floorf(cy - bh * 0.5f), bw = (f32)U.W - bx * 2;
    r_rect(bx, by, bw, bh, t->dark ? RGBA(0, 0, 0, 56) : RGBA(255, 255, 255, 200), bh * 0.5f);
    r_border(bx, by, bw, bh, t->dark ? RGBA(255, 255, 255, 22) : RGBA(0, 0, 0, 26), bh * 0.5f);
    draw_search();
}

static void st_draw_highlight(f32 off)
{
    Row *r = &U.rows[U.sel];
    f32 in = r->cell == CELL_TILE ? SR(4) : 0;
    f32 x = floorf(U.hl_x + in + 0.5f), y = floorf(off + U.hl_y + in * 0.5f + 0.5f);
    f32 h = floorf(U.hl_h - in + 0.5f);
    r_rect(x, y, floorf(U.hl_w - in * 2 + 0.5f), h, TH.sel, SR(LS()->sel_r));
    if (st_search() && !r->cell) r_rect(x, floorf(y + h * 0.5f - SR(8)), SR(3), SR(16), TH.accent, SR(1.5f));
}

// The page's title; the first one carries the "All apps" / "Back" button.
static void st_draw_title(Row *r, f32 y)
{
    Theme *t = &TH;
    f32 x0 = st_grid_x(), tw = SR(ST_TILE_W), isz = (f32)ui_icon_px();
    f32 cy = y + SR(34), fs = S(14);
    // aligned with the icons of the first and the last column
    f32 lx = floorf(x0 + (tw - isz) * 0.5f), rx = floorf(x0 + tw * (ST_COLS - 1) + (tw + isz) * 0.5f);
    text_draw(FONT_TEXT_SEMIBOLD, fs, lx, text_baseline(FONT_TEXT_SEMIBOLD, fs, cy), r->text, -1, t->text);
    if (r != &U.rows[0]) return;
    bool back = U.st.all_view;
    const WCHAR *label = back ? TR("Назад", "Back") : TR("Все приложения", "All apps");
    f32 bfs = S(12.5f), lw = text_width(FONT_TEXT, bfs, label, -1);
    f32 bh = SR(28), bw = floorf(SR(12) + lw + SR(8) + SR(10) + SR(10)), bx = rx + SR(8) - bw, by = floorf(cy - bh * 0.5f);
    f32 hov = U.st.title_hover_t * (U.st.title_press ? 0.6f : 1.f);
    // darker than the page, a little lighter under the pointer
    r_rect(bx, by, bw, bh, t->dark ? RGBA(0, 0, 0, (int)(56 - 22 * hov)) : RGBA(0, 0, 0, (int)(14 - 5 * hov)), SR(7));
    f32 base = text_baseline(FONT_TEXT, bfs, cy);
    if (back) {
        text_draw_icon(0xE76B, S(10), bx + SR(12) + SR(5), cy, t->text);
        text_draw(FONT_TEXT, bfs, bx + SR(12) + SR(10) + SR(8), base, label, -1, t->text);
    } else {
        text_draw(FONT_TEXT, bfs, bx + SR(12), base, label, -1, t->text);
        text_draw_icon(0xE76C, S(10), bx + SR(12) + lw + SR(8) + SR(5), cy, t->text);
    }
    box_set(U.st.title_btn, bx, by, bw, bh);
}

// The name under a tile, centered: on two lines at a space when it does not fit, like Start; the rest is elided.
static void st_draw_label(Row *r, f32 base, const WCHAR *s, int len, u32 col)
{
    f32 fs = S(12), maxw = r->w - SR(12), lh = SR(16);
    int cut = 0;
    if (text_width(FONT_TEXT, fs, s, len) > maxw)
        for (int i = 1; i < len; i++)
            if (s[i] == ' ' && text_width(FONT_TEXT, fs, s, i) <= maxw) cut = i;
    if (cut) {
        f32 w1 = text_width(FONT_TEXT, fs, s, cut);
        text_draw(FONT_TEXT, fs, floorf(r->x + (r->w - w1) * 0.5f), base, s, cut, col);
        s += cut + 1;
        len -= cut + 1;
        base += lh;
    }
    f32 w = MIN(text_width(FONT_TEXT, fs, s, len), maxw);
    text_draw_fit(FONT_TEXT, fs, floorf(r->x + (r->w - w) * 0.5f), base, maxw, s, len, col, false);
}

static void st_draw_cell(Row *r, f32 y, bool selected)
{
    (void)selected;
    if (r->cell == CELL_TITLE) {
        st_draw_title(r, y);
        return;
    }
    if (r->cell == CELL_HINT) {
        f32 fs = S(13), lx = floorf(st_grid_x() + (SR(ST_TILE_W) - (f32)ui_icon_px()) * 0.5f);
        text_draw(FONT_TEXT, fs, lx, text_baseline(FONT_TEXT, fs, y + SR(12)), r->text, -1, TH.dim);
        return;
    }
    Theme *t = &TH;
    App *a = r->app;
    f32 isz = (f32)ui_icon_px(), ix = floorf(r->x + (r->w - isz) * 0.5f), iy = y + SR(16);
    bool armed = U.armed == a;
    if (a->kind == APP_CMD) {
        r_rect(ix, iy, isz, isz, armed ? color_alpha(t->danger, 0.16f) : t->tile, SR(LS()->tile_r));
        text_draw_icon(a->glyph, S(16), ix + isz * 0.5f, iy + isz * 0.5f, armed ? t->danger : t->text);
    } else {
        draw_app_icon(a, ix, iy, isz);
    }
    st_draw_label(r, text_baseline(FONT_TEXT, S(12), iy + isz + SR(17)), a->name, a->len, armed ? t->danger : t->text);
}

static void st_draw_avatar(f32 x, f32 y, f32 sz)
{
    f32 uv[4];
    if (STU.pic[0] && sz == (f32)ui_icon_px() && icon_get(STU.pic_key, ICON_KIND_AVATAR, STU.pic, uv)) {
        r_icon(x, y, sz, sz, uv[0], uv[1], uv[2], uv[3], 1.f);
        return;
    }
    // the default Windows picture: a grey person in a light circle
    u32 bg = RGBA(222, 222, 222, 255), fg = RGBA(140, 140, 140, 255);
    r_rect(x, y, sz, sz, bg, sz * 0.5f);
    f32 hd = floorf(sz * 0.34f + 0.5f);
    r_rect(floorf(x + (sz - hd) * 0.5f + 0.5f), floorf(y + sz * 0.2f + 0.5f), hd, hd, fg, hd * 0.5f);
    f32 bw = floorf(sz * 0.54f + 0.5f), bh = floorf(sz * 0.3f + 0.5f);
    r_rect(floorf(x + (sz - bw) * 0.5f + 0.5f), floorf(y + sz * 0.6f + 0.5f), bw, bh, fg, bw * 0.45f);
}

static void st_draw_footer(void)
{
    Theme *t = &TH;
    st_user_init();
    f32 fy = U.list_y1, cy = floorf(fy + U.footer_h * 0.5f);
    r_rect(0, fy, (f32)U.W, (f32)U.H - fy, t->dark ? RGBA(0, 0, 0, 26) : RGBA(0, 0, 0, 8), 0);
    r_rect(0, fy, (f32)U.W, 1, t->sep, 0);
    memset(U.settings_btn, 0, sizeof U.settings_btn);

    f32 x0 = st_grid_x(), tw = SR(ST_TILE_W), av = (f32)ui_icon_px();
    f32 ax = floorf(x0 + (tw - av) * 0.5f), nfs = S(13);
    f32 nw = STU.name[0] ? MIN(text_width(FONT_TEXT, nfs, STU.name, -1), SR(200)) : 0;
    f32 bh = SR(44), by = floorf(cy - bh * 0.5f);
    box_set(U.st.fb[FB_ACCOUNT], ax - SR(10), by, av + SR(20) + (nw > 0 ? SR(12) + nw : 0), bh);
    // the buttons on the right, power last, under the last column
    static const u32 glyph[FB__COUNT] = { 0, 0xE896, 0xEC50, 0xE713, 0xE7E8 };
    f32 bs = SR(38), pitch = SR(40), pcx = floorf(x0 + tw * (ST_COLS - 0.5f) + SR(8));
    for (int i = FB_DOWNLOADS; i < FB__COUNT; i++) box_set(U.st.fb[i], floorf(pcx - pitch * (f32)(FB_POWER - i) - bs * 0.5f), floorf(cy - bs * 0.5f), bs, bs);

    for (int i = 0; i < FB__COUNT; i++) {
        const f32 *b = U.st.fb[i];
        f32 h = U.st.fb_hover_t[i] * (U.st.fb_press == i ? 0.6f : 1.f);
        if (h > 0.f) r_rect(b[0], b[1], b[2] - b[0], b[3] - b[1], color_alpha(t->sel, h * 1.4f), SR(4));
        if (glyph[i]) text_draw_icon(glyph[i], S(16), (b[0] + b[2]) * 0.5f, cy, t->text);
    }
    st_draw_avatar(ax, floorf(cy - av * 0.5f), av);
    if (nw > 0) text_draw_fit(FONT_TEXT, nfs, ax + av + SR(12), text_baseline(FONT_TEXT, nfs, cy), nw, STU.name, -1, t->text, false);

    memset(U.update_btn, 0, sizeof U.update_btn);
    if (upd_visible()) {
        // centered in the window, unless a long account name or the buttons on the right are in the way
        f32 uw = update_chip_width(), lo = U.st.fb[FB_ACCOUNT][2] + SR(8), hi = U.st.fb[FB_DOWNLOADS][0] - SR(8) - uw;
        draw_update_chip(floorf(CLAMP(((f32)U.W - uw) * 0.5f, lo, MAX(lo, hi))), cy, SR(28), SR(4));
    }
}
