// config.ini: one table (k_cfg) gives every setting its key, type, default, range and comment.

enum { BACKDROP_BLUR, BACKDROP_ACRYLIC, BACKDROP_SOLID };

// STYLE_START and STYLE_WIN10 lay the launcher out like the Windows 11 and Windows 10 Start menus.
enum { STYLE_STANDARD, STYLE_RAYCAST, STYLE_FLUENT, STYLE_COMPACT, STYLE_START, STYLE_WIN10, STYLE__COUNT };
#define STYLES_START_MENU ((1 << STYLE_START) | (1 << STYLE_WIN10))  // always at the taskbar

enum { POS_CENTER, POS_BOTTOM_LEFT, POS_BOTTOM_CENTER, POS__COUNT };

enum { TBI_DIAMOND, TBI_GRID, TBI_PETALS, TBI_LAYERS, TBI_DOTS, TBI_CUSTOM, TBI__COUNT };

enum { HK_CTRL = 1, HK_ALT = 2, HK_SHIFT = 4, HK_WIN = 8 };
#define VK_ANYWIN 0x07  // unassigned VK code, used for "either Win key"

typedef struct Hotkey { u8 mods, vk; } Hotkey;

typedef struct Config {
    WCHAR hotkey[64];
    Hotkey hk;  // parsed from hotkey
    int   language;  // 0 auto, 1 ru, 2 en
    int   theme;     // 0 auto, 1 dark, 2 light
    int   backdrop;
    int   style;
    int   position;
    bool  match_highlight;
    int   width;
    int   rows;
    int   start10_width, start10_height;  // the Windows 10 style's size (logical px)
    int   max_apps;
    int   file_min_chars;
    WCHAR everything_filter[512];
    bool  hide_uninstallers;
    int   keep_query_seconds;
    bool  update_check;
    bool  taskbar_button;
    bool  taskbar_replace_icon;
    int   taskbar_icon;
    WCHAR taskbar_icon_path[MAX_PATH];
    bool  taskbar_search;
    bool  media_widget;
    int   media_position;  // 0 left, 1 right
    bool  media_controls;
    bool  media_hide_paused;
    bool  media_marquee;
    bool  reduced_motion;
    bool  animations;
    int   anim_speed;
    int   anim_open_ms, anim_close_ms;
    int   anim_open_scale;
    int   anim_cascade;
    int   anim_row_ms, anim_stagger_ms;
    int   anim_select_ms;
    int   anim_scroll_ms;
    int   anim_menu_ms;
    int   anim_page_ms;
} Config;

static Config g_cfg;

// Fields in config.ini order.
enum {
    CF_NONE,
    CF_HOTKEY, CF_LANGUAGE, CF_THEME, CF_BACKDROP, CF_STYLE, CF_POSITION, CF_MATCH_HIGHLIGHT, CF_WIDTH, CF_ROWS,
    CF_START10_WIDTH, CF_START10_HEIGHT, CF_MAX_APPS, CF_FILE_MIN_CHARS, CF_EVERYTHING_FILTER, CF_HIDE_UNINSTALLERS,
    CF_KEEP_QUERY, CF_UPDATE_CHECK, CF_TASKBAR_BUTTON, CF_TASKBAR_REPLACE_ICON, CF_TASKBAR_ICON, CF_TASKBAR_ICON_PATH,
    CF_TASKBAR_SEARCH, CF_MEDIA_WIDGET, CF_MEDIA_POSITION, CF_MEDIA_CONTROLS, CF_MEDIA_HIDE_PAUSED, CF_MEDIA_MARQUEE,
    CF_REDUCED_MOTION, CF_ANIMATIONS,
    CF_ANIM_SPEED, CF_ANIM_OPEN_MS, CF_ANIM_CLOSE_MS, CF_ANIM_OPEN_SCALE, CF_ANIM_CASCADE, CF_ANIM_ROW_MS, CF_ANIM_STAGGER_MS,
    CF_ANIM_SELECT_MS, CF_ANIM_SCROLL_MS, CF_ANIM_MENU_MS, CF_ANIM_PAGE_MS,
    CF__COUNT
};
#define CF_ANIM_FIRST CF_ANIM_SPEED
#define CF_ANIM_LAST CF_ANIM_PAGE_MS

