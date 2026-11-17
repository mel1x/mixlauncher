// The launcher's context menu on a result row.

use crate::*;

pub const CM_OPEN: i32 = 1;
pub const CM_REVEAL: i32 = 2;
pub const CM_RUNAS: i32 = 3;
pub const CM_COPY: i32 = 4;
pub const CM_PROPS: i32 = 5;
pub const CM_TAG_ADD: i32 = 6;
pub const CM_TAG_CLEAR: i32 = 7;
pub const CM_TAG_INFO: i32 = 8;
pub const CM_PIN: i32 = 9;
pub const CM_UNPIN: i32 = 10;
pub const CM_PIN_TOP: i32 = 11;
// the Start style's account and power menus
pub const CM_ST_ACCOUNT: i32 = 12;
pub const CM_ST_LOCK: i32 = 13;
pub const CM_ST_SIGNOUT: i32 = 14;
pub const CM_ST_MLSET: i32 = 15;
pub const CM_ST_SLEEP: i32 = 16;
pub const CM_ST_HIBERNATE: i32 = 17;
pub const CM_ST_SHUTDOWN: i32 = 18;
pub const CM_ST_RESTART: i32 = 19;
// Windows 10 tile sizes, in the order of TILE_*
pub const CM_SIZE_SMALL: i32 = 20;
pub const CM_SIZE_MEDIUM: i32 = 21;
pub const CM_SIZE_WIDE: i32 = 22;
pub const CM_SIZE_LARGE: i32 = 23;
pub const CM_ITEM_H: f32 = 32.0;
pub const CM_PAD: f32 = 5.0;
pub const CM_SEP_H: f32 = 9.0;

pub unsafe fn cm_close(animate: bool) {
    if !U.cm.open {
        return;
    }
    U.cm.press = -1;
    if animate && anims_enabled() && anim_sec(g_cfg.anim_menu_ms) > 0.0 {
        U.cm.closing = true;
        U.animating = true;
    } else {
        U.cm.open = false;
        U.cm.closing = false;
    }
    ui_invalidate();
}

pub unsafe fn cm_add(cmd: i32, glyph: u32, label: *const WCHAR, keys: *const WCHAR, sep: bool) {
    if U.cm.n >= 16 {
        return;
    }
    let it = &mut U.cm.it[U.cm.n as usize];
    it.cmd = cmd;
    it.glyph = glyph;
    it.label = label;
    it.keys = keys;
    it.sep = sep;
    it.disabled = false;
    U.cm.n += 1;
}

// What a row's tags are stored under: the app id or the full path.
pub unsafe fn row_tag_key(r: *const Row, type_: &mut u8) -> *const WCHAR {
    if r.is_null() {
        return null();
    }
    if (*r).kind == ROW_FILE {
        *type_ = file_item_type((*r).file);
        return (*(*r).file).full;
    }
    if (*r).kind == ROW_APP {
        *type_ = app_item_type((*r).app);
        return (*(*r).app).id;
    }
    null()
}

// Tags changed: rows may point into what was freed, so everything is searched and laid out again at once.
pub unsafe fn tags_changed() {
    search_apps();
    search_tags();
    rebuild_rows(true);
    snap_highlight();
    ui_invalidate();
}

