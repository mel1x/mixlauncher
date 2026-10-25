// The Windows 10 style: the launcher laid out like the Windows 10 Start menu. A rail of buttons on the left,
// the apps list (most used, then all apps by letter) and the pinned apps as tiles on the right.
// Typing turns it into the search pane: the query on top, results on the left, the chosen one on the right.
// Pins are shared with the Windows 11 style (start.c); here each also has a tile size.

#define W10_MIN_W 300.f  // the rail and the apps column, no tiles
#define W10_H 608.f
#define W10_SEARCH_W 720.f  // the search pane keeps the default width, whatever Start was narrowed to
#define W10_RESULTS 390.f  // the results column of the search pane
#define W10_SEARCH_W_2K 900.f  // on a 2K or bigger monitor: wider, the room going to the results
#define W10_RESULTS_2K 550.f
#define W10_RAIL 48.f
#define W10_RAIL_OPEN 256.f
#define W10_COL 252.f    // the apps column
#define W10_UNIT 52.f    // a small tile and the gap after it; a medium tile is two units
#define W10_GAP 4.f

#define W10_TILES_Y 49.f // the first tile row, under the group title
#define W10_RAIL_ITEM 48.f

enum { RAIL_MENU, RAIL_ACCOUNT, RAIL_EXPLORER, RAIL_DOWNLOADS, RAIL_SETTINGS, RAIL_POWER, RAIL__COUNT };

static bool w10_on(void) { return g_cfg.style == STYLE_WIN10; }

// The list pane: the apps column, or the results column of the search pane.
static f32 list_x0(void) { return cap_on() ? SR(CAP_MARGIN) : w10_on() && !q_active() ? SR(W10_RAIL) : 0; }
static f32 list_x1(void) { return st_search() ? SR(ST_RESULTS) : cap_on() ? (f32)U.W - SR(CAP_MARGIN) : !w10_on() ? (f32)U.W : q_active() ? SR(U.w10.wide ? W10_RESULTS_2K : W10_RESULTS) : SR(W10_RAIL + W10_COL); }

static f32 w10_pane_x(void) { return SR(W10_RAIL + W10_COL + 24); }

// How many small-tile units fit across; the rest of the width stays empty. Small tiles use every unit,
// bigger ones stay on even columns.
static int w10_units(void)
{
    int n = (int)(((f32)U.W - w10_pane_x() - SR(16) + SR(W10_GAP)) / SR(W10_UNIT));
    return CLAMP(n, 0, 64);
}

static void w10_metrics(void)
{
    f32 search_w = U.w10.wide ? W10_SEARCH_W_2K : W10_SEARCH_W;
    U.W = (int)SR(q_active() ? search_w : (f32)CLAMP(g_cfg.start10_width, (int)W10_MIN_W, 4000));
    U.H = (int)SR((f32)CLAMP(g_cfg.start10_height, 420, 2000));
    U.list_y0 = SR(4);
    U.list_y1 = (f32)U.H;
}

// Typing or clearing the query: switch between the search pane's width and the one Start was resized to.
static void w10_fit_size(void)
{
    int w = U.W, h = U.H;
    w10_metrics();
    RECT wa = monitor_work(U.mon);
    U.W = MIN(U.W, (int)(wa.right - wa.left));
    U.H = MIN(U.H, (int)(wa.bottom - wa.top));
    U.list_y1 = (f32)U.H;
    if (U.W == w && U.H == h) return;
    SetWindowPos(g_hwnd, NULL, U.win_x, U.win_y + U.slide_y, U.W, U.H, SWP_NOZORDER | SWP_NOACTIVATE);
    r_resize(U.W, U.H);
    w10_refresh_tiles();
}

static void w10_place_rows(void)
{
    U.list_y0 = q_active() ? U.search_h + 1 : SR(4);
    f32 x = list_x0() + SR(4), w = list_x1() - x - SR(4);
    for (int i = 0; i < U.row_count; i++) {
        U.rows[i].x = x;
        U.rows[i].w = w;
    }
}

static void w10_build_home(void)
{
    if (!U.apps) {
        row_info(TR("Индексирую приложения…", "Indexing applications…"));
        return;
    }
    if (U.frequent_count) {
        row_header(TR("Часто используемые", "Most used"));
        for (int i = 0; i < MIN(U.frequent_count, 5); i++) row_app(U.frequent[i]);
    }
    st_all_apps_rows();
}

static const u8 k_tile_w[TILE__COUNT] = { 1, 2, 4, 4 }, k_tile_h[TILE__COUNT] = { 1, 2, 2, 4 };
static u8 w10_occ[256][64];