enum { CK_BOOL, CK_INT, CK_ENUM, CK_TEXT };

typedef struct CfgField {
    const char *key;
    int kind;
    int off;                   // in Config
    int def, lo, hi;           // CK_INT: the range; CK_ENUM: hi names; CK_TEXT: hi WCHARs of room
    const char *const *names;  // CK_ENUM
    const WCHAR *text;         // CK_TEXT: the default
    const char *ru, *en;       // the comment above the key in config.ini, lines split by \n
    bool gap;                  // a blank line before it
} CfgField;

static const char *const k_lang_names[] = { "auto", "ru", "en" };
static const char *const k_theme_names[] = { "auto", "dark", "light" };
static const char *const k_backdrop_names[] = { "blur", "acrylic", "solid" };
static const char *const k_style_names[STYLE__COUNT] = { "standard", "raycast", "fluent", "compact", "windows", "windows10" };
static const char *const k_pos_names[POS__COUNT] = { "center", "bottom-left", "bottom-center" };
static const char *const k_tbi_names[TBI__COUNT] = { "diamond", "grid", "petals", "layers", "dots", "custom" };
static const char *const k_side_names[] = { "left", "right" };

#define CFG_B(id, field, d, ...) [id] = { #field, CK_BOOL, offsetof(Config, field), d, 0, 1, __VA_ARGS__ }
#define CFG_I(id, field, d, lo, hi, ...) [id] = { #field, CK_INT, offsetof(Config, field), d, lo, hi, __VA_ARGS__ }
#define CFG_E(id, field, d, names, ...) [id] = { #field, CK_ENUM, offsetof(Config, field), d, 0, countof(names), names, __VA_ARGS__ }
#define CFG_T(id, field, d, ...) [id] = { #field, CK_TEXT, offsetof(Config, field), 0, 0, countof(((Config *)0)->field), NULL, d, __VA_ARGS__ }

