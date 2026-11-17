// The launcher: its state (U), the rows it shows, the search behind them and the query field.
// Window and visibility are in ui_window.rs, drawing in ui_draw.rs, input in ui_input.rs; the Start styles add
// start.rs and start10.rs.

use crate::*;

pub const ROW_HEADER: u8 = 0;
pub const ROW_APP: u8 = 1;
pub const ROW_FILE: u8 = 2;
pub const ROW_INFO: u8 = 3;
pub const ROW_VIRTUAL: u8 = 4;

// A full-width row, a Start grid tile, a Start section title, its hint, a Windows 10 tile.
pub const CELL_ROW: u8 = 0;
pub const CELL_TILE: u8 = 1;
pub const CELL_TITLE: u8 = 2;
pub const CELL_HINT: u8 = 3;
pub const CELL_W10: u8 = 4;
pub const TILE_SMALL: u8 = 0;
pub const TILE_MEDIUM: u8 = 1;
pub const TILE_WIDE: u8 = 2;
pub const TILE_LARGE: u8 = 3;
pub const TILE__COUNT: u8 = 4; // Windows 10 tile sizes

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Row {
    pub kind: u8,
    pub cell: u8,
    pub tsize: u8,
    pub app: *mut App,
    pub file: *mut FileItem,
    pub text: *const WCHAR,
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32, // in the list's content, y from its top
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AppHit {
    pub app: *mut App,
    pub score: f32,
    pub raw: i32,   // the match alone, before frecency
    pub alt: bool,  // matched in the other keyboard layout
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct TagHit {
    pub file: *mut FileItem,
    pub score: f32,
}

pub const Q_MAX: usize = 500;
pub const TIMER_CARET: usize = 1;
pub const TIMER_REHOOK: usize = 2;
pub const TIMER_PRTSC: usize = 4;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct CmItem {
    pub cmd: i32,
    pub glyph: u32,
    pub label: *const WCHAR,
    pub keys: *const WCHAR,
    pub sep: bool,
    pub disabled: bool,
    pub y: f32,
}

#[repr(C)]
pub struct CtxMenu {
    pub open: bool,
    pub closing: bool,
    pub t: f32,
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    pub n: i32,
    pub hover: i32,
    pub press: i32,
    pub hl_y: f32,
    pub hl_v: f32,
    pub hl_init: bool,
    pub it: [CmItem; 16],
    pub tag_count: [WCHAR; 32],
    pub tag_list: [WCHAR; 96],
}

#[repr(C)]
pub struct TagDlg {
    pub open: bool,
    pub closing: bool,
    pub t: f32,
    pub buf: [WCHAR; TAG_INPUT_MAX as usize + 1],
    pub len: i32,
    pub caret: i32,
    pub scroll: f32,
    pub type_: u8,
    pub key: *mut WCHAR,
    pub name: [WCHAR; 128],
    pub panel: [f32; 4],
    pub field: [f32; 4],
    pub btn: [[f32; 4]; 2],
    pub hover: i32,
    pub press: i32,
}

#[repr(C)]
pub struct StState {
    pub all_view: bool,      // "All apps" instead of the pinned grid
    pub tiles: [*mut App; 36], // pinned
    pub tile_count: i32,
    pub recs: [*mut App; 18], // recommended: the most launched of the rest
    pub rec_count: i32,
    pub title_btn: [f32; 4], // the section title's button
    pub title_hover: bool,
    pub title_press: bool,
    pub title_hover_t: f32,
    pub fb: [[f32; 4]; 5], // footer: account, downloads, explorer, settings, power
    pub fb_hover: i32,
    pub fb_press: i32,
    pub fb_hover_t: [f32; 5],
    pub letters: [[WCHAR; 2]; 72],
    pub letter_count: i32,
}

#[repr(C)]
pub struct W10State {
    pub tiles: [Row; 64],
    pub tile_count: i32,
    pub tiles_h: f32,
    pub tscroll: f32,
    pub tscroll_target: f32,
    pub press_tile: i32,
    pub rail_open: bool,
    pub rail_t: f32,
    pub rail: [[f32; 4]; 6], // menu, account, explorer, downloads, settings, power
    pub rail_hover: i32,
    pub rail_press: i32,
    pub rail_hover_t: [f32; 6],
    pub act: [[f32; 4]; 4], // search: the actions of the selected result
    pub act_cmd: [i32; 4],
    pub act_n: i32,
    pub act_hover: i32,
    pub act_press: i32,
    pub tdrag: *mut App, // the tile being dragged to a new cell, and that cell
    pub drag_cx: i32,
    pub drag_cy: i32,
    pub press_pt: POINT,
    pub grab_x: f32,
    pub grab_y: f32, // where in the tile it was taken
    pub drag: i32,   // resizing by an edge: EDGE_* bits
    pub drag_pt: POINT,
    pub drag_w: i32,
    pub drag_h: i32,
    pub drag_y: i32,
    pub wide: bool, // on a monitor 2560 px wide or more: a wider search pane
}

#[repr(C)]
pub struct Ui {
    pub visible: bool,
    pub closing: bool,
    pub vis: f32,
    pub vis_target: f32, // 0..1, how far it has opened
    pub alpha: i32,
    pub prev_fg: HWND,
    pub app_mode: bool,
    pub slide: bool, // bottom positions: the window slides out from under the taskbar like Start
    pub mon: HMONITOR,
    pub win_x: i32,
    pub win_y: i32,
    pub slide_dy: i32,
    pub slide_y: i32,
    pub slide_off: f32,
    pub slide_v: f32, // offset from the resting place and its velocity, px
    pub no_focus_restore: bool,
    pub prtsc_ticks: i32, // PrtSc hand-off (hotkey.rs): the foreground is taken back when the screenshot tool is done
    pub settings_btn: [f32; 4],
    pub update_btn: [f32; 4],
    pub settings_hover: bool,
    pub press_settings: bool,
    pub update_hover: bool,
    pub press_update: bool,
    pub update_hover_t: f32,
    pub hide_time: f64,

    pub q: [WCHAR; Q_MAX + 1],
    pub qlen: i32,
    pub caret: i32,
    pub anchor: i32,
    pub search_mode: bool,
    pub want_search: bool, // the Start styles opened from the taskbar's search: results even with no query
    pub q_scroll: f32,
    pub caret_on: bool,
    pub text_drag: bool,

    pub apps: *mut AppList,
    pub hits: *mut AppHit,
    pub hit_count: i32,
    pub hit_cap: i32,
    pub thits: [TagHit; 16],
    pub thit_count: i32,
    pub tags_first: bool,
    pub files_alt: bool, // the file search goes in the other layout too
    pub files: *mut FileResults,
    pub files_req_id: u32,
    pub files_pending: bool,
    pub fpages: *mut *mut FileResults, // pages fetched while scrolling, each owning its items
    pub fpage_count: i32,
    pub fpage_cap: i32,
    pub flist: *mut *mut FileItem, // every file so far, in order
    pub flist_count: i32,
    pub flist_cap: i32,
    pub files_shown: i32, // of flist; the rest of the total is a placeholder row (ROW_VIRTUAL)
    pub files_total: u32,
    pub files_fetched: u32,
    pub files_more_pending: bool,
    pub files_end_y: f32,
    pub files_header: [WCHAR; 64],

    pub frequent: [*mut App; 8],
    pub frequent_count: i32,
    pub recent: [FileItem; 6],
    pub recent_count: i32,
    pub recent_arena: Arena,

    pub rows: *mut Row,
    pub row_count: i32,
    pub row_cap: i32,
    pub sel: i32,
    pub press_row: i32,
    pub content_h: f32,
    pub scroll: f32,
    pub scroll_target: f32,
    pub scroll_v: f32,
    pub hl_y: f32,
    pub hl_h: f32,
    pub hl_vy: f32,
    pub hl_vh: f32, // the highlight on its springs
    pub hl_x: f32,
    pub hl_w: f32,
    pub hl_vx: f32,
    pub hl_vw: f32,
    pub open_time: f64,
    pub settings_hover_t: f32,
    pub sb_t: f32,
    pub sb_prev: f32, // the scrollbar: shown while scrolling or hovered
    pub hq: [WCHAR; Q_MAX + 1],
    pub hq_alt: [WCHAR; Q_MAX + 1], // the query as highlighted in names
    pub hq_len: i32,
    pub hq_has_alt: bool,
    pub sb_seen: f64,
    pub sb_hot: bool,
    pub sub_text: *const WCHAR, // the note at the right of the selected row
    pub sub_col: u32,
    pub sub_t: f32,
    pub cm: CtxMenu,  // the context menu, ctxmenu.rs
    pub tg: TagDlg,   // the tag dialog, tagdlg.rs
    pub last_frame: f64,
    pub dirty: bool,
    pub animating: bool,

    pub armed: *mut App, // a command that asks for a second Enter
    pub mouse_x: i32,
    pub mouse_y: i32,
    pub menu_open: bool,
    pub settings_after_hide: bool,

    pub st: StState, // the Windows 11 style, start.rs
    pub tsel: i32,   // Windows 10: the tile under the pointer (sel_row() then returns it), -1 none
    pub w10: W10State,

    pub dpi: u32,
    pub s: f32,
    pub W: i32,
    pub H: i32,
    pub search_h: f32,
    pub footer_h: f32,
    pub list_y0: f32,
    pub list_y1: f32,
    pub backdrop_ok: bool,
    pub last_reindex: f64,
}

pub static mut U: Ui = unsafe { zeroed() };

#[inline]
pub unsafe fn uu() -> &'static mut Ui {
    &mut *(&raw mut U)
}

#[inline]
pub unsafe fn row(i: i32) -> &'static mut Row {
    &mut *U.rows.add(i as usize)
}

