// Launcher keyboard and mouse input.

use crate::*;

pub unsafe fn open_settings_from_launcher() {
    U.no_focus_restore = true;
    U.settings_after_hide = true;
    ui_hide();
    if !U.visible && U.settings_after_hide {
        U.settings_after_hide = false;
        settings_open();
    }
}

// Keys go to the tag dialog, the menu and the Start styles first; true when handled.
pub unsafe fn ui_keydown(vk: WPARAM) -> bool {
    if tg_key(vk) {
        return true;
    }
    if cm_key(vk) {
        return true;
    }
    if st_on() && st_key(vk) {
        return true;
    }
    if w10_on() && w10_key(vk) {
        return true;
    }
    let ctrl = GetKeyState(VK_CONTROL as i32) < 0;
    let shift = GetKeyState(VK_SHIFT as i32) < 0;
    let alt = GetKeyState(VK_MENU as i32) < 0;
    let page = max(1, g_cfg.rows - 1);
    match vk as u16 {
        VK_ESCAPE => {
            if !U.armed.is_null() {
                U.armed = null_mut();
                ui_invalidate();
            } else if U.qlen != 0 {
                edit_replace(0, U.qlen, w!(""), 0);
            } else {
                ui_hide();
            }
            return true;
        }
        VK_RETURN => {
            ui_activate(if alt {
                ACT_PROPERTIES
            } else if ctrl && shift {
                ACT_RUNAS
            } else if ctrl {
                ACT_REVEAL
            } else {
                ACT_OPEN
            });
            return true;
        }
        VK_DOWN => {
            move_sel(1);
            return true;
        }
        VK_UP => {
            move_sel(-1);
            return true;
        }
        VK_NEXT => {
            move_sel(page);
            return true;
        }
        VK_PRIOR => {
            move_sel(-page);
            return true;
        }
        VK_TAB => {
            jump_section(if shift { -1 } else { 1 });
            return true;
        }
        VK_HOME => {
            if ctrl {
                move_sel(-100000);
            } else {
                edit_move(0, shift);
            }
            return true;
        }
        VK_END => {
            if ctrl {
                move_sel(100000);
            } else {
                edit_move(U.qlen, shift);
            }
            return true;
        }
        VK_LEFT => {
            if !shift && U.anchor != U.caret {
                edit_move(min(U.anchor, U.caret), false);
            } else {
                edit_move(if ctrl { word_left(U.caret) } else { char_left(U.caret) }, shift);
            }
            return true;
        }
        VK_RIGHT => {
            if !shift && U.anchor != U.caret {
                edit_move(max(U.anchor, U.caret), false);
            } else {
                edit_move(if ctrl { word_right(U.caret) } else { char_right(U.caret) }, shift);
            }
            return true;
        }
        VK_BACK => {
            if U.anchor != U.caret {
                edit_replace(U.anchor, U.caret, w!(""), 0);
            } else if U.caret > 0 {
                edit_replace(if ctrl { word_left(U.caret) } else { char_left(U.caret) }, U.caret, w!(""), 0);
            }
            return true;
        }
        VK_DELETE => {
            if U.anchor != U.caret {
                edit_replace(U.anchor, U.caret, w!(""), 0);
            } else if U.caret < U.qlen {
                edit_replace(U.caret, if ctrl { word_right(U.caret) } else { char_right(U.caret) }, w!(""), 0);
            }
            return true;
        }
        VK_F4 => {
            if alt {
                ui_hide();
                return true;
            }
        }
        VK_APPS => {
            let r = sel_row();
            if !r.is_null() {
                let off = U.list_y0 - U.scroll;
                ui_context_menu(if (*r).cell != 0 { ((*r).x + SR(8.0)) as i32 } else { SR(56.0) as i32 }, (off + (*r).y + (*r).h) as i32, true);
            }
            return true;
        }
        _ => {}
    }
    if ctrl && !alt {
        match vk as u16 {
            0x41 /*A*/ => {
                U.anchor = 0;
                U.caret = U.qlen;
                ui_invalidate();
                return true;
            }
            0x43 /*C*/ => {
                if U.anchor != U.caret {
                    clipboard_set(U.q.as_ptr().add(min(U.anchor, U.caret) as usize), (U.caret - U.anchor).abs());
                } else {
                    ui_copy_path();
                }
                return true;
            }
            0x58 /*X*/ => {
                if U.anchor != U.caret {
                    clipboard_set(U.q.as_ptr().add(min(U.anchor, U.caret) as usize), (U.caret - U.anchor).abs());
                    edit_replace(U.anchor, U.caret, w!(""), 0);
                }
                return true;
            }
            0x56 /*V*/ => {
                clipboard_paste();
                return true;
            }
            0x4E | 0x4A /*N J*/ => {
                move_sel(1);
                return true;
            }
            0x50 | 0x4B /*P K*/ => {
                move_sel(-1);
                return true;
            }
            VK_OEM_COMMA => {
                open_settings_from_launcher();
                return true;
            }
            _ => {}
        }
    }
    false
}