static const CfgField k_cfg[CF__COUNT] = {
    CFG_T(CF_HOTKEY, hotkey, L"win", .gap = 1,
          .ru = "Клавиша или сочетание, которое открывает и закрывает лаунчер. Прежнее действие сочетания\n"
                "отключается (Win больше не открывает «Пуск», Win+D не сворачивает окна).\n"
                "Примеры: win, alt+space, ctrl+space, win+d, f13, rctrl (одиночное нажатие правого Ctrl)",
          .en = "The key or shortcut that opens and closes the launcher. Whatever it did before is turned off\n"
                "(Win no longer opens Start, Win+D no longer shows the desktop).\n"
                "Examples: win, alt+space, ctrl+space, win+d, f13, rctrl (a tap of the right Ctrl key)"),
    CFG_E(CF_LANGUAGE, language, 0, k_lang_names, .gap = 1,
          .ru = "Язык: auto (как в Windows) | ru | en", .en = "Language: auto (follows Windows) | ru | en"),
    CFG_E(CF_THEME, theme, 0, k_theme_names, .gap = 1, .ru = "Тема: auto | dark | light", .en = "Theme: auto | dark | light"),
    CFG_E(CF_BACKDROP, backdrop, BACKDROP_BLUR, k_backdrop_names, .gap = 1,
          .ru = "Фон окна: blur (полупрозрачное стекло, как в macOS) | acrylic (системный акрил Windows 11) | solid",
          .en = "Window background: blur (translucent glass, macOS-like) | acrylic (Windows 11 system acrylic) | solid"),
    CFG_E(CF_STYLE, style, STYLE_STANDARD, k_style_names, .gap = 1,
          .ru = "Стиль лаунчера: standard | raycast | fluent | compact | windows | windows10 (как меню Пуск Windows 11 и 10)",
          .en = "Launcher style: standard | raycast | fluent | compact | windows | windows10 (like the Windows 11 and 10 Start menus)"),
    CFG_E(CF_POSITION, position, POS_CENTER, k_pos_names,
          .ru = "Положение: center (по центру экрана) | bottom-left (слева снизу, как Пуск) | bottom-center (снизу по центру).\n"
                "Стили windows и windows10 всегда снизу.",
          .en = "Position: center (screen center) | bottom-left (like Start on the left) | bottom-center (like Start in the center).\n"
                "The windows and windows10 styles are always at the bottom."),
    CFG_B(CF_MATCH_HIGHLIGHT, match_highlight, 1,
          .ru = "Подсвечивать совпадения с запросом в названиях (1/0)", .en = "Highlight the query matches in names (1/0)"),
    CFG_I(CF_WIDTH, width, 720, 480, 1600, .gap = 1,
          .ru = "Ширина окна в логических пикселях и количество видимых строк списка",
          .en = "Window width in logical pixels and number of visible list rows"),
    CFG_I(CF_ROWS, rows, 9, 4, 20),
    CFG_I(CF_START10_WIDTH, start10_width, 648, 300, 4000,
          .ru = "Размер стиля windows10 (меняется и перетаскиванием края окна)",
          .en = "Size of the windows10 style (also set by dragging the window edge)"),
    CFG_I(CF_START10_HEIGHT, start10_height, 608, 420, 2000),
    CFG_I(CF_MAX_APPS, max_apps, 8, 1, 50, .gap = 1,
          .ru = "Сколько приложений показывать при поиске (файлы из Everything подгружаются при прокрутке)",
          .en = "How many apps to show while searching (files from Everything load as you scroll)"),
    CFG_I(CF_FILE_MIN_CHARS, file_min_chars, 2, 1, 10, .gap = 1,
          .ru = "Минимальная длина запроса для поиска файлов", .en = "Minimum query length for file search"),
    CFG_T(CF_EVERYTHING_FILTER, everything_filter, L"", .gap = 1,
          .ru = "Фильтр Everything, добавляется к каждому запросу файлов. Пример: !C:\\Windows\\ !\\AppData\\",
          .en = "Everything filter appended to every file query. Example: !C:\\Windows\\ !\\AppData\\"),
    CFG_B(CF_HIDE_UNINSTALLERS, hide_uninstallers, 1, .gap = 1,
          .ru = "Скрывать деинсталляторы из списка приложений (1/0)", .en = "Hide uninstallers from the app list (1/0)"),
    CFG_I(CF_KEEP_QUERY, keep_query_seconds, 60, 0, 24 * 3600, .gap = 1,
          .ru = "Через сколько секунд после закрытия окна сбрасывать введённый запрос",
          .en = "Seconds after closing before the typed query is cleared"),
    CFG_B(CF_UPDATE_CHECK, update_check, 1, .gap = 1,
          .ru = "Проверять обновления на GitHub раз в несколько часов (1/0)", .en = "Check GitHub for updates every few hours (1/0)"),
    CFG_B(CF_TASKBAR_BUTTON, taskbar_button, 1, .gap = 1,
          .ru = "Кнопка Пуск на панели задач Windows 11 открывает лаунчер (1/0)",
          .en = "The Windows 11 taskbar Start button opens the launcher (1/0)"),
    CFG_B(CF_TASKBAR_REPLACE_ICON, taskbar_replace_icon, 1,
          .ru = "Рисовать на ней наш значок вместо значка Windows (1/0)", .en = "Draw our icon on it instead of the Windows logo (1/0)"),
    CFG_E(CF_TASKBAR_ICON, taskbar_icon, TBI_DIAMOND, k_tbi_names,
          .ru = "Её значок: diamond (ромб) | grid (сетка) | petals (лепестки) | layers (слои) | dots (точки) | "
                "custom (свой файл PNG/ICO/JPG из taskbar_icon_path)",
          .en = "Its icon: diamond | grid | petals | layers | dots | custom (a PNG/ICO/JPG file from taskbar_icon_path)"),
    CFG_T(CF_TASKBAR_ICON_PATH, taskbar_icon_path, L""),
    CFG_B(CF_TASKBAR_SEARCH, taskbar_search, 0,
          .ru = "Клик по поиску на панели задач Windows 10/11 открывает лаунчер (1/0)",
          .en = "Clicking the search on the Windows 10/11 taskbar opens the launcher (1/0)"),
    CFG_B(CF_MEDIA_WIDGET, media_widget, 1, .gap = 1,
          .ru = "«Сейчас играет» на панели задач: обложка, название и кнопки управления музыкой и видео (1/0)",
          .en = "Now playing on the taskbar: cover, title and playback buttons for music and video (1/0)"),
    CFG_E(CF_MEDIA_POSITION, media_position, 0, k_side_names,
          .ru = "Где: left (слева) | right (справа, у значков в трее). При значках, выровненных по левому краю, всегда справа",
          .en = "Where: left | right (next to the tray icons). Always right when taskbar icons are aligned left"),
    CFG_B(CF_MEDIA_CONTROLS, media_controls, 1,
          .ru = "Кнопки «назад», «пауза», «вперёд» (1/0) и скрывать виджет, пока ничего не играет (1/0)",
          .en = "Previous / pause / next buttons (1/0) and hiding the widget while nothing plays (1/0)"),
    CFG_B(CF_MEDIA_HIDE_PAUSED, media_hide_paused, 0),
    CFG_B(CF_MEDIA_MARQUEE, media_marquee, 1,
          .ru = "Прокручивать название и исполнителя, если они не помещаются (1/0)",
          .en = "Scroll the title and artist when they do not fit (1/0)"),
    CFG_B(CF_REDUCED_MOTION, reduced_motion, 0, .gap = 1,
          .ru = "Уменьшение движения (1/0): без анимаций и задержек, всё происходит сразу. Настройки анимаций ниже сохраняются.",
          .en = "Reduced motion (1/0): no animations or delays, everything happens at once. The animation settings below are kept."),
    CFG_B(CF_ANIMATIONS, animations, 1, .gap = 1,
          .ru = "Анимации открытия и закрытия (1/0). Выключаются и системной настройкой «Эффекты анимации».",
          .en = "Open/close animations (1/0). Also off when Windows \"Animation effects\" are disabled."),
    CFG_I(CF_ANIM_SPEED, anim_speed, 100, 30, 400, .gap = 1,
          .ru = "Тонкая настройка анимаций (миллисекунды). anim_speed - общая скорость в процентах (200 = вдвое быстрее),\n"
                "anim_open_scale - с какого масштаба (в процентах) проявляется окно, anim_cascade - каскад строк (1/0).\n"
                "Выделение и прокрутка движутся на пружине: значение - примерное время, за которое она доезжает.",
          .en = "Fine-tuning of animations (milliseconds). anim_speed is a global speed in percent (200 = twice as fast),\n"
                "anim_open_scale is the scale (percent) the window opens from, anim_cascade cascades the rows in (1/0).\n"
                "Selection and scrolling move on a spring: the value is roughly the time it takes to arrive."),
    CFG_I(CF_ANIM_OPEN_MS, anim_open_ms, 160, 0, 1000),
    CFG_I(CF_ANIM_CLOSE_MS, anim_close_ms, 90, 0, 1000),
    CFG_I(CF_ANIM_OPEN_SCALE, anim_open_scale, 97, 80, 100),
    CFG_I(CF_ANIM_CASCADE, anim_cascade, 1, 0, 1),
    CFG_I(CF_ANIM_ROW_MS, anim_row_ms, 200, 0, 1000),
    CFG_I(CF_ANIM_STAGGER_MS, anim_stagger_ms, 12, 0, 60),
    CFG_I(CF_ANIM_SELECT_MS, anim_select_ms, 120, 0, 600),
    CFG_I(CF_ANIM_SCROLL_MS, anim_scroll_ms, 160, 0, 600),
    CFG_I(CF_ANIM_MENU_MS, anim_menu_ms, 140, 0, 600),
    CFG_I(CF_ANIM_PAGE_MS, anim_page_ms, 220, 0, 800),
};