static bool w10_fits(int x, int y, int w, int h, int units)
{
    if (x < 0 || y < 0 || x + w > units || y + h > countof(w10_occ)) return false;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (w10_occ[y + j][x + i]) return false;
    return true;
}

// Lays the pinned apps out as wide as the window allows. Each tile keeps its own cell, the dragged one takes the
// cell under the pointer first; a tile without a cell, or whose cell is taken or out of the width, goes to the
// first free place.
static void w10_refresh_tiles(void)
{
    int units = w10_units();
    pins_load();
    U.w10.tile_count = 0;
    U.w10.tiles_h = 0;
    if (U.tsel >= 0) U.tsel = -1;
    if (!U.apps) return;
    memset(w10_occ, 0, sizeof w10_occ);
    f32 unit = SR(W10_UNIT), gap = SR(W10_GAP), px = w10_pane_x(), py = SR(W10_TILES_Y);
    int order[PINS_MAX], n = 0;
    for (int pass = 0; pass < 3; pass++)
        for (int k = 0; k < PIN.n; k++) {
            bool dragged = U.w10.tdrag && !wcscmp(PIN.p[k].id, U.w10.tdrag->id), placed = PIN.p[k].col >= 0;
            if (pass == 0 ? dragged : !dragged && (pass == 1) == placed) order[n++] = k;
        }
    for (int o = 0; o < n && U.w10.tile_count < countof(U.w10.tiles); o++) {
        int k = order[o];
        App *a = app_by_id(PIN.p[k].id);
        if (!a || app_hidden(a)) continue;
        int s = PIN.p[k].size, w = k_tile_w[s], h = k_tile_h[s], fx = -1, fy = -1;
        bool dragged = U.w10.tdrag && U.w10.tdrag == a;
        int cx = dragged ? U.w10.drag_cx : PIN.p[k].col, cy = dragged ? U.w10.drag_cy : PIN.p[k].row;
        if (w10_fits(cx, cy, w, h, units)) {
            fx = cx;
            fy = cy;
        }
        for (int y = 0; fx < 0 && y + h <= countof(w10_occ); y++)
            for (int x = 0; fx < 0 && x + w <= units; x++)
                if (w10_fits(x, y, w, h, units)) {
                    fx = x;
                    fy = y;
                }
        if (fx < 0) continue;
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) w10_occ[fy + j][fx + i] = 1;
        Row *r = &U.w10.tiles[U.w10.tile_count++];
        memset(r, 0, sizeof *r);
        r->kind = ROW_APP;
        r->cell = CELL_W10;
        r->tsize = (u8)s;
        r->app = a;
        r->x = px + unit * (f32)fx;
        r->y = py + unit * (f32)fy;
        r->w = unit * (f32)w - gap;
        r->h = unit * (f32)h - gap;
        U.w10.tiles_h = MAX(U.w10.tiles_h, r->y + r->h);
    }
}

// Remembers the cell every tile has now, so the layout stays as it is seen.
static void w10_pins_freeze(void)
{
    f32 unit = SR(W10_UNIT), px = w10_pane_x(), py = SR(W10_TILES_Y);
    for (int i = 0; i < U.w10.tile_count; i++) {
        Row *r = &U.w10.tiles[i];
        int k = pin_index(r->app->id);
        if (k < 0) continue;
        PIN.p[k].col = (i16)floorf((r->x - px) / unit + 0.5f);
        PIN.p[k].row = (i16)floorf((r->y - py) / unit + 0.5f);
    }
    pins_save();
}

// ---- hit testing

static f32 w10_max_tscroll(void) { return MAX(0.f, U.w10.tiles_h + SR(16) - U.list_y1); }

static int w10_tile_at(int x, int y)
{
    if (q_active() || U.w10.rail_open || (f32)y >= U.list_y1) return -1;
    f32 cy = (f32)y + U.w10.tscroll;
    for (int i = 0; i < U.w10.tile_count; i++) {
        Row *r = &U.w10.tiles[i];
        if ((f32)x >= r->x && (f32)x < r->x + r->w && cy >= r->y && cy < r->y + r->h) return i;
    }
    return -1;
}

static int w10_rail_at(int x, int y)
{
    for (int i = 0; i < RAIL__COUNT; i++)
        if (in_box(U.w10.rail[i], (f32)x, (f32)y)) return i;
    return -1;
}

// ---- actions

