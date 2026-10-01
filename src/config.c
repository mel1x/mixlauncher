// config.c — tiny ini reader for %LOCALAPPDATA%\MixLauncher\config.ini.

enum { BACKDROP_SOLID, BACKDROP_ACRYLIC, BACKDROP_BLUR };

// Icon of our Start button (taskbar.c).
enum { TBI_DIAMOND, TBI_GRID, TBI_LINES, TBI_CUSTOM, TBI__COUNT };
static const char *const k_tbi_names[TBI__COUNT] = { "diamond", "grid", "lines", "custom" };

enum { HK_CTRL = 1, HK_ALT = 2, HK_SHIFT = 4, HK_WIN = 8 };
#define VK_ANYWIN 0x07  // unassigned VK code, used for "either Win key"

typedef struct Hotkey { u8 mods, vk; } Hotkey;

typedef struct Config {
    WCHAR hotkey[64];
    Hotkey hk;              // parsed hotkey
    int   theme;            // 0 auto, 1 dark, 2 light
    int   backdrop;         // BACKDROP_*
    int   width;
    int   rows;
    int   max_apps;
    int   file_min_chars;
    WCHAR everything_filter[512];
    bool  hide_uninstallers;
    int   keep_query_seconds;
    bool  animations;
    bool  taskbar_button;
    int   taskbar_icon;     // TBI_*
    WCHAR taskbar_icon_path[MAX_PATH];  // TBI_CUSTOM: PNG/ICO/JPG
    bool  media_widget;     // now playing on the taskbar (media.c)
    int   media_position;   // 0 left, 1 right
    bool  media_controls;
    bool  media_hide_paused;
    int   anim_speed;
    int   anim_open_ms, anim_close_ms;
    int   anim_open_scale;  // content scale the window opens from, percent
    int   anim_cascade;     // rows cascade in after opening (1/0)
    int   anim_row_ms, anim_stagger_ms;
    int   anim_select_ms;   // selection highlight travel (spring settle time)
    int   anim_scroll_ms;
    int   anim_menu_ms;     // context menu / dropdown entrance
    int   anim_page_ms;     // settings page switch
} Config;

static Config g_cfg;

static const char k_default_config_ru[] =
    "; MixLauncher — настройки. Удобнее менять в окне настроек (кнопка «Настройки» в лаунчере или Ctrl+,).\r\n"
    "\r\n"
    "; Клавиша или сочетание, которое открывает и закрывает лаунчер. Прежнее действие сочетания\r\n"
    "; отключается (Win больше не открывает «Пуск», Win+D не сворачивает окна).\r\n"
    "; Примеры: win, alt+space, ctrl+space, win+d, f13, rctrl (одиночное нажатие правого Ctrl)\r\n"
    "hotkey = win\r\n"
    "\r\n"
    "; Тема: auto | dark | light\r\n"
    "theme = auto\r\n"
    "\r\n"
    "; Фон окна: blur (полупрозрачное стекло, как в macOS) | acrylic (системный акрил Windows 11) | solid\r\n"
    "backdrop = blur\r\n"
    "\r\n"
    "; Ширина окна в логических пикселях и количество видимых строк списка\r\n"
    "width = 720\r\n"
    "rows = 9\r\n"
    "\r\n"
    "; Сколько приложений показывать при поиске (файлы из Everything подгружаются при прокрутке)\r\n"
    "max_apps = 8\r\n"
    "\r\n"
    "; Минимальная длина запроса для поиска файлов\r\n"
    "file_min_chars = 2\r\n"
    "\r\n"
    "; Фильтр Everything, добавляется к каждому запросу файлов. Пример: !C:\\Windows\\ !\\AppData\\\r\n"
    "everything_filter =\r\n"
    "\r\n"
    "; Скрывать деинсталляторы из списка приложений (1/0)\r\n"
    "hide_uninstallers = 1\r\n"
    "\r\n"
    "; Через сколько секунд после закрытия окна сбрасывать введённый запрос\r\n"
    "keep_query_seconds = 60\r\n"
    "\r\n"
    "; Кнопка Пуск на панели задач Windows 11 с нашим значком открывает лаунчер (1/0)\r\n"
    "taskbar_button = 1\r\n"
    "; Её значок: diamond (ромб) | grid (сетка) | lines (строки) | custom (свой файл PNG/ICO/JPG из taskbar_icon_path)\r\n"
    "taskbar_icon = diamond\r\n"
    "taskbar_icon_path =\r\n"
    "\r\n"
    "; «Сейчас играет» на панели задач: обложка, название и кнопки управления музыкой и видео (1/0)\r\n"
    "media_widget = 1\r\n"
    "; Где: left (слева) | right (справа, у значков в трее). При значках, выровненных по левому краю, всегда справа\r\n"
    "media_position = left\r\n"
    "; Кнопки «назад», «пауза», «вперёд» (1/0) и скрывать виджет, пока ничего не играет (1/0)\r\n"
    "media_controls = 1\r\n"
    "media_hide_paused = 0\r\n"
    "\r\n"
    "; Анимации открытия и закрытия (1/0). Выключаются и системной настройкой «Эффекты анимации».\r\n"
    "animations = 1\r\n"
    "\r\n"
    "; Тонкая настройка анимаций (миллисекунды). anim_speed — общая скорость в процентах (200 = вдвое быстрее),\r\n"
    "; anim_open_scale — с какого масштаба (в процентах) проявляется окно, anim_cascade — каскад строк (1/0).\r\n"
    "; Выделение и прокрутка движутся на пружине: значение — примерное время, за которое она доезжает.\r\n"
    "anim_speed = 100\r\n"
    "anim_open_ms = 160\r\n"
    "anim_close_ms = 90\r\n"
    "anim_open_scale = 97\r\n"
    "anim_cascade = 1\r\n"
    "anim_row_ms = 200\r\n"
    "anim_stagger_ms = 12\r\n"
    "anim_select_ms = 120\r\n"
    "anim_scroll_ms = 160\r\n"
    "anim_menu_ms = 140\r\n"
    "anim_page_ms = 220\r\n";