static void *cfg_ptr(int id) { return (char *)&g_cfg + k_cfg[id].off; }

// A bool, int or enum field as an int.
static int cfg_get(int id)
{
    if (k_cfg[id].kind == CK_BOOL) return *(bool *)cfg_ptr(id);
    if (k_cfg[id].kind == CK_TEXT) return 0;
    return *(int *)cfg_ptr(id);
}

static void cfg_put(int id, int v)
{
    const CfgField *f = &k_cfg[id];
    if (f->kind == CK_BOOL) *(bool *)cfg_ptr(id) = v != 0;
    else if (f->kind == CK_INT) *(int *)cfg_ptr(id) = CLAMP(v, f->lo, f->hi);
    else if (f->kind == CK_ENUM) *(int *)cfg_ptr(id) = CLAMP(v, 0, f->hi - 1);
}

static int cfg_find(const char *key)
{
    for (int id = 1; id < CF__COUNT; id++)
        if (!_stricmp(k_cfg[id].key, key)) return id;
    return CF_NONE;
}

static bool parse_bool(const char *v) { return v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T' || !_stricmp(v, "on"); }

// Sets a field from its config.ini text. An unknown enum name leaves the field as it is.
static void cfg_parse(int id, const char *v)
{
    const CfgField *f = &k_cfg[id];
    switch (f->kind) {
    case CK_BOOL: cfg_put(id, parse_bool(v)); break;
    case CK_INT: cfg_put(id, atoi(v)); break;
    case CK_ENUM:
        for (int i = 0; i < f->hi; i++)
            if (!_stricmp(v, f->names[i])) cfg_put(id, i);
        break;
    case CK_TEXT: utf8_to_w(v, -1, (WCHAR *)cfg_ptr(id), f->hi); break;
    }
}

static void cfg_format(int id, char *out, int cap)
{
    const CfgField *f = &k_cfg[id];
    int v = cfg_get(id);
    switch (f->kind) {
    case CK_BOOL:
    case CK_INT: snprintf(out, (size_t)cap, "%d", v); break;
    case CK_ENUM: snprintf(out, (size_t)cap, "%s", f->names[CLAMP(v, 0, f->hi - 1)]); break;
    case CK_TEXT: w_to_utf8((const WCHAR *)cfg_ptr(id), -1, out, cap); break;
    }
}

// The Start layouts always sit at the taskbar: center falls back to bottom center.
static int launcher_position(void)
{
    int p = CLAMP(g_cfg.position, 0, POS__COUNT - 1);
    return ((STYLES_START_MENU >> g_cfg.style) & 1) && p == POS_CENTER ? POS_BOTTOM_CENTER : p;
}

// ---- keys and hotkeys: "ctrl+alt+k", "win", "rctrl"

typedef struct KeyName { u8 vk; const char *name; const WCHAR *label; } KeyName;

static const KeyName k_key_names[] = {
    { VK_ANYWIN, "win", L"Win" }, { VK_CONTROL, "ctrl", L"Ctrl" }, { VK_MENU, "alt", L"Alt" }, { VK_SHIFT, "shift", L"Shift" },
    { VK_LWIN, "lwin", L"Win" }, { VK_RWIN, "rwin", L"Win" }, { VK_LCONTROL, "lctrl", L"Ctrl" }, { VK_RCONTROL, "rctrl", L"Ctrl" },
    { VK_LMENU, "lalt", L"Alt" }, { VK_RMENU, "ralt", L"Alt" }, { VK_LSHIFT, "lshift", L"Shift" }, { VK_RSHIFT, "rshift", L"Shift" },
    { VK_SPACE, "space", L"Space" }, { VK_RETURN, "enter", L"Enter" }, { VK_TAB, "tab", L"Tab" }, { VK_ESCAPE, "esc", L"Esc" },
    { VK_BACK, "backspace", L"Backspace" }, { VK_DELETE, "delete", L"Del" }, { VK_INSERT, "insert", L"Ins" },
    { VK_HOME, "home", L"Home" }, { VK_END, "end", L"End" }, { VK_PRIOR, "pageup", L"PgUp" }, { VK_NEXT, "pagedown", L"PgDn" },
    { VK_UP, "up", L"\x2191" }, { VK_DOWN, "down", L"\x2193" }, { VK_LEFT, "left", L"\x2190" }, { VK_RIGHT, "right", L"\x2192" },
    { VK_CAPITAL, "capslock", L"Caps Lock" }, { VK_PAUSE, "pause", L"Pause" }, { VK_SCROLL, "scrolllock", L"Scroll Lock" },
    { VK_SNAPSHOT, "printscreen", L"PrtSc" }, { VK_APPS, "menu", L"Menu" }, { VK_NUMLOCK, "numlock", L"Num Lock" },
    { VK_OEM_3, "`", L"`" }, { VK_OEM_MINUS, "-", L"-" }, { VK_OEM_PLUS, "=", L"=" }, { VK_OEM_4, "[", L"[" },
    { VK_OEM_6, "]", L"]" }, { VK_OEM_5, "\\", L"\\" }, { VK_OEM_1, ";", L";" }, { VK_OEM_7, "'", L"'" },
    { VK_OEM_COMMA, ",", L"," }, { VK_OEM_PERIOD, ".", L"." }, { VK_OEM_2, "/", L"/" },
    { VK_MULTIPLY, "num*", L"Num *" }, { VK_ADD, "numplus", L"Num +" }, { VK_SUBTRACT, "num-", L"Num -" },
    { VK_DECIMAL, "num.", L"Num ." }, { VK_DIVIDE, "num/", L"Num /" },
};

static const KeyName *key_entry(u32 vk)
{
    for (int i = 0; i < countof(k_key_names); i++)
        if (k_key_names[i].vk == vk) return &k_key_names[i];
    return NULL;
}

static u8 vk_mod_bit(u32 vk)
{
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return HK_CTRL;
    case VK_MENU: case VK_LMENU: case VK_RMENU: return HK_ALT;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return HK_SHIFT;
    case VK_ANYWIN: case VK_LWIN: case VK_RWIN: return HK_WIN;
    }
    return 0;
}

