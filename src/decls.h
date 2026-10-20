#pragma once

// Functions called from modules included before the one that defines them (see the include order in main.c).

struct Row;

// taskbar.c, media.c
static bool tb_hit(POINT pt);
static bool tb_search_hit(POINT pt);
static void tb_set_enabled(bool on);
static void tb_set_search(bool on);
static void tb_icon_changed(void);
static void tb_set_open(HMONITOR mon);
static void media_set_enabled(bool on);
static void media_settings_changed(void);
// settings.c, settings_wnd.c, popmenu.c
static void settings_open(void);
static void settings_open_at(int id);
static void settings_device_lost(void);
static void settings_device_restored(void);
static void settings_apply_theme(void);
static void settings_invalidate(void);
static void pm_close(void);
// the launcher's context menu and tag dialog (ctxmenu.c, tagdlg.c), drawn and animated by ui_draw.c
static bool cm_animate(f32 dt);
static void cm_draw(void);
static void cm_close(bool animate);
static bool tg_animate(f32 dt);
static void tg_draw(void);
static void tg_open(void);
static void open_settings_from_launcher(void);
// the Windows 11 style, start.c
static bool st_on(void);
static bool st_search(void);
static void st_build_home(void);
static void st_refresh_tiles(void);
static void st_metrics(void);
static void st_place_rows(void);
static void st_draw_chrome(void);
static void st_draw_footer(void);
static void st_draw_cell(struct Row *r, f32 y, bool selected);
static void st_draw_highlight(f32 off);
static bool st_animate(f32 dt);
static void st_pins_changed(void);
static void st_menu_pick(int cmd);
static int pin_index(const WCHAR *id);
static int pin_size(int i);
static void pin_set_size(const WCHAR *id, int size);
static void pin_set(const WCHAR *id, bool on);
static void pin_to_top(const WCHAR *id);
// the Windows 10 style, start10.c
static bool w10_on(void);
static void w10_metrics(void);
static void w10_fit_size(void);
static void w10_build_home(void);
static void w10_refresh_tiles(void);
static void w10_pins_freeze(void);
static void w10_place_rows(void);
static void w10_draw_chrome(void);
static void w10_draw_overlay(void);
static void w10_draw_preview(void);
static bool w10_animate(f32 dt);
static int w10_act_at(int x, int y);
static void w10_act_run(int i);
static f32 list_x0(void);
static f32 list_x1(void);