// The menu for the selected row, at x, y (keyboard: its first item already hovered).
pub unsafe fn ui_context_menu(x: i32, y: i32, keyboard: bool) {
    let r = sel_row();
    if r.is_null() {
        return;
    }
    let is_cmd = (*r).kind == ROW_APP && (*(*r).app).kind == APP_CMD;
    let path = row_path(r);
    U.cm.n = 0;
    cm_add(CM_OPEN, if is_cmd { 0xE768 } else { 0xE8A7 }, if is_cmd { tr!("Выполнить", "Run") } else { tr!("Открыть", "Open") }, w!("Enter"), false);
    if !is_cmd {
        if !path.is_null() {
            cm_add(CM_REVEAL, 0xE838, tr!("Показать в папке", "Show in folder"), w!("Ctrl+Enter"), false);
        }
        cm_add(CM_RUNAS, 0xE7EF, tr!("Запустить от имени администратора", "Run as administrator"), w!("Ctrl+Shift+Enter"), false);
        cm_add(CM_COPY, 0xE8C8, if !path.is_null() { tr!("Копировать путь", "Copy path") } else { tr!("Копировать имя", "Copy name") }, w!("Ctrl+C"), true);
        if !path.is_null() {
            cm_add(CM_PROPS, 0xE946, tr!("Свойства", "Properties"), w!("Alt+Enter"), false);
        }
    }
    if (st_on() || w10_on()) && (*r).kind == ROW_APP {
        let pi = pin_index((*(*r).app).id);
        if pi < 0 {
            cm_add(CM_PIN, 0xE718, tr!("Закрепить на начальном экране", "Pin to Start"), w!(""), true);
        } else {
            cm_add(CM_UNPIN, 0xE77A, tr!("Открепить от начального экрана", "Unpin from Start"), w!(""), true);
            if pi > 0 && !w10_on() {
                cm_add(CM_PIN_TOP, 0xE74A, tr!("Переместить в начало", "Move to front"), w!(""), false); // Windows 10 tiles are dragged
            }
            if w10_on() {
                let names = [tr!("Мелкая плитка", "Small tile"), tr!("Средняя плитка", "Medium tile"), tr!("Широкая плитка", "Wide tile"), tr!("Крупная плитка", "Large tile")];
                let cur = pin_size(pi);
                for k in 0..TILE__COUNT as i32 {
                    cm_add(CM_SIZE_SMALL + k, if k == cur { 0xE73E } else { 0 }, names[k as usize], w!(""), k == 0);
                }
            }
        }
    }
    let mut ttype = 0u8;
    let tkey = row_tag_key(r, &mut ttype);
    if !tkey.is_null() {
        cm_add(CM_TAG_ADD, 0xE8EC, tr!("Добавить тег", "Add tag"), w!(""), true);
        let ti = tags_find(ttype, tkey, 0);
        if !ti.is_null() && (*ti).ntags != 0 {
            cm_add(CM_TAG_CLEAR, 0xE74D, tr!("Удалить все теги", "Remove all tags"), w!(""), false);
            tags_count_label(U.cm.tag_count.as_mut_ptr(), 32, (*ti).ntags);
            // the tags themselves go to the shortcut column, cut to a sane width
            let mut n = 0i32;
            let cap = 40;
            U.cm.tag_list[0] = 0;
            let mut i = 0;
            while i < (*ti).ntags && n < cap {
                let tl = U.cm.tag_list.as_mut_ptr().add(n as usize);
                let add = wfmt!(tl, 96 - n, "{}{}", if i != 0 { ", " } else { "" }, Ws((*(*ti).tags.add(i as usize)).text));
                if add < 0 {
                    break;
                }
                n += add;
                i += 1;
            }
            U.cm.tag_list[95] = 0;
            if n > cap {
                wcopy(U.cm.tag_list.as_mut_ptr().add(cap as usize), 96 - cap, w!("…"));
            }
            cm_add(CM_TAG_INFO, 0, U.cm.tag_count.as_ptr(), U.cm.tag_list.as_ptr(), false);
            U.cm.it[U.cm.n as usize - 1].disabled = true;
        }
    }
    cm_show(x, y, keyboard);
}

// Lays out the items added since U.cm.n = 0 and opens the menu at x, y (above it when there is no room below).
pub unsafe fn cm_show(x: i32, y: i32, keyboard: bool) {
    let reopen = U.cm.open && !U.cm.closing;
    let fs = S(13.5);
    let kfs = S(12.0);
    let mut lw: f32 = 0.0;
    let mut kw: f32 = 0.0;
    for i in 0..U.cm.n as usize {
        lw = max(lw, text_width(FONT_TEXT, fs, U.cm.it[i].label, -1));
        kw = max(kw, text_width(FONT_TEXT, kfs, U.cm.it[i].keys, -1));
    }
    let mut h = SR(CM_PAD);
    let mut w = (SR(CM_PAD) + SR(12.0) + SR(16.0) + SR(12.0) + lw + SR(32.0) + kw + SR(12.0) + SR(CM_PAD)).floor();
    for i in 0..U.cm.n as usize {
        if U.cm.it[i].sep {
            h += SR(CM_SEP_H);
        }
        U.cm.it[i].y = h;
        h += SR(CM_ITEM_H);
    }
    h += SR(CM_PAD);
    let m = SR(8.0);
    w = min(w, U.W as f32 - 2.0 * m);
    let px = clamp(x as f32, m, U.W as f32 - w - m);
    let mut py = y as f32 + SR(4.0);
    if py + h > U.H as f32 - m {
        py = max(m, y as f32 - SR(4.0) - h);
    }
    U.cm.x = px.floor();
    U.cm.y = py.floor();
    U.cm.w = w;
    U.cm.h = h;
    U.cm.hover = if keyboard { 0 } else { -1 };
    U.cm.press = -1;
    U.cm.hl_init = false;
    if !reopen || U.cm.t <= 0.0 {
        U.cm.t = if anims_enabled() { 0.0 } else { 1.0 };
    }
    if reopen {
        U.cm.t = min(U.cm.t, 0.35);
    }
    U.cm.open = true;
    U.cm.closing = false;
    U.animating = true;
    ui_invalidate();
}

