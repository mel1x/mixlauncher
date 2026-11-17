// The Windows 11 style: the launcher laid out like the Start menu. A grid of pinned and frequent apps
// (or all apps), the search box on top, the account and power buttons at the bottom.
// Search results reuse the regular list.

use crate::*;
use windows_sys::Win32::Security::Authorization::ConvertSidToStringSidW;

const ST_W: f32 = 654.0;
const ST_H: f32 = 701.0;
pub const ST_COLS: i32 = 6;
pub const ST_TILE_W: f32 = 96.0;
const ST_TILE_H: f32 = 100.0; // room for a name on two lines
const ST_TITLE_H: f32 = 60.0; // the title sits 34 below its top, the rest is room above the grid
pub const PINS_MAX: usize = 64;

pub const FB_ACCOUNT: i32 = 0;
pub const FB_DOWNLOADS: i32 = 1;
pub const FB_EXPLORER: i32 = 2;
pub const FB_SETTINGS: i32 = 3;
pub const FB_POWER: i32 = 4;
pub const FB__COUNT: i32 = 5;

pub unsafe fn st_on() -> bool {
    g_cfg.style == STYLE_START
}

pub const ST_RESULTS: f32 = 304.0; // the results column of the search; the chosen one is shown on the right

// Searching, with results to show: the list on the left and the pane about the chosen one.
// An empty query or no results take the whole width, the hint centered.
pub unsafe fn st_search() -> bool {
    st_on() && q_active() && first_selectable(0, 1) >= 0
}

pub unsafe fn st_place_rows() {
    let x = SR(16.0);
    let w = SR(ST_RESULTS) - x - SR(8.0);
    for i in 0..U.row_count {
        row(i).x = x;
        row(i).w = w;
    }
}

pub unsafe fn st_grid_x() -> f32 {
    ((U.W as f32 - SR(ST_TILE_W) * ST_COLS as f32) * 0.5).floor()
}

pub unsafe fn st_metrics() {
    U.W = SR(ST_W) as i32;
    U.H = SR(ST_H) as i32;
    U.list_y0 = U.search_h;
    U.list_y1 = U.H as f32 - U.footer_h;
}

// ---- pins (pins.txt): app ids in the order they were pinned or moved, each with its Windows 10 tile size and
// cell (column and row, -1 until it has one). Shared with the Windows 10 style.

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Pin {
    pub id: *mut WCHAR,
    pub size: u8, // TILE_*
    pub col: i16,
    pub row: i16,
}

#[repr(C)]
pub struct Pins {
    pub p: [Pin; PINS_MAX],
    pub n: i32,
    pub loaded: bool,
}

pub static mut PIN: Pins = unsafe { zeroed() };

// <id> [<size> <col> <row>], tab-separated
pub unsafe fn pins_load() {
    if PIN.loaded {
        return;
    }
    PIN.loaded = true;
    let text = data_read(w!("pins.txt"));
    if text.is_null() {
        return;
    }
    let mut cur = text;
    let mut id = vec![0u16; 1024];
    while PIN.n < PINS_MAX as i32 {
        let line = next_line(&mut cur);
        if line.is_null() {
            break;
        }
        if *line == 0 || *line == b'#' {
            continue;
        }
        let tab = strchr(line, b'\t' as i32);
        let mut size = TILE_MEDIUM as i32;
        let mut col = -1;
        let mut rw = -1;
        if !tab.is_null() {
            *tab = 0;
            let mut f: *mut u8 = null_mut();
            size = clamp(strtol(tab.add(1), &mut f, 10), 0, TILE__COUNT as i32 - 1);
            if *f == b'\t' {
                col = strtol(f.add(1), &mut f, 10);
            }
            if *f == b'\t' {
                rw = strtol(f.add(1), &mut f, 10);
            } else {
                col = -1;
            }
        }
        utf8_to_w(line, -1, id.as_mut_ptr(), 1024);
        if id[0] == 0 {
            continue;
        }
        let p = &mut PIN.p[PIN.n as usize];
        PIN.n += 1;
        let cell = col >= 0 && rw >= 0;
        p.size = size as u8;
        p.col = if cell { min(col, 63) as i16 } else { -1 };
        p.row = if cell { min(rw, 250) as i16 } else { -1 };
        p.id = wdup_heap(id.as_ptr());
    }
    free(text as *mut c_void);
}