static const char k_default_config_en[] =
    "; MixLauncher settings. Easier to change in the settings window (Settings button in the launcher or Ctrl+,).\r\n"
    "\r\n"
    "; The key or shortcut that opens and closes the launcher. Whatever it did before is turned off\r\n"
    "; (Win no longer opens Start, Win+D no longer shows the desktop).\r\n"
    "; Examples: win, alt+space, ctrl+space, win+d, f13, rctrl (a tap of the right Ctrl key)\r\n"
    "hotkey = win\r\n"
    "\r\n"
    "; Theme: auto | dark | light\r\n"
    "theme = auto\r\n"
    "\r\n"
    "; Window background: blur (translucent glass, macOS-like) | acrylic (Windows 11 system acrylic) | solid\r\n"
    "backdrop = blur\r\n"
    "\r\n"
    "; Window width in logical pixels and number of visible list rows\r\n"
    "width = 720\r\n"
    "rows = 9\r\n"
    "\r\n"
    "; How many apps to show while searching (files from Everything load as you scroll)\r\n"
    "max_apps = 8\r\n"
    "\r\n"
    "; Minimum query length for file search\r\n"
    "file_min_chars = 2\r\n"
    "\r\n"
    "; Everything filter appended to every file query. Example: !C:\\Windows\\ !\\AppData\\\r\n"
    "everything_filter =\r\n"
    "\r\n"
    "; Hide uninstallers from the app list (1/0)\r\n"
    "hide_uninstallers = 1\r\n"
    "\r\n"
    "; Seconds after closing before the typed query is cleared\r\n"
    "keep_query_seconds = 60\r\n"
    "\r\n"
    "; Our icon on the Windows 11 taskbar Start button, which then opens the launcher (1/0)\r\n"
    "taskbar_button = 1\r\n"
    "; Its icon: diamond | grid | lines | custom (a PNG/ICO/JPG file from taskbar_icon_path)\r\n"
    "taskbar_icon = diamond\r\n"
    "taskbar_icon_path =\r\n"
    "\r\n"
    "; Now playing on the taskbar: cover, title and playback buttons for music and video (1/0)\r\n"
    "media_widget = 1\r\n"
    "; Where: left | right (next to the tray icons). Always right when taskbar icons are aligned left\r\n"
    "media_position = left\r\n"
    "; Previous / pause / next buttons (1/0) and hiding the widget while nothing plays (1/0)\r\n"
    "media_controls = 1\r\n"
    "media_hide_paused = 0\r\n"
    "\r\n"
    "; Open/close animations (1/0). Also off when Windows \"Animation effects\" are disabled.\r\n"
    "animations = 1\r\n"
    "\r\n"
    "; Fine-tuning of animations (milliseconds). anim_speed is a global speed in percent (200 = twice as fast),\r\n"
    "; anim_open_scale is the scale (percent) the window opens from, anim_cascade cascades the rows in (1/0).\r\n"
    "; Selection and scrolling move on a spring: the value is roughly the time it takes to arrive.\r\n"
    "anim_speed = 100\r\n"
    "anim_open_ms = 160\r\n"
    "anim_close_ms = 90\r\n"
    "anim_open_scale = 97\r\n"
    "anim_cascade = 1\r\n"
    "anim_row_ms = 200\r\n"
    "anim_stagger_ms = 12\r\n"
    "anim_select_ms = 120\r\n"
    "anim_scroll_ms = 160\r\n"
    "anim_menu_ms = 140\r\n"
    "anim_page_ms = 220\r\n";