// Showing results: a query is typed, or the search was opened empty.
pub unsafe fn q_active() -> bool {
    U.qlen != 0 || U.search_mode
}

pub unsafe fn ui_invalidate() {
    U.dirty = true;
}

pub unsafe fn S(v: f32) -> f32 {
    v * U.s
}
pub unsafe fn SR(v: f32) -> f32 {
    (v * U.s + 0.5).floor()
}

pub unsafe fn ROW_H() -> f32 {
    if st_on() && q_active() { 56.0 } else { LS().row_h } // the Windows 11 search: room for the icon plates
}
pub unsafe fn HEADER_H() -> f32 {
    LS().header_h
}
pub const LIST_PAD: f32 = 6.0;
pub unsafe fn ICON_LOGICAL() -> f32 {
    LS().icon
}

pub unsafe fn ui_icon_px() -> i32 {
    (ICON_LOGICAL() * U.s + 0.5) as i32
}

pub unsafe fn ui_metrics(dpi: u32) {
    U.dpi = if dpi != 0 { dpi } else { 96 };
    U.s = U.dpi as f32 / 96.0 * g_cfg.scale as f32 / 100.0;
    U.search_h = SR(LS().search_h);
    U.footer_h = SR(LS().footer_h);
    if st_on() {
        st_metrics();
        return;
    }
    if w10_on() {
        w10_metrics();
        return;
    }
    let list_h = SR(g_cfg.rows as f32 * ROW_H() + 2.0 * LIST_PAD);
    U.W = SR(g_cfg.width as f32) as i32;
    if cap_on() {
        // the query capsule, a gap, then one panel for the list and the footer
        let m = SR(CAP_MARGIN);
        U.W += (m * 2.0) as i32;
        U.list_y0 = m + U.search_h + SR(CAP_GAP);
        U.list_y1 = U.list_y0 + list_h;
        U.H = (U.list_y1 + U.footer_h + m) as i32;
        return;
    }
    U.H = (U.search_h + 1.0 + list_h + 1.0 + U.footer_h) as i32;
    U.list_y0 = U.search_h + 1.0;
    U.list_y1 = U.list_y0 + list_h;
}