pub unsafe fn pins_save() {
    let mut b = Buf::new();
    b.str("# MixLauncher pinned apps\n");
    for i in 0..PIN.n as usize {
        let p = PIN.p[i];
        b.put_w(p.id);
        buf_printf!(b, "\t{}\t{}\t{}\n", p.size, p.col, p.row);
    }
    buf_save_data(&mut b, w!("pins.txt"));
}

pub unsafe fn pin_index(id: *const WCHAR) -> i32 {
    pins_load();
    for i in 0..PIN.n {
        if wcscmp(PIN.p[i as usize].id, id) == 0 {
            return i;
        }
    }
    -1
}

pub unsafe fn pin_size(i: i32) -> i32 {
    if i >= 0 && i < PIN.n { PIN.p[i as usize].size as i32 } else { TILE_MEDIUM as i32 }
}

pub unsafe fn pin_set_size(id: *const WCHAR, size: i32) {
    let i = pin_index(id);
    if i < 0 || PIN.p[i as usize].size as i32 == size {
        return;
    }
    PIN.p[i as usize].size = size as u8;
    pins_save();
}

pub unsafe fn pin_set(id: *const WCHAR, on: bool) {
    let i = pin_index(id);
    if on && i < 0 && PIN.n < PINS_MAX as i32 {
        let p = &mut PIN.p[PIN.n as usize];
        PIN.n += 1;
        p.id = wdup_heap(id);
        p.size = TILE_MEDIUM;
        p.col = -1;
        p.row = -1;
    } else if !on && i >= 0 {
        free(PIN.p[i as usize].id as *mut c_void);
        let i = i as usize;
        let n = PIN.n as usize;
        PIN.p.copy_within(i + 1..n, i);
        PIN.n -= 1;
    } else {
        return;
    }
    pins_save();
}

pub unsafe fn pin_to_top(id: *const WCHAR) {
    let i = pin_index(id);
    if i <= 0 {
        return;
    }
    let p = PIN.p[i as usize];
    PIN.p.copy_within(0..i as usize, 1);
    PIN.p[0] = p;
    pins_save();
}

// ---- the grid

pub unsafe fn app_by_id(id: *const WCHAR) -> *mut App {
    if U.apps.is_null() {
        return null_mut();
    }
    for i in 0..(*U.apps).count as usize {
        let a = (*U.apps).apps.add(i);
        if wcscmp((*a).id, id) == 0 {
            return a;
        }
    }
    null_mut()
}