// The bound key matches the pressed one; a generic modifier matches either side.
static bool hk_vk_match(u32 bind, u32 vk)
{
    if (bind == vk) return true;
    switch (bind) {
    case VK_CONTROL: return vk == VK_LCONTROL || vk == VK_RCONTROL;
    case VK_MENU: return vk == VK_LMENU || vk == VK_RMENU;
    case VK_SHIFT: return vk == VK_LSHIFT || vk == VK_RSHIFT;
    case VK_ANYWIN: return vk == VK_LWIN || vk == VK_RWIN;
    }
    return false;
}

static int key_by_name(const char *name)
{
    for (int i = 0; i < countof(k_key_names); i++)
        if (!strcmp(k_key_names[i].name, name)) return k_key_names[i].vk;
    size_t n = strlen(name);
    if (n == 1 && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9'))) return toupper((u8)name[0]);
    if (name[0] == 'f' && n >= 2 && n <= 3 && isdigit((u8)name[1])) {
        int f = atoi(name + 1);
        if (f >= 1 && f <= 24) return VK_F1 + f - 1;
    }
    if (!strncmp(name, "num", 3) && n == 4 && isdigit((u8)name[3])) return VK_NUMPAD0 + (name[3] - '0');
    if (!strncmp(name, "vk", 2) && n == 4) {
        int v = (int)strtol(name + 2, NULL, 16);
        if (v > 0 && v < 0xFF) return v;
    }
    if (!strcmp(name, "control")) return VK_CONTROL;
    if (!strcmp(name, "escape")) return VK_ESCAPE;
    if (!strcmp(name, "tilde")) return VK_OEM_3;
    if (!strcmp(name, "del")) return VK_DELETE;
    return 0;
}

static void key_name(u32 vk, char *out, size_t cap)
{
    const KeyName *k = key_entry(vk);
    if (k) snprintf(out, cap, "%s", k->name);
    else if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) snprintf(out, cap, "%c", (char)tolower((int)vk));
    else if (vk >= VK_F1 && vk <= VK_F24) snprintf(out, cap, "f%u", vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) snprintf(out, cap, "num%u", vk - VK_NUMPAD0);
    else snprintf(out, cap, "vk%02x", vk & 0xFF);
}