static void w10_set_rail(bool open)
{
    U.w10.rail_open = open;
    if (!anims_enabled()) U.w10.rail_t = open ? 1.f : 0.f;
    U.animating = true;
    ui_invalidate();
}

static void w10_rail_click(int i)
{
    if (i != RAIL_MENU) w10_set_rail(false);
    switch (i) {
    case RAIL_MENU: w10_set_rail(!U.w10.rail_open); break;
    case RAIL_ACCOUNT:
    case RAIL_POWER: st_menu_open(i == RAIL_ACCOUNT, (int)SR(W10_RAIL), (int)U.w10.rail[i][1]); break;
    case RAIL_EXPLORER: st_open(L"explorer.exe"); break;
    case RAIL_DOWNLOADS: st_open(L"shell:Downloads"); break;
    case RAIL_SETTINGS: st_open(L"ms-settings:"); break;
    }
}

// ---- the search pane's actions for the chosen result

static int w10_act_at(int x, int y)
{
    if (!q_active()) return -1;
    for (int i = 0; i < U.w10.act_n; i++)
        if (in_box(U.w10.act[i], (f32)x, (f32)y)) return i;
    return -1;
}

static void w10_act_run(int i)
{
    int cmd = U.w10.act_cmd[i];
    if (cmd == CM_COPY) ui_copy_path();
    else ui_activate(cmd == CM_RUNAS ? ACT_RUNAS : cmd == CM_REVEAL ? ACT_REVEAL : cmd == CM_PROPS ? ACT_PROPERTIES : ACT_OPEN);
}

// ---- resizing by the free edges: the right one, and the one away from the taskbar (top, usually),
// the width by whole columns of tiles, as Windows 10 Start does

enum { EDGE_SIDE = 1, EDGE_FREE = 2 };

static bool w10_bottom_anchored(void) { return U.slide_dy >= 0; }

static int w10_edge_at(int x, int y)
{
    if (U.w10.rail_open || U.cm.open || U.tg.open) return 0;
    int g = (int)SR(6), e = 0;
    if (x >= U.W - g && !q_active()) e |= EDGE_SIDE;  // the search pane has a fixed width
    if (w10_bottom_anchored() ? y < g : y >= U.H - g) e |= EDGE_FREE;
    return e;
}

static const WCHAR *w10_edge_cursor(void)
{
    int e = U.w10.drag;
    if (!e) {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(g_hwnd, &p);
        e = w10_edge_at(p.x, p.y);
    }
    if (e == (EDGE_SIDE | EDGE_FREE)) return w10_bottom_anchored() ? (const WCHAR *)IDC_SIZENESW : (const WCHAR *)IDC_SIZENWSE;
    return e == EDGE_SIDE ? (const WCHAR *)IDC_SIZEWE : e == EDGE_FREE ? (const WCHAR *)IDC_SIZENS : NULL;
}

static bool w10_drag_begin(int x, int y)
{
    int e = w10_edge_at(x, y);
    if (!e) return false;
    U.w10.drag = e;
    GetCursorPos(&U.w10.drag_pt);
    U.w10.drag_w = U.W;
    U.w10.drag_h = U.H;
    U.w10.drag_y = U.win_y;
    SetCapture(g_hwnd);
    return true;
}

static void w10_drag_move(void)
{
    POINT p;
    GetCursorPos(&p);
    RECT wa = monitor_work(U.mon);
    bool changed = false;
    if (U.w10.drag & EDGE_SIDE) {
        int w = CLAMP(U.w10.drag_w + p.x - U.w10.drag_pt.x, (int)SR(W10_MIN_W), MAX((int)SR(W10_MIN_W), (int)(wa.right - U.win_x)));
        int lw = (int)((f32)w / U.s + 0.5f);
        if (lw != g_cfg.start10_width) {
            g_cfg.start10_width = lw;
            changed = true;
        }
    }
    if (U.w10.drag & EDGE_FREE) {
        int dy = p.y - U.w10.drag_pt.y;
        int h = CLAMP(U.w10.drag_h + (w10_bottom_anchored() ? -dy : dy), (int)SR(420), (int)(wa.bottom - wa.top));
        int lh = (int)((f32)h / U.s + 0.5f);
        if (lh != g_cfg.start10_height) {
            g_cfg.start10_height = lh;
            changed = true;
        }
    }
    if (!changed) return;
    ui_metrics(U.dpi);
    U.H = MIN(U.H, (int)(wa.bottom - wa.top));
    U.list_y1 = (f32)U.H;
    if (w10_bottom_anchored()) {  // the bottom stays on the taskbar
        U.win_y = U.w10.drag_y + U.w10.drag_h - U.H;
        U.slide_dy = MAX(0, (int)wa.bottom - U.win_y);
    }
    SetWindowPos(g_hwnd, NULL, U.win_x, U.win_y + U.slide_y, U.W, U.H, SWP_NOZORDER | SWP_NOACTIVATE);
    r_resize(U.W, U.H);
    w10_refresh_tiles();
    rebuild_rows(true);
    snap_highlight();
    U.dirty = true;
    ui_render();
}

