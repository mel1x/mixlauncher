// The Windows 10 style: the launcher laid out like the Windows 10 Start menu. A rail of buttons on the left,
// the apps list (most used, then all apps by letter) and the pinned apps as tiles on the right.
// Typing turns it into the search pane: the query on top, results on the left, the chosen one on the right.
// Pins are shared with the Windows 11 style (start.rs); here each also has a tile size.

use crate::*;

const W10_MIN_W: f32 = 300.0; // the rail and the apps column, no tiles
const W10_H: f32 = 608.0;
const W10_SEARCH_W: f32 = 720.0; // the search pane keeps the default width, whatever Start was narrowed to
const W10_RESULTS: f32 = 390.0; // the results column of the search pane
const W10_SEARCH_W_2K: f32 = 900.0; // on a 2K or bigger monitor: wider, the room going to the results
const W10_RESULTS_2K: f32 = 550.0;
const W10_RAIL: f32 = 48.0;
const W10_RAIL_OPEN: f32 = 256.0;
const W10_COL: f32 = 252.0; // the apps column
const W10_UNIT: f32 = 52.0; // a small tile and the gap after it; a medium tile is two units
const W10_GAP: f32 = 4.0;

const W10_TILES_Y: f32 = 49.0; // the first tile row, under the group title
const W10_RAIL_ITEM: f32 = 48.0;

const RAIL_MENU: i32 = 0;
const RAIL_ACCOUNT: i32 = 1;
const RAIL_EXPLORER: i32 = 2;
const RAIL_DOWNLOADS: i32 = 3;
const RAIL_SETTINGS: i32 = 4;
const RAIL_POWER: i32 = 5;
const RAIL__COUNT: i32 = 6;

pub unsafe fn w10_on() -> bool {
    g_cfg.style == STYLE_WIN10
}

// The list pane: the apps column, or the results column of the search pane.
pub unsafe fn list_x0() -> f32 {
    if cap_on() {
        SR(CAP_MARGIN)
    } else if w10_on() && !q_active() {
        SR(W10_RAIL)
    } else {
        0.0
    }
}
pub unsafe fn list_x1() -> f32 {
    if st_search() {
        SR(ST_RESULTS)
    } else if cap_on() {
        U.W as f32 - SR(CAP_MARGIN)
    } else if !w10_on() {
        U.W as f32
    } else if q_active() {
        SR(if U.w10.wide { W10_RESULTS_2K } else { W10_RESULTS })
    } else {
        SR(W10_RAIL + W10_COL)
    }
}

unsafe fn w10_pane_x() -> f32 {
    SR(W10_RAIL + W10_COL + 24.0)
}

// How many small-tile units fit across; the rest of the width stays empty. Small tiles use every unit,
// bigger ones stay on even columns.
unsafe fn w10_units() -> i32 {
    let n = ((U.W as f32 - w10_pane_x() - SR(16.0) + SR(W10_GAP)) / SR(W10_UNIT)) as i32;
    clamp(n, 0, 64)
}

pub unsafe fn w10_metrics() {
    let search_w = if U.w10.wide { W10_SEARCH_W_2K } else { W10_SEARCH_W };
    U.W = SR(if q_active() { search_w } else { clamp(g_cfg.start10_width, W10_MIN_W as i32, 4000) as f32 }) as i32;
    U.H = SR(clamp(g_cfg.start10_height, 420, 2000) as f32) as i32;
    U.list_y0 = SR(4.0);
    U.list_y1 = U.H as f32;
}

// Typing or clearing the query: switch between the search pane's width and the one Start was resized to.
pub unsafe fn w10_fit_size() {
    let w = U.W;
    let h = U.H;
    w10_metrics();
    let wa = monitor_work(U.mon);
    U.W = min(U.W, wa.right - wa.left);
    U.H = min(U.H, wa.bottom - wa.top);
    U.list_y1 = U.H as f32;
    if U.W == w && U.H == h {
        return;
    }
    SetWindowPos(g_hwnd, null_mut(), U.win_x, U.win_y + U.slide_y, U.W, U.H, SWP_NOZORDER | SWP_NOACTIVATE);
    r_resize(U.W, U.H);
    w10_refresh_tiles();
}

pub unsafe fn w10_place_rows() {
    U.list_y0 = if q_active() { U.search_h + 1.0 } else { SR(4.0) };
    let x = list_x0() + SR(4.0);
    let w = list_x1() - x - SR(4.0);
    for i in 0..U.row_count {
        row(i).x = x;
        row(i).w = w;
    }
}

pub unsafe fn w10_build_home() {
    if U.apps.is_null() {
        row_info(tr!("Индексирую приложения…", "Indexing applications…"));
        return;
    }
    if U.frequent_count != 0 {
        row_header(tr!("Часто используемые", "Most used"));
        for i in 0..min(U.frequent_count, 5) as usize {
            row_app(U.frequent[i]);
        }
    }
    st_all_apps_rows_pub();
}