unsafe extern "C" fn cmp_frequent_st(a: *const c_void, b: *const c_void) -> i32 {
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

// The pinned grid, and the recommended one: the most launched apps that are not pinned.
pub unsafe fn st_refresh_tiles() {
    pins_load();
    U.st.tile_count = 0;
    U.st.rec_count = 0;
    if U.apps.is_null() {
        return;
    }
    let cap = 36;
    let mut k = 0;
    while k < PIN.n && U.st.tile_count < cap {
        let a = app_by_id(PIN.p[k as usize].id);
        if !a.is_null() && !app_hidden(a) {
            U.st.tiles[U.st.tile_count as usize] = a;
            U.st.tile_count += 1;
        }
        k += 1;
    }
    let mut tmp: [*mut App; 512] = [null_mut(); 512];
    let mut n = 0usize;
    let mut i = 0;
    while i < (*U.apps).shell_count as usize && n < 512 {
        let a = (*U.apps).apps.add(i);
        if (*a).frec > 0.0 && pin_index((*a).id) < 0 && !app_hidden(a) {
            tmp[n] = a;
            n += 1;
        }
        i += 1;
    }
    qsort(tmp.as_mut_ptr() as *mut c_void, n, size_of::<*mut App>(), cmp_frequent_st);
    let mut i = 0;
    while i < n && U.st.rec_count < 18 {
        U.st.recs[U.st.rec_count as usize] = tmp[i];
        U.st.rec_count += 1;
        i += 1;
    }
}

unsafe fn st_title(text: *const WCHAR) {
    let t = row_add(ROW_HEADER, SR(ST_TITLE_H));
    if !t.is_null() {
        (*t).cell = CELL_TITLE;
        (*t).text = text;
    }
}

unsafe fn st_hint(text: *const WCHAR) {
    let r = row_add(ROW_INFO, SR(36.0));
    if !r.is_null() {
        (*r).cell = CELL_HINT;
        (*r).text = text;
    }
}

unsafe fn st_grid(apps: *const *mut App, n: i32) {
    let tw = SR(ST_TILE_W);
    let th = SR(ST_TILE_H);
    let x0 = st_grid_x();
    let y0 = U.content_h;
    for i in 0..n {
        let r = row_add(ROW_APP, 0.0);
        if r.is_null() {
            break;
        }
        (*r).app = *apps.add(i as usize);
        (*r).cell = CELL_TILE;
        (*r).x = x0 + tw * (i % ST_COLS) as f32;
        (*r).w = tw;
        (*r).y = y0 + th * (i / ST_COLS) as f32;
        (*r).h = th;
    }
    U.content_h = y0 + th * ((n + ST_COLS - 1) / ST_COLS) as f32;
}

// All apps by name, under a header for each first letter.
unsafe fn st_all_apps_rows() {
    U.st.letter_count = 0;
    let mut last: WCHAR = 0;
    for i in 0..(*U.apps).shell_count as usize {
        let a = (*U.apps).apps.add(i);
        if app_hidden(a) {
            continue; // before its letter: no header left empty
        }
        let mut c = towupper(*(*a).name);
        if !char::from_u32(c as u32).map(|ch| ch.is_alphabetic()).unwrap_or(false) {
            c = '#' as u16;
        }
        if c != last && U.st.letter_count < 72 {
            let l = &mut U.st.letters[U.st.letter_count as usize];
            U.st.letter_count += 1;
            l[0] = c;
            l[1] = 0;
            row_header(l.as_ptr());
            last = c;
        }
        row_app(a);
    }
}

pub unsafe fn st_build_home() {
    U.content_h = 0.0;
    if U.st.all_view {
        st_title(tr!("Все приложения", "All apps"));
    } else if U.st.tile_count != 0 || U.st.rec_count == 0 {
        st_title(tr!("Закреплено", "Pinned")); // hidden until something is pinned
    }
    if U.apps.is_null() {
        row_info(tr!("Индексирую приложения…", "Indexing applications…"));
        return;
    }
    if U.st.all_view {
        st_all_apps_rows();
        return;
    }
    if U.st.tile_count != 0 {
        st_grid(U.st.tiles.as_ptr(), U.st.tile_count);
    } else if U.st.rec_count == 0 {
        st_hint(tr!("Закрепляйте приложения через правый клик", "Pin apps with a right click"));
    }
    if U.st.rec_count != 0 {
        st_title(tr!("Рекомендуем", "Recommended"));
        st_grid(U.st.recs.as_ptr(), U.st.rec_count);
    }
}

unsafe fn st_set_all_view(on: bool) {
    U.st.all_view = on;
    rebuild_rows(false);
    U.scroll = 0.0;
    U.scroll_target = 0.0;
    U.scroll_v = 0.0;
    snap_highlight();
    U.open_time = if anims_enabled() { time_now() } else { 0.0 }; // the rows cascade in, as the pages of Start slide
    U.animating = true;
    ui_invalidate();
}

pub unsafe fn st_pins_changed() {
    if w10_on() {
        w10_refresh_tiles();
        w10_pins_freeze(); // a new, resized or bumped tile keeps the cell it got
    } else {
        st_refresh_tiles();
    }
    rebuild_rows(true);
    snap_highlight();
    ui_invalidate();
}

// ---- the account

#[repr(C)]
pub struct StUser {
    pub init: bool,
    pub name: [WCHAR; 128],
    pub pic: [WCHAR; MAX_PATH],
    pub pic_key: u64,
}

pub static mut STU: StUser = unsafe { zeroed() };

pub unsafe fn st_user_init() {
    if STU.init {
        return;
    }
    STU.init = true;
    type Pfn = unsafe extern "system" fn(i32, *mut u16, *mut u32) -> u8;
    let m = LoadLibraryW(w!("secur32.dll"));
    let gun: Option<Pfn> = if m.is_null() { None } else { core::mem::transmute(GetProcAddress(m, s!("GetUserNameExW"))) };
    let mut n = 128u32;
    let ok = match gun {
        Some(f) => f(3 /*NameDisplay*/, STU.name.as_mut_ptr(), &mut n) != 0 && STU.name[0] != 0,
        None => false,
    };
    if !ok {
        let mut k = 128u32;
        if GetUserNameW(STU.name.as_mut_ptr(), &mut k) == 0 {
            STU.name[0] = 0;
        }
    }
    // The account picture, where Settings keeps it for this user's SID.
    let mut tok: HANDLE = null_mut();
    if OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut tok) == 0 {
        return;
    }
    let mut buf = [0u64; 32];
    let mut got = 0u32;
    let mut sid: PWSTR = null_mut();
    if GetTokenInformation(tok, TokenUser, buf.as_mut_ptr() as *mut c_void, 256, &mut got) != 0 {
        ConvertSidToStringSidW((*(buf.as_ptr() as *const TOKEN_USER)).User.Sid, &mut sid);
    }
    CloseHandle(tok);
    if sid.is_null() {
        return;
    }
    let key = wstr(&format!("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AccountPicture\\Users\\{}", Ws(sid)));
    LocalFree(sid as *mut c_void);
    let vals = [w!("Image192"), w!("Image96"), w!("Image208"), w!("Image448")];
    for v in vals {
        if STU.pic[0] != 0 {
            break;
        }
        let mut sz = (MAX_PATH * 2) as u32;
        if RegGetValueW(HKEY_LOCAL_MACHINE, key.as_ptr(), v, RRF_RT_REG_SZ, null_mut(), STU.pic.as_mut_ptr() as *mut c_void, &mut sz) != ERROR_SUCCESS
            || GetFileAttributesW(STU.pic.as_ptr()) == INVALID_FILE_ATTRIBUTES
        {
            STU.pic[0] = 0;
        }
    }
    if STU.pic[0] != 0 {
        STU.pic_key = hash_wstr_i(STU.pic.as_ptr(), -1) ^ 0xA7A7;
    }
}