pub unsafe fn list_height() -> f32 {
    U.list_y1 - U.list_y0
}

// ---- rows

pub unsafe fn row_add(kind: u8, h: f32) -> *mut Row {
    if !grow(&raw mut U.rows, &raw mut U.row_cap, U.row_count + 1, 256) {
        return null_mut();
    }
    let r = U.rows.add(U.row_count as usize);
    U.row_count += 1;
    zero(r);
    (*r).kind = kind;
    (*r).x = SR(LS().inset);
    (*r).w = U.W as f32 - (*r).x * 2.0;
    (*r).y = U.content_h;
    (*r).h = h;
    U.content_h += h;
    r
}

pub unsafe fn row_header(text: *const WCHAR) {
    let r = row_add(ROW_HEADER, SR(HEADER_H()));
    if !r.is_null() {
        (*r).text = text;
    }
}

pub unsafe fn row_info(text: *const WCHAR) {
    let r = row_add(ROW_INFO, SR(ROW_H()));
    if !r.is_null() {
        (*r).text = text;
    }
}

// The Design page's previews leave the AI apps out (pv_update sets this while it draws them).
pub static mut g_hide_ai: bool = false;

pub unsafe fn app_hidden(a: *const App) -> bool {
    if !g_hide_ai || a.is_null() || (*a).name.is_null() {
        return false;
    }
    let k = [
        w!("ChatGPT"), w!("Claude"), w!("Codex"), w!("Gemini"), w!("Copilot"), w!("Perplexity"), w!("Antigravity"), w!("OpenAI"), w!("Anthropic"),
        w!("Grok"), w!("DeepSeek"), w!("Ollama"), w!("LM Studio"), w!("Cursor"), w!("Windsurf"), w!("Mistral"), w!("Qwen"), w!("Kimi"), w!("Kiro"),
        w!("Midjourney"), w!("Character.AI"),
    ];
    for s in k {
        if !StrStrIW((*a).name, s).is_null() {
            return true;
        }
    }
    false
}

pub unsafe fn row_app(a: *mut App) {
    if app_hidden(a) {
        return;
    }
    let r = row_add(ROW_APP, SR(ROW_H()));
    if !r.is_null() {
        (*r).app = a;
    }
}

pub unsafe fn row_file(f: *mut FileItem) {
    let r = row_add(ROW_FILE, SR(ROW_H()));
    if !r.is_null() {
        (*r).file = f;
    }
}