static void w10_drag_end(void)
{
    U.w10.drag = 0;
    ReleaseCapture();
    cfg_save(CF_START10_WIDTH);
    cfg_save(CF_START10_HEIGHT);
}

// ---- dragging a tile to a new place: the others make way as it passes over them

static Row *w10_tile_of(App *a)
{
    for (int i = 0; i < U.w10.tile_count; i++)
        if (U.w10.tiles[i].app == a) return &U.w10.tiles[i];
    return NULL;
}

// The tile goes to the cell nearest to where it is held; the tiles it covers step aside to free places.
static void w10_tile_drag(int x, int y)
{
    Row *r = NULL;
    if (!U.w10.tdrag) {
        if (abs(x - U.w10.press_pt.x) < (int)SR(5) && abs(y - U.w10.press_pt.y) < (int)SR(5)) return;
        w10_pins_freeze();  // the others stay put while it moves, unless it lands on them
        r = &U.w10.tiles[U.w10.press_tile];
        U.w10.tdrag = r->app;
        U.w10.drag_cx = U.w10.drag_cy = -1;
        U.w10.grab_x = (f32)U.w10.press_pt.x - r->x;
        U.w10.grab_y = (f32)U.w10.press_pt.y + U.w10.tscroll - r->y;
        SetCapture(g_hwnd);
    }
    r = w10_tile_of(U.w10.tdrag);
    int k = pin_index(U.w10.tdrag->id);
    if (!r || k < 0) return;
    f32 unit = SR(W10_UNIT);
    int w = k_tile_w[PIN.p[k].size], h = k_tile_h[PIN.p[k].size];
    int cx = (int)floorf(((f32)x - U.w10.grab_x - w10_pane_x()) / unit + 0.5f);
    int cy = (int)floorf(((f32)y - U.w10.grab_y + U.w10.tscroll - SR(W10_TILES_Y)) / unit + 0.5f);
    cx = CLAMP(cx, 0, MAX(0, w10_units() - w));
    cy = CLAMP(cy, 0, countof(w10_occ) - h);
    if (cx != U.w10.drag_cx || cy != U.w10.drag_cy) {
        U.w10.drag_cx = cx;
        U.w10.drag_cy = cy;
        w10_refresh_tiles();
    }
    U.tsel = -1;
    ui_invalidate();
}

static void w10_tile_drop(void)
{
    w10_pins_freeze();  // the dropped tile keeps its cell, so do the ones it moved
    U.w10.tdrag = NULL;
    ReleaseCapture();
    w10_refresh_tiles();
    U.tsel = w10_tile_at(U.mouse_x, U.mouse_y);  // it is under the pointer again
    ui_invalidate();
}

// ---- input

static bool w10_mouse_move(int x, int y)
{
    if (U.w10.press_tile >= 0 && !q_active()) {
        w10_tile_drag(x, y);
        if (U.w10.tdrag) return true;
    }
    if (q_active()) {
        int a = w10_act_at(x, y);
        if (a != U.w10.act_hover) {
            U.w10.act_hover = a;
            ui_invalidate();
        }
        return (f32)x >= list_x1();
    }
    int rh = w10_rail_at(x, y);
    if (rh != U.w10.rail_hover) {
        U.w10.rail_hover = rh;
        U.animating = true;
        ui_invalidate();
    }
    int t = w10_tile_at(x, y);
    if (t != U.tsel) {
        U.tsel = t;
        U.armed = NULL;
        ui_invalidate();
    }
    bool over_rail = (f32)x < SR(W10_RAIL) + (SR(W10_RAIL_OPEN) - SR(W10_RAIL)) * U.w10.rail_t;
    return rh >= 0 || over_rail || t >= 0 || (!q_active() && (f32)x >= list_x1());
}