// ---- actions

pub unsafe fn st_open(uri: *const WCHAR) {
    AllowSetForegroundWindow(ASFW_ANY);
    ui_hide();
    launch_open(uri, OPEN_URI);
}

pub unsafe fn st_run(cmd: u8) {
    ui_hide();
    launch_command(cmd);
}

// Hibernation is turned on (powercfg /h on) and the machine supports it.
unsafe fn hibernate_allowed() -> bool {
    type Pfn = unsafe extern "system" fn() -> u8;
    let m = LoadLibraryW(w!("powrprof.dll"));
    let f: Option<Pfn> = if m.is_null() { None } else { core::mem::transmute(GetProcAddress(m, s!("IsPwrHibernateAllowed"))) };
    match f {
        Some(f) => f() != 0,
        None => false,
    }
}

// The account or the power menu, at a button's top left.
pub unsafe fn st_menu_open(account: bool, x: i32, y: i32) {
    U.cm.n = 0;
    if account {
        cm_add(CM_ST_ACCOUNT, 0xE77B, tr!("Изменить параметры учётной записи", "Change account settings"), w!(""), false);
        cm_add(CM_ST_LOCK, 0xE72E, tr!("Заблокировать", "Lock"), w!(""), false);
        cm_add(CM_ST_SIGNOUT, 0xF3B1, tr!("Выйти", "Sign out"), w!(""), false);
        cm_add(CM_ST_MLSET, 0xE713, tr!("Настройки MixLauncher", "MixLauncher settings"), w!("Ctrl+,"), true);
    } else {
        cm_add(CM_ST_LOCK, 0xE72E, tr!("Заблокировать", "Lock"), w!(""), false);
        cm_add(CM_ST_SLEEP, 0xE708, tr!("Спящий режим", "Sleep"), w!(""), true);
        if hibernate_allowed() {
            cm_add(CM_ST_HIBERNATE, 0xE823, tr!("Гибернация", "Hibernate"), w!(""), false);
        }
        cm_add(CM_ST_SHUTDOWN, 0xE7E8, tr!("Завершение работы", "Shut down"), w!(""), false);
        cm_add(CM_ST_RESTART, 0xE777, tr!("Перезагрузка", "Restart"), w!(""), false);
    }
    cm_show(x, y, false);
}

unsafe fn st_footer_click(b: i32) {
    match b {
        FB_ACCOUNT | FB_POWER => st_menu_open(b == FB_ACCOUNT, U.st.fb[b as usize][0] as i32, U.st.fb[b as usize][1] as i32),
        FB_DOWNLOADS => st_open(w!("shell:Downloads")),
        FB_EXPLORER => st_open(w!("explorer.exe")),
        FB_SETTINGS => st_open(w!("ms-settings:")),
        _ => {}
    }
}