pub unsafe fn cm_item_at(x: i32, y: i32) -> i32 {
    if !U.cm.open || U.cm.closing {
        return -1;
    }
    let fx = x as f32 - U.cm.x;
    let fy = y as f32 - U.cm.y;
    if fx < 0.0 || fx >= U.cm.w || fy < 0.0 || fy >= U.cm.h {
        return -1;
    }
    for i in 0..U.cm.n as usize {
        if fy >= U.cm.it[i].y && fy < U.cm.it[i].y + SR(CM_ITEM_H) {
            return i as i32;
        }
    }
    -1
}

pub unsafe fn cm_inside(x: i32, y: i32) -> bool {
    U.cm.open && x as f32 >= U.cm.x && (x as f32) < U.cm.x + U.cm.w && y as f32 >= U.cm.y && (y as f32) < U.cm.y + U.cm.h
}

pub unsafe fn cm_run(i: i32) {
    if i < 0 || i >= U.cm.n || U.cm.it[i as usize].disabled {
        return;
    }
    let cmd = U.cm.it[i as usize].cmd;
    U.cm.open = false;
    U.cm.closing = false;
    ui_invalidate();
    match cmd {
        CM_OPEN => ui_activate(ACT_OPEN),
        CM_REVEAL => ui_activate(ACT_REVEAL),
        CM_RUNAS => ui_activate(ACT_RUNAS),
        CM_COPY => ui_copy_path(),
        CM_PROPS => ui_activate(ACT_PROPERTIES),
        CM_TAG_ADD => tg_open(),
        CM_TAG_CLEAR => {
            let mut type_ = 0u8;
            let key = row_tag_key(sel_row(), &mut type_);
            if !key.is_null() {
                tags_clear(type_, key); // frees what tagged rows point to: rebuild right away
                tags_changed();
            }
        }
        CM_PIN | CM_UNPIN | CM_PIN_TOP => {
            let r = sel_row();
            if r.is_null() || (*r).kind != ROW_APP {
                return;
            }
            if cmd == CM_PIN_TOP {
                pin_to_top((*(*r).app).id);
            } else {
                pin_set((*(*r).app).id, cmd == CM_PIN);
            }
            st_pins_changed();
        }
        CM_SIZE_SMALL | CM_SIZE_MEDIUM | CM_SIZE_WIDE | CM_SIZE_LARGE => {
            let r = sel_row();
            if !r.is_null() && (*r).kind == ROW_APP {
                pin_set_size((*(*r).app).id, cmd - CM_SIZE_SMALL);
            }
            st_pins_changed();
        }
        _ => st_menu_pick(cmd),
    }
}

pub unsafe fn cm_animate(dt: f32) -> bool {
    if !U.cm.open {
        return false;
    }
    let mut anim = false;
    let dur = anim_sec(g_cfg.anim_menu_ms);
    if U.cm.closing {
        U.cm.t = if dur > 0.0 { U.cm.t - dt / (dur * 0.6) } else { 0.0 };
        if U.cm.t <= 0.0 {
            U.cm.t = 0.0;
            U.cm.open = false;
            U.cm.closing = false;
            return true;
        }
        anim = true;
    } else if U.cm.t < 1.0 {
        U.cm.t = if dur > 0.0 { min(1.0, U.cm.t + dt / dur) } else { 1.0 };
        anim = true;
    }
    if U.cm.hover >= 0 {
        let ty = U.cm.it[U.cm.hover as usize].y;
        if !U.cm.hl_init {
            U.cm.hl_y = ty;
            U.cm.hl_v = 0.0;
            U.cm.hl_init = true;
        }
        let cm = &mut uu().cm;
        anim |= spring_step(&mut cm.hl_y, &mut cm.hl_v, ty, anim_omega(g_cfg.anim_select_ms), dt);
    }
    anim
}