const k_tile_w: [u8; 4] = [1, 2, 4, 4];
const k_tile_h: [u8; 4] = [1, 2, 2, 4];
static mut w10_occ: [[u8; 64]; 256] = [[0; 64]; 256];

unsafe fn w10_fits(x: i32, y: i32, w: i32, h: i32, units: i32) -> bool {
    if x < 0 || y < 0 || x + w > units || y + h > 256 {
        return false;
    }
    let occ = &*(&raw const w10_occ);
    for j in 0..h {
        for i in 0..w {
            if occ[(y + j) as usize][(x + i) as usize] != 0 {
                return false;
            }
        }
    }
    true
}

// Lays the pinned apps out as wide as the window allows. Each tile keeps its own cell, the dragged one takes the
// cell under the pointer first; a tile without a cell, or whose cell is taken or out of the width, goes to the
// first free place.
pub unsafe fn w10_refresh_tiles() {
    let units = w10_units();
    pins_load();
    U.w10.tile_count = 0;
    U.w10.tiles_h = 0.0;
    if U.tsel >= 0 {
        U.tsel = -1;
    }
    if U.apps.is_null() {
        return;
    }
    let occ = &mut *(&raw mut w10_occ);
    for row_ in occ.iter_mut() {
        row_.fill(0);
    }
    let unit = SR(W10_UNIT);
    let gap = SR(W10_GAP);
    let px = w10_pane_x();
    let py = SR(W10_TILES_Y);
    let mut order = [0i32; PINS_MAX];
    let mut n = 0usize;
    for pass in 0..3 {
        for k in 0..PIN.n as usize {
            let dragged = !U.w10.tdrag.is_null() && wcscmp(PIN.p[k].id, (*U.w10.tdrag).id) == 0;
            let placed = PIN.p[k].col >= 0;
            if if pass == 0 { dragged } else { !dragged && (pass == 1) == placed } {
                order[n] = k as i32;
                n += 1;
            }
        }
    }
    let mut o = 0;
    while o < n && U.w10.tile_count < 64 {
        let k = order[o] as usize;
        o += 1;
        let a = app_by_id(PIN.p[k].id);
        if a.is_null() || app_hidden(a) {
            continue;
        }
        let s = PIN.p[k].size as usize;
        let w = k_tile_w[s] as i32;
        let h = k_tile_h[s] as i32;
        let (mut fx, mut fy) = (-1, -1);
        let dragged = !U.w10.tdrag.is_null() && U.w10.tdrag == a;
        let cx = if dragged { U.w10.drag_cx } else { PIN.p[k].col as i32 };
        let cy = if dragged { U.w10.drag_cy } else { PIN.p[k].row as i32 };
        if w10_fits(cx, cy, w, h, units) {
            fx = cx;
            fy = cy;
        }
        let mut y = 0;
        while fx < 0 && y + h <= 256 {
            let mut x = 0;
            while fx < 0 && x + w <= units {
                if w10_fits(x, y, w, h, units) {
                    fx = x;
                    fy = y;
                }
                x += 1;
            }
            y += 1;
        }
        if fx < 0 {
            continue;
        }
        for j in 0..h {
            for i in 0..w {
                occ[(fy + j) as usize][(fx + i) as usize] = 1;
            }
        }
        let r = &mut U.w10.tiles[U.w10.tile_count as usize];
        U.w10.tile_count += 1;
        *r = zeroed();
        r.kind = ROW_APP;
        r.cell = CELL_W10;
        r.tsize = s as u8;
        r.app = a;
        r.x = px + unit * fx as f32;
        r.y = py + unit * fy as f32;
        r.w = unit * w as f32 - gap;
        r.h = unit * h as f32 - gap;
        U.w10.tiles_h = max(U.w10.tiles_h, r.y + r.h);
    }
}

// Remembers the cell every tile has now, so the layout stays as it is seen.
pub unsafe fn w10_pins_freeze() {
    let unit = SR(W10_UNIT);
    let px = w10_pane_x();
    let py = SR(W10_TILES_Y);
    for i in 0..U.w10.tile_count as usize {
        let r = U.w10.tiles[i];
        let k = pin_index((*r.app).id);
        if k < 0 {
            continue;
        }
        PIN.p[k as usize].col = ((r.x - px) / unit + 0.5).floor() as i16;
        PIN.p[k as usize].row = ((r.y - py) / unit + 0.5).floor() as i16;
    }
    pins_save();
}

// ---- hit testing

unsafe fn w10_max_tscroll() -> f32 {
    max(0.0, U.w10.tiles_h + SR(16.0) - U.list_y1)
}

unsafe fn w10_tile_at(x: i32, y: i32) -> i32 {
    if q_active() || U.w10.rail_open || y as f32 >= U.list_y1 {
        return -1;
    }
    let cy = y as f32 + U.w10.tscroll;
    for i in 0..U.w10.tile_count as usize {
        let r = &U.w10.tiles[i];
        if x as f32 >= r.x && (x as f32) < r.x + r.w && cy >= r.y && cy < r.y + r.h {
            return i as i32;
        }
    }
    -1
}