// Commands of the footer menus, from cm_run.
pub unsafe fn st_menu_pick(cmd: i32) {
    match cmd {
        CM_ST_ACCOUNT => st_open(w!("ms-settings:yourinfo")),
        CM_ST_LOCK => st_run(CMD_LOCK),
        CM_ST_SIGNOUT => st_run(CMD_SIGNOUT),
        CM_ST_MLSET => open_settings_from_launcher(),
        CM_ST_SLEEP => st_run(CMD_SLEEP),
        CM_ST_HIBERNATE => st_run(CMD_HIBERNATE),
        CM_ST_SHUTDOWN => st_run(CMD_SHUTDOWN),
        CM_ST_RESTART => st_run(CMD_RESTART),
        _ => {}
    }
}

// ---- input

unsafe fn st_footer_at(x: i32, y: i32) -> i32 {
    for i in 0..FB__COUNT {
        if in_box(&U.st.fb[i as usize], x as f32, y as f32) {
            return i;
        }
    }
    -1
}

unsafe fn st_title_at(x: i32, y: i32) -> bool {
    y as f32 >= U.list_y0 && (y as f32) < U.list_y1 && in_box(&U.st.title_btn, x as f32, y as f32)
}

// Hover over the footer buttons and the section button; true when the pointer is on one.
pub unsafe fn st_mouse_move(x: i32, y: i32) -> bool {
    if st_search() {
        let a = w10_act_at(x, y);
        if a != U.w10.act_hover {
            U.w10.act_hover = a;
            ui_invalidate();
        }
    }
    let f = st_footer_at(x, y);
    let t = st_title_at(x, y);
    if f != U.st.fb_hover || t != U.st.title_hover {
        U.st.fb_hover = f;
        U.st.title_hover = t;
        U.animating = true;
        ui_invalidate();
    }
    f >= 0 || t
}

pub unsafe fn st_mouse_down(x: i32, y: i32) -> bool {
    if st_search() {
        U.w10.act_press = w10_act_at(x, y);
        if x as f32 >= list_x1() && y as f32 >= U.list_y0 && (y as f32) < U.list_y1 {
            return true;
        }
    }
    U.st.fb_press = st_footer_at(x, y);
    U.st.title_press = st_title_at(x, y);
    U.st.fb_press >= 0 || U.st.title_press
}

pub unsafe fn st_mouse_up(x: i32, y: i32) -> bool {
    let f = U.st.fb_press;
    let ap = U.w10.act_press;
    let t = U.st.title_press;
    U.st.fb_press = -1;
    U.w10.act_press = -1;
    U.st.title_press = false;
    if ap >= 0 {
        if w10_act_at(x, y) == ap {
            w10_act_run(ap);
        }
        return true;
    }
    if f >= 0 {
        if st_footer_at(x, y) == f {
            st_footer_click(f);
        }
        return true;
    }
    if t {
        if st_title_at(x, y) {
            st_set_all_view(!U.st.all_view);
        }
        return true;
    }
    false
}

// Up and down in the grid go to the tile above or below, not the next one in the list.
unsafe fn st_move_vertical(dir: i32) {
    let r = *row(U.sel);
    let cx = r.x + r.w * 0.5;
    let mut line = 0.0;
    let mut bd = 1e9f32;
    let mut best = -1;
    let mut i = U.sel + dir;
    while i >= 0 && i < U.row_count {
        let c = *row(i);
        if !row_selectable(i) || c.y == r.y {
            i += dir;
            continue;
        }
        if best >= 0 && c.y != line {
            break;
        }
        line = c.y;
        let d = (c.x + c.w * 0.5 - cx).abs();
        if d < bd {
            bd = d;
            best = i;
        }
        i += dir;
    }
    if best < 0 {
        return;
    }
    U.sel = best;
    U.armed = null_mut();
    ensure_visible(best, true);
    ui_invalidate();
}