// The first row that ends below y.
pub unsafe fn row_first_below(y: f32) -> i32 {
    let mut lo = 0;
    let mut hi = U.row_count;
    while lo < hi {
        let mid = (lo + hi) / 2;
        if row(mid).y + row(mid).h <= y {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    lo
}

pub unsafe fn row_selectable(i: i32) -> bool {
    i >= 0 && i < U.row_count && (row(i).kind == ROW_APP || row(i).kind == ROW_FILE)
}

pub unsafe fn first_selectable(from: i32, dir: i32) -> i32 {
    let mut i = from;
    while i >= 0 && i < U.row_count {
        if row_selectable(i) {
            return i;
        }
        i += dir;
    }
    -1
}

pub const FILES_STEP: i32 = 40;
pub const FILES_VIRTUAL_MAX: i32 = 100000;

// Rows the file list takes: the ones loaded, or the total Everything reported while more pages are to come.
unsafe fn files_virtual_total() -> i32 {
    let mut n = U.flist_count;
    if U.files_fetched < U.files_total {
        n = max(n, min(U.files_total, FILES_VIRTUAL_MAX as u32) as i32);
    }
    n
}

// Lays out what the launcher shows now: a Start home, the empty view or the results. keep_sel keeps the
// selected row where it can.
pub unsafe fn rebuild_rows(keep_sel: bool) {
    let old = U.sel;
    U.row_count = 0;
    U.files_end_y = -1.0;
    U.content_h = SR(LIST_PAD);
    if !q_active() && st_on() {
        st_build_home();
    } else if !q_active() && w10_on() {
        w10_build_home();
    } else if U.qlen == 0 && U.search_mode {
        row_info(tr!("Введите запрос", "Type to search"));
    } else if U.qlen == 0 {
        if U.apps.is_null() {
            row_info(tr!("Индексирую приложения…", "Indexing applications…"));
        }
        if U.frequent_count != 0 {
            row_header(tr!("Часто используемые", "Frequently used"));
            for i in 0..U.frequent_count as usize {
                row_app(U.frequent[i]);
            }
        }
        if U.recent_count != 0 {
            row_header(tr!("Недавние файлы", "Recent files"));
            for i in 0..U.recent_count as usize {
                row_file(&raw mut U.recent[i]);
            }
        }
        if !U.apps.is_null() && (*U.apps).shell_count != 0 {
            row_header(tr!("Все приложения", "All apps"));
            for i in 0..(*U.apps).shell_count as usize {
                row_app((*U.apps).apps.add(i));
            }
        }
    } else {
        for pass in 0..2 {
            if U.thit_count != 0 && pass == if U.tags_first { 0 } else { 1 } {
                row_header(tr!("По тегу", "Tagged"));
                for i in 0..U.thit_count as usize {
                    row_file(U.thits[i].file);
                }
            }
            if U.hit_count != 0 && pass == if U.tags_first { 1 } else { 0 } {
                row_header(tr!("Приложения", "Applications"));
                for i in 0..U.hit_count as usize {
                    row_app((*U.hits.add(i)).app);
                }
            }
        }
        let want_files = U.qlen >= g_cfg.file_min_chars;
        if want_files && !U.files.is_null() {
            if (*U.files).status == EV_NOT_RUNNING {
                row_header(tr!("Файлы", "Files"));
                row_info(tr!("Everything не запущен - поиск файлов недоступен", "Everything is not running - file search is unavailable"));
            } else if (*U.files).status == EV_ERROR {
                row_header(tr!("Файлы", "Files"));
                row_info(tr!("Everything не ответил", "Everything did not respond"));
            } else if U.flist_count != 0 {
                let total = files_virtual_total();
                if total > U.files_shown {
                    wfmt!(U.files_header.as_mut_ptr(), 64, "{}  \u{00B7}  {}", Ws(tr!("Файлы", "Files")), U.files_total as i32);
                } else {
                    wcopy(U.files_header.as_mut_ptr(), 64, tr!("Файлы", "Files"));
                }
                row_header(U.files_header.as_ptr());
                for i in 0..U.files_shown as usize {
                    let f = *U.flist.add(i);
                    if U.thit_count == 0 || !tag_hit_has((*f).full) {
                        row_file(f);
                    }
                }
                U.files_end_y = U.content_h;
                if total > U.files_shown {
                    row_add(ROW_VIRTUAL, SR(ROW_H()) * (total - U.files_shown) as f32);
                }
            }
        }
        if first_selectable(0, 1) < 0 && !(want_files && U.files_pending) && !(!U.files.is_null() && (*U.files).status != EV_OK) {
            row_info(tr!("Ничего не найдено", "No results"));
        }
    }
    U.content_h += SR(LIST_PAD);
    if w10_on() {
        w10_place_rows();
    } else if st_search() {
        st_place_rows();
    }

    if keep_sel && row_selectable(old) {
        U.sel = old;
    } else if keep_sel && old >= U.row_count {
        U.sel = first_selectable(U.row_count - 1, -1);
    } else {
        U.sel = first_selectable(0, 1);
    }
    if !row_selectable(U.sel) {
        U.sel = first_selectable(0, 1);
    }
}

pub unsafe fn max_scroll() -> f32 {
    max(0.0, U.content_h - list_height())
}

pub unsafe fn snap_highlight() {
    if row_selectable(U.sel) {
        let r = *row(U.sel);
        U.hl_y = r.y;
        U.hl_h = r.h;
        U.hl_x = r.x;
        U.hl_w = r.w;
    }
    U.hl_vy = 0.0;
    U.hl_vh = 0.0;
    U.hl_vx = 0.0;
    U.hl_vw = 0.0;
}

// Scrolls a row into view, with its section header (and from the very top for the first one).
pub unsafe fn ensure_visible(idx: i32, animate: bool) {
    if !row_selectable(idx) {
        return;
    }
    let r = *row(idx);
    let mut top = r.y;
    let bot = r.y + r.h;
    let view = list_height();
    if idx > 0 && row(idx - 1).kind == ROW_HEADER {
        top = row(idx - 1).y;
    }
    if first_selectable(0, 1) == idx {
        top = 0.0;
    }
    if top < U.scroll_target {
        U.scroll_target = top;
    } else if bot + SR(LIST_PAD) > U.scroll_target + view {
        U.scroll_target = bot + SR(LIST_PAD) - view;
    }
    U.scroll_target = clamp(U.scroll_target, 0.0, max_scroll());
    if !animate {
        U.scroll = U.scroll_target;
        U.scroll_v = 0.0;
    }
    U.animating = true;
}

// ---- search

unsafe extern "C" fn cmp_hits(a: *const c_void, b: *const c_void) -> i32 {
    let x = &*(a as *const AppHit);
    let y = &*(b as *const AppHit);
    if x.score != y.score {
        return if x.score < y.score { 1 } else { -1 };
    }
    if x.app < y.app { -1 } else { 1 }
}

// The query trimmed and normalized for matching.
pub unsafe fn query_norm(out: *mut WCHAR) -> i32 {
    let mut b = 0;
    let mut e = U.qlen;
    while b < e && U.q[b as usize] == ' ' as u16 {
        b += 1;
    }
    while e > b && U.q[e as usize - 1] == ' ' as u16 {
        e -= 1;
    }
    norm_str(out, U.q.as_ptr().add(b as usize), e - b);
    e - b
}

pub unsafe fn app_hist_hash(a: *const App) -> u64 {
    item_hash_of(app_item_type(a), (*a).id_hash)
}

pub unsafe fn refresh_frecency() {
    if U.apps.is_null() {
        return;
    }
    let now = unix_now();
    for i in 0..(*U.apps).count as usize {
        let a = (*U.apps).apps.add(i);
        (*a).frec = frec_bonus(hist_frecency(app_item_type(a), (*a).id, app_hist_hash(a), now));
    }
}

unsafe extern "C" fn cmp_frequent(a: *const c_void, b: *const c_void) -> i32 {
    let x = *(a as *const *mut App);
    let y = *(b as *const *mut App);
    if (*x).frec < (*y).frec {
        1
    } else if (*x).frec > (*y).frec {
        -1
    } else {
        0
    }
}

// The empty launcher: the apps used most and the files opened lately.
pub unsafe fn refresh_empty_view() {
    U.frequent_count = 0;
    if !U.apps.is_null() {
        let mut tmp: [*mut App; 512] = [null_mut(); 512];
        let mut n = 0usize;
        let min_frec = frec_bonus(0.3);
        let mut i = 0;
        while i < (*U.apps).shell_count as usize && n < 512 {
            let a = (*U.apps).apps.add(i);
            if (*a).frec > min_frec && !app_hidden(a) {
                tmp[n] = a;
                n += 1;
            }
            i += 1;
        }
        qsort(tmp.as_mut_ptr() as *mut c_void, n, size_of::<*mut App>(), cmp_frequent);
        U.frequent_count = min(n, 8) as i32;
        for k in 0..U.frequent_count as usize {
            U.frequent[k] = tmp[k];
        }
    }
    let mut rf: [RecentFile; 6] = zeroed();
    let n = history_top_files(rf.as_mut_ptr(), 6);
    arena_reset(&raw mut U.recent_arena);
    U.recent_count = 0;
    for i in 0..n as usize {
        let f = &mut *(&raw mut U.recent[U.recent_count as usize]);
        U.recent_count += 1;
        zero(f);
        f.full = wdup(&raw mut U.recent_arena, rf[i].path, -1);
        let mut name: *const WCHAR = null();
        let dl = path_split(f.full, &mut name);
        f.name = name as *mut WCHAR;
        f.dir = wdup(&raw mut U.recent_arena, f.full, dl);
        f.flags = if rf[i].folder { FI_FOLDER } else { 0 };
        f.icon_key = icon_key_for_file(f.full, rf[i].folder);
    }
    if st_on() {
        st_refresh_tiles();
    }
    if w10_on() {
        w10_refresh_tiles();
    }
}

const KW_MIN: i32 = SCORE_SUBSTRING - 400; // a keyword counts from a substring match up, a fuzzy one is too loose

// Scores every app against the query (and the query in the other layout): its name, its hidden keywords and
// its tags, then the launch history on top.
pub unsafe fn search_apps() {
    U.hit_count = 0;
    U.files_alt = false;
    if U.apps.is_null() || U.qlen == 0 {
        return;
    }
    if !grow(&raw mut U.hits, &raw mut U.hit_cap, (*U.apps).count, 256) {
        return;
    }
    let mut qn = [0u16; Q_MAX + 1];
    let mut alt = [0u16; Q_MAX + 1];
    let ql = query_norm(qn.as_mut_ptr());
    if ql == 0 {
        return;
    }
    let qn = qn.as_ptr();
    let has_alt = layout_convert(qn, ql, alt.as_mut_ptr());
    let alt = alt.as_ptr();
    let now = unix_now();
    for i in 0..(*U.apps).count as usize {
        let a = &mut *(*U.apps).apps.add(i);
        let mut sc = score_query(a.norm, a.len, a.ws, qn, ql);
        let mut is_alt = false;
        if has_alt {
            let sa = score_query(a.norm, a.len, a.ws, alt, ql) * 9 / 10;
            if sa > sc {
                sc = sa;
                is_alt = true;
            }
        }
        let kw_pen = 300 + min(a.len, 60) * 2;
        for k in 0..a.kw_count as usize {
            let sk = score_query(a.kw[k], a.kw_len[k], a.kw_ws[k], qn, ql);
            if sk >= KW_MIN && sk - kw_pen > sc {
                sc = sk - kw_pen;
                is_alt = false;
            }
            if has_alt {
                let sa = score_query(a.kw[k], a.kw_len[k], a.kw_ws[k], alt, ql);
                if sa >= KW_MIN && (sa - kw_pen) * 9 / 10 > sc {
                    sc = (sa - kw_pen) * 9 / 10;
                    is_alt = true;
                }
            }
        }
        if a.kind == APP_CMD {
            // commands only for a clear match, the risky ones from three letters
            if sc < SCORE_SUBSTRING - 600 || (a.danger && ql < 3) {
                sc = 0;
            } else {
                sc -= 500;
            }
        }
        let type_ = app_item_type(a);
        let h = app_hist_hash(a);
        if TG.n != 0 {
            let mut tag_alt = false;
            let st = tags_score(tags_find(type_, a.id, h), qn, ql, alt, has_alt, &mut tag_alt);
            if st != 0 && st + 400 > sc {
                // the user's own word for it beats a name match of the same kind
                sc = st + 400;
                is_alt = tag_alt;
            }
        }
        if sc <= 0 {
            continue;
        }
        let hit = &mut *U.hits.add(U.hit_count as usize);
        U.hit_count += 1;
        hit.app = a;
        hit.alt = is_alt;
        hit.raw = sc;
        hit.score = sc as f32 + a.frec + qmem_bonus(h, qn, ql, now);
    }
    qsort(U.hits as *mut c_void, U.hit_count as usize, size_of::<AppHit>(), cmp_hits);
    // Other-layout file search only for a clear name match: a loose fuzzy hit must not hide the file results.
    if U.hit_count != 0 && (*U.hits).alt {
        let mut any_direct = false;
        let mut strong_alt = false;
        for i in 0..U.hit_count as usize {
            let h = &*U.hits.add(i);
            if !h.alt {
                any_direct = true;
            } else if h.raw > SCORE_FUZZY_MAX {
                strong_alt = true;
            }
        }
        U.files_alt = !any_direct && strong_alt;
    }
    U.hit_count = min(U.hit_count, g_cfg.max_apps);
}

unsafe extern "C" fn cmp_tag_hits(a: *const c_void, b: *const c_void) -> i32 {
    let x = &*(a as *const TagHit);
    let y = &*(b as *const TagHit);
    if x.score < y.score {
        1
    } else if x.score > y.score {
        -1
    } else {
        0
    }
}

// Files and folders found by their tags. They get a section of their own: Everything finds them by name only.
pub unsafe fn search_tags() {
    U.thit_count = 0;
    U.tags_first = false;
    if TG.n == 0 || U.qlen == 0 {
        return;
    }
    let mut qn = [0u16; Q_MAX + 1];
    let mut alt = [0u16; Q_MAX + 1];
    let ql = query_norm(qn.as_mut_ptr());
    if ql == 0 {
        return;
    }
    let has_alt = layout_convert(qn.as_ptr(), ql, alt.as_mut_ptr());
    let now = unix_now();
    let mut tmp: [TagHit; 256] = zeroed();
    let mut n = 0usize;
    let mut i = 0;
    while i < TG.n as usize && n < 256 {
        let ti = *TG.items.add(i);
        i += 1;
        if (*ti).type_ != IT_FILE && (*ti).type_ != IT_DIR {
            continue;
        }
        let mut is_alt = false;
        let st = tags_score(ti, qn.as_ptr(), ql, alt.as_ptr(), has_alt, &mut is_alt);
        if st == 0 {
            continue;
        }
        tmp[n].file = &mut (*ti).file;
        tmp[n].score = (st + 400) as f32 + frec_bonus(hist_frecency((*ti).type_, (*ti).key, (*ti).hash, now)) + qmem_bonus((*ti).hash, qn.as_ptr(), ql, now);
        n += 1;
    }
    qsort(tmp.as_mut_ptr() as *mut c_void, n, size_of::<TagHit>(), cmp_tag_hits);
    U.thit_count = min(n, 16) as i32;
    for k in 0..U.thit_count as usize {
        U.thits[k] = tmp[k];
    }
    U.tags_first = U.thit_count != 0 && (U.hit_count == 0 || U.thits[0].score > (*U.hits).score);
}

unsafe fn tag_hit_has(full: *const WCHAR) -> bool {
    for i in 0..U.thit_count as usize {
        if _wcsicmp((*U.thits[i].file).full, full) == 0 {
            return true;
        }
    }
    false
}

// ---- files (everything.rs answers through on_files_ready)

pub unsafe fn files_clear() {
    fileresults_free(U.files);
    U.files = null_mut();
    for i in 0..U.fpage_count as usize {
        fileresults_free(*U.fpages.add(i));
    }
    U.fpage_count = 0;
    U.flist_count = 0;
    U.files_shown = 0;
    U.files_total = 0;
    U.files_fetched = 0;
    U.files_more_pending = false;
}

unsafe fn files_append(r: *mut FileResults) -> bool {
    if !grow(&raw mut U.flist, &raw mut U.flist_cap, U.flist_count + (*r).count, 256) {
        return false;
    }
    for i in 0..(*r).count as usize {
        *U.flist.add(U.flist_count as usize) = (*r).items.add(i);
        U.flist_count += 1;
    }
    true
}

pub unsafe fn request_files() {
    if U.qlen >= g_cfg.file_min_chars {
        let mut q = [0u16; Q_MAX + 1];
        if U.files_alt {
            let mut qn = [0u16; Q_MAX + 1];
            let ql = query_norm(qn.as_mut_ptr());
            if !layout_convert(qn.as_ptr(), ql, q.as_mut_ptr()) {
                wcopy(q.as_mut_ptr(), (Q_MAX + 1) as i32, U.q.as_ptr());
            }
        } else {
            wcopy(q.as_mut_ptr(), (Q_MAX + 1) as i32, U.q.as_ptr());
        }
        U.files_req_id += 1;
        U.files_pending = true;
        U.files_more_pending = false;
        ev_request(U.files_req_id, q.as_ptr());
    } else {
        U.files_pending = false;
        files_clear();
    }
}

// Shows more of the loaded files as the list scrolls toward their end, and asks for the next page in time.
pub unsafe fn files_load_more() {
    if U.files_end_y < 0.0 || U.files.is_null() || (*U.files).status != EV_OK {
        return;
    }
    let rh = SR(ROW_H());
    let need = max(U.scroll, U.scroll_target) + list_height() * 2.0;
    if need < U.files_end_y {
        return;
    }
    let want = U.files_shown + ((need - U.files_end_y) / rh).ceil() as i32;
    if U.files_shown < U.flist_count {
        let n = min(U.flist_count, max(U.files_shown + FILES_STEP, want));
        if n != U.files_shown {
            U.files_shown = n;
            rebuild_rows(true);
            ui_invalidate();
        }
    }
    if !U.files_more_pending && U.files_fetched < U.files_total && U.flist_count - U.files_shown < FILES_STEP && U.flist_count < FILES_VIRTUAL_MAX {
        U.files_more_pending = true;
        ev_request_more(U.files_req_id);
    }
}

unsafe extern "C" fn cmp_file_score_ui(a: *const c_void, b: *const c_void) -> i32 {
    let x = &*(a as *const FileItem);
    let y = &*(b as *const FileItem);
    if x.score < y.score {
        1
    } else if x.score > y.score {
        -1
    } else {
        0
    }
}

pub unsafe fn on_files_ready(r: *mut FileResults) {
    if r.is_null() {
        return;
    }
    if (*r).id != U.files_req_id || U.qlen < g_cfg.file_min_chars || ((*r).page && U.files.is_null()) {
        fileresults_free(r);
        return;
    }
    let now = unix_now();
    for i in 0..(*r).count as usize {
        let f = &mut *(*r).items.add(i);
        let type_ = file_item_type(f);
        f.score += frec_bonus(hist_frecency(type_, f.full, item_hash(type_, f.full), now));
    }
    qsort((*r).items as *mut c_void, (*r).count as usize, size_of::<FileItem>(), cmp_file_score_ui);
    if (*r).page {
        U.files_more_pending = false;
        U.files_total = (*r).total;
        U.files_fetched = (*r).fetched;
        if !grow(&raw mut U.fpages, &raw mut U.fpage_cap, U.fpage_count + 1, 16) || !files_append(r) {
            fileresults_free(r);
            return;
        }
        *U.fpages.add(U.fpage_count as usize) = r;
        U.fpage_count += 1;
    } else {
        U.files_pending = false;
        files_clear();
        U.files = r;
        files_append(r);
        U.files_shown = min(U.flist_count, FILES_STEP);
        U.files_total = (*r).total;
        U.files_fetched = (*r).fetched;
    }
    rebuild_rows(true);
    if U.scroll_target > max_scroll() {
        U.scroll_target = max_scroll();
    }
    files_load_more();
    ui_invalidate();
}

// The caret shows at once and blinks from now.
pub unsafe fn caret_reset() {
    U.caret_on = true;
    if U.visible {
        SetTimer(g_hwnd, TIMER_CARET, GetCaretBlinkTime(), None);
    }
    ui_invalidate();
}

pub unsafe fn on_query_changed() {
    U.armed = null_mut();
    cm_close(false);
    if w10_on() {
        w10_fit_size();
    }
    search_apps();
    search_tags();
    request_files();
    rebuild_rows(false);
    U.scroll = 0.0;
    U.scroll_target = 0.0;
    U.scroll_v = 0.0;
    snap_highlight();
    caret_reset();
}

// A new app list from the indexer (or the cache): the old one goes once nothing points into it.
pub unsafe fn on_apps_ready(l: *mut AppList) {
    let old = U.apps;
    U.apps = l;
    // what points into the old list or was made for its rows: an armed command, a held or dragged tile, the menu
    U.armed = null_mut();
    U.press_row = -1;
    U.w10.press_tile = -1;
    if !U.w10.tdrag.is_null() {
        U.w10.tdrag = null_mut();
        ReleaseCapture();
    }
    cm_close(false);
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

fn is_word_char(c: WCHAR) -> bool {
    c != ' ' as u16 && !is_sep(c)
}

pub unsafe fn word_left(mut i: i32) -> i32 {
    while i > 0 && !is_word_char(U.q[i as usize - 1]) {
        i -= 1;
    }
    while i > 0 && is_word_char(U.q[i as usize - 1]) {
        i -= 1;
    }
    i
}

pub unsafe fn word_right(mut i: i32) -> i32 {
    while i < U.qlen && !is_word_char(U.q[i as usize]) {
        i += 1;
    }
    while i < U.qlen && is_word_char(U.q[i as usize]) {
        i += 1;
    }
    i
}

unsafe fn surrogate_pair_at(i: i32) -> bool {
    i > 0 && (0xDC00..=0xDFFF).contains(&U.q[i as usize]) && (0xD800..=0xDBFF).contains(&U.q[i as usize - 1])
}

pub unsafe fn char_left(i: i32) -> i32 {
    if i <= 0 {
        return 0;
    }
    let i = i - 1;
    if surrogate_pair_at(i) { i - 1 } else { i }
}

pub unsafe fn char_right(i: i32) -> i32 {
    if i >= U.qlen {
        return U.qlen;
    }
    let i = i + 1;
    if i < U.qlen && surrogate_pair_at(i) { i + 1 } else { i }
}

pub unsafe fn edit_replace(a: i32, b: i32, s: *const WCHAR, n: i32) {
    let c = text_splice(U.q.as_mut_ptr(), &raw mut U.qlen, (Q_MAX + 1) as i32, a, b, s, n);
    U.caret = c;
    U.anchor = c;
    on_query_changed();
}

pub unsafe fn edit_move(pos: i32, extend: bool) {
    U.caret = clamp(pos, 0, U.qlen);
    if !extend {
        U.anchor = U.caret;
    }
    caret_reset();
}

pub unsafe fn clipboard_paste() {
    let mut tmp = [0u16; Q_MAX + 1];
    let n = clipboard_get(tmp.as_mut_ptr(), (Q_MAX + 1) as i32);
    if n != 0 {
        edit_replace(U.anchor, U.caret, tmp.as_ptr(), n);
    }
}