unsafe fn w10_rail_at(x: i32, y: i32) -> i32 {
    for i in 0..RAIL__COUNT {
        if in_box(&U.w10.rail[i as usize], x as f32, y as f32) {
            return i;
        }
    }
    -1
}

// ---- actions

unsafe fn w10_set_rail(open: bool) {
    U.w10.rail_open = open;
    if !anims_enabled() {
        U.w10.rail_t = if open { 1.0 } else { 0.0 };
    }
    U.animating = true;
    ui_invalidate();
}

unsafe fn w10_rail_click(i: i32) {
    if i != RAIL_MENU {
        w10_set_rail(false);
    }
    match i {
        RAIL_MENU => w10_set_rail(!U.w10.rail_open),
        RAIL_ACCOUNT | RAIL_POWER => st_menu_open(i == RAIL_ACCOUNT, SR(W10_RAIL) as i32, U.w10.rail[i as usize][1] as i32),
        RAIL_EXPLORER => st_open(w!("explorer.exe")),
        RAIL_DOWNLOADS => st_open(w!("shell:Downloads")),
        RAIL_SETTINGS => st_open(w!("ms-settings:")),
        _ => {}
    }
}

// ---- the search pane's actions for the chosen result

pub unsafe fn w10_act_at(x: i32, y: i32) -> i32 {
    if !q_active() {
        return -1;
    }
    for i in 0..U.w10.act_n {
        if in_box(&U.w10.act[i as usize], x as f32, y as f32) {
            return i;
        }
    }
    -1
}

pub unsafe fn w10_act_run(i: i32) {
    let cmd = U.w10.act_cmd[i as usize];
    if cmd == CM_COPY {
        ui_copy_path();
    } else {
        ui_activate(if cmd == CM_RUNAS {
            ACT_RUNAS
        } else if cmd == CM_REVEAL {
            ACT_REVEAL
        } else if cmd == CM_PROPS {
            ACT_PROPERTIES
        } else {
            ACT_OPEN
        });
    }
}

// ---- resizing by the free edges: the right one, and the one away from the taskbar (top, usually),
// the width by whole columns of tiles, as Windows 10 Start does

const EDGE_SIDE: i32 = 1;
const EDGE_FREE: i32 = 2;

unsafe fn w10_bottom_anchored() -> bool {
    U.slide_dy >= 0
}

unsafe fn w10_edge_at(x: i32, y: i32) -> i32 {
    if U.w10.rail_open || U.cm.open || U.tg.open {
        return 0;
    }
    let g = SR(6.0) as i32;
    let mut e = 0;
    if x >= U.W - g && !q_active() {
        e |= EDGE_SIDE; // the search pane has a fixed width
    }
    if if w10_bottom_anchored() { y < g } else { y >= U.H - g } {
        e |= EDGE_FREE;
    }
    e
}

pub unsafe fn w10_edge_cursor() -> PCWSTR {
    let mut e = U.w10.drag;
    if e == 0 {
        let mut p = POINT { x: 0, y: 0 };
        GetCursorPos(&mut p);
        ScreenToClient(g_hwnd, &mut p);
        e = w10_edge_at(p.x, p.y);
    }
    if e == (EDGE_SIDE | EDGE_FREE) {
        return if w10_bottom_anchored() { IDC_SIZENESW } else { IDC_SIZENWSE };
    }
    if e == EDGE_SIDE {
        IDC_SIZEWE
    } else if e == EDGE_FREE {
        IDC_SIZENS
    } else {
        null()
    }
}

pub unsafe fn w10_drag_begin(x: i32, y: i32) -> bool {
    let e = w10_edge_at(x, y);
    if e == 0 {
        return false;
    }
    U.w10.drag = e;
    GetCursorPos(&raw mut U.w10.drag_pt);
    U.w10.drag_w = U.W;
    U.w10.drag_h = U.H;
    U.w10.drag_y = U.win_y;
    SetCapture(g_hwnd);
    true
}