pub unsafe fn st_key(vk: WPARAM) -> bool {
    if vk as u16 == VK_ESCAPE && U.st.all_view && !q_active() && U.armed.is_null() {
        st_set_all_view(false);
        return true;
    }
    let r = sel_row();
    if r.is_null() || (*r).cell != CELL_TILE || q_active() {
        return false;
    }
    match vk as u16 {
        VK_LEFT | VK_RIGHT => {
            move_sel(if vk as u16 == VK_RIGHT { 1 } else { -1 });
            true
        }
        VK_UP | VK_DOWN => {
            st_move_vertical(if vk as u16 == VK_DOWN { 1 } else { -1 });
            true
        }
        _ => false,
    }
}

pub unsafe fn st_animate(dt: f32) -> bool {
    let st = &mut uu().st;
    let mut anim = approach(&mut st.title_hover_t, if st.title_hover { 1.0 } else { 0.0 }, 22.0, dt);
    for i in 0..FB__COUNT as usize {
        let target = if st.fb_hover == i as i32 { 1.0 } else { 0.0 };
        anim |= approach(&mut st.fb_hover_t[i], target, 22.0, dt);
    }
    anim
}

// ---- drawing

pub unsafe fn st_draw_chrome() {
    let t = &*(&raw const TH);
    U.st.title_btn = [0.0; 4]; // set again when the title is drawn, so not once it scrolls away
    // a dense acrylic close to the taskbar's color, as Start has; the page a shade lighter
    r_rect(0.0, 0.0, U.W as f32, U.H as f32, color_alpha(if t.dark { RGBA(38, 38, 39, 236) } else { RGBA(243, 243, 243, 236) }, t.veil), 0.0);
    r_rect(0.0, U.list_y0, U.W as f32, U.list_y1 - U.list_y0, if t.dark { RGBA(255, 255, 255, 6) } else { RGBA(255, 255, 255, 60) }, 0.0);
    let (mut fx0, mut fx1, mut cy) = (0.0, 0.0, 0.0);
    search_field(&mut fx0, &mut fx1, &mut cy);
    let bx = SR(32.0);
    let bh = SR(34.0);
    let by = (cy - bh * 0.5).floor();
    let bw = U.W as f32 - bx * 2.0;
    r_rect(bx, by, bw, bh, if t.dark { RGBA(0, 0, 0, 56) } else { RGBA(255, 255, 255, 200) }, bh * 0.5);
    r_border(bx, by, bw, bh, if t.dark { RGBA(255, 255, 255, 22) } else { RGBA(0, 0, 0, 26) }, bh * 0.5);
    draw_search();
}

pub unsafe fn st_draw_highlight(off: f32) {
    let r = *row(U.sel);
    let inn = if r.cell == CELL_TILE { SR(4.0) } else { 0.0 };
    let x = (U.hl_x + inn + 0.5).floor();
    let y = (off + U.hl_y + inn * 0.5 + 0.5).floor();
    let h = (U.hl_h - inn + 0.5).floor();
    r_rect(x, y, (U.hl_w - inn * 2.0 + 0.5).floor(), h, TH.sel, SR(LS().sel_r));
    if st_search() && r.cell == 0 {
        r_rect(x, (y + h * 0.5 - SR(8.0)).floor(), SR(3.0), SR(16.0), TH.accent, SR(1.5));
    }
}

// The page's title; the first one carries the "All apps" / "Back" button.
unsafe fn st_draw_title(r: *const Row, y: f32) {
    let t = &*(&raw const TH);
    let x0 = st_grid_x();
    let tw = SR(ST_TILE_W);
    let isz = ui_icon_px() as f32;
    let cy = y + SR(34.0);
    let fs = S(14.0);
    // aligned with the icons of the first and the last column
    let lx = (x0 + (tw - isz) * 0.5).floor();
    let rx = (x0 + tw * (ST_COLS - 1) as f32 + (tw + isz) * 0.5).floor();
    text_draw(FONT_TEXT_SEMIBOLD, fs, lx, text_baseline(FONT_TEXT_SEMIBOLD, fs, cy), (*r).text, -1, t.text);
    if r != U.rows as *const Row {
        return;
    }
    let back = U.st.all_view;
    let label = if back { tr!("Назад", "Back") } else { tr!("Все приложения", "All apps") };
    let bfs = S(12.5);
    let lw = text_width(FONT_TEXT, bfs, label, -1);
    let bh = SR(28.0);
    let bw = (SR(12.0) + lw + SR(8.0) + SR(10.0) + SR(10.0)).floor();
    let bx = rx + SR(8.0) - bw;
    let by = (cy - bh * 0.5).floor();
    let hov = U.st.title_hover_t * if U.st.title_press { 0.6 } else { 1.0 };
    // darker than the page, a little lighter under the pointer
    r_rect(bx, by, bw, bh, if t.dark { RGBA(0, 0, 0, (56.0 - 22.0 * hov) as i32 as u32) } else { RGBA(0, 0, 0, (14.0 - 5.0 * hov) as i32 as u32) }, SR(7.0));
    let base = text_baseline(FONT_TEXT, bfs, cy);
    if back {
        text_draw_icon(0xE76B, S(10.0), bx + SR(12.0) + SR(5.0), cy, t.text);
        text_draw(FONT_TEXT, bfs, bx + SR(12.0) + SR(10.0) + SR(8.0), base, label, -1, t.text);
    } else {
        text_draw(FONT_TEXT, bfs, bx + SR(12.0), base, label, -1, t.text);
        text_draw_icon(0xE76C, S(10.0), bx + SR(12.0) + lw + SR(8.0) + SR(5.0), cy, t.text);
    }
    box_set(&mut U.st.title_btn, bx, by, bw, bh);
}

