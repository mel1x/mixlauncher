// Launcher keyboard and mouse input.

static void open_settings_from_launcher(void)
{
    U.no_focus_restore = true;
    U.settings_after_hide = true;
    ui_hide();
    if (!U.visible && U.settings_after_hide) {
        U.settings_after_hide = false;
        settings_open();
    }
}

// Keys go to the tag dialog, the menu and the Start styles first; true when handled.
static bool ui_keydown(WPARAM vk)
{
    if (tg_key(vk)) return true;
    if (cm_key(vk)) return true;
    if (st_on() && st_key(vk)) return true;
    if (w10_on() && w10_key(vk)) return true;
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
            ui_context_menu(r->cell ? (int)(r->x + SR(8)) : (int)SR(56), (int)(off + r->y + r->h), true);
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

static bool in_update_button(int x, int y) { return in_box(U.update_btn, (f32)x, (f32)y); }
static bool in_settings_button(int x, int y) { return in_box(U.settings_btn, (f32)x, (f32)y); }

// The pointer goes to what is on top: the tag dialog, the menu, a resize, the update chip, the Start styles,
// then the scrollbar, the settings button, the query and the rows.
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
            caret_reset();
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
    if (U.w10.drag) {
        w10_drag_move();
        return;
    }
    // the update chip can sit over the Windows 10 tiles or search bar: before their handlers
    bool hover = in_update_button(x, y);
    if (hover != U.update_hover) {
        U.update_hover = hover;
        U.animating = true;
        ui_invalidate();
    }
    if (st_on()) st_mouse_move(x, y);
    if (w10_on() && w10_mouse_move(x, y)) return;
    bool sb_hot = x >= (int)list_x1() - (int)SR(14) && x < (int)list_x1() && y >= U.list_y0 && y < U.list_y1;
    if (sb_hot != U.sb_hot) {
        U.sb_hot = sb_hot;
        U.animating = true;
        ui_invalidate();
    }
    hover = in_settings_button(x, y);
    if (hover != U.settings_hover) {
        U.settings_hover = hover;
        U.animating = true;
        ui_invalidate();
    }
    if (U.text_drag) {
        edit_move(text_index_at(x), true);
        return;
    }
    int r = row_at(x, y);
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
        if (U.tg.press < 0 && in_box(U.tg.field, (f32)x, (f32)y)) {
            U.tg.press = 2;  // placing the caret, and dragging it
            U.tg.caret = tg_index_at(x);
            SetCapture(g_hwnd);
            caret_reset();
        } else if (U.tg.press < 0 && !in_box(U.tg.panel, (f32)x, (f32)y)) {
            tg_close(false);
        }
        return;
    }
    if (U.cm.open) {
        U.cm.press = cm_item_at(x, y);
        if (!cm_inside(x, y)) cm_close(true);
        return;
    }
    U.press_update = in_update_button(x, y);  // before the search bar and the Windows 10 tiles it can sit over
    if (U.press_update) return;
    if (w10_on() && w10_drag_begin(x, y)) return;
    f32 sy0, sy1;
    search_band(&sy0, &sy1);
    if ((f32)y >= sy0 && (f32)y < sy1) {
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
    if (st_on() && st_mouse_down(x, y)) return;
    if (w10_on() && w10_mouse_down(x, y)) return;
    U.press_settings = in_settings_button(x, y);
    if (U.press_settings) return;
    int r = row_at(x, y);
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
    if (U.w10.drag) {
        w10_drag_end();
        return;
    }
    if (U.press_update) {
        U.press_update = false;
        if (in_update_button(x, y)) upd_download();
        ui_invalidate();
        return;
    }
    if (st_on() && st_mouse_up(x, y)) return;
    if (w10_on() && w10_mouse_up(x, y)) return;
    if (U.press_settings) {
        U.press_settings = false;
        if (in_settings_button(x, y)) open_settings_from_launcher();
        return;
    }
    int r = row_at(x, y);
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
    if (w10_on() && w10_wheel(delta)) return;
    U.scroll_target -= (f32)delta / WHEEL_DELTA * SR(ROW_H) * 3.f;
    U.scroll_target = CLAMP(U.scroll_target, 0.f, max_scroll());
    U.animating = true;
    ui_invalidate();
}