pub unsafe fn w10_drag_move() {
    let mut p = POINT { x: 0, y: 0 };
    GetCursorPos(&mut p);
    let wa = monitor_work(U.mon);
    let mut changed = false;
    if (U.w10.drag & EDGE_SIDE) != 0 {
        let w = clamp(U.w10.drag_w + p.x - U.w10.drag_pt.x, SR(W10_MIN_W) as i32, max(SR(W10_MIN_W) as i32, wa.right - U.win_x));
        let lw = (w as f32 / U.s + 0.5) as i32;
        if lw != g_cfg.start10_width {
            g_cfg.start10_width = lw;
            changed = true;
        }
    }
    if (U.w10.drag & EDGE_FREE) != 0 {
        let dy = p.y - U.w10.drag_pt.y;
        let h = clamp(U.w10.drag_h + if w10_bottom_anchored() { -dy } else { dy }, SR(420.0) as i32, wa.bottom - wa.top);
        let lh = (h as f32 / U.s + 0.5) as i32;
        if lh != g_cfg.start10_height {
            g_cfg.start10_height = lh;
            changed = true;
        }
    }
    if !changed {
        return;
    }
    ui_metrics(U.dpi);
    U.H = min(U.H, wa.bottom - wa.top);
    U.list_y1 = U.H as f32;
    if w10_bottom_anchored() {
        // the bottom stays on the taskbar
        U.win_y = U.w10.drag_y + U.w10.drag_h - U.H;
        U.slide_dy = max(0, wa.bottom - U.win_y);
    }
    SetWindowPos(g_hwnd, null_mut(), U.win_x, U.win_y + U.slide_y, U.W, U.H, SWP_NOZORDER | SWP_NOACTIVATE);
    r_resize(U.W, U.H);
    w10_refresh_tiles();
    rebuild_rows(true);
    snap_highlight();
    U.dirty = true;
    ui_render();
}

pub unsafe fn w10_drag_end() {
    U.w10.drag = 0;
    ReleaseCapture();
    cfg_save(CF_START10_WIDTH);
    cfg_save(CF_START10_HEIGHT);
}

// ---- dragging a tile to a new place: the others make way as it passes over them

unsafe fn w10_tile_of(a: *mut App) -> *mut Row {
    for i in 0..U.w10.tile_count as usize {
        if U.w10.tiles[i].app == a {
            return &raw mut U.w10.tiles[i];
        }
    }
    null_mut()
}

// The tile goes to the cell nearest to where it is held; the tiles it covers step aside to free places.
unsafe fn w10_tile_drag(x: i32, y: i32) {
    if U.w10.tdrag.is_null() {
        if (x - U.w10.press_pt.x).abs() < SR(5.0) as i32 && (y - U.w10.press_pt.y).abs() < SR(5.0) as i32 {
            return;
        }
        w10_pins_freeze(); // the others stay put while it moves, unless it lands on them
        let r = U.w10.tiles[U.w10.press_tile as usize];
        U.w10.tdrag = r.app;
        U.w10.drag_cx = -1;
        U.w10.drag_cy = -1;
        U.w10.grab_x = U.w10.press_pt.x as f32 - r.x;
        U.w10.grab_y = U.w10.press_pt.y as f32 + U.w10.tscroll - r.y;
        SetCapture(g_hwnd);
    }
    let r = w10_tile_of(U.w10.tdrag);
    let k = pin_index((*U.w10.tdrag).id);
    if r.is_null() || k < 0 {
        return;
    }
    let unit = SR(W10_UNIT);
    let sz = PIN.p[k as usize].size as usize;
    let w = k_tile_w[sz] as i32;
    let h = k_tile_h[sz] as i32;
    let mut cx = ((x as f32 - U.w10.grab_x - w10_pane_x()) / unit + 0.5).floor() as i32;
    let mut cy = ((y as f32 - U.w10.grab_y + U.w10.tscroll - SR(W10_TILES_Y)) / unit + 0.5).floor() as i32;
    cx = clamp(cx, 0, max(0, w10_units() - w));
    cy = clamp(cy, 0, 256 - h);
    if cx != U.w10.drag_cx || cy != U.w10.drag_cy {
        U.w10.drag_cx = cx;
        U.w10.drag_cy = cy;
        w10_refresh_tiles();
    }
    U.tsel = -1;
    ui_invalidate();
}

unsafe fn w10_tile_drop() {
    w10_pins_freeze(); // the dropped tile keeps its cell, so do the ones it moved
    U.w10.tdrag = null_mut();
    ReleaseCapture();
    w10_refresh_tiles();
    U.tsel = w10_tile_at(U.mouse_x, U.mouse_y); // it is under the pointer again
    ui_invalidate();
}

// ---- input

pub unsafe fn w10_mouse_move(x: i32, y: i32) -> bool {
    if U.w10.press_tile >= 0 && !q_active() {
        w10_tile_drag(x, y);
        if !U.w10.tdrag.is_null() {
            return true;
        }
    }
    if q_active() {
        let a = w10_act_at(x, y);
        if a != U.w10.act_hover {
            U.w10.act_hover = a;
            ui_invalidate();
        }
        return x as f32 >= list_x1();
    }
    let rh = w10_rail_at(x, y);
    if rh != U.w10.rail_hover {
        U.w10.rail_hover = rh;
        U.animating = true;
        ui_invalidate();
    }
    let t = w10_tile_at(x, y);
    if t != U.tsel {
        U.tsel = t;
        U.armed = null_mut();
        ui_invalidate();
    }
    let over_rail = (x as f32) < SR(W10_RAIL) + (SR(W10_RAIL_OPEN) - SR(W10_RAIL)) * U.w10.rail_t;
    rh >= 0 || over_rail || t >= 0 || (!q_active() && x as f32 >= list_x1())
}