// Hotkey names

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

// Does a physical (left/right specific) key match the hotkey key?
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
    // Older spellings.
    if (!strcmp(name, "control")) return VK_CONTROL;
    if (!strcmp(name, "escape")) return VK_ESCAPE;
    if (!strcmp(name, "tilde")) return VK_OEM_3;
    if (!strcmp(name, "del")) return VK_DELETE;
    return 0;
}

static void key_name(u32 vk, char *out, size_t cap)
{
    for (int i = 0; i < countof(k_key_names); i++)
        if (k_key_names[i].vk == vk) {
            snprintf(out, cap, "%s", k_key_names[i].name);
            return;
        }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) snprintf(out, cap, "%c", (char)tolower((int)vk));
    else if (vk >= VK_F1 && vk <= VK_F24) snprintf(out, cap, "f%u", vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) snprintf(out, cap, "num%u", vk - VK_NUMPAD0);
    else snprintf(out, cap, "vk%02x", vk & 0xFF);
}

static void key_label(u32 vk, WCHAR *out, int cap, const WCHAR **side)
{
    *side = NULL;
    if (vk == VK_LCONTROL || vk == VK_LMENU || vk == VK_LSHIFT || vk == VK_LWIN) *side = L"L";
    if (vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_RSHIFT || vk == VK_RWIN) *side = L"R";
    for (int i = 0; i < countof(k_key_names); i++)
        if (k_key_names[i].vk == vk) {
            wcopy(out, cap, k_key_names[i].label);
            return;
        }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) _snwprintf(out, cap, L"%c", (WCHAR)vk);
    else if (vk >= VK_F1 && vk <= VK_F24) _snwprintf(out, cap, L"F%u", vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) _snwprintf(out, cap, L"Num %u", vk - VK_NUMPAD0);
    else {
        // Media, browser and other rare keys: ask the keyboard layout.
        UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
        LONG lp = (LONG)((sc & 0xFF) << 16) | ((sc & 0xE000) ? (1 << 24) : 0);
        if (!sc || GetKeyNameTextW(lp, out, cap) <= 0) _snwprintf(out, cap, L"VK %02X", vk & 0xFF);
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
        if (last) {  // not the last token after all: must be a modifier
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
    char buf[96] = "", key[24];
    if (h.mods & HK_CTRL) strcat(buf, "ctrl+");
    if (h.mods & HK_ALT) strcat(buf, "alt+");
    if (h.mods & HK_SHIFT) strcat(buf, "shift+");
    if (h.mods & HK_WIN) strcat(buf, "win+");
    key_name(h.vk, key, sizeof key);
    strcat(buf, key);
    utf8_to_w(buf, -1, out, cap);
}

static void config_anim_defaults(Config *c)
{
    c->anim_speed = 100;
    c->anim_open_ms = 160;
    c->anim_close_ms = 90;
    c->anim_open_scale = 97;
    c->anim_cascade = 1;
    c->anim_row_ms = 200;
    c->anim_stagger_ms = 12;
    c->anim_select_ms = 120;
    c->anim_scroll_ms = 160;
    c->anim_menu_ms = 140;
    c->anim_page_ms = 220;
}

static void config_defaults(Config *c)
{
    memset(c, 0, sizeof(*c));
    wcopy(c->hotkey, countof(c->hotkey), L"win");
    c->hk.vk = VK_ANYWIN;
    c->theme = 0;
    c->backdrop = BACKDROP_BLUR;
    c->width = 720;
    c->rows = 9;
    c->max_apps = 8;
    c->file_min_chars = 2;
    c->hide_uninstallers = true;
    c->keep_query_seconds = 60;
    c->animations = true;
    c->taskbar_button = true;
    c->taskbar_icon = 0;
    c->media_widget = true;
    c->media_controls = true;
    config_anim_defaults(c);
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static bool parse_bool(const char *v) { return v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T' || !_stricmp(v, "on"); }

static void config_path(WCHAR *out) { data_path(out, L"config.ini"); }

static bool config_value(const char *k, char *out, size_t cap)
{
    if (!strcmp(k, "hotkey")) w_to_utf8(g_cfg.hotkey, -1, out, (int)cap);
    else if (!strcmp(k, "theme")) snprintf(out, cap, "%s", g_cfg.theme == 1 ? "dark" : g_cfg.theme == 2 ? "light" : "auto");
    else if (!strcmp(k, "backdrop")) snprintf(out, cap, "%s", g_cfg.backdrop == BACKDROP_SOLID ? "solid" : g_cfg.backdrop == BACKDROP_ACRYLIC ? "acrylic" : "blur");
    else if (!strcmp(k, "width")) snprintf(out, cap, "%d", g_cfg.width);
    else if (!strcmp(k, "rows")) snprintf(out, cap, "%d", g_cfg.rows);
    else if (!strcmp(k, "max_apps")) snprintf(out, cap, "%d", g_cfg.max_apps);
    else if (!strcmp(k, "file_min_chars")) snprintf(out, cap, "%d", g_cfg.file_min_chars);
    else if (!strcmp(k, "everything_filter")) w_to_utf8(g_cfg.everything_filter, -1, out, (int)cap);
    else if (!strcmp(k, "hide_uninstallers")) snprintf(out, cap, "%d", g_cfg.hide_uninstallers ? 1 : 0);
    else if (!strcmp(k, "keep_query_seconds")) snprintf(out, cap, "%d", g_cfg.keep_query_seconds);
    else if (!strcmp(k, "animations")) snprintf(out, cap, "%d", g_cfg.animations ? 1 : 0);
    else if (!strcmp(k, "taskbar_button")) snprintf(out, cap, "%d", g_cfg.taskbar_button ? 1 : 0);
    else if (!strcmp(k, "taskbar_icon")) snprintf(out, cap, "%s", k_tbi_names[CLAMP(g_cfg.taskbar_icon, 0, TBI__COUNT - 1)]);
    else if (!strcmp(k, "taskbar_icon_path")) w_to_utf8(g_cfg.taskbar_icon_path, -1, out, (int)cap);
    else if (!strcmp(k, "media_widget")) snprintf(out, cap, "%d", g_cfg.media_widget ? 1 : 0);
    else if (!strcmp(k, "media_position")) snprintf(out, cap, "%s", g_cfg.media_position ? "right" : "left");
    else if (!strcmp(k, "media_controls")) snprintf(out, cap, "%d", g_cfg.media_controls ? 1 : 0);
    else if (!strcmp(k, "media_hide_paused")) snprintf(out, cap, "%d", g_cfg.media_hide_paused ? 1 : 0);
    else if (!strcmp(k, "anim_speed")) snprintf(out, cap, "%d", g_cfg.anim_speed);
    else if (!strcmp(k, "anim_open_ms")) snprintf(out, cap, "%d", g_cfg.anim_open_ms);
    else if (!strcmp(k, "anim_close_ms")) snprintf(out, cap, "%d", g_cfg.anim_close_ms);
    else if (!strcmp(k, "anim_open_scale")) snprintf(out, cap, "%d", g_cfg.anim_open_scale);
    else if (!strcmp(k, "anim_cascade")) snprintf(out, cap, "%d", g_cfg.anim_cascade);
    else if (!strcmp(k, "anim_row_ms")) snprintf(out, cap, "%d", g_cfg.anim_row_ms);
    else if (!strcmp(k, "anim_stagger_ms")) snprintf(out, cap, "%d", g_cfg.anim_stagger_ms);
    else if (!strcmp(k, "anim_select_ms")) snprintf(out, cap, "%d", g_cfg.anim_select_ms);
    else if (!strcmp(k, "anim_scroll_ms")) snprintf(out, cap, "%d", g_cfg.anim_scroll_ms);
    else if (!strcmp(k, "anim_menu_ms")) snprintf(out, cap, "%d", g_cfg.anim_menu_ms);
    else if (!strcmp(k, "anim_page_ms")) snprintf(out, cap, "%d", g_cfg.anim_page_ms);
    else return false;
    return true;
}

static void config_write_all(void)
{
    const char *def = g_lang_ru ? k_default_config_ru : k_default_config_en;
    Buf b = { 0 };
    buf_put(&b, "\xEF\xBB\xBF", 3);
    for (const char *p = def; *p;) {
        const char *e = strstr(p, "\r\n");
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char line[256], key[64], val[1100];
        snprintf(line, sizeof line, "%.*s", (int)MIN(n, sizeof line - 1), p);
        char *eq = strchr(line, '=');
        bool done = false;
        if (line[0] != ';' && eq) {
            *eq = 0;
            snprintf(key, sizeof key, "%s", trim(line));
            if (config_value(key, val, sizeof val)) {
                if (val[0]) buf_printf(&b, "%s = %s\r\n", key, val);
                else buf_printf(&b, "%s =\r\n", key);
                done = true;
            }
        }
        if (!done) {
            buf_put(&b, p, n);
            buf_put(&b, "\r\n", 2);
        }
        p += n + (e ? 2 : 0);
    }
    WCHAR path[MAX_PATH];
    config_path(path);
    write_file_atomic(path, b.data, (DWORD)b.len);
    free(b.data);
}

static void config_load(void)
{
    config_defaults(&g_cfg);
    WCHAR path[MAX_PATH];
    config_path(path);
    char *text = read_file(path, NULL);
    if (!text) {
        const char *def = g_lang_ru ? k_default_config_ru : k_default_config_en;
        // UTF-8 BOM so Notepad never guesses the wrong code page.
        Buf b = { 0 };
        buf_put(&b, "\xEF\xBB\xBF", 3);
        buf_put(&b, def, strlen(def));
        write_file_atomic(path, b.data, (DWORD)b.len);
        free(b.data);
        return;
    }
    int old_win_key = -1;
    char *cur = text;
    if ((u8)cur[0] == 0xEF && (u8)cur[1] == 0xBB && (u8)cur[2] == 0xBF) cur += 3;
    char *line;
    while ((line = next_line(&cur)) != NULL) {
        line = trim(line);
        if (!*line || *line == ';' || *line == '#' || *line == '[') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (!_stricmp(k, "win_key")) old_win_key = parse_bool(v);
        else if (!_stricmp(k, "hotkey")) utf8_to_w(v, -1, g_cfg.hotkey, countof(g_cfg.hotkey));
        else if (!_stricmp(k, "theme")) g_cfg.theme = !_stricmp(v, "dark") ? 1 : !_stricmp(v, "light") ? 2 : 0;
        else if (!_stricmp(k, "backdrop")) g_cfg.backdrop = !_stricmp(v, "solid") ? BACKDROP_SOLID : !_stricmp(v, "acrylic") ? BACKDROP_ACRYLIC : BACKDROP_BLUR;
        else if (!_stricmp(k, "width")) g_cfg.width = CLAMP(atoi(v), 480, 1600);
        else if (!_stricmp(k, "rows")) g_cfg.rows = CLAMP(atoi(v), 4, 20);
        else if (!_stricmp(k, "max_apps")) g_cfg.max_apps = CLAMP(atoi(v), 1, 50);
        else if (!_stricmp(k, "file_min_chars")) g_cfg.file_min_chars = CLAMP(atoi(v), 1, 10);
        else if (!_stricmp(k, "everything_filter")) utf8_to_w(v, -1, g_cfg.everything_filter, countof(g_cfg.everything_filter));
        else if (!_stricmp(k, "hide_uninstallers")) g_cfg.hide_uninstallers = parse_bool(v);
        else if (!_stricmp(k, "keep_query_seconds")) g_cfg.keep_query_seconds = CLAMP(atoi(v), 0, 24 * 3600);
        else if (!_stricmp(k, "animations")) g_cfg.animations = parse_bool(v);
        else if (!_stricmp(k, "taskbar_button")) g_cfg.taskbar_button = parse_bool(v);
        else if (!_stricmp(k, "taskbar_icon")) {
            for (int i = 0; i < TBI__COUNT; i++)
                if (!_stricmp(v, k_tbi_names[i])) g_cfg.taskbar_icon = i;
        } else if (!_stricmp(k, "taskbar_icon_path")) utf8_to_w(v, -1, g_cfg.taskbar_icon_path, countof(g_cfg.taskbar_icon_path));
        else if (!_stricmp(k, "media_widget")) g_cfg.media_widget = parse_bool(v);
        else if (!_stricmp(k, "media_position")) g_cfg.media_position = !_stricmp(v, "right") ? 1 : 0;
        else if (!_stricmp(k, "media_controls")) g_cfg.media_controls = parse_bool(v);
        else if (!_stricmp(k, "media_hide_paused")) g_cfg.media_hide_paused = parse_bool(v);
        else if (!_stricmp(k, "anim_speed")) g_cfg.anim_speed = CLAMP(atoi(v), 30, 400);
        else if (!_stricmp(k, "anim_open_ms")) g_cfg.anim_open_ms = CLAMP(atoi(v), 0, 1000);
        else if (!_stricmp(k, "anim_close_ms")) g_cfg.anim_close_ms = CLAMP(atoi(v), 0, 1000);
        else if (!_stricmp(k, "anim_open_scale")) g_cfg.anim_open_scale = CLAMP(atoi(v), 80, 100);
        else if (!_stricmp(k, "anim_cascade")) g_cfg.anim_cascade = CLAMP(atoi(v), 0, 1);
        else if (!_stricmp(k, "anim_row_ms")) g_cfg.anim_row_ms = CLAMP(atoi(v), 0, 1000);
        else if (!_stricmp(k, "anim_stagger_ms")) g_cfg.anim_stagger_ms = CLAMP(atoi(v), 0, 60);
        else if (!_stricmp(k, "anim_select_ms")) g_cfg.anim_select_ms = CLAMP(atoi(v), 0, 600);
        else if (!_stricmp(k, "anim_scroll_ms")) g_cfg.anim_scroll_ms = CLAMP(atoi(v), 0, 600);
        else if (!_stricmp(k, "anim_menu_ms")) g_cfg.anim_menu_ms = CLAMP(atoi(v), 0, 600);
        else if (!_stricmp(k, "anim_page_ms")) g_cfg.anim_page_ms = CLAMP(atoi(v), 0, 800);
    }
    free(text);
    if (old_win_key == 1) wcopy(g_cfg.hotkey, countof(g_cfg.hotkey), L"win");
    if (!hotkey_parse(g_cfg.hotkey, &g_cfg.hk) || !g_cfg.hk.vk) {
        wcopy(g_cfg.hotkey, countof(g_cfg.hotkey), L"win");
        g_cfg.hk.mods = 0;
        g_cfg.hk.vk = VK_ANYWIN;
    }
    if (old_win_key >= 0) config_write_all();
}

static void config_set(const char *key, const char *value)
{
    WCHAR path[MAX_PATH];
    config_path(path);
    char *text = read_file(path, NULL);
    Buf b = { 0 };
    bool found = false;
    if (text) {
        char *cur = text;
        char *line;
        while ((line = next_line(&cur)) != NULL) {
            char *p = line;
            if ((u8)p[0] == 0xEF && (u8)p[1] == 0xBB && (u8)p[2] == 0xBF) {
                buf_put(&b, p, 3);
                p += 3;
            }
            char *t = p;
            while (*t == ' ' || *t == '\t') t++;
            size_t kl = strlen(key);
            if (!found && !_strnicmp(t, key, kl) && (t[kl] == ' ' || t[kl] == '=' || t[kl] == '\t')) {
                buf_printf(&b, "%s = %s\r\n", key, value);
                found = true;
            } else {
                buf_put(&b, p, strlen(p));
                buf_put(&b, "\r\n", 2);
            }
        }
        free(text);
    }
    if (!found) buf_printf(&b, "%s = %s\r\n", key, value);
    write_file_atomic(path, b.data, (DWORD)b.len);
    free(b.data);
}