// What a keycap shows; *side is "L" or "R" for a sided modifier.
static void key_label(u32 vk, WCHAR *out, int cap, const WCHAR **side)
{
    *side = NULL;
    if (vk == VK_LCONTROL || vk == VK_LMENU || vk == VK_LSHIFT || vk == VK_LWIN) *side = L"L";
    if (vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_RSHIFT || vk == VK_RWIN) *side = L"R";
    const KeyName *k = key_entry(vk);
    if (k) {
        wcopy(out, cap, k->label);
    } else if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        wfmt(out, cap, L"%c", (WCHAR)vk);
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        wfmt(out, cap, L"F%u", vk - VK_F1 + 1);
    } else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        wfmt(out, cap, L"Num %u", vk - VK_NUMPAD0);
    } else {
        UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
        LONG lp = (LONG)((sc & 0xFF) << 16) | ((sc & 0xE000) ? (1 << 24) : 0);
        if (!sc || GetKeyNameTextW(lp, out, cap) <= 0) wfmt(out, cap, L"VK %02X", vk & 0xFF);
    }
    out[cap - 1] = 0;
}

static bool hotkey_parse(const WCHAR *s, Hotkey *out)
{
    char buf[128];
    w_to_utf8(s, -1, buf, sizeof buf);
    for (char *p = buf; *p; p++) *p = (char)tolower((u8)*p);
    Hotkey h = { 0, 0 };
    int last = 0;
    char *cur = buf;
    for (;;) {
        while (*cur == ' ' || *cur == '+') cur++;
        if (!*cur) break;
        char *tok = cur;
        while (*cur && *cur != '+' && *cur != ' ') cur++;
        char save = *cur;
        *cur = 0;
        int vk = key_by_name(tok);
        *cur = save;
        if (!vk) return false;
        if (last) {  // every key but the last is a modifier
            u8 m = vk_mod_bit((u32)last);
            if (!m) return false;
            h.mods |= m;
        }
        last = vk;
    }
    if (!last) return false;
    h.vk = (u8)last;
    h.mods &= (u8)~vk_mod_bit((u32)last);
    *out = h;
    return true;
}