pub unsafe fn w10_mouse_down(x: i32, y: i32) -> bool {
    if q_active() {
        U.w10.act_press = w10_act_at(x, y);
        return x as f32 >= list_x1() && y as f32 >= U.list_y0;
    }
    let rh = w10_rail_at(x, y);
    if rh >= 0 {
        U.w10.rail_press = rh;
        return true;
    }
    if U.w10.rail_open {
        // a click anywhere else folds the rail back
        w10_set_rail(false);
        return true;
    }
    let t = w10_tile_at(x, y);
    U.w10.press_tile = t;
    U.w10.press_pt.x = x;
    U.w10.press_pt.y = y;
    if t >= 0 {
        U.tsel = t;
        ui_invalidate();
        return true;
    }
    !q_active() && x as f32 >= list_x1() && (y as f32) < U.list_y1
}

pub unsafe fn w10_mouse_up(x: i32, y: i32) -> bool {
    let rp = U.w10.rail_press;
    let tp = U.w10.press_tile;
    let ap = U.w10.act_press;
    U.w10.rail_press = -1;
    U.w10.press_tile = -1;
    U.w10.act_press = -1;
    if ap >= 0 {
        if w10_act_at(x, y) == ap {
            w10_act_run(ap);
        }
        return true;
    }
    if rp >= 0 {
        if w10_rail_at(x, y) == rp {
            w10_rail_click(rp);
        }
        return true;
    }
    if !U.w10.tdrag.is_null() {
        w10_tile_drop();
        return true;
    }
    if tp >= 0 {
        if w10_tile_at(x, y) == tp {
            U.tsel = tp;
            ui_activate(ACT_OPEN);
        }
        return true;
    }
    false
}

pub unsafe fn w10_rclick(x: i32, y: i32) -> bool {
    let t = w10_tile_at(x, y);
    if t >= 0 {
        U.tsel = t;
        U.armed = null_mut();
        ui_context_menu(x, y, false);
        return true;
    }
    (x as f32) < list_x0() || (x as f32 >= list_x1() && y as f32 >= U.list_y0)
}

pub unsafe fn w10_wheel(delta: i32) -> bool {
    if q_active() || (U.mouse_x as f32) < list_x1() {
        return false;
    }
    U.w10.tscroll_target = clamp(U.w10.tscroll_target - delta as f32 / WHEEL_DELTA as f32 * SR(W10_UNIT) * 2.0, 0.0, w10_max_tscroll());
    U.animating = true;
    ui_invalidate();
    true
}

pub unsafe fn w10_key(vk: WPARAM) -> bool {
    if vk as u16 == VK_ESCAPE && U.w10.rail_open {
        w10_set_rail(false);
        return true;
    }
    if U.tsel < 0 {
        return false;
    }
    if vk as u16 == VK_APPS {
        let r = U.w10.tiles[U.tsel as usize];
        ui_context_menu((r.x + SR(8.0)) as i32, (r.y + r.h - U.w10.tscroll) as i32, true);
        return true;
    }
    // the keyboard walks the list; a tile is chosen with the pointer
    let v = vk as u16;
    if v == VK_UP || v == VK_DOWN || v == VK_PRIOR || v == VK_NEXT || v == VK_TAB {
        U.tsel = -1;
        ui_invalidate();
    }
    false
}

pub unsafe fn w10_animate(dt: f32) -> bool {
    let w = &mut uu().w10;
    let mut anim = approach(&mut w.rail_t, if w.rail_open { 1.0 } else { 0.0 }, 18.0, dt);
    for i in 0..RAIL__COUNT as usize {
        let target = if w.rail_hover == i as i32 { 1.0 } else { 0.0 };
        anim |= approach(&mut w.rail_hover_t[i], target, 22.0, dt);
    }
    w.tscroll_target = clamp(w.tscroll_target, 0.0, w10_max_tscroll());
    let target = w.tscroll_target;
    anim |= approach(&mut w.tscroll, target, 16.0, dt);
    anim
}

// ---- drawing

// The Windows 10 Start colors: a dense, dark (or light grey) acrylic.
unsafe fn w10_bg() -> u32 {
    if TH.dark { RGBA(31, 31, 31, 255) } else { RGBA(228, 228, 228, 255) }
}

pub unsafe fn w10_draw_chrome() {
    let t = &*(&raw const TH);
    r_rect(0.0, 0.0, U.W as f32, U.H as f32, color_alpha(w10_bg(), 0.86 * t.veil), 0.0);
    let (mut y0, mut y1) = (0.0, 0.0);
    search_band(&mut y0, &mut y1);
    if q_active() {
        r_rect(0.0, y0, U.W as f32, y1 - y0, if t.dark { RGBA(255, 255, 255, 8) } else { RGBA(255, 255, 255, 150) }, 0.0);
        r_rect(0.0, y1, U.W as f32, 1.0, t.sep, 0.0);
        draw_search();
    } else {
        y0 = U.H as f32 - SR(30.0); // the update chip sits in the tiles' corner
        y1 = y0;
    }
    U.settings_btn = [0.0; 4];
    U.update_btn = [0.0; 4];
    if upd_visible() {
        draw_update_chip(U.W as f32 - SR(12.0) - update_chip_width(), ((y0 + y1) * 0.5).floor(), SR(28.0), 0.0);
    }
}