pub unsafe fn ui_char(c: WCHAR) {
    if U.tg.open {
        tg_char(c);
        return;
    }
    if c < 0x20 || c == 0x7F {
        return;
    }
    if U.cm.open && !U.cm.closing {
        return;
    }
    let cc = c;
    edit_replace(U.anchor, U.caret, &cc, 1);
}

unsafe fn in_update_button(x: i32, y: i32) -> bool {
    in_box(&U.update_btn, x as f32, y as f32)
}
unsafe fn in_settings_button(x: i32, y: i32) -> bool {
    in_box(&U.settings_btn, x as f32, y as f32)
}

// The pointer goes to what is on top: the tag dialog, the menu, a resize, the update chip, the Start styles,
// then the scrollbar, the settings button, the query and the rows.
pub unsafe fn ui_mouse_move(x: i32, y: i32) {
    if x == U.mouse_x && y == U.mouse_y {
        return;
    }
    U.mouse_x = x;
    U.mouse_y = y;
    if U.tg.open {
        let h = if U.tg.closing { -1 } else { tg_btn_at(x, y) };
        if h != U.tg.hover {
            U.tg.hover = h;
            ui_invalidate();
        }
        if U.tg.press == 2 {
            U.tg.caret = tg_index_at(x);
            caret_reset();
        }
        return;
    }
    if U.cm.open && !U.cm.closing {
        let h = cm_item_at(x, y);
        if h >= 0 && h != U.cm.hover && !U.cm.it[h as usize].disabled {
            U.cm.hover = h;
            U.animating = true;
            ui_invalidate();
        }
        return;
    }
    if U.w10.drag != 0 {
        w10_drag_move();
        return;
    }
    // the update chip can sit over the Windows 10 tiles or search bar: before their handlers
    let mut hover = in_update_button(x, y);
    if hover != U.update_hover {
        U.update_hover = hover;
        U.animating = true;
        ui_invalidate();
    }
    if st_on() {
        st_mouse_move(x, y);
    }
    if w10_on() && w10_mouse_move(x, y) {
        return;
    }
    let sb_hot = x >= list_x1() as i32 - SR(14.0) as i32 && x < list_x1() as i32 && y as f32 >= U.list_y0 && (y as f32) < U.list_y1;
    if sb_hot != U.sb_hot {
        U.sb_hot = sb_hot;
        U.animating = true;
        ui_invalidate();
    }
    hover = in_settings_button(x, y);
    if hover != U.settings_hover {
        U.settings_hover = hover;
        U.animating = true;
        ui_invalidate();
    }
    if U.text_drag {
        edit_move(text_index_at(x), true);
        return;
    }
    let r = row_at(x, y);
    if row_selectable(r) && r != U.sel {
        U.sel = r;
        U.armed = null_mut();
        ui_invalidate();
    }
}