// The name under a tile, centered: on two lines at a space when it does not fit, like Start; the rest is elided.
pub unsafe fn st_draw_label(r: *const Row, base: f32, s: *const WCHAR, len: i32, col: u32) {
    let r = &*r;
    let fs = S(12.0);
    let maxw = r.w - SR(12.0);
    let lh = SR(16.0);
    let mut cut = 0;
    if text_width(FONT_TEXT, fs, s, len) > maxw {
        let mut w = 0.0; // of the text before i
        let mut i = 0;
        while i < len {
            if *s.add(i as usize) == ' ' as u16 && i > 0 && w <= maxw {
                cut = i;
            }
            let cp = utf16_next(s, len, &mut i);
            w += (*glyph_map(FONT_TEXT, if cp == '\t' as u32 { ' ' as u32 } else { cp })).adv * fs;
        }
    }
    let mut s = s;
    let mut len = len;
    let mut base = base;
    if cut != 0 {
        let w1 = text_width(FONT_TEXT, fs, s, cut);
        text_draw(FONT_TEXT, fs, (r.x + (r.w - w1) * 0.5).floor(), base, s, cut, col);
        s = s.add(cut as usize + 1);
        len -= cut + 1;
        base += lh;
    }
    let w = min(text_width(FONT_TEXT, fs, s, len), maxw);
    text_draw_fit(FONT_TEXT, fs, (r.x + (r.w - w) * 0.5).floor(), base, maxw, s, len, col, false);
}

pub unsafe fn st_draw_cell(r: *const Row, y: f32, _selected: bool) {
    if (*r).cell == CELL_TITLE {
        st_draw_title(r, y);
        return;
    }
    if (*r).cell == CELL_HINT {
        let fs = S(13.0);
        let lx = (st_grid_x() + (SR(ST_TILE_W) - ui_icon_px() as f32) * 0.5).floor();
        text_draw(FONT_TEXT, fs, lx, text_baseline(FONT_TEXT, fs, y + SR(12.0)), (*r).text, -1, TH.dim);
        return;
    }
    let t = &*(&raw const TH);
    let a = &*(*r).app;
    let isz = ui_icon_px() as f32;
    let ix = ((*r).x + ((*r).w - isz) * 0.5).floor();
    let iy = y + SR(16.0);
    let armed = U.armed == (*r).app;
    if a.kind == APP_CMD {
        r_rect(ix, iy, isz, isz, if armed { color_alpha(t.danger, 0.16) } else { t.tile }, SR(LS().tile_r));
        text_draw_icon(a.glyph as u32, S(16.0), ix + isz * 0.5, iy + isz * 0.5, if armed { t.danger } else { t.text });
    } else {
        draw_app_icon(a, ix, iy, isz);
    }
    st_draw_label(r, text_baseline(FONT_TEXT, S(12.0), iy + isz + SR(17.0)), a.name, a.len, if armed { t.danger } else { t.text });
}