unsafe fn w10_draw_tile(r: *const Row, i: i32) {
    let t = &*(&raw const TH);
    let r = &*r;
    let x = r.x;
    let y = (r.y - U.w10.tscroll + 0.5).floor();
    let w = r.w;
    let h = r.h;
    if y + h < 0.0 || y > U.list_y1 {
        return;
    }
    let lifted = i < 0;
    let hot = i == U.tsel || lifted;
    let pressed = !lifted && i == U.w10.press_tile && U.w10.tdrag.is_null();
    r_rect(x, y, w, h, if t.dark { RGBA(255, 255, 255, if hot { 30 } else { 16 }) } else { RGBA(0, 0, 0, if hot { 24 } else { 12 }) }, 0.0);
    if hot {
        r_rect_ex(
            x,
            y,
            w,
            h,
            if t.dark { RGBA(255, 255, 255, if pressed { 70 } else { 120 }) } else { RGBA(0, 0, 0, 80) },
            0.0,
            (S(1.5) + 0.5).floor(),
            0.0,
        );
    }
    let a = &*r.app;
    let armed = U.armed == r.app;
    let icon_only = r.tsize == TILE_SMALL;
    let isz = ui_icon_px() as f32;
    let ix = (x + (w - isz) * 0.5).floor();
    let iy = (y + (h - isz) * 0.5 - if icon_only { 0.0 } else { SR(8.0) }).floor();
    if a.kind == APP_CMD {
        text_draw_icon(a.glyph as u32, S(20.0), ix + isz * 0.5, iy + isz * 0.5, if armed { t.danger } else { t.text });
    } else {
        draw_app_icon(a, ix, iy, isz);
    }
    if !icon_only {
        text_draw_fit(FONT_TEXT, S(12.0), x + SR(8.0), (y + h - SR(9.0)).floor(), w - SR(16.0), a.name, a.len, if armed { t.danger } else { t.text }, false);
    }
}

unsafe fn w10_draw_rail() {
    let t = &*(&raw const TH);
    let e = ease_out_cubic(U.w10.rail_t);
    let rw = (SR(W10_RAIL) + (SR(W10_RAIL_OPEN) - SR(W10_RAIL)) * e + 0.5).floor();
    let ih = SR(W10_RAIL_ITEM);
    let y1 = U.list_y1;
    if e > 0.0 {
        // the open rail lies over the list
        r_rect_ex(rw - SR(2.0), 0.0, SR(18.0), y1, color_alpha(RGBA(0, 0, 0, if t.dark { 110 } else { 50 }), e), 0.0, 0.0, SR(14.0));
        r_rect(0.0, 0.0, rw, y1, if t.dark { RGBA(24, 24, 24, 255) } else { RGBA(238, 238, 238, 255) }, 0.0);
    }
    let glyph: [u32; 6] = [0xE700, 0, 0xEC50, 0xE896, 0xE713, 0xE7E8];
    let label: [*const WCHAR; 6] = [
        tr!("ПУСК", "START"),
        STU.name.as_ptr(),
        tr!("Проводник", "File Explorer"),
        tr!("Загрузки", "Downloads"),
        tr!("Параметры", "Settings"),
        tr!("Выключение", "Power"),
    ];
    st_user_init();
    let fs = S(13.5);
    for i in 0..RAIL__COUNT {
        let iy = if i == RAIL_MENU { SR(4.0) } else { y1 - ih * (RAIL__COUNT - i) as f32 };
        box_set(&mut U.w10.rail[i as usize], 0.0, iy, rw, ih);
        let h = U.w10.rail_hover_t[i as usize] * if U.w10.rail_press == i { 0.6 } else { 1.0 };
        if h > 0.0 {
            r_rect(0.0, iy, rw, ih, color_alpha(t.sel, h * 1.4), 0.0);
        }
        let cx = SR(W10_RAIL) * 0.5;
        let cy = iy + ih * 0.5;
        if i == RAIL_ACCOUNT {
            let p = ui_icon_px() as f32;
            st_draw_avatar((cx - p * 0.5).floor(), (cy - p * 0.5).floor(), p);
        } else {
            text_draw_icon(glyph[i as usize], S(16.0), cx, cy, t.text);
        }
        let l = label[i as usize];
        if e > 0.0 && !l.is_null() && *l != 0 {
            R.opacity = e;
            let font = if i == RAIL_MENU { FONT_TEXT_SEMIBOLD } else { FONT_TEXT };
            r_set_clip(0.0, 0.0, rw, y1);
            text_draw_fit(font, fs, SR(W10_RAIL) + SR(4.0), text_baseline(font, fs, cy), SR(W10_RAIL_OPEN) - SR(W10_RAIL) - SR(16.0), l, -1, t.text, false);
            r_clip_all();
            R.opacity = 1.0;
        }
    }
}