static bool w10_mouse_down(int x, int y)
{
    if (q_active()) {
        U.w10.act_press = w10_act_at(x, y);
        return (f32)x >= list_x1() && (f32)y >= U.list_y0;
    }
    int rh = w10_rail_at(x, y);
    if (rh >= 0) {
        U.w10.rail_press = rh;
        return true;
    }
    if (U.w10.rail_open) {  // a click anywhere else folds the rail back
        w10_set_rail(false);
        return true;
    }
    int t = w10_tile_at(x, y);
    U.w10.press_tile = t;
    U.w10.press_pt.x = x;
    U.w10.press_pt.y = y;
    if (t >= 0) {
        U.tsel = t;
        ui_invalidate();
        return true;
    }
    return !q_active() && (f32)x >= list_x1() && (f32)y < U.list_y1;
}

static bool w10_mouse_up(int x, int y)
{
    int rp = U.w10.rail_press, tp = U.w10.press_tile, ap = U.w10.act_press;
    U.w10.rail_press = U.w10.press_tile = U.w10.act_press = -1;
    if (ap >= 0) {
        if (w10_act_at(x, y) == ap) w10_act_run(ap);
        return true;
    }
    if (rp >= 0) {
        if (w10_rail_at(x, y) == rp) w10_rail_click(rp);
        return true;
    }
    if (U.w10.tdrag) {
        w10_tile_drop();
        return true;
    }
    if (tp >= 0) {
        if (w10_tile_at(x, y) == tp) {
            U.tsel = tp;
            ui_activate(ACT_OPEN);
        }
        return true;
    }
    return false;
}

static bool w10_rclick(int x, int y)
{
    int t = w10_tile_at(x, y);
    if (t >= 0) {
        U.tsel = t;
        U.armed = NULL;
        ui_context_menu(x, y, false);
        return true;
    }
    return (f32)x < list_x0() || ((f32)x >= list_x1() && (f32)y >= U.list_y0);
}

static bool w10_wheel(int delta)
{
    if (q_active() || (f32)U.mouse_x < list_x1()) return false;
    U.w10.tscroll_target = CLAMP(U.w10.tscroll_target - (f32)delta / WHEEL_DELTA * SR(W10_UNIT) * 2.f, 0.f, w10_max_tscroll());
    U.animating = true;
    ui_invalidate();
    return true;
}

static bool w10_key(WPARAM vk)
{
    if (vk == VK_ESCAPE && U.w10.rail_open) {
        w10_set_rail(false);
        return true;
    }
    if (U.tsel < 0) return false;
    if (vk == VK_APPS) {
        Row *r = &U.w10.tiles[U.tsel];
        ui_context_menu((int)(r->x + SR(8)), (int)(r->y + r->h - U.w10.tscroll), true);
        return true;
    }
    // the keyboard walks the list; a tile is chosen with the pointer
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_TAB) {
        U.tsel = -1;
        ui_invalidate();
    }
    return false;
}

static bool w10_animate(f32 dt)
{
    bool anim = approach(&U.w10.rail_t, U.w10.rail_open ? 1.f : 0.f, 18.f, dt);
    for (int i = 0; i < RAIL__COUNT; i++) anim |= approach(&U.w10.rail_hover_t[i], U.w10.rail_hover == i ? 1.f : 0.f, 22.f, dt);
    U.w10.tscroll_target = CLAMP(U.w10.tscroll_target, 0.f, w10_max_tscroll());
    anim |= approach(&U.w10.tscroll, U.w10.tscroll_target, 16.f, dt);
    return anim;
}

// ---- drawing

// The Windows 10 Start colors: a dense, dark (or light grey) acrylic.
static u32 w10_bg(void) { return TH.dark ? RGBA(31, 31, 31, 255) : RGBA(228, 228, 228, 255); }

static void w10_draw_chrome(void)
{
    Theme *t = &TH;
    r_rect(0, 0, (f32)U.W, (f32)U.H, color_alpha(w10_bg(), 0.86f * t->veil), 0);
    f32 y0, y1;
    search_band(&y0, &y1);
    if (q_active()) {
        r_rect(0, y0, (f32)U.W, y1 - y0, t->dark ? RGBA(255, 255, 255, 8) : RGBA(255, 255, 255, 150), 0);
        r_rect(0, y1, (f32)U.W, 1, t->sep, 0);
        draw_search();
    } else {
        y0 = y1 = (f32)U.H - SR(30);  // the update chip sits in the tiles' corner
    }
    memset(U.settings_btn, 0, sizeof U.settings_btn);
    memset(U.update_btn, 0, sizeof U.update_btn);
    if (upd_visible()) draw_update_chip((f32)U.W - SR(12) - update_chip_width(), floorf((y0 + y1) * 0.5f), SR(28), 0);
}