pub unsafe fn st_draw_avatar(x: f32, y: f32, sz: f32) {
    let mut uv = [0.0f32; 4];
    if STU.pic[0] != 0 && sz == ui_icon_px() as f32 && icon_get(STU.pic_key, ICON_KIND_AVATAR, STU.pic.as_ptr(), &mut uv) {
        r_icon(x, y, sz, sz, uv[0], uv[1], uv[2], uv[3], 1.0);
        return;
    }
    // the default Windows picture: a grey person in a light circle
    let bg = RGBA(222, 222, 222, 255);
    let fg = RGBA(140, 140, 140, 255);
    r_rect(x, y, sz, sz, bg, sz * 0.5);
    let hd = (sz * 0.34 + 0.5).floor();
    r_rect((x + (sz - hd) * 0.5 + 0.5).floor(), (y + sz * 0.2 + 0.5).floor(), hd, hd, fg, hd * 0.5);
    let bw = (sz * 0.54 + 0.5).floor();
    let bh = (sz * 0.3 + 0.5).floor();
    r_rect((x + (sz - bw) * 0.5 + 0.5).floor(), (y + sz * 0.6 + 0.5).floor(), bw, bh, fg, bw * 0.45);
}

pub unsafe fn st_draw_footer() {
    let t = &*(&raw const TH);
    st_user_init();
    let fy = U.list_y1;
    let cy = (fy + U.footer_h * 0.5).floor();
    r_rect(0.0, fy, U.W as f32, U.H as f32 - fy, if t.dark { RGBA(0, 0, 0, 26) } else { RGBA(0, 0, 0, 8) }, 0.0);
    r_rect(0.0, fy, U.W as f32, 1.0, t.sep, 0.0);
    U.settings_btn = [0.0; 4];

    let x0 = st_grid_x();
    let tw = SR(ST_TILE_W);
    let av = ui_icon_px() as f32;
    let ax = (x0 + (tw - av) * 0.5).floor();
    let nfs = S(13.0);
    let nw = if STU.name[0] != 0 { min(text_width(FONT_TEXT, nfs, STU.name.as_ptr(), -1), SR(200.0)) } else { 0.0 };
    let bh = SR(44.0);
    let by = (cy - bh * 0.5).floor();
    box_set(&mut U.st.fb[FB_ACCOUNT as usize], ax - SR(10.0), by, av + SR(20.0) + if nw > 0.0 { SR(12.0) + nw } else { 0.0 }, bh);
    // the buttons on the right, power last, under the last column
    let glyph: [u32; 5] = [0, 0xE896, 0xEC50, 0xE713, 0xE7E8];
    let bs = SR(38.0);
    let pitch = SR(40.0);
    let pcx = (x0 + tw * (ST_COLS as f32 - 0.5) + SR(8.0)).floor();
    for i in FB_DOWNLOADS..FB__COUNT {
        box_set(&mut U.st.fb[i as usize], (pcx - pitch * (FB_POWER - i) as f32 - bs * 0.5).floor(), (cy - bs * 0.5).floor(), bs, bs);
    }

    for i in 0..FB__COUNT as usize {
        let b = U.st.fb[i];
        let h = U.st.fb_hover_t[i] * if U.st.fb_press == i as i32 { 0.6 } else { 1.0 };
        if h > 0.0 {
            r_rect(b[0], b[1], b[2] - b[0], b[3] - b[1], color_alpha(t.sel, h * 1.4), SR(4.0));
        }
        if glyph[i] != 0 {
            text_draw_icon(glyph[i], S(16.0), (b[0] + b[2]) * 0.5, cy, t.text);
        }
    }
    st_draw_avatar(ax, (cy - av * 0.5).floor(), av);
    if nw > 0.0 {
        text_draw_fit(FONT_TEXT, nfs, ax + av + SR(12.0), text_baseline(FONT_TEXT, nfs, cy), nw, STU.name.as_ptr(), -1, t.text, false);
    }

    U.update_btn = [0.0; 4];
    if upd_visible() {
        // centered in the window, unless a long account name or the buttons on the right are in the way
        let uw = update_chip_width();
        let lo = U.st.fb[FB_ACCOUNT as usize][2] + SR(8.0);
        let hi = U.st.fb[FB_DOWNLOADS as usize][0] - SR(8.0) - uw;
        draw_update_chip(clamp((U.W as f32 - uw) * 0.5, lo, max(lo, hi)).floor(), cy, SR(28.0), SR(4.0));
    }
}

pub unsafe fn st_all_apps_rows_pub() {
    st_all_apps_rows();
}