pub unsafe fn ui_mouse_down(x: i32, y: i32, dbl: bool) {
    if U.tg.open {
        if U.tg.closing {
            return;
        }
        U.tg.press = tg_btn_at(x, y);
        if U.tg.press < 0 && in_box(&U.tg.field, x as f32, y as f32) {
            U.tg.press = 2; // placing the caret, and dragging it
            U.tg.caret = tg_index_at(x);
            SetCapture(g_hwnd);
            caret_reset();
        } else if U.tg.press < 0 && !in_box(&U.tg.panel, x as f32, y as f32) {
            tg_close(false);
        }
        return;
    }
    if U.cm.open {
        U.cm.press = cm_item_at(x, y);
        if !cm_inside(x, y) {
            cm_close(true);
        }
        return;
    }
    if cap_on() && cap_outside(x, y) {
        // a click past the panels is a click outside the launcher
        ui_hide();
        return;
    }
    U.press_update = in_update_button(x, y); // before the search bar and the Windows 10 tiles it can sit over
    if U.press_update {
        return;
    }
    if w10_on() && w10_drag_begin(x, y) {
        return;
    }
    let (mut sy0, mut sy1) = (0.0, 0.0);
    search_band(&mut sy0, &mut sy1);
    if y as f32 >= sy0 && (y as f32) < sy1 {
        if dbl {
            U.anchor = 0;
            U.caret = U.qlen;
            ui_invalidate();
            return;
        }
        edit_move(text_index_at(x), GetKeyState(VK_SHIFT as i32) < 0);
        U.text_drag = true;
        SetCapture(g_hwnd);
        return;
    }
    if st_on() && st_mouse_down(x, y) {
        return;
    }
    if w10_on() && w10_mouse_down(x, y) {
        return;
    }
    U.press_settings = in_settings_button(x, y);
    if U.press_settings {
        return;
    }
    let r = row_at(x, y);
    U.press_row = if row_selectable(r) { r } else { -1 };
    if U.press_row >= 0 {
        U.sel = r;
        ui_invalidate();
    }
}

pub unsafe fn ui_mouse_up(x: i32, y: i32) {
    if U.tg.open || U.tg.press >= 0 {
        let p = U.tg.press;
        let i = tg_btn_at(x, y);
        U.tg.press = -1;
        if p == 2 {
            ReleaseCapture();
        }
        if U.tg.open && !U.tg.closing && i >= 0 && i == p {
            if i == 0 {
                tg_close(false);
            } else if tg_can_save_pub() {
                tg_close(true);
            }
        }
        return;
    }
    if U.cm.open || U.cm.press >= 0 {
        let i = cm_item_at(x, y);
        if i >= 0 && i == U.cm.press {
            cm_run(i);
        }
        U.cm.press = -1;
        U.press_row = -1;
        return;
    }
    if U.text_drag {
        U.text_drag = false;
        ReleaseCapture();
        return;
    }
    if U.w10.drag != 0 {
        w10_drag_end();
        return;
    }
    if U.press_update {
        U.press_update = false;
        if in_update_button(x, y) {
            upd_download();
        }
        ui_invalidate();
        return;
    }
    if st_on() && st_mouse_up(x, y) {
        return;
    }
    if w10_on() && w10_mouse_up(x, y) {
        return;
    }
    if U.press_settings {
        U.press_settings = false;
        if in_settings_button(x, y) {
            open_settings_from_launcher();
        }
        return;
    }
    let r = row_at(x, y);
    if r >= 0 && r == U.press_row {
        U.sel = r;
        ui_activate(ACT_OPEN);
    }
    U.press_row = -1;
}

pub unsafe fn ui_wheel(delta: i32) {
    if U.tg.open {
        return;
    }
    if U.cm.open {
        cm_close(true);
        return;
    }
    if w10_on() && w10_wheel(delta) {
        return;
    }
    U.scroll_target -= delta as f32 / WHEEL_DELTA as f32 * SR(ROW_H()) * 3.0;
    U.scroll_target = clamp(U.scroll_target, 0.0, max_scroll());
    U.animating = true;
    ui_invalidate();
}