static void w10_draw_tile(Row *r, int i)
{
    Theme *t = &TH;
    f32 x = r->x, y = floorf(r->y - U.w10.tscroll + 0.5f), w = r->w, h = r->h;
    if (y + h < 0 || y > U.list_y1) return;
    bool lifted = i < 0, hot = i == U.tsel || lifted, pressed = !lifted && i == U.w10.press_tile && !U.w10.tdrag;
    r_rect(x, y, w, h, t->dark ? RGBA(255, 255, 255, hot ? 30 : 16) : RGBA(0, 0, 0, hot ? 24 : 12), 0);
    if (hot) r_rect_ex(x, y, w, h, t->dark ? RGBA(255, 255, 255, pressed ? 70 : 120) : RGBA(0, 0, 0, 80), 0, floorf(S(1.5f) + 0.5f), 0);
    App *a = r->app;
    bool armed = U.armed == a, icon_only = r->tsize == TILE_SMALL;
    f32 isz = (f32)ui_icon_px(), ix = floorf(x + (w - isz) * 0.5f), iy = floorf(y + (h - isz) * 0.5f - (icon_only ? 0 : SR(8)));
    if (a->kind == APP_CMD) text_draw_icon(a->glyph, S(20), ix + isz * 0.5f, iy + isz * 0.5f, armed ? t->danger : t->text);
    else draw_app_icon(a, ix, iy, isz);
    if (!icon_only) text_draw_fit(FONT_TEXT, S(12), x + SR(8), floorf(y + h - SR(9)), w - SR(16), a->name, a->len, armed ? t->danger : t->text, false);
}

static void w10_draw_rail(void)
{
    Theme *t = &TH;
    f32 e = ease_out_cubic(U.w10.rail_t), rw = floorf(SR(W10_RAIL) + (SR(W10_RAIL_OPEN) - SR(W10_RAIL)) * e + 0.5f);
    f32 ih = SR(W10_RAIL_ITEM), y1 = U.list_y1;
    if (e > 0.f) {  // the open rail lies over the list
        r_rect_ex(rw - SR(2), 0, SR(18), y1, color_alpha(RGBA(0, 0, 0, t->dark ? 110 : 50), e), 0, 0, SR(14));
        r_rect(0, 0, rw, y1, t->dark ? RGBA(24, 24, 24, 255) : RGBA(238, 238, 238, 255), 0);
    }
    static const u32 glyph[RAIL__COUNT] = { 0xE700, 0, 0xEC50, 0xE896, 0xE713, 0xE7E8 };
    const WCHAR *label[RAIL__COUNT] = { TR("ПУСК", "START"), STU.name, TR("Проводник", "File Explorer"), TR("Загрузки", "Downloads"),
                                        TR("Параметры", "Settings"), TR("Выключение", "Power") };
    st_user_init();
    f32 fs = S(13.5f);
    for (int i = 0; i < RAIL__COUNT; i++) {
        f32 iy = i == RAIL_MENU ? SR(4) : y1 - ih * (f32)(RAIL__COUNT - i);
        box_set(U.w10.rail[i], 0, iy, rw, ih);
        f32 h = U.w10.rail_hover_t[i] * (U.w10.rail_press == i ? 0.6f : 1.f);
        if (h > 0.f) r_rect(0, iy, rw, ih, color_alpha(t->sel, h * 1.4f), 0);
        f32 cx = SR(W10_RAIL) * 0.5f, cy = iy + ih * 0.5f;
        if (i == RAIL_ACCOUNT) st_draw_avatar(floorf(cx - (f32)ui_icon_px() * 0.5f), floorf(cy - (f32)ui_icon_px() * 0.5f), (f32)ui_icon_px());
        else text_draw_icon(glyph[i], S(16), cx, cy, t->text);
        if (e > 0.f && label[i] && label[i][0]) {
            R.opacity = e;
            int font = i == RAIL_MENU ? FONT_TEXT_SEMIBOLD : FONT_TEXT;
            r_set_clip(0, 0, rw, y1);
            text_draw_fit(font, fs, SR(W10_RAIL) + SR(4), text_baseline(font, fs, cy), SR(W10_RAIL_OPEN) - SR(W10_RAIL) - SR(16), label[i], -1, t->text, false);
            r_clip_all();
            R.opacity = 1.f;
        }
    }
}

static void w10_add_act(int cmd)
{
    if (U.w10.act_n < countof(U.w10.act_cmd)) U.w10.act_cmd[U.w10.act_n++] = cmd;
}