unsafe fn w10_add_act(cmd: i32) {
    if U.w10.act_n < 4 {
        U.w10.act_cmd[U.w10.act_n as usize] = cmd;
        U.w10.act_n += 1;
    }
}

// The search pane's right side: the chosen result, big, with what can be done with it.
pub unsafe fn w10_draw_preview() {
    let t = &*(&raw const TH);
    let card = st_on(); // Windows 11: a rounded card beside the list, the name on top, the icon on a wide plate
    let mut x0 = list_x1();
    let mut y0 = U.list_y0;
    let mut w = U.W as f32 - x0;
    let mut rad = 0.0;
    if card {
        x0 += SR(4.0);
        y0 += SR(8.0);
        w = U.W as f32 - SR(16.0) - x0;
        rad = SR(8.0);
        r_rect(x0, y0, w, U.list_y1 - SR(12.0) - y0, if t.dark { RGBA(255, 255, 255, 10) } else { RGBA(255, 255, 255, 170) }, rad);
    } else {
        r_rect(x0, y0, w, U.H as f32 - y0, if t.dark { RGBA(255, 255, 255, 7) } else { RGBA(255, 255, 255, 110) }, 0.0);
    }
    U.w10.act_n = 0;
    let r = sel_row();
    if r.is_null() {
        return;
    }
    let cx = (x0 + w * 0.5).floor();
    let mut y = y0 + SR(36.0);
    let mut bx_ = SR(64.0);
    let isz = ui_icon_px() as f32;
    let mut bw = bx_;
    if card {
        y = y0 + SR(20.0) + SR(46.0); // under the name and the kind
        bx_ = SR(112.0);
        bw = w - SR(20.0) * 2.0;
    }
    let boxh = bx_;
    let bx = (cx - bw * 0.5).floor();
    r_rect(bx, y, bw, boxh, if t.dark { RGBA(255, 255, 255, if card { 8 } else { 16 }) } else { RGBA(0, 0, 0, if card { 6 } else { 12 }) }, rad * 0.75);
    if card {
        r_border(bx, y, bw, boxh, if t.dark { RGBA(255, 255, 255, 14) } else { RGBA(0, 0, 0, 16) }, rad * 0.75);
    }
    let ix = (cx - isz * 0.5).floor();
    let iy = (y + (boxh - isz) * 0.5).floor();
    let name: *const WCHAR;
    let kind: *const WCHAR;
    let mut where_: *const WCHAR = null();
    if (*r).kind == ROW_APP {
        let a = &*(*r).app;
        name = a.name;
        kind = if a.kind == APP_CMD { tr!("Команда", "Command") } else { tr!("Приложение", "App") };
        w10_add_act(CM_OPEN);
        if a.kind == APP_CMD {
            text_draw_icon(a.glyph as u32, S(22.0), cx, y + boxh * 0.5, t.text);
        } else {
            draw_app_icon(a, ix, iy, isz);
            w10_add_act(CM_RUNAS);
            if !a.path.is_null() {
                w10_add_act(CM_REVEAL);
            }
            w10_add_act(CM_COPY);
        }
    } else {
        let f = &*(*r).file;
        name = f.name;
        kind = if (f.flags & FI_FOLDER) != 0 { tr!("Папка", "Folder") } else { tr!("Файл", "File") };
        where_ = f.dir;
        draw_file_icon(f, ix, iy, isz);
        w10_add_act(CM_OPEN);
        w10_add_act(CM_REVEAL);
        w10_add_act(CM_COPY);
        w10_add_act(CM_PROPS);
    }
    let pad = if card { SR(20.0) } else { SR(24.0) };
    let tw = w - pad * 2.0;
    let fs = S(18.0);
    let sfs = S(12.5);
    if card {
        let nfs = S(15.0);
        let mut ty = y0 + pad + font_cap_height(FONT_TEXT_SEMIBOLD, nfs);
        text_draw_fit(FONT_TEXT_SEMIBOLD, nfs, x0 + pad, (ty + 0.5).floor(), tw, name, -1, t.text, false);
        ty += SR(21.0);
        if !where_.is_null() && *where_ != 0 {
            let kw = text_width(FONT_TEXT, sfs, kind, -1);
            text_draw(FONT_TEXT, sfs, x0 + pad, (ty + 0.5).floor(), kind, -1, t.dim);
            text_draw_fit(FONT_TEXT, sfs, x0 + pad + kw + SR(10.0), (ty + 0.5).floor(), tw - kw - SR(10.0), where_, -1, t.faint, true);
        } else {
            text_draw(FONT_TEXT, sfs, x0 + pad, (ty + 0.5).floor(), kind, -1, t.dim);
        }
        y += boxh + SR(12.0);
    } else {
        y += boxh + SR(30.0);
        let nw = min(text_width(FONT_DISPLAY, fs, name, -1), tw);
        text_draw_fit(FONT_DISPLAY, fs, (cx - nw * 0.5).floor(), y, tw, name, -1, t.text, false);
        y += SR(22.0);
        let kw = text_width(FONT_TEXT, sfs, kind, -1);
        text_draw(FONT_TEXT, sfs, (cx - kw * 0.5).floor(), y, kind, -1, t.dim);
        if !where_.is_null() && *where_ != 0 {
            y += SR(18.0);
            let ww = min(text_width(FONT_TEXT, sfs, where_, -1), tw);
            text_draw_fit(FONT_TEXT, sfs, (cx - ww * 0.5).floor(), y, tw, where_, -1, t.faint, true);
        }
        y += SR(20.0);
        r_rect(x0 + pad, y, tw, 1.0, t.sep, 0.0);
        y += SR(8.0);
    }
    let k_acts: [(i32, u32); 5] = [(CM_OPEN, 0xE8A7), (CM_RUNAS, 0xE7EF), (CM_REVEAL, 0xE838), (CM_COPY, 0xE8C8), (CM_PROPS, 0xE946)];
    let ah = SR(36.0);
    let afs = S(13.5);
    for i in 0..U.w10.act_n {
        let cmd = U.w10.act_cmd[i as usize];
        let label = if cmd == CM_OPEN {
            if (*r).kind == ROW_APP && (*(*r).app).kind == APP_CMD { tr!("Выполнить", "Run") } else { tr!("Открыть", "Open") }
        } else if cmd == CM_RUNAS {
            tr!("Запуск от имени администратора", "Run as administrator")
        } else if cmd == CM_REVEAL {
            tr!("Перейти к расположению файла", "Open file location")
        } else if cmd == CM_COPY {
            if !row_path(r).is_null() { tr!("Копировать путь", "Copy path") } else { tr!("Копировать имя", "Copy name") }
        } else {
            tr!("Свойства", "Properties")
        };
        let mut glyph = 0u32;
        for (c, g) in k_acts {
            if c == cmd {
                glyph = g;
            }
        }
        let ax = x0 + pad - SR(8.0);
        let aw = tw + SR(16.0);
        box_set(&mut U.w10.act[i as usize], ax, y, aw, ah);
        if U.w10.act_hover == i {
            r_rect(ax, y, aw, ah, if U.w10.act_press == i { color_alpha(t.sel, 0.7) } else { t.sel }, if card { SR(4.0) } else { 0.0 });
        }
        let acy = y + ah * 0.5;
        text_draw_icon(glyph, S(14.0), ax + SR(18.0), acy, t.text);
        text_draw_fit(FONT_TEXT, afs, ax + SR(40.0), text_baseline(FONT_TEXT, afs, acy), aw - SR(48.0), label, -1, t.text, false);
        y += ah;
    }
}