pub unsafe fn cm_draw() {
    if !U.cm.open {
        return;
    }
    let t = &*(&raw const TH);
    let e = if U.cm.closing { U.cm.t } else { ease_out_cubic(U.cm.t) };
    let x = U.cm.x;
    let y = U.cm.y - ((1.0 - e) * SR(6.0) + 0.5).floor();
    let w = U.cm.w;
    let h = U.cm.h;
    let rad = SR(10.0);
    R.opacity = e;
    r_clip_all();
    r_panel(
        x,
        y,
        w,
        h,
        rad,
        if t.dark { RGBA(40, 40, 43, 252) } else { RGBA(252, 252, 253, 252) },
        if t.dark { RGBA(255, 255, 255, 20) } else { RGBA(0, 0, 0, 22) },
        if t.dark { RGBA(0, 0, 0, 110) } else { RGBA(0, 0, 0, 40) },
        SR(8.0),
        SR(18.0),
    );
    let ix = x + SR(CM_PAD);
    let iw = w - SR(CM_PAD) * 2.0;
    if U.cm.hover >= 0 && U.cm.hl_init {
        let hy = (y + U.cm.hl_y + 0.5).floor();
        r_rect(ix, hy, iw, SR(CM_ITEM_H), t.sel, SR(6.0));
    }
    let fs = S(13.5);
    let kfs = S(12.0);
    for i in 0..U.cm.n as usize {
        let it = U.cm.it[i];
        let iy = y + it.y;
        let cy = iy + SR(CM_ITEM_H) * 0.5;
        if it.sep {
            r_rect(ix + SR(8.0), (iy - SR(CM_SEP_H) * 0.5).floor(), iw - SR(16.0), 1.0, t.sep, 0.0);
        }
        let hot = i as i32 == U.cm.hover;
        let off = it.disabled;
        if it.glyph != 0 {
            text_draw_icon(it.glyph, S(14.0), ix + SR(12.0) + SR(8.0), cy, if hot { t.text } else { t.dim });
        }
        let lx = ix + SR(12.0) + SR(16.0) + SR(12.0);
        text_draw(FONT_TEXT, fs, lx, text_baseline(FONT_TEXT, fs, cy), it.label, -1, if off { t.faint } else { t.text });
        let kw = text_width(FONT_TEXT, kfs, it.keys, -1);
        text_draw(FONT_TEXT, kfs, (ix + iw - SR(12.0) - kw).floor(), text_baseline(FONT_TEXT, kfs, cy), it.keys, -1, t.faint);
    }
    R.opacity = 1.0;
}

// Keys while the menu is open; any other key closes it and goes on to the launcher.
pub unsafe fn cm_key(vk: WPARAM) -> bool {
    if !U.cm.open || U.cm.closing {
        return false;
    }
    match vk as u16 {
        VK_UP | VK_DOWN => {
            let d = if vk as u16 == VK_DOWN { 1 } else { -1 };
            let mut h = U.cm.hover;
            for _ in 0..U.cm.n {
                h = if h < 0 {
                    if d > 0 { 0 } else { U.cm.n - 1 }
                } else {
                    (h + d + U.cm.n) % U.cm.n
                };
                if !U.cm.it[h as usize].disabled {
                    break;
                }
            }
            U.cm.hover = h;
            U.animating = true;
            ui_invalidate();
            return true;
        }
        VK_RETURN | VK_SPACE => {
            if U.cm.hover >= 0 {
                cm_run(U.cm.hover);
            } else {
                cm_close(true);
            }
            return true;
        }
        VK_ESCAPE | VK_APPS | VK_LEFT => {
            cm_close(true);
            return true;
        }
        VK_TAB => return true,
        _ => {}
    }
    cm_close(true);
    false
}