// The search pane's right side: the chosen result, big, with what can be done with it.
static void w10_draw_preview(void)
{
    Theme *t = &TH;
    bool card = st_on();  // Windows 11: a rounded card beside the list, the name on top, the icon on a wide plate
    f32 x0 = list_x1(), y0 = U.list_y0, w = (f32)U.W - x0, rad = 0;
    if (card) {
        x0 += SR(4);
        y0 += SR(8);
        w = (f32)U.W - SR(16) - x0;
        rad = SR(8);
        r_rect(x0, y0, w, U.list_y1 - SR(12) - y0, t->dark ? RGBA(255, 255, 255, 10) : RGBA(255, 255, 255, 170), rad);
    } else {
        r_rect(x0, y0, w, (f32)U.H - y0, t->dark ? RGBA(255, 255, 255, 7) : RGBA(255, 255, 255, 110), 0);
    }
    U.w10.act_n = 0;
    Row *r = sel_row();
    if (!r) return;
    f32 cx = floorf(x0 + w * 0.5f), y = y0 + SR(36), box = SR(64), isz = (f32)ui_icon_px(), bw = box;
    if (card) {
        y = y0 + SR(20) + SR(46);  // under the name and the kind
        box = SR(112);
        bw = w - SR(20) * 2;
    }
    f32 bx = floorf(cx - bw * 0.5f);
    r_rect(bx, y, bw, box, t->dark ? RGBA(255, 255, 255, card ? 8 : 16) : RGBA(0, 0, 0, card ? 6 : 12), rad * 0.75f);
    if (card) r_border(bx, y, bw, box, t->dark ? RGBA(255, 255, 255, 14) : RGBA(0, 0, 0, 16), rad * 0.75f);
    f32 ix = floorf(cx - isz * 0.5f), iy = floorf(y + (box - isz) * 0.5f);
    const WCHAR *name, *kind, *where = NULL;
    if (r->kind == ROW_APP) {
        App *a = r->app;
        name = a->name;
        kind = a->kind == APP_CMD ? TR("Команда", "Command") : TR("Приложение", "App");
        w10_add_act(CM_OPEN);
        if (a->kind == APP_CMD) {
            text_draw_icon(a->glyph, S(22), cx, y + box * 0.5f, t->text);
        } else {
            draw_app_icon(a, ix, iy, isz);
            w10_add_act(CM_RUNAS);
            if (a->path) w10_add_act(CM_REVEAL);
            w10_add_act(CM_COPY);
        }
    } else {
        FileItem *f = r->file;
        name = f->name;
        kind = (f->flags & FI_FOLDER) ? TR("Папка", "Folder") : TR("Файл", "File");
        where = f->dir;
        draw_file_icon(f, ix, iy, isz);
        w10_add_act(CM_OPEN);
        w10_add_act(CM_REVEAL);
        w10_add_act(CM_COPY);
        w10_add_act(CM_PROPS);
    }
    f32 pad = card ? SR(20) : SR(24), tw = w - pad * 2, fs = S(18), sfs = S(12.5f);
    if (card) {
        f32 nfs = S(15), ty = y0 + pad + font_cap_height(FONT_TEXT_SEMIBOLD, nfs);
        text_draw_fit(FONT_TEXT_SEMIBOLD, nfs, x0 + pad, floorf(ty + 0.5f), tw, name, -1, t->text, false);
        ty += SR(21);
        if (where && where[0]) {
            f32 kw = text_width(FONT_TEXT, sfs, kind, -1);
            text_draw(FONT_TEXT, sfs, x0 + pad, floorf(ty + 0.5f), kind, -1, t->dim);
            text_draw_fit(FONT_TEXT, sfs, x0 + pad + kw + SR(10), floorf(ty + 0.5f), tw - kw - SR(10), where, -1, t->faint, true);
        } else {
            text_draw(FONT_TEXT, sfs, x0 + pad, floorf(ty + 0.5f), kind, -1, t->dim);
        }
        y += box + SR(12);
    } else {
        y += box + SR(30);
        f32 nw = MIN(text_width(FONT_DISPLAY, fs, name, -1), tw);
        text_draw_fit(FONT_DISPLAY, fs, floorf(cx - nw * 0.5f), y, tw, name, -1, t->text, false);
        y += SR(22);
        f32 kw = text_width(FONT_TEXT, sfs, kind, -1);
        text_draw(FONT_TEXT, sfs, floorf(cx - kw * 0.5f), y, kind, -1, t->dim);
        if (where && where[0]) {
            y += SR(18);
            f32 ww = MIN(text_width(FONT_TEXT, sfs, where, -1), tw);
            text_draw_fit(FONT_TEXT, sfs, floorf(cx - ww * 0.5f), y, tw, where, -1, t->faint, true);
        }
        y += SR(20);
        r_rect(x0 + pad, y, tw, 1, t->sep, 0);
        y += SR(8);
    }
    static const struct { int cmd; u32 glyph; } k_acts[] = { { CM_OPEN, 0xE8A7 }, { CM_RUNAS, 0xE7EF }, { CM_REVEAL, 0xE838 }, { CM_COPY, 0xE8C8 }, { CM_PROPS, 0xE946 } };
    f32 ah = SR(36), afs = S(13.5f);
    for (int i = 0; i < U.w10.act_n; i++) {
        int cmd = U.w10.act_cmd[i];
        const WCHAR *label = cmd == CM_OPEN ? (r->kind == ROW_APP && r->app->kind == APP_CMD ? TR("Выполнить", "Run") : TR("Открыть", "Open"))
                             : cmd == CM_RUNAS ? TR("Запуск от имени администратора", "Run as administrator")
                             : cmd == CM_REVEAL ? TR("Перейти к расположению файла", "Open file location")
                             : cmd == CM_COPY ? (row_path(r) ? TR("Копировать путь", "Copy path") : TR("Копировать имя", "Copy name"))
                                              : TR("Свойства", "Properties");
        u32 glyph = 0;
        for (int k = 0; k < countof(k_acts); k++)
            if (k_acts[k].cmd == cmd) glyph = k_acts[k].glyph;
        f32 ax = x0 + pad - SR(8), aw = tw + SR(16);
        box_set(U.w10.act[i], ax, y, aw, ah);
        if (U.w10.act_hover == i) r_rect(ax, y, aw, ah, U.w10.act_press == i ? color_alpha(t->sel, 0.7f) : t->sel, card ? SR(4) : 0);
        f32 acy = y + ah * 0.5f;
        text_draw_icon(glyph, S(14), ax + SR(18), acy, t->text);
        text_draw_fit(FONT_TEXT, afs, ax + SR(40), text_baseline(FONT_TEXT, afs, acy), aw - SR(48), label, -1, t->text, false);
        y += ah;
    }
}