static void hotkey_format(Hotkey h, WCHAR *out, int cap)
{
    char buf[96], key[24];
    key_name(h.vk, key, sizeof key);
    snprintf(buf, sizeof buf, "%s%s%s%s%s", h.mods & HK_CTRL ? "ctrl+" : "", h.mods & HK_ALT ? "alt+" : "", h.mods & HK_SHIFT ? "shift+" : "",
             h.mods & HK_WIN ? "win+" : "", key);
    utf8_to_w(buf, -1, out, cap);
}

// ---- config.ini

static void config_defaults(void)
{
    memset(&g_cfg, 0, sizeof g_cfg);
    for (int id = 1; id < CF__COUNT; id++) {
        if (k_cfg[id].kind == CK_TEXT) wcopy((WCHAR *)cfg_ptr(id), k_cfg[id].hi, k_cfg[id].text);
        else cfg_put(id, k_cfg[id].def);
    }
    g_cfg.hk.vk = VK_ANYWIN;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static void config_path(WCHAR *out) { data_path(out, L"config.ini"); }

static void buf_comment(Buf *b, const char *text)
{
    for (const char *p = text; *p;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        buf_str(b, "; ");
        buf_put(b, p, n);
        buf_str(b, "\r\n");
        p += n + (e ? 1 : 0);
    }
}

// Writes the whole file, with comments, in the UI language.
static void config_write_all(void)
{
    Buf b = { 0 };
    buf_put(&b, "\xEF\xBB\xBF", 3);
    buf_comment(&b, g_lang_ru ? "MixLauncher - настройки. Удобнее менять в окне настроек (кнопка «Настройки» в лаунчере или Ctrl+,)."
                              : "MixLauncher settings. Easier to change in the settings window (Settings button in the launcher or Ctrl+,).");
    for (int id = 1; id < CF__COUNT; id++) {
        const CfgField *f = &k_cfg[id];
        char val[1100];
        cfg_format(id, val, sizeof val);
        if (f->gap) buf_str(&b, "\r\n");
        if (f->ru) buf_comment(&b, g_lang_ru ? f->ru : f->en);
        buf_printf(&b, val[0] ? "%s = %s\r\n" : "%s =%s\r\n", f->key, val);
    }
    WCHAR path[MAX_PATH];
    config_path(path);
    buf_save(&b, path);
}

static void lang_apply(void)
{
    g_lang_ru = g_cfg.language == 1 || (g_cfg.language == 0 && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN);
}

static void config_load(void)
{
    config_defaults();
    WCHAR path[MAX_PATH];
    config_path(path);
    char *text = read_file(path, NULL);
    if (!text) {
        // First run: reduced motion follows the Windows "Animation effects" setting.
        BOOL anim = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
        g_cfg.reduced_motion = !anim;
        config_write_all();
        return;
    }
    int old_win_key = -1;
    char *cur = utf8_bom(text) ? text + 3 : text, *line;
    while ((line = next_line(&cur)) != NULL) {
        line = trim(line);
        if (!*line || *line == ';' || *line == '#' || *line == '[') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        int id = cfg_find(k);
        if (id) cfg_parse(id, v);
        else if (!_stricmp(k, "win_key")) old_win_key = parse_bool(v);  // before hotkey existed
    }
    free(text);
    lang_apply();
    if (old_win_key == 1) wcopy(g_cfg.hotkey, countof(g_cfg.hotkey), L"win");
    if (!hotkey_parse(g_cfg.hotkey, &g_cfg.hk) || !g_cfg.hk.vk) {
        wcopy(g_cfg.hotkey, countof(g_cfg.hotkey), L"win");
        g_cfg.hk.mods = 0;
        g_cfg.hk.vk = VK_ANYWIN;
    }
    if (old_win_key >= 0) config_write_all();
}

// Rewrites one key in the file, keeping everything else (comments, the user's own lines) as it is.
static void config_set(const char *key, const char *value)
{
    WCHAR path[MAX_PATH];
    config_path(path);
    char *text = read_file(path, NULL);
    Buf b = { 0 };
    bool found = false;
    size_t kl = strlen(key);
    char *cur = text, *line;
    while (text && (line = next_line(&cur)) != NULL) {
        if (utf8_bom(line)) {
            buf_put(&b, line, 3);
            line += 3;
        }
        char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (!found && !_strnicmp(t, key, kl) && (t[kl] == ' ' || t[kl] == '=' || t[kl] == '\t')) {
            buf_printf(&b, "%s = %s\r\n", key, value);
            found = true;
        } else {
            buf_str(&b, line);
            buf_str(&b, "\r\n");
        }
    }
    free(text);
    if (!found) buf_printf(&b, "%s = %s\r\n", key, value);
    buf_save(&b, path);
}

static void cfg_save(int id)
{
    char val[1100];
    cfg_format(id, val, sizeof val);
    config_set(k_cfg[id].key, val);
}

static void cfg_set(int id, int v)
{
    cfg_put(id, v);
    cfg_save(id);
}
