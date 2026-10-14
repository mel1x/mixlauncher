#pragma once

// Functions called from modules included before the one that defines them (see the include order in main.c).

static bool tb_hit(POINT pt);
static void tb_set_enabled(bool on);
static void tb_icon_changed(void);
static void media_set_enabled(bool on);
static void media_settings_changed(void);
static void settings_open(void);
static void settings_device_lost(void);
static void settings_device_restored(void);
static void pm_close(void);
static void settings_apply_theme(void);
// launcher context menu and tag dialog, drawn and animated by ui.c
static bool cm_animate(f32 dt);
static void cm_draw(void);
static void cm_close(bool animate);
static bool tg_animate(f32 dt);
static void tg_draw(void);
static void tg_open(void);