// The tiles and the rail over the list, or the search pane's right side.
static void w10_draw_overlay(void)
{
    Theme *t = &TH;
    if (q_active()) {
        w10_draw_preview();
        memset(U.w10.rail, 0, sizeof U.w10.rail);  // no rail while searching
        return;
    }
    if (w10_units() > 0) {  // none when narrowed down to the list
        f32 px = w10_pane_x();
        r_set_clip(list_x1(), 0, (f32)U.W, U.list_y1);
        f32 fs = S(13), ty = floorf(SR(32) - U.w10.tscroll + 0.5f);
        if (U.w10.tile_count) {
            text_draw(FONT_TEXT, fs, px, ty, TR("Закреплено", "Pinned"), -1, t->text);
            for (int i = 0; i < U.w10.tile_count; i++)
                if (U.w10.tiles[i].app != U.w10.tdrag) w10_draw_tile(&U.w10.tiles[i], i);
            Row *d = U.w10.tdrag ? w10_tile_of(U.w10.tdrag) : NULL;
            if (d) {  // where it will land, and the lifted tile under the pointer, a little see-through
                r_border(d->x, floorf(d->y - U.w10.tscroll + 0.5f), d->w, d->h, t->dark ? RGBA(255, 255, 255, 60) : RGBA(0, 0, 0, 50), 0);
                Row lift = *d;
                lift.x = floorf((f32)U.mouse_x - U.w10.grab_x + 0.5f);
                lift.y = (f32)U.mouse_y - U.w10.grab_y + U.w10.tscroll;
                r_set_clip(0, 0, (f32)U.W, U.list_y1);
                R.opacity = 0.85f;
                w10_draw_tile(&lift, -1);
                R.opacity = 1.f;
            }
        } else {
            text_draw(FONT_TEXT, fs, px, ty, TR("Закрепляйте приложения через правый клик -", "Pin apps with a right click -"), -1, t->dim);
            text_draw(FONT_TEXT, fs, px, ty + SR(20), TR("здесь они станут плитками", "they become tiles here"), -1, t->dim);
        }
        r_clip_all();
    }
    w10_draw_rail();
}