// The tiles and the rail over the list, or the search pane's right side.
pub unsafe fn w10_draw_overlay() {
    let t = &*(&raw const TH);
    if q_active() {
        w10_draw_preview();
        U.w10.rail = [[0.0; 4]; 6]; // no rail while searching
        return;
    }
    if w10_units() > 0 {
        // none when narrowed down to the list
        let px = w10_pane_x();
        r_set_clip(list_x1(), 0.0, U.W as f32, U.list_y1);
        let fs = S(13.0);
        let ty = (SR(32.0) - U.w10.tscroll + 0.5).floor();
        if U.w10.tile_count != 0 {
            text_draw(FONT_TEXT, fs, px, ty, tr!("Закреплено", "Pinned"), -1, t.text);
            for i in 0..U.w10.tile_count {
                if U.w10.tiles[i as usize].app != U.w10.tdrag {
                    w10_draw_tile(&raw const U.w10.tiles[i as usize], i);
                }
            }
            let d = if U.w10.tdrag.is_null() { null_mut() } else { w10_tile_of(U.w10.tdrag) };
            if !d.is_null() {
                // where it will land, and the lifted tile under the pointer, a little see-through
                let d = &*d;
                r_border(d.x, (d.y - U.w10.tscroll + 0.5).floor(), d.w, d.h, if t.dark { RGBA(255, 255, 255, 60) } else { RGBA(0, 0, 0, 50) }, 0.0);
                let mut lift = *d;
                lift.x = (U.mouse_x as f32 - U.w10.grab_x + 0.5).floor();
                lift.y = U.mouse_y as f32 - U.w10.grab_y + U.w10.tscroll;
                r_set_clip(0.0, 0.0, U.W as f32, U.list_y1);
                R.opacity = 0.85;
                w10_draw_tile(&lift, -1);
                R.opacity = 1.0;
            }
        } else {
            text_draw(FONT_TEXT, fs, px, ty, tr!("Закрепляйте приложения через правый клик -", "Pin apps with a right click -"), -1, t.dim);
            text_draw(FONT_TEXT, fs, px, ty + SR(20.0), tr!("здесь они станут плитками", "they become tiles here"), -1, t.dim);
        }
        r_clip_all();
    }
    w10_draw_rail();
}
