enum { SK_TOGGLE, SK_CHOICE, SK_STEPPER, SK_TEXT, SK_HOTKEY, SK_BUTTON, SK_INFO, SK_ABOUT };
enum {
    SID_NONE, SID_HOTKEY, SID_AUTOSTART, SID_KEEPQUERY, SID_THEME, SID_BACKDROP, SID_ANIM, SID_WIDTH, SID_ROWS,
    SID_MAXAPPS, SID_HIDENOISE, SID_REINDEX, SID_MINCHARS, SID_FILTER, SID_CLEARHIST, SID_CONFIG,
    SID_DATADIR, SID_ADMIN, SID_EVERYTHING, SID_ABOUT,
    SID_ASPEED, SID_AOPEN, SID_ACLOSE, SID_ASCALE, SID_ACASCADE, SID_AROW, SID_ASTAGGER, SID_ASELECT, SID_ASCROLL, SID_AMENU,
    SID_APAGE, SID_APREVIEW, SID_ARESET, SID_TASKBAR, SID_TBICON, SID_TBFILE, SID_MEDIA, SID_MEDIAPOS, SID_MEDIACTL, SID_MEDIAHIDE, SID_LANG,
    SID_STYLE, SID_MATCHHL, SID_UPDCHECK, SID_UPDNOW
};
enum { PG_GENERAL, PG_APPEARANCE, PG_ANIM, PG_SEARCH, PG_ADVANCED, PG_ABOUT, PG__COUNT };

typedef struct Str { const WCHAR *ru, *en; } Str;
static const WCHAR *ss(Str s) { return g_lang_ru ? s.ru : s.en; }

static const struct { Str name; u32 glyph; } k_pages[PG__COUNT] = {
    { { L"Общие", L"General" }, 0xE713 },
    { { L"Внешний вид", L"Appearance" }, 0xE790 },
    { { L"Анимация", L"Animation" }, 0xE916 },
    { { L"Поиск", L"Search" }, 0xE721 },
    { { L"Дополнительно", L"Advanced" }, 0xE90F },
    { { L"О программе", L"About" }, 0xE946 },
};

typedef struct SItem {
    u8 page, kind, id;
    Str group;
    Str title, desc;
    int lo, hi, step;
    Str unit;
    Str opt[4];
    u32 glyph;
} SItem;

static const SItem k_sitems[] = {
    { .page = PG_GENERAL, .kind = SK_HOTKEY, .id = SID_HOTKEY, .group = { L"Вызов", L"Hotkey" },
      .title = { L"Открыть MixLauncher", L"Open MixLauncher" },
      .desc = { L"Клавиша или сочетание, которое открывает и закрывает лаунчер. Прежнее действие сочетания отключается: "
                L"например, Win больше не открывает «Пуск», а Win+D не сворачивает окна.",
                L"The key or shortcut that opens and closes the launcher. Whatever it did before is turned off: "
                L"Win no longer opens Start, Win+D no longer shows the desktop." } },
    { .page = PG_GENERAL, .kind = SK_TOGGLE, .id = SID_AUTOSTART, .group = { L"Запуск", L"Startup" },
      .title = { L"Запускать при входе в Windows", L"Start with Windows" },
      .desc = { L"Через планировщик заданий: с правами администратора и без запроса UAC при входе",
                L"Through Task Scheduler: as administrator, without a UAC prompt at sign-in" } },
    { .page = PG_GENERAL, .kind = SK_TOGGLE, .id = SID_TASKBAR, .group = { L"Панель задач", L"Taskbar" },
      .title = { L"Кнопка Пуск открывает MixLauncher", L"Start button opens MixLauncher" },
      .desc = { L"Наш значок на кнопке Пуск (Windows 11). Правый клик по-прежнему открывает меню Win+X",
                L"Our icon on the Start button (Windows 11). Right click still opens the Win+X menu" } },
    { .page = PG_GENERAL, .kind = SK_CHOICE, .id = SID_TBICON, .title = { L"Значок кнопки", L"Button icon" },
      .opt = { { L"Ромб", L"Diamond" }, { L"Сетка", L"Grid" }, { L"Строки", L"Lines" }, { L"Свой", L"Custom" } } },
    { .page = PG_GENERAL, .kind = SK_BUTTON, .id = SID_TBFILE, .title = { L"Свой значок", L"Custom icon" },
      .desc = { L"PNG, ICO или JPG. Лучше квадратный, от 48×48, с прозрачным фоном", L"PNG, ICO or JPG. Square, 48×48 or larger, with a transparent background works best" },
      .opt = { { L"Выбрать…", L"Choose…" } }, .glyph = 0xE8E5 },
    { .page = PG_GENERAL, .kind = SK_TOGGLE, .id = SID_MEDIA, .group = { L"Сейчас играет", L"Now playing" },
      .title = { L"Медиа на панели задач", L"Media on the taskbar" },
      .desc = { L"Обложка, название и исполнитель из любого плеера: Spotify, браузера, Медиаплеера. Клик открывает плеер",
                L"Cover, title and artist from any player: Spotify, a browser, Media Player. A click opens the player" } },
    { .page = PG_GENERAL, .kind = SK_CHOICE, .id = SID_MEDIAPOS, .title = { L"Положение", L"Position" },
      .desc = { L"Справа - у значков в трее. Если значки панели выровнены по левому краю, всегда справа",
                L"Right sits next to the tray icons. Always right when taskbar icons are aligned left" },
      .opt = { { L"Слева", L"Left" }, { L"Справа", L"Right" } } },
    { .page = PG_GENERAL, .kind = SK_TOGGLE, .id = SID_MEDIACTL, .title = { L"Кнопки управления", L"Playback buttons" },
      .desc = { L"Предыдущий трек, пауза и следующий", L"Previous track, pause and next" } },
    { .page = PG_GENERAL, .kind = SK_TOGGLE, .id = SID_MEDIAHIDE, .title = { L"Скрывать на паузе", L"Hide when paused" },
      .desc = { L"Показывать виджет, только пока что-то играет", L"Show the widget only while something plays" } },
    { .page = PG_GENERAL, .kind = SK_STEPPER, .id = SID_KEEPQUERY, .group = { L"Поведение", L"Behavior" },
      .title = { L"Помнить запрос", L"Keep the query" },
      .desc = { L"Сколько секунд после закрытия хранить набранный текст. 0 - всегда начинать с пустого поля",
                L"Seconds to keep the typed text after closing. 0 always starts empty" },
      .lo = 0, .hi = 3600, .step = 10, .unit = { L"с", L"s" } },
    { .page = PG_GENERAL, .kind = SK_CHOICE, .id = SID_LANG, .group = { L"Язык", L"Language" },
      .title = { L"Язык интерфейса", L"Interface language" },
      .desc = { L"«Системный» берёт язык Windows: русский для русской Windows, иначе английский",
                L"System follows Windows: Russian on a Russian Windows, English otherwise" },
      .opt = { { L"Системный", L"System" }, { L"Русский", L"Русский" }, { L"English", L"English" } } },

    { .page = PG_APPEARANCE, .kind = SK_CHOICE, .id = SID_THEME, .group = { L"Окно", L"Window" },
      .title = { L"Тема", L"Theme" }, .desc = { L"«Системная» следует настройке Windows", L"System follows the Windows setting" },
      .opt = { { L"Системная", L"System" }, { L"Тёмная", L"Dark" }, { L"Светлая", L"Light" } } },
    { .page = PG_APPEARANCE, .kind = SK_CHOICE, .id = SID_BACKDROP, .title = { L"Фон", L"Background" },
      .desc = { L"Стекло - полупрозрачное размытие, акрил - как у системных меню Windows 11",
                L"Glass is a translucent blur, acrylic looks like Windows 11 system menus" },
      .opt = { { L"Стекло", L"Glass" }, { L"Акрил", L"Acrylic" }, { L"Сплошной", L"Solid" } } },
    { .page = PG_APPEARANCE, .kind = SK_CHOICE, .id = SID_STYLE, .group = { L"Лаунчер", L"Launcher" },
      .title = { L"Стиль", L"Style" },
      .desc = { L"Стандарт - мягкая подложка с контуром. Raycast - мелкие значки и плотная подложка. "
                L"Windows 11 - полоска акцента у выбранной строки. Компактный - больше строк на экране",
                L"Standard has a soft outlined highlight. Raycast uses small icons and a solid highlight. "
                L"Windows 11 marks the selected row with an accent bar. Compact fits more rows on screen" },
      .opt = { { L"Стандарт", L"Standard" }, { L"Raycast", L"Raycast" }, { L"Windows 11", L"Windows 11" }, { L"Компактный", L"Compact" } } },
    { .page = PG_APPEARANCE, .kind = SK_TOGGLE, .id = SID_MATCHHL, .title = { L"Подсветка совпадений", L"Highlight matches" },
      .desc = { L"Буквы, по которым нашлись приложение или файл, выделяются жирным",
                L"The letters that matched the query are drawn in bold" } },
    { .page = PG_APPEARANCE, .kind = SK_BUTTON, .id = SID_APREVIEW, .title = { L"Проверить", L"Try it" },
      .desc = { L"Открыть лаунчер с текущими настройками. Esc вернёт сюда", L"Open the launcher with the current settings. Esc comes back here" },
      .opt = { { L"Открыть лаунчер", L"Open launcher" } }, .glyph = 0xE768 },
    { .page = PG_APPEARANCE, .kind = SK_STEPPER, .id = SID_WIDTH, .group = { L"Размер", L"Size" },
      .title = { L"Ширина окна", L"Window width" }, .lo = 480, .hi = 1600, .step = 20, .unit = { L"px", L"px" } },
    { .page = PG_APPEARANCE, .kind = SK_STEPPER, .id = SID_ROWS, .title = { L"Строк в списке", L"Visible rows" },
      .lo = 4, .hi = 20, .step = 1 },

    { .page = PG_ANIM, .kind = SK_TOGGLE, .id = SID_ANIM, .group = { L"Общее", L"General" }, .title = { L"Анимации", L"Animations" },
      .desc = { L"Все анимации лаунчера и настроек. Выключаются и системной настройкой «Эффекты анимации»",
                L"Every animation of the launcher and settings. Also off when Windows animation effects are off" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ASPEED, .title = { L"Общая скорость", L"Overall speed" },
      .desc = { L"Множитель для всех длительностей ниже: 200 % - всё вдвое быстрее, 50 % - вдвое медленнее",
                L"Multiplies every duration below: 200% makes everything twice as fast, 50% twice as slow" },
      .lo = 30, .hi = 400, .step = 10, .unit = { L"%", L"%" } },
    { .page = PG_ANIM, .kind = SK_BUTTON, .id = SID_APREVIEW, .title = { L"Проверить", L"Try it" },
      .desc = { L"Открыть лаунчер с текущими настройками. Esc вернёт сюда", L"Open the launcher with the current settings. Esc comes back here" },
      .opt = { { L"Открыть лаунчер", L"Open launcher" } }, .glyph = 0xE768 },

    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_AOPEN, .group = { L"Открытие и закрытие", L"Open and close" },
      .title = { L"Появление окна", L"Window appears" }, .desc = { L"Проявление стекла и содержимого. 0 - сразу", L"Fade-in of the glass and content. 0 is instant" },
      .lo = 0, .hi = 1000, .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ACLOSE, .title = { L"Закрытие", L"Window closes" },
      .desc = { L"Короче появления, чтобы Esc не ощущался задержкой", L"Shorter than opening so Esc never feels delayed" },
      .lo = 0, .hi = 1000, .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ASCALE, .title = { L"Начальный масштаб", L"Starting scale" },
      .desc = { L"С какого размера содержимое «наплывает» при открытии. 100 % - без увеличения",
                L"The size the content grows from when opening. 100% means no zoom" },
      .lo = 80, .hi = 100, .step = 1, .unit = { L"%", L"%" } },
    { .page = PG_ANIM, .kind = SK_TOGGLE, .id = SID_ACASCADE, .title = { L"Каскад строк", L"Row cascade" },
      .desc = { L"После открытия строки появляются по очереди, сверху вниз", L"After opening the rows appear one after another, top to bottom" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_AROW, .title = { L"Появление строки", L"Row fade-in" },
      .lo = 0, .hi = 1000, .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ASTAGGER, .title = { L"Задержка между строками", L"Delay between rows" },
      .lo = 0, .hi = 60, .step = 2, .unit = { L"мс", L"ms" } },

    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ASELECT, .group = { L"Движение", L"Motion" },
      .title = { L"Выделение", L"Selection" },
      .desc = { L"За сколько подложка доезжает до выбранной строки (пружина: при быстром листании скорость не рвётся)",
                L"How fast the highlight reaches the selected row (a spring: fast key repeat never jerks it)" },
      .lo = 0, .hi = 600, .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ASCROLL, .title = { L"Прокрутка", L"Scrolling" },
      .lo = 0, .hi = 600, .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_AMENU, .title = { L"Меню", L"Menus" },
      .desc = { L"Контекстное меню лаунчера и выпадающие списки настроек", L"The launcher context menu and the dropdowns in settings" },
      .lo = 0, .hi = 600, .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_APAGE, .title = { L"Смена раздела настроек", L"Settings page switch" },
      .lo = 0, .hi = 800, .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_BUTTON, .id = SID_ARESET, .group = { L"Сброс", L"Reset" },
      .title = { L"Значения по умолчанию", L"Default values" }, .desc = { L"Вернуть все тайминги этой страницы", L"Restore every timing on this page" },
      .opt = { { L"Сбросить", L"Reset" } }, .glyph = 0xE777 },

    { .page = PG_SEARCH, .kind = SK_STEPPER, .id = SID_MAXAPPS, .group = { L"Приложения", L"Applications" },
      .title = { L"Приложений в результатах", L"Apps in results" }, .lo = 1, .hi = 50, .step = 1 },
    { .page = PG_SEARCH, .kind = SK_TOGGLE, .id = SID_HIDENOISE, .title = { L"Скрывать деинсталляторы и справку", L"Hide uninstallers and help links" },
      .desc = { L"Ярлыки «Удалить…», «Readme» и подобные из меню «Пуск»", L"“Uninstall…”, “Readme” and similar Start menu shortcuts" } },
    { .page = PG_SEARCH, .kind = SK_BUTTON, .id = SID_REINDEX, .title = { L"Список приложений", L"App list" },
      .desc = { L"Перечитать меню «Пуск» прямо сейчас. Обычно это происходит само", L"Re-read the Start menu now. Normally this happens by itself" },
      .opt = { { L"Обновить", L"Refresh" } }, .glyph = 0xE72C },
    { .page = PG_SEARCH, .kind = SK_STEPPER, .id = SID_MINCHARS, .group = { L"Файлы (Everything)", L"Files (Everything)" },
      .title = { L"Искать файлы с", L"Search files from" },
      .desc = { L"Минимальная длина запроса", L"Minimum query length" }, .lo = 1, .hi = 10, .step = 1, .unit = { L"симв.", L"chars" } },
    { .page = PG_SEARCH, .kind = SK_TEXT, .id = SID_FILTER, .title = { L"Фильтр Everything", L"Everything filter" },
      .desc = { L"Добавляется к каждому запросу. Например, !C:\\Windows\\ исключает папку Windows",
                L"Appended to every query. For example, !C:\\Windows\\ excludes the Windows folder" } },

    { .page = PG_ADVANCED, .kind = SK_BUTTON, .id = SID_CLEARHIST, .group = { L"Данные", L"Data" },
      .title = { L"История запусков", L"Launch history" },
      .desc = { L"Частота запусков и запомненные запросы, по которым строится порядок результатов",
                L"Launch counts and remembered queries that order the results" },
      .opt = { { L"Очистить", L"Clear" } }, .glyph = 0xE74D },
    { .page = PG_ADVANCED, .kind = SK_BUTTON, .id = SID_CONFIG, .title = { L"Файл настроек", L"Settings file" },
      .desc = { L"config.ini - все параметры с комментариями", L"config.ini with every option and comments" },
      .opt = { { L"Открыть", L"Open" } }, .glyph = 0xE8E5 },
    { .page = PG_ADVANCED, .kind = SK_BUTTON, .id = SID_DATADIR, .title = { L"Папка данных", L"Data folder" },
      .desc = { L"Настройки, история, кэш списка приложений и журнал", L"Settings, history, app list cache and log" },
      .opt = { { L"Открыть", L"Open" } }, .glyph = 0xE838 },
    { .page = PG_ADVANCED, .kind = SK_TOGGLE, .id = SID_UPDCHECK, .group = { L"Обновления", L"Updates" },
      .title = { L"Проверять обновления", L"Check for updates" },
      .desc = { L"Раз в несколько часов спрашивает GitHub о новой версии. Если она есть, в лаунчере появится кнопка «Обновить»",
                L"Asks GitHub for a new version every few hours. When there is one, the launcher shows an Update button" } },
    { .page = PG_ADVANCED, .kind = SK_BUTTON, .id = SID_UPDNOW, .title = { L"Версия " ML_VER_WSTR, L"Version " ML_VER_WSTR },
      .desc = { L"Проверить сейчас или установить найденное обновление", L"Check now or install the update that was found" },
      .opt = { { L"Проверить", L"Check now" } }, .glyph = 0xE896 },
    { .page = PG_ADVANCED, .kind = SK_INFO, .id = SID_ADMIN, .group = { L"Права", L"Permissions" },
      .title = { L"Права администратора", L"Administrator rights" },
      .desc = { L"Нужны, чтобы клавиша вызова работала поверх игр и окон администратора. Программы из лаунчера запускаются с обычными правами",
                L"Needed for the hotkey to work over games and admin windows. Programs started from the launcher get normal rights" } },

    { .page = PG_ABOUT, .kind = SK_ABOUT, .id = SID_ABOUT,
      .desc = { L"Быстрый лаунчер для Windows. Приложения, файлы и команды по одной клавише, музыка на панели задач. "
                L"Всё работает на вашем компьютере: без ИИ и без телеметрии.",
                L"A fast launcher for Windows. Apps, files and commands on one key, music on the taskbar. "
                L"It all runs on your computer: no AI, no telemetry." } },
    { .page = PG_ABOUT, .kind = SK_INFO, .id = SID_EVERYTHING, .group = { L"Состояние", L"Status" },
      .title = { L"Everything", L"Everything" },
      .desc = { L"Поиск файлов идёт через ваш Everything, а если его нет - через встроенный",
                L"File search goes through your own Everything, or the built-in one when you have none" } },
};
#define SITEMS countof(k_sitems)
#define SROWS_MAX 24

typedef struct SRow {
    int item;
    bool first;
    f32 x, y, w, h;
    f32 ty;
    f32 cx, cy, cw, ch;
    int ndesc, desc_at[4];
} SRow;

typedef struct SBox { f32 x, y, w, h; } SBox;

enum { HT_NONE, HT_NAV, HT_ROW, HT_WINMIN, HT_WINCLOSE, HT_POPITEM, HT_POPOUT, HT_RECCANCEL, HT_RECSAVE, HT_RECPANEL, HT_RECOUT,
       HT_SBUPDATE, HT_DONATE, HT_DON };
enum { DON_OUT = -1, DON_PANEL = -2, DON_CLOSE = -3, DON_LINK = -4 };
enum { PART_ROW, PART_CONTROL, PART_MINUS, PART_PLUS };

typedef struct SHit { u8 type, part; int idx; } SHit;

typedef struct KeyTok { WCHAR label[24]; const WCHAR *side; } KeyTok;

#define SETTINGS_CLASS L"MixLauncherSettings"
#define SETTINGS_TIMER_CARET 1
#define SETTINGS_TIMER_FLASH 2

enum { RM_LCTRL = 1, RM_RCTRL = 2, RM_LALT = 4, RM_RALT = 8, RM_LSHIFT = 16, RM_RSHIFT = 32, RM_LWIN = 64, RM_RWIN = 128 };

static struct {
    HWND hwnd;
    RTarget target;
    bool dirty, mica, active;
    u32 dpi;
    f32 s;
    int W, H;
    int page;
    f32 scroll, content_h;
    SRow rows[SROWS_MAX];
    int nrows;
    SBox cards[12];
    int ncards;
    SBox heads[12];
    Str head_text[12];
    int nheads;
    int focus;
    bool focus_visible;
    SHit hover, press;
    int editing;
    WCHAR edit[512];
    int edit_len, edit_caret;
    bool caret_on;
    bool armed;
    int flash_id;
    f64 flash_until;
    bool autostart;
    int everything;
    bool pop_open;
    int pop_row, pop_hover;
    f32 pop_x, pop_y, pop_w, pop_h;
    struct {
        bool open;
        u8 held;
        u8 key;
        bool used;
        bool has;
        Hotkey hk;
    } rec;
    f32 rec_panel[4], rec_btn[2][4];
    bool animating;
    f64 last_frame;
    f32 scroll_target, scroll_v;
    f32 nav_y, nav_v;
    f32 nav_hov[PG__COUNT];
    f32 win_hov[2];
    f32 tog[SITEMS];
    f32 page_t, pop_t, rec_t;
    f32 sb_t, sb_prev, row_hov;
    int row_hov_idx;
    f32 seg[SITEMS];
    int seg_click;
    struct {
        bool open, closing;
        f32 t, scroll, scroll_target, scroll_v, content_h, view_h;
        f32 panel[4], close[4], link[4], copy[8][4];
        f32 hov[10], cp[8], sb_t, sb_prev;
        int flash;
        f64 flash_until, open_time, sb_seen;
    } don;
    f32 sb_btn[2][4], sb_hov[2];
    f64 sb_seen;
    int mx, my;
} SW = { .focus = -1, .editing = -1, .pop_row = -1, .flash_id = -1, .page_t = 1, .pop_t = 1, .rec_t = 1, .seg_click = -1 };

static f32 SSC(f32 v) { return v * SW.s; }
static f32 SSR(f32 v) { return floorf(v * SW.s + 0.5f); }

static void settings_invalidate(void) { SW.dirty = true; }
static bool settings_needs_render(void) { return SW.hwnd && (SW.dirty || SW.animating) && SW.target.ok; }

static bool in_box(const f32 *b, f32 x, f32 y) { return x >= b[0] && x < b[2] && y >= b[1] && y < b[3]; }

static f32 sb_x(void) { return SSR(10); }
static f32 sb_w(void) { return SSR(228); }
static f32 nav_y0(void) { return sb_x() + SSR(78); }
static f32 nav_pitch(void) { return SSR(38); }
static f32 top_h(void) { return SSR(60); }
static f32 content_x0(void) { return sb_x() + sb_w() + SSR(26); }
static f32 content_x1(void) { return (f32)SW.W - SSR(26); }

static void winbtn_rect(int i, f32 *r)
{
    f32 w = SSR(46), h = SSR(34);
    r[0] = (f32)SW.W - w * (f32)(2 - i);
    r[1] = 0;
    r[2] = r[0] + w;
    r[3] = h;
}

static void nav_rect(int i, f32 *r)
{
    r[0] = sb_x() + SSR(8);
    r[1] = nav_y0() + nav_pitch() * (f32)i;
    r[2] = sb_x() + sb_w() - SSR(8);
    r[3] = r[1] + SSR(36);
}

static int sval(int id)
{
    switch (id) {
    case SID_AUTOSTART: return SW.autostart;
    case SID_THEME: return g_cfg.theme;
    case SID_STYLE: return g_cfg.style;
    case SID_UPDCHECK: return g_cfg.update_check;
    case SID_MATCHHL: return g_cfg.match_highlight;
    case SID_LANG: return g_cfg.language;
    case SID_BACKDROP: return g_cfg.backdrop == BACKDROP_BLUR ? 0 : g_cfg.backdrop == BACKDROP_ACRYLIC ? 1 : 2;
    case SID_ANIM: return g_cfg.animations;
    case SID_TASKBAR: return g_cfg.taskbar_button;
    case SID_TBICON: return g_cfg.taskbar_icon;
    case SID_MEDIA: return g_cfg.media_widget;
    case SID_MEDIAPOS: return g_cfg.media_position;
    case SID_MEDIACTL: return g_cfg.media_controls;
    case SID_MEDIAHIDE: return g_cfg.media_hide_paused;
    case SID_WIDTH: return g_cfg.width;
    case SID_ROWS: return g_cfg.rows;
    case SID_MAXAPPS: return g_cfg.max_apps;
    case SID_MINCHARS: return g_cfg.file_min_chars;
    case SID_HIDENOISE: return g_cfg.hide_uninstallers;
    case SID_KEEPQUERY: return g_cfg.keep_query_seconds;
    case SID_ASPEED: return g_cfg.anim_speed;
    case SID_AOPEN: return g_cfg.anim_open_ms;
    case SID_ACLOSE: return g_cfg.anim_close_ms;
    case SID_ASCALE: return g_cfg.anim_open_scale;
    case SID_ACASCADE: return g_cfg.anim_cascade;
    case SID_AROW: return g_cfg.anim_row_ms;
    case SID_ASTAGGER: return g_cfg.anim_stagger_ms;
    case SID_ASELECT: return g_cfg.anim_select_ms;
    case SID_ASCROLL: return g_cfg.anim_scroll_ms;
    case SID_AMENU: return g_cfg.anim_menu_ms;
    case SID_APAGE: return g_cfg.anim_page_ms;
    }
    return 0;
}

static void save_int(const char *key, int v)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%d", v);
    config_set(key, buf);
}

static void save_wstr(const char *key, const WCHAR *v)
{
    char buf[1024];
    w_to_utf8(v, -1, buf, sizeof buf);
    config_set(key, buf);
}

static void settings_apply_theme(void);
static void tb_set_enabled(bool on);
static void tb_icon_changed(void);
static void media_set_enabled(bool on);
static void media_settings_changed(void);

static int choice_count(const SItem *it)
{
    int n = 0;
    while (n < (int)countof(it->opt) && it->opt[n].ru) n++;
    return MAX(n, 1);
}

// Short option lists (up to three) are shown as a segmented control instead of a dropdown.
static bool is_seg(const SItem *it) { return it->kind == SK_CHOICE && choice_count(it) <= 3; }

static f32 seg_layout(const SItem *it, f32 x, f32 *xs, f32 *ws)
{
    f32 fs = SSC(13.5f), x0 = x;
    for (int k = 0; k < choice_count(it); k++) {
        xs[k] = x;
        ws[k] = floorf(text_width(FONT_TEXT, fs, ss(it->opt[k]), -1) + SSR(28));
        x += ws[k];
    }
    return x - x0;
}

static int seg_index_at(const SItem *it, f32 cx, f32 mx)
{
    f32 xs[4], ws[4];
    seg_layout(it, cx + SSR(2), xs, ws);
    for (int k = choice_count(it) - 1; k >= 0; k--)
        if (mx >= xs[k]) return k;
    return 0;
}

static bool pick_taskbar_icon(void)
{
    static const GUID clsid = { 0xdc1c5a9c, 0xe88a, 0x4dde, { 0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7 } };
    static const GUID iid = { 0xd57c7288, 0xd4ad, 0x4768, { 0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60 } };
    IFileOpenDialog *d = NULL;
    if (FAILED(CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &iid, (void **)&d)) || !d) return false;
    COMDLG_FILTERSPEC types[] = { { g_lang_ru ? L"Изображения" : L"Images", L"*.png;*.ico;*.jpg;*.jpeg;*.bmp" } };
    IFileOpenDialog_SetFileTypes(d, 1, types);
    IFileOpenDialog_SetTitle(d, g_lang_ru ? L"Значок кнопки Пуск" : L"Start button icon");
    bool ok = false;
    if (SUCCEEDED(IFileOpenDialog_Show(d, SW.hwnd))) {
        IShellItem *item = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResult(d, &item)) && item) {
            WCHAR *path = NULL;
            if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path)) && path) {
                wcopy(g_cfg.taskbar_icon_path, countof(g_cfg.taskbar_icon_path), path);
                save_wstr("taskbar_icon_path", path);
                CoTaskMemFree(path);
                ok = true;
            }
            IShellItem_Release(item);
        }
    }
    IFileOpenDialog_Release(d);
    return ok;
}

static const struct { u8 id; const char *key; size_t off; } k_anim_fields[] = {
    { SID_ASPEED, "anim_speed", offsetof(Config, anim_speed) },
    { SID_AOPEN, "anim_open_ms", offsetof(Config, anim_open_ms) },
    { SID_ACLOSE, "anim_close_ms", offsetof(Config, anim_close_ms) },
    { SID_ASCALE, "anim_open_scale", offsetof(Config, anim_open_scale) },
    { SID_ACASCADE, "anim_cascade", offsetof(Config, anim_cascade) },
    { SID_AROW, "anim_row_ms", offsetof(Config, anim_row_ms) },
    { SID_ASTAGGER, "anim_stagger_ms", offsetof(Config, anim_stagger_ms) },
    { SID_ASELECT, "anim_select_ms", offsetof(Config, anim_select_ms) },
    { SID_ASCROLL, "anim_scroll_ms", offsetof(Config, anim_scroll_ms) },
    { SID_AMENU, "anim_menu_ms", offsetof(Config, anim_menu_ms) },
    { SID_APAGE, "anim_page_ms", offsetof(Config, anim_page_ms) },
};

static int *anim_field(int id)
{
    for (int i = 0; i < (int)countof(k_anim_fields); i++)
        if (k_anim_fields[i].id == id) return (int *)((char *)&g_cfg + k_anim_fields[i].off);
    return NULL;
}

static const char *anim_key(int id)
{
    for (int i = 0; i < (int)countof(k_anim_fields); i++)
        if (k_anim_fields[i].id == id) return k_anim_fields[i].key;
    return "";
}

static void sset(const SItem *it, int v)
{
    if (it->kind == SK_STEPPER) v = CLAMP(v, it->lo, it->hi);
    if (it->kind == SK_TOGGLE) v = v ? 1 : 0;
    if (it->kind == SK_CHOICE) v = CLAMP(v, 0, choice_count(it) - 1);
    if (v == sval(it->id)) return;
    switch (it->id) {
    case SID_AUTOSTART:
        autostart_set(v != 0);
        SW.autostart = autostart_get();
        break;
    case SID_LANG:
        g_cfg.language = v;
        config_set("language", v == 1 ? "ru" : v == 2 ? "en" : "auto");
        lang_apply();
        apps_relocalize(U.apps);
        SetWindowTextW(SW.hwnd, TR("Настройки MixLauncher", "MixLauncher Settings"));
        if (U.visible) {
            rebuild_rows(true);
            ui_invalidate();
        }
        break;
    case SID_THEME:
        g_cfg.theme = v;
        config_set("theme", v == 1 ? "dark" : v == 2 ? "light" : "auto");
        theme_update();
        U.backdrop_ok = backdrop_apply(g_hwnd);
        settings_apply_theme();
        break;
    case SID_STYLE:
        g_cfg.style = v;
        config_set("style", k_style_names[v]);
        U.dpi = 0;
        U.backdrop_ok = backdrop_apply(g_hwnd);
        break;
    case SID_UPDCHECK:
        g_cfg.update_check = v;
        config_set("update_check", v ? "1" : "0");
        upd_schedule(2000);
        break;
    case SID_MATCHHL:
        g_cfg.match_highlight = v;
        config_set("match_highlight", v ? "1" : "0");
        if (U.visible) ui_invalidate();
        break;
    case SID_BACKDROP:
        g_cfg.backdrop = v == 0 ? BACKDROP_BLUR : v == 1 ? BACKDROP_ACRYLIC : BACKDROP_SOLID;
        config_set("backdrop", v == 0 ? "blur" : v == 1 ? "acrylic" : "solid");
        theme_update();
        U.backdrop_ok = backdrop_apply(g_hwnd);
        break;
    case SID_TBICON:
        if (v == TBI_CUSTOM && !g_cfg.taskbar_icon_path[0] && !pick_taskbar_icon()) return;
        g_cfg.taskbar_icon = v;
        config_set("taskbar_icon", k_tbi_names[v]);
        tb_icon_changed();
        break;
    case SID_TASKBAR:
        g_cfg.taskbar_button = v;
        config_set("taskbar_button", v ? "1" : "0");
        tb_set_enabled(v != 0);
        break;
    case SID_MEDIA:
        g_cfg.media_widget = v;
        config_set("media_widget", v ? "1" : "0");
        media_set_enabled(v != 0);
        break;
    case SID_MEDIAPOS:
        g_cfg.media_position = v;
        config_set("media_position", v ? "right" : "left");
        media_settings_changed();
        break;
    case SID_MEDIACTL:
        g_cfg.media_controls = v;
        config_set("media_controls", v ? "1" : "0");
        media_settings_changed();
        break;
    case SID_MEDIAHIDE:
        g_cfg.media_hide_paused = v;
        config_set("media_hide_paused", v ? "1" : "0");
        media_settings_changed();
        break;
    case SID_ANIM:
        g_cfg.animations = v;
        config_set("animations", v ? "1" : "0");
        break;
    case SID_WIDTH:
        g_cfg.width = v;
        save_int("width", v);
        U.dpi = 0;
        break;
    case SID_ROWS:
        g_cfg.rows = v;
        save_int("rows", v);
        U.dpi = 0;
        break;
    case SID_MAXAPPS:
        g_cfg.max_apps = v;
        save_int("max_apps", v);
        break;
    case SID_MINCHARS:
        g_cfg.file_min_chars = v;
        save_int("file_min_chars", v);
        break;
    case SID_HIDENOISE:
        g_cfg.hide_uninstallers = v;
        config_set("hide_uninstallers", v ? "1" : "0");
        apps_request_reindex();
        break;
    case SID_KEEPQUERY:
        g_cfg.keep_query_seconds = v;
        save_int("keep_query_seconds", v);
        break;
    default: {
        int *f = anim_field(it->id);
        if (f) {
            *f = v;
            save_int(anim_key(it->id), v);
        }
        break;
    }
    }
    settings_invalidate();
}

static void open_path(const WCHAR *path)
{
    LaunchJob j;
    memset(&j, 0, sizeof j);
    j.act = ACT_OPEN;
    j.target = wdup_heap(path);
    launch_submit(&j);
}

static void button_action(const SItem *it)
{
    switch (it->id) {
    case SID_REINDEX:
        apps_request_reindex();
        SW.flash_id = it->id;
        SW.flash_until = time_now() + 1.6;
        SetTimer(SW.hwnd, SETTINGS_TIMER_FLASH, 1700, NULL);
        break;
    case SID_CLEARHIST:
        if (!SW.armed) {
            SW.armed = true;
            break;
        }
        SW.armed = false;
        history_clear();
        refresh_frecency();
        refresh_empty_view();
        SW.flash_id = it->id;
        SW.flash_until = time_now() + 1.6;
        SetTimer(SW.hwnd, SETTINGS_TIMER_FLASH, 1700, NULL);
        break;
    case SID_CONFIG: {
        WCHAR path[MAX_PATH];
        config_path(path);
        open_path(path);
        break;
    }
    case SID_DATADIR:
        open_path(g_data_dir);
        break;
    case SID_TBFILE:
        if (pick_taskbar_icon()) {
            g_cfg.taskbar_icon = TBI_CUSTOM;
            config_set("taskbar_icon", k_tbi_names[TBI_CUSTOM]);
            tb_icon_changed();
        }
        break;
    case SID_APREVIEW:
        ui_show();
        break;
    case SID_UPDNOW:
        if (upd_state() == UPD_AVAILABLE || (upd_state() == UPD_ERROR && UPD.url[0])) upd_download();
        else upd_check();
        break;
    case SID_ARESET: {
        Config d;
        config_anim_defaults(&d);
        for (int i = 0; i < (int)countof(k_anim_fields); i++) {
            int v = *(int *)((char *)&d + k_anim_fields[i].off);
            *(int *)((char *)&g_cfg + k_anim_fields[i].off) = v;
            save_int(k_anim_fields[i].key, v);
        }
        SW.flash_id = it->id;
        SW.flash_until = time_now() + 1.6;
        SetTimer(SW.hwnd, SETTINGS_TIMER_FLASH, 1700, NULL);
        break;
    }
    }
    settings_invalidate();
}

static void refresh_status(void)
{
    SW.autostart = autostart_get();
    SW.everything = ev_state();
}

static const WCHAR *info_text(int id, bool *good)
{
    if (id == SID_ADMIN) {
        *good = g_elevated;
        return g_elevated ? ss((Str){ L"Есть", L"Granted" }) : ss((Str){ L"Нет", L"No" });
    }
    *good = SW.everything != EVS_NONE;
    if (SW.everything == EVS_OWN) return ss((Str){ L"Встроенный, запущен", L"Built-in, running" });
    return SW.everything == EVS_USER ? ss((Str){ L"Запущен", L"Running" }) : ss((Str){ L"Не запущен", L"Not running" });
}

static int hk_tokens(Hotkey h, KeyTok *out)
{
    int n = 0;
    static const WCHAR *const names[4] = { L"Ctrl", L"Alt", L"Shift", L"Win" };
    for (int b = 0; b < 4; b++)
        if (h.mods & (1 << b)) {
            wcopy(out[n].label, countof(out[n].label), names[b]);
            out[n++].side = NULL;
        }
    if (h.vk) {
        key_label(h.vk, out[n].label, countof(out[n].label), &out[n].side);
        n++;
    }
    return n;
}

static u8 rec_bit(UINT vk)
{
    switch (vk) {
    case VK_LCONTROL: return RM_LCTRL;
    case VK_RCONTROL: return RM_RCTRL;
    case VK_LMENU: return RM_LALT;
    case VK_RMENU: return RM_RALT;
    case VK_LSHIFT: return RM_LSHIFT;
    case VK_RSHIFT: return RM_RSHIFT;
    case VK_LWIN: return RM_LWIN;
    case VK_RWIN: return RM_RWIN;
    }
    return 0;
}

static u8 rec_generic(u8 held)
{
    u8 m = 0;
    if (held & (RM_LCTRL | RM_RCTRL)) m |= HK_CTRL;
    if (held & (RM_LALT | RM_RALT)) m |= HK_ALT;
    if (held & (RM_LSHIFT | RM_RSHIFT)) m |= HK_SHIFT;
    if (held & (RM_LWIN | RM_RWIN)) m |= HK_WIN;
    return m;
}

static const Str *hotkey_problem(Hotkey h)
{
    static const Str none = { L"Нажмите клавишу или сочетание", L"Press a key or a shortcut" };
    static const Str typing = { L"Эта клавиша нужна для набора текста: добавьте Ctrl, Alt или Win",
                                L"This key is needed for typing: add Ctrl, Alt or Win" };
    static const Str reserved = { L"Это сочетание Windows переназначить не даёт", L"Windows does not allow reassigning this shortcut" };
    if (!h.vk) return &none;
    u32 vk = h.vk;
    bool typing_key = (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || vk == VK_SPACE || vk == VK_RETURN || vk == VK_TAB ||
                      vk == VK_ESCAPE || vk == VK_BACK || vk == VK_DELETE || vk == VK_INSERT || (vk >= VK_PRIOR && vk <= VK_DOWN) ||
                      (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE) || (vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_8) ||
                      vk == VK_OEM_102;
    if (typing_key && !(h.mods & (HK_CTRL | HK_ALT | HK_WIN))) return &typing;
    if ((h.mods == HK_WIN && vk == 'L') || (vk == VK_DELETE && (h.mods & (HK_CTRL | HK_ALT)) == (HK_CTRL | HK_ALT))) return &reserved;
    return NULL;
}

static void end_edit(bool commit);

static void rec_open(void)
{
    end_edit(true);
    SW.pop_open = false;
    memset(&SW.rec, 0, sizeof SW.rec);
    SW.rec.open = true;
    SW.rec_t = 0;
    SW.rec.has = true;
    SW.rec.hk = g_cfg.hk;
    hook_capture(SW.hwnd);
    settings_invalidate();
}

static void rec_close(bool save)
{
    if (!SW.rec.open) return;
    hook_capture(NULL);
    if (save && SW.rec.has && !hotkey_problem(SW.rec.hk)) {
        g_cfg.hk = SW.rec.hk;
        hotkey_format(g_cfg.hk, g_cfg.hotkey, countof(g_cfg.hotkey));
        save_wstr("hotkey", g_cfg.hotkey);
        hook_set_hotkey(g_cfg.hk);
    }
    SW.rec.open = false;
    settings_invalidate();
}

static void rec_key(UINT vk, bool down)
{
    u8 bit = rec_bit(vk);
    if (down) {
        if (bit ? (SW.rec.held & bit) != 0 : SW.rec.key == vk) return;
        bool idle = !SW.rec.held && !SW.rec.key;
        if (idle && vk == VK_ESCAPE) {
            rec_close(false);
            return;
        }
        if (idle && vk == VK_RETURN && SW.rec.has && !hotkey_problem(SW.rec.hk)) {
            rec_close(true);
            return;
        }
        if (idle) SW.rec.has = SW.rec.used = false;
        if (bit) {
            SW.rec.held |= bit;
        } else {
            SW.rec.key = (u8)vk;
            SW.rec.hk.mods = rec_generic(SW.rec.held);
            SW.rec.hk.vk = (u8)vk;
            SW.rec.has = SW.rec.used = true;
        }
    } else if (bit) {
        if (!(SW.rec.held & bit)) return;
        if (!SW.rec.used) {
            SW.rec.hk.mods = (u8)(rec_generic(SW.rec.held & ~bit) & ~vk_mod_bit(vk));
            SW.rec.hk.vk = (vk == VK_LWIN || vk == VK_RWIN) ? VK_ANYWIN : (u8)vk;
            SW.rec.has = SW.rec.used = true;
        }
        SW.rec.held &= (u8)~bit;
    } else if (SW.rec.key == vk) {
        SW.rec.key = 0;
    }
    settings_invalidate();
}

static UINT sided_vk(WPARAM vk, LPARAM lp)
{
    UINT sc = (UINT)(lp >> 16) & 0xFF;
    bool ext = (lp >> 24) & 1;
    if (vk == VK_SHIFT) return MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX);
    if (vk == VK_CONTROL) return ext ? VK_RCONTROL : VK_LCONTROL;
    if (vk == VK_MENU) return ext ? VK_RMENU : VK_LMENU;
    return (UINT)vk;
}

static void begin_edit(int row)
{
    SW.editing = row;
    wcopy(SW.edit, countof(SW.edit), g_cfg.everything_filter);
    SW.edit_len = SW.edit_caret = wlen(SW.edit);
    SW.caret_on = true;
    SetTimer(SW.hwnd, SETTINGS_TIMER_CARET, GetCaretBlinkTime(), NULL);
}

static void end_edit(bool commit)
{
    if (SW.editing < 0) return;
    if (commit && wcscmp(SW.edit, g_cfg.everything_filter)) {
        wcopy(g_cfg.everything_filter, countof(g_cfg.everything_filter), SW.edit);
        save_wstr("everything_filter", SW.edit);
    }
    SW.editing = -1;
    KillTimer(SW.hwnd, SETTINGS_TIMER_CARET);
    settings_invalidate();
}

static void edit_insert(const WCHAR *s, int n)
{
    for (int i = 0; i < n && SW.edit_len < (int)countof(SW.edit) - 1; i++) {
        if (s[i] < 0x20 || s[i] == 0x7F) continue;
        memmove(SW.edit + SW.edit_caret + 1, SW.edit + SW.edit_caret, (size_t)(SW.edit_len - SW.edit_caret + 1) * sizeof(WCHAR));
        SW.edit[SW.edit_caret++] = s[i];
        SW.edit_len++;
    }
}

static void edit_key(UINT vk, bool ctrl)
{
    switch (vk) {
    case VK_RETURN: end_edit(true); return;
    case VK_ESCAPE: end_edit(false); return;
    case VK_LEFT: if (SW.edit_caret > 0) SW.edit_caret--; break;
    case VK_RIGHT: if (SW.edit_caret < SW.edit_len) SW.edit_caret++; break;
    case VK_HOME: SW.edit_caret = 0; break;
    case VK_END: SW.edit_caret = SW.edit_len; break;
    case VK_BACK:
        if (SW.edit_caret > 0) {
            int from = SW.edit_caret - 1;
            if (ctrl) {
                while (from > 0 && SW.edit[from - 1] == ' ') from--;
                while (from > 0 && SW.edit[from - 1] != ' ') from--;
            }
            memmove(SW.edit + from, SW.edit + SW.edit_caret, (size_t)(SW.edit_len - SW.edit_caret + 1) * sizeof(WCHAR));
            SW.edit_len -= SW.edit_caret - from;
            SW.edit_caret = from;
        }
        break;
    case VK_DELETE:
        if (SW.edit_caret < SW.edit_len) {
            memmove(SW.edit + SW.edit_caret, SW.edit + SW.edit_caret + 1, (size_t)(SW.edit_len - SW.edit_caret) * sizeof(WCHAR));
            SW.edit_len--;
        }
        break;
    case 'V':
        if (ctrl && OpenClipboard(SW.hwnd)) {
            HANDLE h = GetClipboardData(CF_UNICODETEXT);
            const WCHAR *p = h ? (const WCHAR *)GlobalLock(h) : NULL;
            if (p) edit_insert(p, wlen(p));
            if (h) GlobalUnlock(h);
            CloseClipboard();
        }
        break;
    }
    SW.caret_on = true;
    settings_invalidate();
}

static int wrap_lines(int font, f32 size, const WCHAR *s, f32 max_w, int *at, int max_lines)
{
    int n = wlen(s), count = 0, pos = 0;
    while (pos < n && count < max_lines) {
        at[count++] = pos;
        f32 w = 0;
        int i = pos, space = -1;
        for (; i < n; i++) {
            if (s[i] == ' ') space = i;
            w += text_width(font, size, s + i, 1);
            if (w > max_w) break;
        }
        if (i >= n) break;
        pos = space > pos ? space : MAX(i, pos + 1);
        while (pos < n && s[pos] == ' ') pos++;
    }
    return count;
}

static f32 toggle_w(void) { return SSR(40); }

static f32 keycap_w(const KeyTok *t, int font, f32 fs, f32 h, f32 pad)
{
    f32 w = text_width(font, fs, t->label, -1);
    if (t->side) w += text_width(FONT_TEXT, fs * 0.7f, t->side, -1) + fs * 0.35f;
    return MAX(h, floorf(w + pad * 2));
}

static f32 keycaps_width(const KeyTok *t, int n, int font, f32 fs, f32 h, f32 pad, f32 gap)
{
    f32 w = 0;
    for (int i = 0; i < n; i++) w += keycap_w(&t[i], font, fs, h, pad) + (i ? gap : 0);
    return w;
}

static void control_size(const SItem *it, f32 *w, f32 *h)
{
    f32 fs = SSC(13.5f);
    *h = SSR(32);
    switch (it->kind) {
    case SK_TOGGLE:
        *w = toggle_w();
        *h = SSR(22);
        break;
    case SK_CHOICE: {
        if (is_seg(it)) {
            f32 xs[4], ws[4];
            *w = seg_layout(it, 0, xs, ws) + SSR(4);
            break;
        }
        f32 tw = 0;
        for (int k = 0; k < choice_count(it); k++) tw = MAX(tw, text_width(FONT_TEXT, fs, ss(it->opt[k]), -1));
        *w = MAX(SSR(130), floorf(tw + SSR(14) + SSR(10) + SSR(12) + SSR(14)));
        break;
    }
    case SK_STEPPER:
        *w = SSR(140);
        break;
    case SK_TEXT:
        *w = SSR(250);
        break;
    case SK_HOTKEY: {
        KeyTok t[5];
        int n = hk_tokens(g_cfg.hk, t);
        *w = MAX(SSR(130), floorf(SSR(6) + keycaps_width(t, n, FONT_TEXT, SSC(12.5f), SSR(22), SSR(7), SSR(4)) + SSR(12) + SSR(14) + SSR(12)));
        break;
    }
    case SK_BUTTON: {
        f32 tw = text_width(FONT_TEXT, fs, ss(it->opt[0]), -1);
        if (it->id == SID_CLEARHIST) tw = MAX(tw, text_width(FONT_TEXT, fs, ss((Str){ L"Точно очистить?", L"Clear for sure?" }), -1));
        if (it->id == SID_UPDNOW) tw = MAX(tw, text_width(FONT_TEXT, fs, ss((Str){ L"Обновить до 10.10.10", L"Update to 10.10.10" }), -1));
        *w = MAX(SSR(130), floorf(tw + SSR(14) + SSR(8) + SSR(32)));
        break;
    }
    case SK_INFO: {
        bool good;
        *w = floorf(text_width(FONT_TEXT, fs, info_text(it->id, &good), -1) + SSR(16));
        break;
    }
    }
}

static void settings_layout(void)
{
    SW.nrows = SW.ncards = SW.nheads = 0;
    f32 x0 = content_x0(), x1 = content_x1(), w = x1 - x0;
    f32 y = SSR(2);
    int card = -1;
    for (int i = 0; i < (int)SITEMS && SW.nrows < SROWS_MAX; i++) {
        const SItem *it = &k_sitems[i];
        if (it->page != SW.page) continue;
        SRow *r = &SW.rows[SW.nrows++];
        memset(r, 0, sizeof *r);
        r->item = i;
        r->x = x0;
        r->w = w;
        if (it->kind == SK_ABOUT) {
            r->y = y;
            r->ndesc = wrap_lines(FONT_TEXT, SSC(13.5f), ss(it->desc), w - SSR(4), r->desc_at, 4);
            r->h = SSR(64) + SSR(22) + SSR(21) * (f32)r->ndesc + SSR(6);
            y += r->h;
            card = -1;
            continue;
        }
        if (it->group.ru || card < 0) {
            if (SW.nrows > 1) y += SSR(20);
            if (it->group.ru && SW.nheads < (int)countof(SW.heads)) {
                SW.heads[SW.nheads] = (SBox){ x0 + SSR(4), y + SSR(15), 0, 0 };
                SW.head_text[SW.nheads++] = it->group;
                y += SSR(28);
            }
            if (SW.ncards < (int)countof(SW.cards)) {
                card = SW.ncards++;
                SW.cards[card] = (SBox){ x0, y, w, 0 };
            }
            r->first = true;
        }
        control_size(it, &r->cw, &r->ch);
        r->cx = floorf(x1 - SSR(18) - r->cw);
        f32 tx = x0 + SSR(18), tmax = r->cx - SSR(28) - tx;
        const WCHAR *desc = it->desc.ru ? ss(it->desc) : NULL;
        if (desc) r->ndesc = wrap_lines(FONT_TEXT, SSC(12.5f), desc, tmax, r->desc_at, 4);
        r->y = y;
        r->h = r->ndesc ? SSR(14) + SSR(20) + SSR(3) + SSR(18) * (f32)r->ndesc + SSR(14) : SSR(54);
        r->ty = r->ndesc ? y + SSR(24) : y + floorf(r->h * 0.5f);
        r->cy = floorf(r->ty - r->ch * 0.5f);
        y += r->h;
        if (card >= 0) SW.cards[card].h = y - SW.cards[card].y;
    }
    SW.content_h = y + SSR(28);
}

static f32 max_scroll_s(void) { return MAX(0.f, SW.content_h - ((f32)SW.H - top_h())); }

static bool row_focusable(int r)
{
    if (r < 0 || r >= SW.nrows) return false;
    u8 k = k_sitems[SW.rows[r].item].kind;
    return k != SK_INFO && k != SK_ABOUT;
}

typedef struct SColors {
    u32 bg, panel, panel_border, card, card_border, sep, nav_sel, nav_sel_border, nav_hover, ctl, ctl_hover, field, field_border;
    u32 seg, seg_border;
    u32 pop, pop_border, pop_hover, shadow, overlay, knob, cap, cap_border;
} SColors;

static SColors scolors(bool dark)
{
    SColors c;
    if (dark) {
        c.bg = RGBA(30, 30, 32, 255);
        c.panel = RGBA(255, 255, 255, 9);
        c.panel_border = RGBA(255, 255, 255, 15);
        c.card = RGBA(255, 255, 255, 10);
        c.card_border = RGBA(255, 255, 255, 6);
        c.sep = RGBA(255, 255, 255, 14);
        c.nav_sel = RGBA(255, 255, 255, 20);
        c.nav_sel_border = RGBA(255, 255, 255, 12);
        c.nav_hover = RGBA(255, 255, 255, 11);
        c.ctl = RGBA(255, 255, 255, 16);
        c.ctl_hover = RGBA(255, 255, 255, 28);
        c.field = RGBA(0, 0, 0, 56);
        c.field_border = RGBA(255, 255, 255, 26);
        c.seg = RGBA(255, 255, 255, 30);
        c.seg_border = RGBA(255, 255, 255, 14);
        c.pop = RGBA(44, 44, 47, 252);
        c.pop_border = RGBA(255, 255, 255, 22);
        c.pop_hover = RGBA(255, 255, 255, 20);
        c.shadow = RGBA(0, 0, 0, 120);
        c.overlay = RGBA(0, 0, 0, 120);
        c.knob = RGBA(255, 255, 255, 255);
        c.cap = RGBA(255, 255, 255, 24);
        c.cap_border = RGBA(255, 255, 255, 22);
    } else {
        c.bg = RGBA(243, 243, 245, 255);
        c.panel = RGBA(255, 255, 255, 120);
        c.panel_border = RGBA(0, 0, 0, 14);
        c.card = RGBA(255, 255, 255, 185);
        c.card_border = RGBA(0, 0, 0, 12);
        c.sep = RGBA(0, 0, 0, 16);
        c.nav_sel = RGBA(0, 0, 0, 13);
        c.nav_sel_border = RGBA(0, 0, 0, 8);
        c.nav_hover = RGBA(0, 0, 0, 8);
        c.ctl = RGBA(0, 0, 0, 10);
        c.ctl_hover = RGBA(0, 0, 0, 18);
        c.field = RGBA(255, 255, 255, 230);
        c.field_border = RGBA(0, 0, 0, 30);
        c.seg = RGBA(255, 255, 255, 255);
        c.seg_border = RGBA(0, 0, 0, 16);
        c.pop = RGBA(251, 251, 252, 252);
        c.pop_border = RGBA(0, 0, 0, 22);
        c.pop_hover = RGBA(0, 0, 0, 10);
        c.shadow = RGBA(0, 0, 0, 60);
        c.overlay = RGBA(0, 0, 0, 60);
        c.knob = RGBA(255, 255, 255, 255);
        c.cap = RGBA(255, 255, 255, 230);
        c.cap_border = RGBA(0, 0, 0, 24);
    }
    return c;
}

static f32 baseline_for(int font, f32 fs, f32 cy) { return floorf(cy + font_cap_height(font, fs) * 0.5f + 0.5f); }

static void draw_app_tile(f32 x, f32 y, f32 sz)
{
    x = floorf(x);
    y = floorf(y);
    f32 rad = floorf(sz * 0.225f + 0.5f);
    r_rect(x, y, sz, sz, RGBA(17, 17, 19, 255), rad);
    r_rect_ex(x, y, sz, sz, RGBA(255, 255, 255, 26), rad, 1.f, 0);
    f32 bars[3][4];
    app_logo_bars(floorf(sz), bars);
    for (int i = 0; i < 3; i++)
        r_rect(x + bars[i][0], y + bars[i][1], bars[i][2], bars[i][3], RGBA(255, 255, 255, i == 1 ? 255 : (int)(APP_LOGO_DIM * 255.f + 0.5f)), bars[i][3] * 0.5f);
}

static void draw_skeycaps(f32 x, f32 cy, const KeyTok *t, int n, int font, f32 fs, f32 h, f32 pad, f32 gap, f32 rad, u32 bg, u32 border,
                         u32 fg, u32 side_fg)
{
    for (int i = 0; i < n; i++) {
        f32 w = keycap_w(&t[i], font, fs, h, pad), y = floorf(cy - h * 0.5f);
        r_rect(x, y, w, h, bg, rad);
        if (border) r_rect_ex(x, y, w, h, border, rad, 1.f, 0);
        f32 sfs = fs * 0.7f, lw = text_width(font, fs, t[i].label, -1), sw = 0;
        if (t[i].side) sw = text_width(FONT_TEXT, sfs, t[i].side, -1) + fs * 0.35f;
        f32 tx = floorf(x + (w - lw - sw) * 0.5f + 0.5f), base = baseline_for(font, fs, cy);
        if (t[i].side) {
            text_draw(FONT_TEXT, sfs, tx, base, t[i].side, -1, side_fg);
            tx += sw;
        }
        text_draw(font, fs, tx, base, t[i].label, -1, fg);
        x += w + gap;
    }
}

static void draw_sidebar(const Theme *t, const SColors *c)
{
    f32 x = sb_x(), y = sb_x(), w = sb_w(), h = (f32)SW.H - sb_x() * 2;
    r_rect(x, y, w, h, c->panel, SSR(12));
    r_rect_ex(x, y, w, h, c->panel_border, SSR(12), 1.f, 0);

    f32 tile = SSR(36), tx = x + SSR(16), ty = y + SSR(18);
    draw_app_tile(tx, ty, tile);
    f32 nx = tx + tile + SSR(12);
    text_draw_fit(FONT_TEXT_SEMIBOLD, SSC(15), nx, floorf(ty + SSR(16)), x + w - nx - SSR(10), L"MixLauncher", -1, t->text, false);
    text_draw_fit(FONT_TEXT, SSC(12.5f), nx, floorf(ty + SSR(33)), x + w - nx - SSR(10), ss((Str){ L"Настройки", L"Settings" }), -1, t->dim, false);

    {
        f32 r[4];
        nav_rect(SW.page, r);
        f32 py = floorf(SW.nav_y + 0.5f), ph = r[3] - r[1];
        r_rect(r[0], py, r[2] - r[0], ph, c->nav_sel, SSR(8));
        r_rect_ex(r[0], py, r[2] - r[0], ph, c->nav_sel_border, SSR(8), 1.f, 0);
        f32 bh = floorf(MIN(SSR(16) + fabsf(SW.nav_v) * 0.02f, ph - SSR(8)) + 0.5f), bw = SSR(3);
        r_rect(r[0] + SSR(4), floorf(py + (ph - bh) * 0.5f + 0.5f), bw, bh, t->accent, bw * 0.5f);
    }
    for (int i = 0; i < PG__COUNT; i++) {
        f32 r[4];
        nav_rect(i, r);
        bool sel = SW.page == i;
        if (SW.nav_hov[i] > 0) r_rect(r[0], r[1], r[2] - r[0], r[3] - r[1], color_alpha(c->nav_hover, SW.nav_hov[i]), SSR(8));
        f32 cy = (r[1] + r[3]) * 0.5f, isz = SSR(20);
        f32 ix = r[0] + SSR(10);
        u32 fg = sel ? t->text : color_mix(t->dim, t->text, SW.nav_hov[i]);
        text_draw_icon(k_pages[i].glyph, SSC(15), ix + isz * 0.5f, cy, sel ? t->text : color_mix(t->faint, t->dim, SW.nav_hov[i]));
        f32 fs = SSC(14);
        text_draw_fit(sel ? FONT_TEXT_SEMIBOLD : FONT_TEXT, fs, ix + isz + SSR(12), baseline_for(FONT_TEXT, fs, cy), r[2] - ix - isz - SSR(18),
                      ss(k_pages[i].name), -1, fg, false);
    }
}

static void draw_toggle(f32 x, f32 cy, f32 on, bool hov, const Theme *t, const SColors *c)
{
    f32 w = toggle_w(), h = SSR(22), y = floorf(cy - h * 0.5f);
    u32 off_bg = hov ? c->ctl_hover : c->ctl;
    r_rect(x, y, w, h, color_mix(off_bg, t->accent, on), h * 0.5f);
    if (on < 1.f) r_rect_ex(x, y, w, h, color_alpha(hov ? t->dim : t->faint, 1.f - on), h * 0.5f, 1.f, 0);
    f32 k = floorf(h - SSR(10) + SSR(4) * on + 0.5f);
    f32 kcx = x + h * 0.5f + (w - h) * on, ky = cy - k * 0.5f;
    u32 knob = c->knob;
    f32 lum = (0.299f * (f32)(t->accent & 255) + 0.587f * (f32)((t->accent >> 8) & 255) + 0.114f * (f32)((t->accent >> 16) & 255)) / 255.f;
    if (lum > 0.62f) knob = RGBA(24, 24, 26, 255);
    r_rect(floorf(kcx - k * 0.5f + 0.5f), ky, k, k, color_mix(hov ? t->text : t->dim, knob, on), k * 0.5f);
}

static void draw_srow(SRow *r, f32 off, const Theme *t, const SColors *c)
{
    const SItem *it = &k_sitems[r->item];
    f32 y = r->y + off, ty = r->ty + off;
    int ri = (int)(r - SW.rows);
    bool hov = SW.hover.type == HT_ROW && SW.hover.idx == ri;
    f32 fs = SSC(14), dfs = SSC(12.5f), cfs = SSC(13.5f);

    if (it->kind == SK_ABOUT) {
        f32 tile = SSR(64);
        draw_app_tile(r->x, y + SSR(2), tile);
        f32 nx = r->x + tile + SSR(18);
        text_draw(FONT_TEXT_SEMIBOLD, SSC(22), nx, floorf(y + SSR(32)), L"MixLauncher", -1, t->text);
        text_draw(FONT_TEXT, SSC(13), nx, floorf(y + SSR(54)), ss((Str){ L"Версия " ML_VER_WSTR, L"Version " ML_VER_WSTR }), -1, t->dim);
        const WCHAR *d = ss(it->desc);
        int n = wlen(d);
        for (int k = 0; k < r->ndesc; k++) {
            int a = r->desc_at[k], b = k + 1 < r->ndesc ? r->desc_at[k + 1] : n;
            while (b > a && d[b - 1] == ' ') b--;
            text_draw_fit(FONT_TEXT, SSC(13.5f), r->x + SSR(2), floorf(y + SSR(64) + SSR(34) + SSR(21) * (f32)k), r->w - SSR(4), d + a, b - a,
                          t->dim, false);
        }
        return;
    }

    if (!r->first) r_rect(r->x + SSR(18), y, r->w - SSR(36), 1, c->sep, 0);
    if (SW.row_hov_idx == ri && SW.row_hov > 0.f) {
        f32 g = SSR(4);
        r_rect(r->x + g, y + g, r->w - g * 2, r->h - g * 2, color_alpha(c->nav_hover, SW.row_hov), SSR(7));
    }
    f32 tx = r->x + SSR(18), tmax = r->cx - SSR(28) - tx;
    text_draw_fit(FONT_TEXT, fs, tx, baseline_for(FONT_TEXT, fs, ty), tmax, ss(it->title), -1, t->text, false);
    if (r->ndesc) {
        const WCHAR *d = ss(it->desc);
        int n = wlen(d);
        for (int k = 0; k < r->ndesc; k++) {
            int a = r->desc_at[k], b = k + 1 < r->ndesc ? r->desc_at[k + 1] : n;
            while (b > a && d[b - 1] == ' ') b--;
            f32 lc = y + SSR(14) + SSR(20) + SSR(3) + SSR(18) * (f32)k + SSR(9);
            text_draw_fit(FONT_TEXT, dfs, tx, baseline_for(FONT_TEXT, dfs, lc), tmax, d + a, b - a, t->dim, false);
        }
    }

    f32 cx = r->cx, cy = r->cy + off, cw = r->cw, ch = r->ch, mid = cy + ch * 0.5f;
    bool chov = hov && SW.hover.part != PART_ROW;
    switch (it->kind) {
    case SK_TOGGLE:
        draw_toggle(cx, mid, SW.tog[r->item], hov, t, c);
        break;
    case SK_CHOICE: {
        if (is_seg(it)) {
            f32 xs[4], ws[4];
            int n = choice_count(it), cur = sval(it->id);
            seg_layout(it, cx + SSR(2), xs, ws);
            r_rect(cx, cy, cw, ch, c->ctl, SSR(8));
            f32 p = CLAMP(SW.seg[r->item], 0.f, (f32)(n - 1));
            int k0 = (int)floorf(p), k1 = MIN(k0 + 1, n - 1);
            f32 fr = p - (f32)k0, px = floorf(xs[k0] + (xs[k1] - xs[k0]) * fr + 0.5f), pw = floorf(ws[k0] + (ws[k1] - ws[k0]) * fr + 0.5f);
            if (!t->dark) r_rect_ex(px, cy + SSR(3), pw, ch - SSR(4), RGBA(0, 0, 0, 22), SSR(6), 0, SSR(2));
            r_rect(px, cy + SSR(2), pw, ch - SSR(4), c->seg, SSR(6));
            r_rect_ex(px, cy + SSR(2), pw, ch - SSR(4), c->seg_border, SSR(6), 1.f, 0);
            int hk = chov ? seg_index_at(it, cx, (f32)SW.mx) : -1;
            for (int k = 0; k < n; k++) {
                const WCHAR *lbl = ss(it->opt[k]);
                f32 lw = text_width(FONT_TEXT, cfs, lbl, -1);
                u32 col = k == cur ? t->text : k == hk ? color_mix(t->dim, t->text, 0.6f) : t->dim;
                text_draw(FONT_TEXT, cfs, floorf(xs[k] + (ws[k] - lw) * 0.5f + 0.5f), baseline_for(FONT_TEXT, cfs, mid), lbl, -1, col);
            }
            break;
        }
        bool open = SW.pop_open && SW.pop_row == ri;
        r_rect(cx, cy, cw, ch, chov || open ? c->ctl_hover : c->ctl, SSR(7));
        const WCHAR *lbl = ss(it->opt[sval(it->id)]);
        f32 gx = cx + cw - SSR(14) - SSR(5);
        text_draw_fit(FONT_TEXT, cfs, cx + SSR(14), baseline_for(FONT_TEXT, cfs, mid), gx - SSR(10) - cx - SSR(14), lbl, -1, t->text, false);
        text_draw_icon(0xE70D, SSC(10), gx, mid, t->dim);
        break;
    }
    case SK_STEPPER: {
        int v = sval(it->id);
        f32 bw = SSR(34);
        bool can_dec = v > it->lo, can_inc = v < it->hi;
        r_rect(cx, cy, cw, ch, c->ctl, SSR(7));
        if (hov && SW.hover.part == PART_MINUS && can_dec) r_rect(cx, cy, bw, ch, c->ctl_hover, SSR(7));
        if (hov && SW.hover.part == PART_PLUS && can_inc) r_rect(cx + cw - bw, cy, bw, ch, c->ctl_hover, SSR(7));
        r_rect(cx + bw, cy + SSR(7), 1, ch - SSR(14), c->sep, 0);
        r_rect(cx + cw - bw, cy + SSR(7), 1, ch - SSR(14), c->sep, 0);
        text_draw_icon(0xE738, SSC(11), cx + bw * 0.5f, mid, can_dec ? t->text : t->faint);
        text_draw_icon(0xE710, SSC(11), cx + cw - bw * 0.5f, mid, can_inc ? t->text : t->faint);
        WCHAR num[16];
        _snwprintf(num, countof(num), L"%d", v);
        num[15] = 0;
        const WCHAR *unit = it->unit.ru ? ss(it->unit) : NULL;
        f32 ufs = SSC(12), nw = text_width(FONT_TEXT, cfs, num, -1), uw = unit ? text_width(FONT_TEXT, ufs, unit, -1) + SSR(4) : 0;
        f32 nx = floorf(cx + (cw - nw - uw) * 0.5f + 0.5f), base = baseline_for(FONT_TEXT, cfs, mid);
        text_draw(FONT_TEXT, cfs, nx, base, num, -1, t->text);
        if (unit) text_draw(FONT_TEXT, ufs, nx + nw + SSR(4), base, unit, -1, t->dim);
        break;
    }
    case SK_TEXT: {
        bool editing = SW.editing == ri;
        r_rect(cx, cy, cw, ch, c->field, SSR(7));
        r_rect_ex(cx, cy, cw, ch, editing ? t->accent : chov ? color_alpha(c->field_border, 1.8f) : c->field_border, SSR(7),
                  editing ? SSR(1.5f) : 1.f, 0);
        const WCHAR *s = editing ? SW.edit : g_cfg.everything_filter;
        f32 ix = cx + SSR(10), iw = cw - SSR(20), base = baseline_for(FONT_TEXT, cfs, mid);
        r_set_clip(ix - SSR(1), MAX(cy, top_h()), ix + iw + SSR(1), cy + ch);
        if (!s[0] && !editing) {
            text_draw(FONT_TEXT, cfs, ix, base, ss((Str){ L"Нет", L"None" }), -1, t->faint);
        } else {
            f32 offs[513];
            int n = wlen(s);
            text_offsets(FONT_TEXT, cfs, s, n, offs);
            f32 caret_x = editing ? offs[SW.edit_caret] : 0;
            f32 shift = caret_x > iw - SSR(2) ? caret_x - iw + SSR(2) : 0;
            text_draw(FONT_TEXT, cfs, ix - shift, base, s, n, t->text);
            if (editing && SW.caret_on)
                r_rect(floorf(ix - shift + caret_x), floorf(mid - cfs * 0.6f), MAX(1.f, floorf(SSC(1.25f))), floorf(cfs * 1.2f), t->text, 0);
        }
        r_set_clip(0, top_h(), (f32)SW.W, (f32)SW.H);
        break;
    }
    case SK_HOTKEY: {
        r_rect(cx, cy, cw, ch, chov ? c->ctl_hover : c->ctl, SSR(7));
        KeyTok tk[5];
        int n = hk_tokens(g_cfg.hk, tk);
        f32 kfs = SSC(12.5f), kh = SSR(22);
        draw_skeycaps(cx + SSR(6), mid, tk, n, FONT_TEXT, kfs, kh, SSR(7), SSR(4), SSR(5), c->cap, 0, t->text, t->faint);
        text_draw_icon(0xE70F, SSC(12), cx + cw - SSR(12) - SSR(7), mid, chov ? t->text : t->dim);
        break;
    }
    case SK_BUTTON: {
        bool armed = it->id == SID_CLEARHIST && SW.armed;
        bool flashing = SW.flash_id == it->id && time_now() < SW.flash_until;
        u32 bg = armed ? color_alpha(t->danger, 0.2f) : chov ? c->ctl_hover : c->ctl;
        r_rect(cx, cy, cw, ch, bg, SSR(7));
        const WCHAR *lbl = armed ? ss((Str){ L"Точно очистить?", L"Clear for sure?" }) : flashing ? ss((Str){ L"Готово", L"Done" }) : ss(it->opt[0]);
        u32 glyph = flashing ? 0xE73E : it->glyph;
        u32 col = armed ? t->danger : flashing ? t->accent : t->text;
        WCHAR ub[64];
        if (it->id == SID_UPDNOW && upd_state() != UPD_IDLE) {
            lbl = upd_label(ub, countof(ub));
            int st = upd_state();
            glyph = st == UPD_LATEST ? 0xE73E : st == UPD_ERROR ? 0xE72C : 0xE896;
            col = st == UPD_ERROR ? t->danger : t->text;
            if (st == UPD_AVAILABLE) r_rect(cx, cy, cw, ch, color_alpha(t->accent, chov ? 0.30f : 0.20f), SSR(7));
        }
        f32 lw = text_width(FONT_TEXT, cfs, lbl, -1), gw = glyph ? SSR(14) + SSR(8) : 0;
        f32 bx = floorf(cx + (cw - lw - gw) * 0.5f + 0.5f);
        if (glyph) text_draw_icon(glyph, SSC(13), bx + SSR(7), mid, col);
        text_draw(FONT_TEXT, cfs, bx + gw, baseline_for(FONT_TEXT, cfs, mid), lbl, -1, col);
        break;
    }
    case SK_INFO: {
        bool good;
        const WCHAR *s = info_text(it->id, &good);
        f32 dot = SSR(8);
        r_rect(cx, floorf(mid - dot * 0.5f), dot, dot, good ? RGBA(48, 176, 99, 255) : t->danger, dot * 0.5f);
        text_draw(FONT_TEXT, cfs, cx + dot + SSR(8), baseline_for(FONT_TEXT, cfs, mid), s, -1, t->text);
        break;
    }
    }
    if (SW.focus == ri && SW.focus_visible && row_focusable(ri)) {
        f32 g = SSR(3);
        r_rect_ex(cx - g, cy - g, cw + g * 2, ch + g * 2, t->accent, SSR(9), SSR(2), 0);
    }
}

static void draw_popover(const Theme *t, const SColors *c)
{
    const SItem *it = &k_sitems[SW.rows[SW.pop_row].item];
    f32 x = SW.pop_x, y = SW.pop_y - floorf((1.f - ease_out_cubic(SW.pop_t)) * SSR(6) + 0.5f), w = SW.pop_w, h = SW.pop_h;
    r_rect_ex(x, y + SSR(6), w, h, c->shadow, SSR(10), 0, SSR(14));
    r_rect(x, y, w, h, c->pop, SSR(10));
    r_rect_ex(x, y, w, h, c->pop_border, SSR(10), 1.f, 0);
    int sel = sval(it->id);
    f32 fs = SSC(13.5f);
    for (int k = 0; k < choice_count(it); k++) {
        f32 iy = y + SSR(5) + SSR(32) * (f32)k, cy = iy + SSR(16);
        if (SW.pop_hover == k) r_rect(x + SSR(5), iy, w - SSR(10), SSR(32), c->pop_hover, SSR(6));
        if (k == sel) text_draw_icon(0xE73E, SSC(12), x + SSR(5) + SSR(16), cy, t->text);
        text_draw(FONT_TEXT, fs, x + SSR(5) + SSR(32), baseline_for(FONT_TEXT, fs, cy), ss(it->opt[k]), -1, t->text);
    }
}

static void draw_recorder(const Theme *t, const SColors *c)
{
    r_rect(0, 0, (f32)SW.W, (f32)SW.H, c->overlay, 0);
    f32 pw = SSR(460), ph = SSR(260);
    f32 px = floorf(((f32)SW.W - pw) * 0.5f), py = floorf(((f32)SW.H - ph) * 0.5f + (1.f - ease_out_cubic(SW.rec_t)) * SSR(12));
    SW.rec_panel[0] = px;
    SW.rec_panel[1] = py;
    SW.rec_panel[2] = px + pw;
    SW.rec_panel[3] = py + ph;
    r_rect_ex(px, py + SSR(12), pw, ph, c->shadow, SSR(16), 0, SSR(26));
    r_rect(px, py, pw, ph, t->dark ? RGBA(36, 36, 39, 255) : RGBA(252, 252, 253, 255), SSR(16));
    r_rect_ex(px, py, pw, ph, c->pop_border, SSR(16), 1.f, 0);

    text_draw(FONT_TEXT, SSC(16), px + SSR(26), py + SSR(42), ss((Str){ L"Задайте сочетание…", L"Set Hotkey…" }), -1, t->faint);

    KeyTok tk[5];
    int n = 0;
    bool live = (SW.rec.held || SW.rec.key) && !SW.rec.used;
    if (live) {
        Hotkey h = { rec_generic(SW.rec.held), 0 };
        n = hk_tokens(h, tk);
    } else if (SW.rec.has) {
        n = hk_tokens(SW.rec.hk, tk);
    }
    f32 kcy = py + SSR(122), kfs = SSC(20), kh = SSR(54), kpad = SSR(16), kgap = SSR(10);
    const Str *problem = SW.rec.has ? hotkey_problem(SW.rec.hk) : NULL;
    if (n) {
        f32 kw = keycaps_width(tk, n, FONT_TEXT_SEMIBOLD, kfs, kh, kpad, kgap);
        u32 fg = (problem && !live) ? t->danger : t->text;
        draw_skeycaps(floorf(px + (pw - kw) * 0.5f), kcy, tk, n, FONT_TEXT_SEMIBOLD, kfs, kh, kpad, kgap, SSR(12),
                     t->dark ? RGBA(255, 255, 255, 24) : RGBA(0, 0, 0, 9), c->cap_border, fg, t->faint);
    } else {
        const WCHAR *s = ss((Str){ L"Нажмите клавишу или сочетание", L"Press a key or a shortcut" });
        f32 fs = SSC(15), w = text_width(FONT_TEXT, fs, s, -1);
        text_draw(FONT_TEXT, fs, floorf(px + (pw - w) * 0.5f), baseline_for(FONT_TEXT, fs, kcy), s, -1, t->faint);
    }
    const WCHAR *msg = NULL;
    u32 msg_col = t->faint;
    if (!live && problem && SW.rec.has) {
        msg = ss(*problem);
        msg_col = t->danger;
    } else if (!n || live) {
        msg = ss((Str){ L"Одна клавиша (Win, F1-F24, правый Ctrl…) или сочетание с Ctrl, Alt, Shift, Win",
                        L"A single key (Win, F1-F24, right Ctrl…) or a shortcut with Ctrl, Alt, Shift, Win" });
    }
    if (msg) {
        f32 fs = SSC(12.5f), w = MIN(text_width(FONT_TEXT, fs, msg, -1), pw - SSR(40));
        text_draw_fit(FONT_TEXT, fs, floorf(px + (pw - w) * 0.5f), baseline_for(FONT_TEXT, fs, py + SSR(180)), pw - SSR(40), msg, -1, msg_col, false);
    }

    f32 by = py + ph - SSR(56), bcy = by + SSR(28);
    r_rect(px, by, pw, 1, c->sep, 0);
    draw_app_tile(px + SSR(22), bcy - SSR(11), SSR(22));
    f32 fs = SSC(14);
    text_draw(FONT_TEXT, fs, px + SSR(22) + SSR(22) + SSR(10), baseline_for(FONT_TEXT, fs, bcy), ss((Str){ L"Открыть MixLauncher", L"Open MixLauncher" }), -1,
              t->text);

    bool can_save = SW.rec.has && !problem;
    f32 xr = px + pw - SSR(14);
    for (int b = 1; b >= 0; b--) {
        const WCHAR *lbl = b ? ss((Str){ L"Сохранить", L"Save" }) : ss((Str){ L"Отмена", L"Cancel" });
        KeyTok cap;
        wcopy(cap.label, countof(cap.label), b ? L"Enter" : L"Esc");
        cap.side = NULL;
        f32 cfs = SSC(12), ch = SSR(22), cw = keycap_w(&cap, FONT_TEXT, cfs, ch, SSR(6));
        f32 lw = text_width(FONT_TEXT, fs, lbl, -1);
        f32 bw = SSR(10) + lw + SSR(8) + cw + SSR(6), bh = SSR(34);
        f32 bx = xr - bw, byy = floorf(bcy - bh * 0.5f);
        f32 *rb = SW.rec_btn[b];
        rb[0] = bx;
        rb[1] = byy;
        rb[2] = bx + bw;
        rb[3] = byy + bh;
        bool hov = SW.hover.type == (b ? HT_RECSAVE : HT_RECCANCEL);
        bool enabled = !b || can_save;
        if (b && can_save) r_rect(bx, byy, bw, bh, color_alpha(t->accent, hov ? 0.32f : 0.22f), SSR(8));
        else if (hov && enabled) r_rect(bx, byy, bw, bh, c->ctl, SSR(8));
        u32 col = !enabled ? t->faint : t->text;
        text_draw(FONT_TEXT, fs, bx + SSR(10), baseline_for(FONT_TEXT, fs, bcy), lbl, -1, col);
        draw_skeycaps(bx + SSR(10) + lw + SSR(8), bcy, &cap, 1, FONT_TEXT, cfs, ch, SSR(6), 0, SSR(5), 0, c->cap_border, !enabled ? t->faint : t->dim,
                     t->faint);
        xr = bx - SSR(6);
    }
}

static bool sb_hot(void)
{
    return SW.hover.type != HT_WINCLOSE && SW.hover.type != HT_WINMIN && SW.mx >= SW.W - (int)SSR(16) && SW.my >= (int)top_h() && SW.my < SW.H;
}

static int row_hover_target(void)
{
    if (SW.hover.type != HT_ROW || SW.pop_open || SW.rec.open || SW.hover.idx < 0 || SW.hover.idx >= SW.nrows) return -1;
    return k_sitems[SW.rows[SW.hover.idx].item].kind == SK_TOGGLE ? SW.hover.idx : -1;
}

// "Support the author": the DonationAlerts QR (drawn from its modules), a link and crypto wallets to copy.
static const u64 k_don_qr[49] = {
    0x1fd2bd08f807full, 0x105fd5bfcf641ull, 0x1758b389c715dull, 0x174b0c6a5d95dull,
    0x17428eff8915dull, 0x1047c0476a541ull, 0x1fd555555557full, 0x00067d45dcb00ull,
    0x07d588fdec87dull, 0x0beb0f323b796ull, 0x1c1ceb57e46dbull, 0x098615aa0b6a3ull,
    0x0e938814dadc0ull, 0x08ca0d9e00bb5ull, 0x1e04e37e96962ull, 0x11c48c0a50182ull,
    0x06d91fd32f652ull, 0x08e138129db34ull, 0x138646f924be5ull, 0x09fa9f409fd1dull,
    0x1c57269d5c759ull, 0x09e11648f8f35ull, 0x1bf5eaff4d9f1ull, 0x091255c66e717ull,
    0x0d53ecd76af58ull, 0x07199e46f9518ull, 0x11f5607da71f8ull, 0x1822378a949a6ull,
    0x07f9e8f83524dull, 0x026ab56bb39b7ull, 0x1d4cdab15d2f6ull, 0x1a3095c5a6824ull,
    0x1d29068589ff0ull, 0x08a1855811c34ull, 0x134fe196ed376ull, 0x0196f57a2a69full,
    0x152d088fa905cull, 0x0abb17d2a6091ull, 0x16f5e20ce0be2ull, 0x10167df8b510eull,
    0x0fffce7de1ec7ull, 0x09130f44ef700ull, 0x1d55c854b3e7full, 0x1912154695541ull,
    0x17f1e87c8155dull, 0x13f11c840eb5dull, 0x00effa16dd55dull, 0x106a0c786fa41ull,
    0x1e2984a961b7full,
};
#define DON_QR_N 49
#define DON_LINK_URL L"https://dalink.to/mel1xy"

static const struct { const WCHAR *net, *addr; } k_wallets[8] = {
    { L"TRC20", L"TD1yryydTBLre9ATmhhMjcdBxUZSuLhcYd" },
    { L"ERC20", L"0x10B2945C3222F9Fb27CDcDBbEfF298961Db10e96" },
    { L"BEP20", L"0x10B2945C3222F9Fb27CDcDBbEfF298961Db10e96" },
    { L"ERC20", L"0x10B2945C3222F9Fb27CDcDBbEfF298961Db10e96" },
    { L"Bitcoin", L"bc1qrhtwhv75jrh8fz6y8qc38evkxrvg7ge0nu4z3w" },
    { L"Litecoin", L"LVtmyVY3YbMMKzjxhkVu5bgNFY5T7hhbf2" },
    { L"ERC20", L"0x10B2945C3222F9Fb27CDcDBbEfF298961Db10e96" },
    { L"TRC20", L"TD1yryydTBLre9ATmhhMjcdBxUZSuLhcYd" },
};

enum { LOGO_USDT, LOGO_ETH, LOGO_BTC, LOGO_LTC, LOGO_USDC, LOGO_TRX };

// One row per coin; USDT has a chip per network, the others copy their single wallet.
static const struct { const WCHAR *name; u32 top, bottom; int logo, wallet, nets; } k_coins[6] = {
    { L"USDT", RGBA(84, 196, 160, 255), RGBA(22, 126, 98, 255), LOGO_USDT, 0, 3 },
    { L"ETH", RGBA(146, 166, 252, 255), RGBA(76, 92, 212, 255), LOGO_ETH, 3, 1 },
    { L"BTC", RGBA(255, 190, 80, 255), RGBA(238, 118, 8, 255), LOGO_BTC, 4, 1 },
    { L"Litecoin", RGBA(112, 152, 222, 255), RGBA(38, 76, 146, 255), LOGO_LTC, 5, 1 },
    { L"USDC", RGBA(86, 164, 242, 255), RGBA(28, 92, 182, 255), LOGO_USDC, 6, 1 },
    { L"TRON", RGBA(255, 86, 104, 255), RGBA(196, 0, 32, 255), LOGO_TRX, 7, 1 },
};

// The logos no font has (Tether, Ethereum, TRON) as shapes in a unit box, rasterized into the glyph atlas.
static bool pt_in_poly(const f32 *p, int n, f32 x, f32 y)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        f32 xi = p[i * 2], yi = p[i * 2 + 1], xj = p[j * 2], yj = p[j * 2 + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

static bool near_seg(f32 ax, f32 ay, f32 bx, f32 by, f32 x, f32 y, f32 half)
{
    f32 dx = bx - ax, dy = by - ay, t = CLAMP(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0.f, 1.f);
    f32 ex = ax + dx * t - x, ey = ay + dy * t - y;
    return ex * ex + ey * ey <= half * half;
}

static bool logo_inside(int id, f32 x, f32 y)
{
    switch (id) {
    case LOGO_USDT: {
        if (x >= 0.10f && x <= 0.90f && y >= 0.14f && y <= 0.33f) return true;
        if (x >= 0.40f && x <= 0.60f && y >= 0.33f && y <= 0.90f) return true;
        f32 ox = (x - 0.5f) / 0.43f, oy = (y - 0.50f) / 0.13f, ix = (x - 0.5f) / 0.33f, iy = (y - 0.50f) / 0.065f;
        return ox * ox + oy * oy <= 1.f && ix * ix + iy * iy >= 1.f && y >= 0.40f;
    }
    case LOGO_ETH: {
        static const f32 top[] = { 0.5f, 0.0f, 0.82f, 0.545f, 0.5f, 0.735f, 0.18f, 0.545f };
        static const f32 bot[] = { 0.5f, 0.80f, 0.82f, 0.605f, 0.5f, 1.0f, 0.18f, 0.605f };
        return pt_in_poly(top, 4, x, y) || pt_in_poly(bot, 4, x, y);
    }
    case LOGO_TRX: {
        const f32 ax = 0.06f, ay = 0.10f, bx = 0.96f, by = 0.36f, cx = 0.44f, cy = 0.96f, dx = 0.66f, dy = 0.46f, w = 0.055f;
        return near_seg(ax, ay, bx, by, x, y, w) || near_seg(bx, by, cx, cy, x, y, w) || near_seg(cx, cy, ax, ay, x, y, w) ||
               near_seg(ax, ay, dx, dy, x, y, w) || near_seg(dx, dy, cx, cy, x, y, w) || near_seg(dx, dy, bx, by, x, y, w * 0.8f);
    }
    }
    return false;
}

static struct { int id, px; u32 gen; int x, y; } g_logo_cache[16];

static bool logo_region(int id, int px, int *ox, int *oy)
{
    int free_slot = -1;
    for (int i = 0; i < countof(g_logo_cache); i++) {
        if (g_logo_cache[i].px == px && g_logo_cache[i].id == id && g_logo_cache[i].gen == g_atlas_gen) {
            *ox = g_logo_cache[i].x;
            *oy = g_logo_cache[i].y;
            return true;
        }
        if (free_slot < 0 && (!g_logo_cache[i].px || g_logo_cache[i].gen != g_atlas_gen)) free_slot = i;
    }
    if (free_slot < 0) free_slot = 0;
    if (!R.glyph_cpu || !atlas_alloc(px, px, ox, oy)) {
        F.overflow = true;
        return false;
    }
    const int ss = 4;
    for (int yy = 0; yy < px; yy++)
        for (int xx = 0; xx < px; xx++) {
            int hit = 0;
            for (int sy = 0; sy < ss; sy++)
                for (int sx = 0; sx < ss; sx++)
                    hit += logo_inside(id, ((f32)xx + ((f32)sx + 0.5f) / ss) / (f32)px, ((f32)yy + ((f32)sy + 0.5f) / ss) / (f32)px);
            R.glyph_cpu[(size_t)(*oy + yy) * GLYPH_ATLAS + *ox + xx] = (u8)(hit * 255 / (ss * ss));
        }
    r_glyph_dirty(*ox, *oy, px, px);
    g_logo_cache[free_slot].id = id;
    g_logo_cache[free_slot].px = px;
    g_logo_cache[free_slot].gen = g_atlas_gen;
    g_logo_cache[free_slot].x = *ox;
    g_logo_cache[free_slot].y = *oy;
    return true;
}

// Squircle with the coin's vertical gradient and its white logo.
static void draw_coin_icon(int i, f32 x, f32 y, f32 sz)
{
    x = floorf(x);
    y = floorf(y);
    f32 rad = floorf(sz * 0.3f + 0.5f), saved[4];
    memcpy(saved, R.clip, sizeof saved);
    for (int r = 0; r < (int)sz; r++) {
        f32 y0 = MAX(y + (f32)r, saved[1]), y1 = MIN(y + (f32)r + 1.f, saved[3]);
        if (y1 <= y0) continue;
        r_set_clip(saved[0], y0, saved[2], y1);
        r_rect(x, y, sz, sz, color_mix(k_coins[i].top, k_coins[i].bottom, ((f32)r + 0.5f) / sz), rad);
    }
    memcpy(R.clip, saved, sizeof saved);
    u32 white = RGBA(255, 255, 255, 255);
    f32 cx = x + sz * 0.5f, cy = y + sz * 0.5f;
    int logo = k_coins[i].logo;
    if (logo == LOGO_USDT || logo == LOGO_ETH || logo == LOGO_TRX) {
        int px = (int)(sz * (logo == LOGO_ETH ? 0.62f : 0.58f) + 0.5f), ox, oy;
        if (logo_region(logo, px, &ox, &oy)) {
            const f32 inv = 1.0f / GLYPH_ATLAS;
            f32 lx = floorf(cx - (f32)px * 0.5f + 0.5f), ly = floorf(cy - (f32)px * 0.5f + 0.5f);
            r_glyph(lx, ly, (f32)px, (f32)px, (f32)ox * inv, (f32)oy * inv, (f32)(ox + px) * inv, (f32)(oy + px) * inv, white);
        }
        return;
    }
    if (logo == LOGO_USDC) {
        f32 ring = floorf(sz * 0.62f + 0.5f);
        r_rect_ex(floorf(cx - ring * 0.5f + 0.5f), floorf(cy - ring * 0.5f + 0.5f), ring, ring, white, ring * 0.5f, MAX(1.f, floorf(sz * 0.06f + 0.5f)), 0);
    }
    const WCHAR *sym = logo == LOGO_BTC ? L"\x20BF" : logo == LOGO_LTC ? L"\x0141" : L"$";
    f32 fs = sz * (logo == LOGO_USDC ? 0.40f : 0.56f), w = text_width(FONT_TEXT_SEMIBOLD, fs, sym, -1);
    text_draw(FONT_TEXT_SEMIBOLD, fs, floorf(cx - w * 0.5f + 0.5f), baseline_for(FONT_TEXT_SEMIBOLD, fs, cy), sym, -1, white);
}

#define DON_HOV_LINK 8
#define DON_HOV_CLOSE 9

static void don_open(void)
{
    end_edit(true);
    SW.pop_open = false;
    SW.don.open = true;
    SW.don.closing = false;
    SW.don.t = anims_enabled() ? 0.f : 1.f;
    SW.don.open_time = time_now();
    SW.don.scroll = SW.don.scroll_target = SW.don.scroll_v = 0;
    SW.don.sb_t = SW.don.sb_prev = 0;
    SW.don.sb_seen = 0;
    SW.don.flash = -1;
    memset(SW.don.hov, 0, sizeof SW.don.hov);
    memset(SW.don.cp, 0, sizeof SW.don.cp);
    settings_invalidate();
}

static void don_close(void)
{
    if (!SW.don.open) return;
    if (anims_enabled()) SW.don.closing = true;
    else SW.don.open = false;
    settings_invalidate();
}

static bool don_active(void) { return SW.don.open && !SW.don.closing; }

// Confetti over the settings window, in the canvas-confetti manner: small squares fired from cannons in
// both top corners, quickly braked by the air, then drifting down at a slow steady speed while they
// wobble sideways, spin and tumble (a horizontal squash that follows the wobble).
typedef struct Confetti { f32 x, y, vx, vy, fall, sway, size, rot, rot_v, wob, wob_v, age, life; u32 color; } Confetti;
static Confetti g_conf[320];
static int g_nconf;
static u32 g_conf_seed = 0x9E3779B9u;

static f32 conf_rand(void)
{
    g_conf_seed = g_conf_seed * 1664525u + 1013904223u;
    return (f32)(g_conf_seed >> 8) / 16777216.f;
}

static void confetti_burst(void)
{
    if (!anims_enabled()) return;
    static const u32 colors[] = { RGBA(255, 94, 58, 255), RGBA(255, 184, 0, 255), RGBA(46, 196, 182, 255), RGBA(58, 134, 255, 255),
                                  RGBA(255, 77, 141, 255), RGBA(150, 92, 255, 255) };
    g_conf_seed ^= (u32)(time_now() * 1000.0);
    for (int i = 0; i < 220; i++) {
        if (g_nconf == countof(g_conf)) memmove(g_conf, g_conf + 1, sizeof(Confetti) * (size_t)--g_nconf);
        Confetti *c = &g_conf[g_nconf++];
        bool left = i & 1;
        // from slightly below to 50 degrees above the horizon, toward the middle; a wide spread of speeds
        // scatters the bits over the whole window instead of two clumps
        f32 r = conf_rand(), a = (-8.f + conf_rand() * 58.f) * 0.0174533f, sp = SSC(500.f + (1.f - r * r) * 2600.f);
        c->x = left ? 0.f : (f32)SW.W;
        c->y = SSC(150.f + conf_rand() * 20.f);
        c->vx = cosf(a) * sp * (left ? 1.f : -1.f);
        c->vy = -sinf(a) * sp;
        c->fall = SSC(130.f + conf_rand() * 140.f);
        c->sway = SSC(5.f + conf_rand() * 12.f);
        c->size = SSC(6.f + conf_rand() * 4.f);
        c->rot = conf_rand() * 6.283f;
        c->rot_v = (conf_rand() - 0.5f) * 10.f;
        c->wob = conf_rand() * 6.283f;
        c->wob_v = 4.f + conf_rand() * 4.f;
        c->age = -conf_rand() * 0.08f;
        c->life = 3.2f + conf_rand() * 1.6f;
        c->color = colors[(int)(conf_rand() * countof(colors)) % countof(colors)];
    }
    settings_invalidate();
}

static bool confetti_animate(f32 dt)
{
    int n = 0;
    f32 k = expf(-6.3f * dt);  // canvas-confetti: velocity *= 0.9 per 60 Hz frame
    for (int i = 0; i < g_nconf; i++) {
        Confetti c = g_conf[i];
        c.age += dt;
        if (c.age >= c.life || c.y > (f32)SW.H + SSC(20)) continue;
        if (c.age > 0) {
            c.vx *= k;
            c.vy *= k;
            c.x += c.vx * dt;
            c.y += (c.vy + c.fall) * dt;
            c.wob += c.wob_v * dt;
            c.rot += c.rot_v * dt;
        }
        g_conf[n++] = c;
    }
    g_nconf = n;
    return n > 0;
}

static void confetti_draw(void)
{
    for (int i = 0; i < g_nconf; i++) {
        Confetti *c = &g_conf[i];
        if (c->age < 0) continue;
        f32 fade = CLAMP((c->life - c->age) / 0.6f, 0.f, 1.f), tumble = cosf(c->wob);
        f32 x = c->x + sinf(c->wob) * c->sway;
        u32 col = color_mix(c->color, RGBA(0, 0, 0, 255), (1.f - fabsf(tumble)) * 0.3f);
        r_rect_rot(x, c->y, c->size, c->size, c->rot, MAX(0.12f, fabsf(tumble)), color_alpha(col, fade), SSC(1.f));
    }
}

static void don_copy(int w)
{
    clipboard_set(k_wallets[w].addr, -1);
    SW.don.flash = w;
    SW.don.flash_until = time_now() + 1.4;
    SetTimer(SW.hwnd, SETTINGS_TIMER_FLASH, 1500, NULL);
    settings_invalidate();
}

static void box_set(f32 *b, f32 x, f32 y, f32 w, f32 h)
{
    b[0] = x;
    b[1] = y;
    b[2] = x + w;
    b[3] = y + h;
}

#define DON_STAGGER 0.035f
#define DON_ITEM_DUR 0.32f
#define DON_ITEMS 11

// Entrance of the k-th block of the modal (header, QR, link, rows...), staggered after opening.
static f32 don_item(int k)
{
    if (!anims_enabled()) return 1.f;
    f32 t = (f32)(time_now() - SW.don.open_time) - DON_STAGGER * (f32)k - 0.05f;
    return ease_out_cubic(t / anim_sec((int)(DON_ITEM_DUR * 1000.f)));
}

static bool don_animate(f32 dt)
{
    if (!SW.don.open) return false;
    bool anim = false;
    if (SW.don.closing) {
        f32 d = anim_sec(130);
        SW.don.t = d > 0.f ? SW.don.t - dt / d : 0.f;
        if (SW.don.t <= 0.f) {
            SW.don.t = 0;
            SW.don.open = SW.don.closing = false;
        }
        return true;
    }
    if (SW.don.t < 1.f) {
        f32 d = anim_sec(240);
        SW.don.t = d > 0.f ? MIN(1.f, SW.don.t + dt / d) : 1.f;
        anim = true;
    }
    if (time_now() - SW.don.open_time < anim_sec((int)((DON_ITEM_DUR + DON_STAGGER * DON_ITEMS + 0.05f) * 1000.f))) anim = true;
    f32 ms = MAX(0.f, SW.don.content_h - SW.don.view_h);
    SW.don.scroll_target = CLAMP(SW.don.scroll_target, 0.f, ms);
    anim |= spring_step(&SW.don.scroll, &SW.don.scroll_v, SW.don.scroll_target, anim_omega(g_cfg.anim_scroll_ms), dt);
    f64 now = time_now();
    if (SW.don.scroll != SW.don.sb_prev) SW.don.sb_seen = now;
    SW.don.sb_prev = SW.don.scroll;
    bool recent = now - SW.don.sb_seen < 0.9;
    anim |= approach(&SW.don.sb_t, recent && ms > 0 ? 1.f : 0.f, recent ? 20.f : 7.f, dt);
    if (recent && ms > 0) anim = true;
    bool flash_on = SW.don.flash >= 0 && now < SW.don.flash_until;
    for (int i = 0; i < 8; i++) anim |= approach(&SW.don.cp[i], flash_on && SW.don.flash == i ? 1.f : 0.f, 16.f, dt);
    for (int i = 0; i < 10; i++) {
        int want = i == DON_HOV_LINK ? DON_LINK : i == DON_HOV_CLOSE ? DON_CLOSE : i;
        anim |= approach(&SW.don.hov[i], SW.hover.type == HT_DON && SW.hover.idx == want ? 1.f : 0.f, 20.f, dt);
    }
    return anim;
}

static void draw_donate(const Theme *t, const SColors *c)
{
    f32 e = SW.don.closing ? SW.don.t * SW.don.t : ease_out_quart(SW.don.t);
    R.opacity = e;
    r_rect(0, 0, (f32)SW.W, (f32)SW.H, c->overlay, 0);
    f32 pw = SSR(380), m = SSR(16);
    f32 qr_sz = SSR(184), row_h = SSR(54), row_gap = SSR(8), link_h = SSR(32);
    qr_sz = floorf(qr_sz / (DON_QR_N + 7)) * (DON_QR_N + 7);
    f32 content = SSR(20) + SSR(24) + SSR(16) + qr_sz + SSR(16) + link_h + SSR(22) + SSR(20) + (row_h + row_gap) * 6.f - row_gap + SSR(18);
    SW.don.content_h = content;
    f32 ph = MIN(content, (f32)SW.H - m * 2);
    SW.don.view_h = ph;
    f32 ms = MAX(0.f, content - ph);
    SW.don.scroll = CLAMP(SW.don.scroll, 0.f, ms);
    f32 px = floorf(((f32)SW.W - pw) * 0.5f), py = floorf(((f32)SW.H - ph) * 0.5f + (1.f - e) * SSR(SW.don.closing ? 8 : 22));
    box_set(SW.don.panel, px, py, pw, ph);
    r_rect_ex(px, py + SSR(14), pw, ph, c->shadow, SSR(16), 0, SSR(30));
    r_rect(px, py, pw, ph, t->dark ? RGBA(36, 36, 39, 255) : RGBA(252, 252, 253, 255), SSR(16));
    r_rect_ex(px, py, pw, ph, c->pop_border, SSR(16), 1.f, 0);

    r_set_clip(px, py + 1, px + pw, py + ph - 1);
    f32 x0 = px + SSR(20), x1 = px + pw - SSR(20), y = py + SSR(20) - floorf(SW.don.scroll + 0.5f);
    int k = 0;
#define DON_BLOCK(dy_var)                                   \
    f32 ek_ = SW.don.closing ? 1.f : don_item(k++);         \
    R.opacity = e * ek_;                                    \
    f32 dy_var = floorf((1.f - ek_) * SSR(10) + 0.5f)

    // header
    {
        DON_BLOCK(dy);
        f32 hy = y + dy;
        text_draw(FONT_TEXT_SEMIBOLD, SSC(17), x0, baseline_for(FONT_TEXT_SEMIBOLD, SSC(17), hy + SSR(12)),
                  ss((Str){ L"Поддержать автора", L"Support the author" }), -1, t->text);
        f32 cb = SSR(30), hv = SW.don.hov[DON_HOV_CLOSE];
        box_set(SW.don.close, x1 - cb + SSR(6), y - SSR(3), cb, cb);
        if (hv > 0) r_rect(SW.don.close[0], SW.don.close[1] + dy, cb, cb, color_alpha(c->ctl_hover, hv), SSR(8));
        text_draw_icon(0xE8BB, SSC(10), SW.don.close[0] + cb * 0.5f, SW.don.close[1] + dy + cb * 0.5f, color_mix(t->dim, t->text, hv));
    }
    y += SSR(24) + SSR(16);

    // QR: dark modules on a white tile with a quiet zone, whatever the theme
    {
        DON_BLOCK(dy);
        f32 mod = floorf(qr_sz / (DON_QR_N + 7)), tile = mod * (DON_QR_N + 7), qo = floorf((tile - mod * DON_QR_N) * 0.5f);
        f32 qx = floorf(px + (pw - tile) * 0.5f), qy = y + floorf((qr_sz - tile) * 0.5f) + dy;
        r_rect(qx, qy, tile, tile, RGBA(255, 255, 255, 255), SSR(10));
        if (!t->dark) r_rect_ex(qx, qy, tile, tile, RGBA(0, 0, 0, 20), SSR(10), 1.f, 0);
        for (int r = 0; r < DON_QR_N; r++) {
            u64 bits = k_don_qr[r];
            for (int col = 0; col < DON_QR_N;) {
                if (!((bits >> col) & 1)) {
                    col++;
                    continue;
                }
                int run = col;
                while (run < DON_QR_N && ((bits >> run) & 1)) run++;
                r_rect(qx + qo + mod * (f32)col, qy + qo + mod * (f32)r, mod * (f32)(run - col), mod, RGBA(17, 17, 19, 255), 0);
                col = run;
            }
        }
    }
    y += qr_sz + SSR(16);

    // DonationAlerts: a compact centered button
    {
        DON_BLOCK(dy);
        f32 hv = SW.don.hov[DON_HOV_LINK], fs = SSC(13.5f), cy = y + dy + link_h * 0.5f;
        const WCHAR *lbl = L"DonationAlerts";
        f32 lw = text_width(FONT_TEXT_SEMIBOLD, fs, lbl, -1), bw = floorf(SSR(16) + lw + SSR(8) + SSR(12) + SSR(14));
        f32 bx = floorf(px + (pw - bw) * 0.5f);
        box_set(SW.don.link, bx, y, bw, link_h);
        r_rect(bx, y + dy, bw, link_h, color_mix(c->ctl, c->ctl_hover, hv), link_h * 0.5f);
        text_draw(FONT_TEXT_SEMIBOLD, fs, bx + SSR(16), baseline_for(FONT_TEXT_SEMIBOLD, fs, cy), lbl, -1, t->text);
        text_draw_icon(0xE8A7, SSC(11), bx + SSR(16) + lw + SSR(8) + SSR(6), cy, color_mix(t->dim, t->text, hv));
    }
    y += link_h + SSR(22);

    {
        DON_BLOCK(dy);
        text_draw(FONT_TEXT_SEMIBOLD, SSC(12.5f), x0 + SSR(2), baseline_for(FONT_TEXT_SEMIBOLD, SSC(12.5f), y + dy + SSR(6)),
                  ss((Str){ L"Криптовалюта", L"Crypto" }), -1, color_mix(t->dim, t->text, 0.35f));
    }
    y += SSR(20);

    u32 ok_col = t->dark ? RGBA(74, 210, 130, 255) : RGBA(24, 140, 72, 255);
    for (int i = 0; i < 6; i++) {
        DON_BLOCK(dy);
        f32 base_op = R.opacity, ry = y + dy, cy = ry + row_h * 0.5f;
        int w0 = k_coins[i].wallet, nets = k_coins[i].nets;
        r_rect(x0, ry, x1 - x0, row_h, c->ctl, SSR(12));
        if (nets == 1) box_set(SW.don.copy[w0], x0, y, x1 - x0, row_h);
        f32 isz = SSR(34), ix = x0 + SSR(10);
        draw_coin_icon(i, ix, cy - isz * 0.5f, isz);
        f32 tx = ix + isz + SSR(12), nfs = SSC(14.5f), afs = SSC(12);
        text_draw(FONT_TEXT_SEMIBOLD, nfs, tx, baseline_for(FONT_TEXT_SEMIBOLD, nfs, cy - SSR(9)), k_coins[i].name, -1, t->text);
        if (nets > 1) {
            text_draw(FONT_TEXT, afs, tx, baseline_for(FONT_TEXT, afs, cy + SSR(10)), L"Tether", -1, t->faint);
            f32 xr = x1 - SSR(10), chh = SSR(30), cfs = SSC(12.5f);
            for (int n = nets - 1; n >= 0; n--) {
                int wi = w0 + n;
                f32 hv = SW.don.hov[wi], cp = SW.don.cp[wi];
                const WCHAR *lbl = k_wallets[wi].net;
                f32 lw = text_width(FONT_TEXT, cfs, lbl, -1), cw = floorf(SSR(10) + lw + SSR(6) + SSR(10) + SSR(10));
                f32 chx = xr - cw, chy = floorf(cy - chh * 0.5f);
                box_set(SW.don.copy[wi], chx, chy - dy, cw, chh);
                if (hv > 0) r_rect(chx, chy, cw, chh, color_alpha(c->ctl_hover, hv), SSR(8));
                if (cp > 0) r_rect(chx, chy, cw, chh, color_alpha(ok_col, 0.16f * cp), SSR(8));
                u32 col = color_mix(color_mix(t->dim, t->text, hv), ok_col, cp);
                text_draw(FONT_TEXT, cfs, chx + SSR(10), baseline_for(FONT_TEXT, cfs, cy), lbl, -1, col);
                f32 gx = chx + cw - SSR(10) - SSR(5), slide = SSR(7);
                r_set_clip(chx, MAX(chy, py + 1), chx + cw, MIN(chy + chh, py + ph - 1));
                R.opacity = base_op * (1.f - cp);
                text_draw_icon(0xE8A7, SSC(9), gx, cy - cp * slide, col);
                R.opacity = base_op * cp;
                text_draw_icon(0xE73E, SSC(10), gx, cy + (1.f - cp) * slide, ok_col);
                R.opacity = base_op;
                r_set_clip(px, py + 1, px + pw, py + ph - 1);
                xr = chx - SSR(2);
            }
        } else {
            WCHAR shortaddr[32];
            const WCHAR *a = k_wallets[w0].addr;
            int n = wlen(a);
            _snwprintf(shortaddr, countof(shortaddr), L"%ls  \x00B7  %.6ls\x2026%ls", k_wallets[w0].net, a, a + n - 4);
            shortaddr[countof(shortaddr) - 1] = 0;
            text_draw(FONT_TEXT, afs, tx, baseline_for(FONT_TEXT, afs, cy + SSR(10)), shortaddr, -1, t->faint);
            f32 hv = SW.don.hov[w0], cp = SW.don.cp[w0];
            const WCHAR *l0 = ss((Str){ L"Копировать", L"Copy" }), *l1 = ss((Str){ L"Скопировано", L"Copied" });
            f32 hfs = SSC(12.5f), phh = SSR(30), slide = SSR(9);
            f32 w0p = floorf(SSR(11) + text_width(FONT_TEXT, hfs, l0, -1) + SSR(8) + SSR(14) + SSR(10));
            f32 w1p = floorf(SSR(11) + text_width(FONT_TEXT, hfs, l1, -1) + SSR(8) + SSR(14) + SSR(10));
            f32 pw2 = floorf(w0p + (w1p - w0p) * cp + 0.5f), hx = x1 - SSR(10) - pw2, hy = floorf(cy - phh * 0.5f);
            if (hv > 0) r_rect(hx, hy, pw2, phh, color_alpha(c->ctl_hover, hv * (1.f - cp)), SSR(8));
            if (cp > 0) r_rect(hx, hy, pw2, phh, color_alpha(ok_col, 0.16f * cp), SSR(8));
            r_set_clip(hx, MAX(hy, py + 1), hx + pw2, MIN(hy + phh, py + ph - 1));
            f32 gx = hx + pw2 - SSR(10) - SSR(7), base = baseline_for(FONT_TEXT, hfs, cy);
            u32 c0 = color_mix(t->dim, t->text, hv);
            R.opacity = base_op * (1.f - cp);
            text_draw(FONT_TEXT, hfs, hx + SSR(11), base - cp * slide, l0, -1, c0);
            text_draw_icon(0xE8C8, SSC(12), gx, cy - cp * slide, c0);
            R.opacity = base_op * cp;
            text_draw(FONT_TEXT, hfs, hx + SSR(11), base + (1.f - cp) * slide, l1, -1, ok_col);
            text_draw_icon(0xE73E, SSC(12), gx, cy + (1.f - cp) * slide, ok_col);
            R.opacity = base_op;
            r_set_clip(px, py + 1, px + pw, py + ph - 1);
        }
        y += row_h + row_gap;
    }
#undef DON_BLOCK
    R.opacity = e;
    r_set_clip(0, 0, (f32)SW.W, (f32)SW.H);
    if (ms > 0 && SW.don.sb_t > 0.f) {
        f32 vh = ph - SSR(24), bar_h = MAX(SSR(28), vh * ph / content), bw = SSR(3);
        r_rect(px + pw - SSR(6) - bw, floorf(py + SSR(12) + (vh - bar_h) * SW.don.scroll / ms), bw, floorf(bar_h), color_alpha(t->faint, SW.don.sb_t),
               bw * 0.5f);
    }
    R.opacity = 1.f;
}

static void sb_button_rects(void)
{
    f32 x = sb_x(), w = sb_w(), bot = (f32)SW.H - sb_x() - SSR(8), bh = SSR(36);
    box_set(SW.sb_btn[1], x + SSR(8), bot - bh, w - SSR(16), bh);
    box_set(SW.sb_btn[0], x + SSR(8), bot - bh * 2 - SSR(6), w - SSR(16), bh);
}

static void draw_sidebar_buttons(const Theme *t, const SColors *c)
{
    sb_button_rects();
    f32 fs = SSC(14);
    for (int i = upd_visible() ? 0 : 1; i < 2; i++) {
        f32 *b = SW.sb_btn[i], bw = b[2] - b[0], bh = b[3] - b[1], cy = (b[1] + b[3]) * 0.5f, ix = b[0] + SSR(10), isz = SSR(20);
        f32 hv = SW.sb_hov[i];
        WCHAR ub[64];
        const WCHAR *lbl;
        u32 glyph, icol, fg;
        if (i == 0) {
            int st = upd_state();
            lbl = upd_label(ub, countof(ub));
            r_rect(b[0], b[1], bw, bh, color_alpha(t->accent, 0.18f + 0.10f * hv), SSR(8));
            if (st == UPD_DOWNLOADING) {
                f32 pw = floorf(bw * (f32)UPD.progress / 1000.f);
                r_set_clip(b[0], b[1], b[0] + pw, b[3]);
                r_rect(b[0], b[1], bw, bh, color_alpha(t->accent, 0.22f), SSR(8));
                r_set_clip(0, 0, (f32)SW.W, (f32)SW.H);
            }
            glyph = st == UPD_ERROR ? 0xE72C : 0xE896;
            icol = fg = st == UPD_ERROR ? t->danger : t->text;
        } else {
            lbl = ss((Str){ L"Поддержать автора", L"Support the author" });
            if (hv > 0) r_rect(b[0], b[1], bw, bh, color_alpha(c->nav_hover, hv), SSR(8));
            glyph = 0xEB51;
            icol = color_mix(t->faint, t->dim, hv);
            fg = color_mix(t->dim, t->text, hv);
        }
        text_draw_icon(glyph, SSC(15), ix + isz * 0.5f, cy, icol);
        text_draw_fit(FONT_TEXT, fs, ix + isz + SSR(12), baseline_for(FONT_TEXT, fs, cy), b[2] - ix - isz - SSR(18), lbl, -1, fg, false);
    }
}

static void settings_draw(void)
{
    const Theme *t = &U.th;
    SColors c = scolors(t->dark);
    if (!SW.mica) r_rect(0, 0, (f32)SW.W, (f32)SW.H, c.bg, 0);
    SW.scroll = CLAMP(SW.scroll, 0.f, max_scroll_s());

    draw_sidebar(t, &c);
    draw_sidebar_buttons(t, &c);

    f32 pe = ease_out_cubic(SW.page_t), rise = floorf((1.f - pe) * SSR(10) + 0.5f);
    R.opacity = pe;

    f32 x0 = content_x0();
    text_draw_fit(FONT_TEXT_SEMIBOLD, SSC(20), x0 + SSR(2), SSR(40) + rise, content_x1() - x0 - SSR(100), ss(k_pages[SW.page].name), -1, t->text,
                  false);

    f32 off = top_h() - floorf(SW.scroll + 0.5f) + rise;
    r_set_clip(0, top_h(), (f32)SW.W, (f32)SW.H);
    for (int i = 0; i < SW.ncards; i++) {
        SBox *b = &SW.cards[i];
        r_rect(b->x, b->y + off, b->w, b->h, c.card, SSR(10));
        r_rect_ex(b->x, b->y + off, b->w, b->h, c.card_border, SSR(10), 1.f, 0);
    }
    for (int i = 0; i < SW.nheads; i++)
        text_draw(FONT_TEXT_SEMIBOLD, SSC(13), SW.heads[i].x, SW.heads[i].y + off + SSR(5), ss(SW.head_text[i]), -1, color_mix(t->dim, t->text, 0.35f));
    for (int i = 0; i < SW.nrows; i++) {
        SRow *r = &SW.rows[i];
        if (r->y + off + r->h < top_h() || r->y + off > SW.H) continue;
        draw_srow(r, off, t, &c);
    }
    r_set_clip(0, 0, (f32)SW.W, (f32)SW.H);
    R.opacity = 1.f;

    f32 sep_k = CLAMP(SW.scroll / SSR(16), 0.f, 1.f);
    if (sep_k > 0.f) r_rect(x0, top_h() - 1, content_x1() - x0, 1, color_alpha(t->sep, sep_k), 0);

    f32 ms = max_scroll_s();
    if (ms > 0 && SW.sb_t > 0.f) {
        bool hot = sb_hot();
        f32 vh = (f32)SW.H - top_h() - SSR(8), bar_h = MAX(SSR(32), vh * vh / SW.content_h);
        f32 by = top_h() + (vh - bar_h) * CLAMP(SW.scroll / ms, 0.f, 1.f), bw = hot ? SSR(5) : SSR(3);
        r_rect((f32)SW.W - SSR(4) - bw, floorf(by), bw, floorf(bar_h), color_alpha(hot ? t->dim : t->faint, SW.sb_t), bw * 0.5f);
    }

    for (int i = 0; i < 2; i++) {
        f32 r[4];
        winbtn_rect(i, r);
        bool hov = SW.hover.type == (i ? HT_WINCLOSE : HT_WINMIN);
        bool down = hov && SW.press.type == SW.hover.type;
        f32 h = SW.win_hov[i];
        u32 fg = SW.active ? t->text : t->faint;
        if (i == 1) {
            r_rect(r[0], r[1], r[2] - r[0], r[3] - r[1], color_alpha(down ? RGBA(200, 64, 50, 255) : RGBA(196, 43, 28, 255), h), 0);
            fg = color_mix(fg, RGBA(255, 255, 255, 255), h);
        } else {
            r_rect(r[0], r[1], r[2] - r[0], r[3] - r[1], color_alpha(down ? c.ctl_hover : c.ctl, h), 0);
        }
        text_draw_icon(i ? 0xE8BB : 0xE921, SSC(10), (r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f, fg);
    }

    if (SW.pop_open && SW.pop_row >= 0 && SW.pop_row < SW.nrows) {
        R.opacity = ease_out_cubic(SW.pop_t);
        draw_popover(t, &c);
        R.opacity = 1.f;
    }
    if (SW.rec.open) {
        R.opacity = ease_out_cubic(SW.rec_t);
        draw_recorder(t, &c);
        R.opacity = 1.f;
    }
    if (SW.don.open) draw_donate(t, &c);
    confetti_draw();
}

static void settings_snap_motion(void)
{
    f32 r[4];
    nav_rect(SW.page, r);
    SW.nav_y = r[1];
    SW.nav_v = 0;
    for (int i = 0; i < (int)SITEMS; i++) {
        SW.tog[i] = k_sitems[i].kind == SK_TOGGLE && sval(k_sitems[i].id) ? 1.f : 0.f;
        SW.seg[i] = is_seg(&k_sitems[i]) ? (f32)sval(k_sitems[i].id) : 0.f;
    }
    memset(SW.nav_hov, 0, sizeof SW.nav_hov);
    memset(SW.win_hov, 0, sizeof SW.win_hov);
    SW.scroll_target = SW.scroll;
    SW.scroll_v = 0;
    SW.page_t = SW.pop_t = SW.rec_t = 1.f;
    if (SW.don.closing) SW.don.open = SW.don.closing = false;
    SW.don.t = 1.f;
    SW.don.scroll = SW.don.scroll_target = CLAMP(SW.don.scroll_target, 0.f, MAX(0.f, SW.don.content_h - SW.don.view_h));
    SW.don.scroll_v = 0;
    SW.don.sb_t = SW.don.content_h > SW.don.view_h ? 1.f : 0.f;
    for (int i = 0; i < 10; i++) SW.don.hov[i] = SW.hover.type == HT_DON && SW.hover.idx == (i == DON_HOV_LINK ? DON_LINK : i == DON_HOV_CLOSE ? DON_CLOSE : i);
    SW.sb_hov[0] = SW.sb_hov[1] = 0;
    SW.sb_t = 0;
    SW.sb_prev = SW.scroll;
    SW.row_hov = 0;
    SW.row_hov_idx = -1;
}

static bool settings_animate(f32 dt)
{
    if (!anims_enabled()) {
        SW.scroll = SW.scroll_target = CLAMP(SW.scroll_target, 0.f, max_scroll_s());
        settings_snap_motion();
        SW.sb_t = max_scroll_s() > 0 ? 1.f : 0.f;
        SW.row_hov_idx = row_hover_target();
        SW.row_hov = SW.row_hov_idx >= 0 ? 1.f : 0.f;
        return false;
    }
    bool anim = false;
    f32 r[4];
    nav_rect(SW.page, r);
    anim |= spring_step(&SW.nav_y, &SW.nav_v, r[1], anim_omega(g_cfg.anim_select_ms + 20), dt);
    for (int i = 0; i < PG__COUNT; i++)
        anim |= approach(&SW.nav_hov[i], SW.hover.type == HT_NAV && SW.hover.idx == i && i != SW.page ? 1.f : 0.f, 20.f, dt);
    for (int i = 0; i < 2; i++) anim |= approach(&SW.win_hov[i], SW.hover.type == (i ? HT_WINCLOSE : HT_WINMIN) ? 1.f : 0.f, 24.f, dt);
    for (int i = 0; i < (int)SITEMS; i++) {
        if (k_sitems[i].kind == SK_TOGGLE) anim |= approach(&SW.tog[i], sval(k_sitems[i].id) ? 1.f : 0.f, 20.f, dt);
        if (is_seg(&k_sitems[i])) anim |= approach(&SW.seg[i], (f32)sval(k_sitems[i].id), 18.f, dt);
    }
    SW.scroll_target = CLAMP(SW.scroll_target, 0.f, max_scroll_s());
    anim |= spring_step(&SW.scroll, &SW.scroll_v, SW.scroll_target, anim_omega(g_cfg.anim_scroll_ms), dt);
    f64 now = time_now();
    if (SW.scroll != SW.sb_prev) SW.sb_seen = now;
    SW.sb_prev = SW.scroll;
    bool recent = now - SW.sb_seen < 0.9, hot = sb_hot();
    bool sb_on = max_scroll_s() > 0 && (hot || recent);
    anim |= approach(&SW.sb_t, sb_on ? 1.f : 0.f, sb_on ? 20.f : 7.f, dt);
    if (recent && !hot && max_scroll_s() > 0) anim = true;
    int rh = row_hover_target();
    if (rh >= 0 && rh != SW.row_hov_idx) {
        SW.row_hov_idx = rh;
        SW.row_hov = 0;
    }
    anim |= approach(&SW.row_hov, rh >= 0 ? 1.f : 0.f, 22.f, dt);
    for (int i = 0; i < 2; i++)
        anim |= approach(&SW.sb_hov[i], SW.hover.type == (i ? HT_DONATE : HT_SBUPDATE) ? 1.f : 0.f, 22.f, dt);
    anim |= don_animate(dt);
    anim |= confetti_animate(dt);
    f32 *entr[3] = { &SW.page_t, &SW.pop_t, &SW.rec_t };
    f32 dur[3] = { anim_sec(g_cfg.anim_page_ms), anim_sec(g_cfg.anim_menu_ms) * 0.85f, anim_sec(160) };
    for (int i = 0; i < 3; i++)
        if (*entr[i] < 1.f) {
            *entr[i] = dur[i] > 0.f ? MIN(1.f, *entr[i] + dt / dur[i]) : 1.f;
            anim = true;
        }
    return anim;
}

static void settings_render(void)
{
    if (!SW.hwnd || !SW.target.ok || !R.ok) return;
    r_use(&SW.target);
    r_wait_frame();
    f64 now = time_now();
    f32 dt = (f32)(now - SW.last_frame);
    if (dt > 0.1f || dt <= 0) dt = 1.f / 60.f;
    SW.last_frame = now;
    settings_layout();
    SW.animating = settings_animate(dt);
    R.scale = 1.f;
    r_begin();
    settings_draw();
    if (F.overflow) {
        font_reset_atlas();
        r_begin();
        settings_draw();
        F.overflow = false;
    }
    r_end_and_present();
    r_use(&R.main);
    SW.dirty = false;
}

static SHit settings_hit(int mx, int my)
{
    SHit h = { HT_NONE, PART_ROW, -1 };
    f32 x = (f32)mx, y = (f32)my;
    if (SW.rec.open) {
        h.type = in_box(SW.rec_btn[1], x, y) ? HT_RECSAVE : in_box(SW.rec_btn[0], x, y) ? HT_RECCANCEL : in_box(SW.rec_panel, x, y) ? HT_RECPANEL : HT_RECOUT;
        return h;
    }
    if (don_active()) {
        h.type = HT_DON;
        h.idx = in_box(SW.don.panel, x, y) ? DON_PANEL : DON_OUT;
        if (h.idx == DON_PANEL) {
            if (in_box(SW.don.close, x, y)) h.idx = DON_CLOSE;
            else if (in_box(SW.don.link, x, y)) h.idx = DON_LINK;
            else
                for (int i = 0; i < 8; i++)
                    if (in_box(SW.don.copy[i], x, y)) h.idx = i;
        }
        return h;
    }
    if (SW.pop_open) {
        h.type = HT_POPOUT;
        if (x >= SW.pop_x && x < SW.pop_x + SW.pop_w && y >= SW.pop_y && y < SW.pop_y + SW.pop_h) {
            int k = (int)floorf((y - SW.pop_y - SSR(5)) / SSR(32));
            if (k >= 0 && k < choice_count(&k_sitems[SW.rows[SW.pop_row].item])) {
                h.type = HT_POPITEM;
                h.idx = k;
            }
        }
        return h;
    }
    for (int i = 0; i < 2; i++) {
        f32 r[4];
        winbtn_rect(i, r);
        if (in_box(r, x, y)) {
            h.type = i ? HT_WINCLOSE : HT_WINMIN;
            return h;
        }
    }
    sb_button_rects();
    if (in_box(SW.sb_btn[1], x, y)) {
        h.type = HT_DONATE;
        return h;
    }
    if (upd_visible() && in_box(SW.sb_btn[0], x, y)) {
        h.type = HT_SBUPDATE;
        return h;
    }
    for (int i = 0; i < PG__COUNT; i++) {
        f32 r[4];
        nav_rect(i, r);
        if (in_box(r, x, y)) {
            h.type = HT_NAV;
            h.idx = i;
            return h;
        }
    }
    if (y < top_h()) return h;
    f32 cy = y - top_h() + floorf(SW.scroll + 0.5f);
    for (int i = 0; i < SW.nrows; i++) {
        SRow *r = &SW.rows[i];
        if (x < r->x || x >= r->x + r->w || cy < r->y || cy >= r->y + r->h) continue;
        h.type = HT_ROW;
        h.idx = i;
        f32 g = SSR(4);
        if (x >= r->cx - g && x < r->cx + r->cw + g && cy >= r->cy - g && cy < r->cy + r->ch + g) {
            h.part = PART_CONTROL;
            if (k_sitems[r->item].kind == SK_STEPPER) {
                if (x < r->cx + SSR(34)) h.part = PART_MINUS;
                else if (x >= r->cx + r->cw - SSR(34)) h.part = PART_PLUS;
            }
        }
        return h;
    }
    return h;
}

static void ensure_focus_visible(void)
{
    if (!row_focusable(SW.focus)) return;
    SRow *r = &SW.rows[SW.focus];
    f32 vh = (f32)SW.H - top_h();
    f32 top = r->y - SSR(40), bot = r->y + r->h + SSR(12);
    if (top < SW.scroll_target) SW.scroll_target = top;
    else if (bot > SW.scroll_target + vh) SW.scroll_target = bot - vh;
    SW.scroll_target = CLAMP(SW.scroll_target, 0.f, max_scroll_s());
}

static void pop_open(int row)
{
    const SItem *it = &k_sitems[SW.rows[row].item];
    SRow *r = &SW.rows[row];
    f32 fs = SSC(13.5f), w = 0;
    int nopt = choice_count(it);
    for (int k = 0; k < nopt; k++) w = MAX(w, text_width(FONT_TEXT, fs, ss(it->opt[k]), -1));
    SW.pop_w = MAX(r->cw, floorf(w + SSR(5) + SSR(32) + SSR(24)));
    SW.pop_h = SSR(32) * (f32)nopt + SSR(10);
    f32 off = top_h() - floorf(SW.scroll + 0.5f);
    SW.pop_x = floorf(r->cx + r->cw - SW.pop_w);
    SW.pop_y = r->cy + off + r->ch + SSR(4);
    if (SW.pop_y + SW.pop_h > (f32)SW.H - SSR(8)) SW.pop_y = r->cy + off - SSR(4) - SW.pop_h;
    SW.pop_open = true;
    SW.pop_t = 0;
    SW.pop_row = row;
    SW.pop_hover = sval(it->id);
    settings_invalidate();
}

static void pop_close(void)
{
    SW.pop_open = false;
    settings_invalidate();
}

static void set_page(int page)
{
    page = (page + PG__COUNT) % PG__COUNT;
    if (page == SW.page) return;
    end_edit(true);
    SW.pop_open = false;
    SW.armed = false;
    SW.page = page;
    SW.scroll = SW.scroll_target = SW.scroll_v = 0;
    SW.page_t = 0;
    SW.focus = -1;
    SW.hover.type = HT_NONE;
    if (page == PG_ABOUT) refresh_status();
    settings_invalidate();
}

static void activate_row(int ri, int part)
{
    const SItem *it = &k_sitems[SW.rows[ri].item];
    if (it->id != SID_CLEARHIST) SW.armed = false;
    switch (it->kind) {
    case SK_TOGGLE: sset(it, !sval(it->id)); break;
    case SK_CHOICE:
        if (is_seg(it)) sset(it, SW.seg_click >= 0 ? SW.seg_click : (sval(it->id) + 1) % choice_count(it));
        else pop_open(ri);
        SW.seg_click = -1;
        break;
    case SK_STEPPER:
        if (part == PART_MINUS) sset(it, sval(it->id) - it->step);
        else if (part == PART_PLUS) sset(it, sval(it->id) + it->step);
        break;
    case SK_TEXT: begin_edit(ri); break;
    case SK_HOTKEY: rec_open(); break;
    case SK_BUTTON: button_action(it); break;
    }
    settings_invalidate();
}

static void move_focus(int dir)
{
    int i = SW.focus;
    if (!row_focusable(i)) i = dir > 0 ? -1 : SW.nrows;
    for (int k = 0; k < SW.nrows; k++) {
        i += dir;
        if (i < 0 || i >= SW.nrows) break;
        if (row_focusable(i)) {
            SW.focus = i;
            break;
        }
    }
    SW.focus_visible = true;
    ensure_focus_visible();
    settings_invalidate();
}

static void settings_key(UINT vk)
{
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (SW.don.open) {
        if (vk == VK_ESCAPE) don_close();
        else if (vk == VK_DOWN || vk == VK_NEXT) SW.don.scroll_target += SSR(vk == VK_NEXT ? 240 : 48);
        else if (vk == VK_UP || vk == VK_PRIOR) SW.don.scroll_target -= SSR(vk == VK_PRIOR ? 240 : 48);
        settings_invalidate();
        return;
    }
    if (SW.pop_open) {
        const SItem *it = &k_sitems[SW.rows[SW.pop_row].item];
        switch (vk) {
        case VK_UP: SW.pop_hover = (SW.pop_hover + choice_count(it) - 1) % choice_count(it); break;
        case VK_DOWN: SW.pop_hover = (SW.pop_hover + 1) % choice_count(it); break;
        case VK_RETURN:
        case VK_SPACE:
            sset(it, SW.pop_hover);
            pop_close();
            break;
        case VK_ESCAPE:
        case VK_TAB: pop_close(); break;
        }
        settings_invalidate();
        return;
    }
    if (SW.editing >= 0) {
        if (vk == VK_TAB) {
            end_edit(true);
            move_focus(shift ? -1 : 1);
            return;
        }
        edit_key(vk, ctrl);
        return;
    }
    if (ctrl && (vk == VK_TAB || vk == VK_NEXT || vk == VK_PRIOR)) {
        set_page(SW.page + ((vk == VK_TAB && shift) || vk == VK_PRIOR ? -1 : 1));
        return;
    }
    switch (vk) {
    case VK_ESCAPE:
        if (SW.armed) {
            SW.armed = false;
            settings_invalidate();
        } else {
            DestroyWindow(SW.hwnd);
        }
        return;
    case VK_DOWN: move_focus(1); return;
    case VK_UP: move_focus(-1); return;
    case VK_TAB: move_focus(shift ? -1 : 1); return;
    case VK_NEXT: SW.scroll_target += ((f32)SW.H - top_h()) * 0.8f; settings_invalidate(); return;
    case VK_PRIOR: SW.scroll_target -= ((f32)SW.H - top_h()) * 0.8f; settings_invalidate(); return;
    case 'W':
        if (ctrl) DestroyWindow(SW.hwnd);
        return;
    }
    if (!row_focusable(SW.focus)) return;
    const SItem *it = &k_sitems[SW.rows[SW.focus].item];
    SW.focus_visible = true;
    switch (vk) {
    case VK_LEFT:
    case VK_RIGHT: {
        int d = vk == VK_LEFT ? -1 : 1;
        if (it->kind == SK_STEPPER) sset(it, sval(it->id) + d * it->step);
        else if (it->kind == SK_CHOICE) sset(it, sval(it->id) + d);
        else if (it->kind == SK_TOGGLE) sset(it, d > 0);
        break;
    }
    case VK_SPACE:
    case VK_RETURN:
        activate_row(SW.focus, it->kind == SK_STEPPER ? PART_ROW : PART_CONTROL);
        break;
    }
    settings_invalidate();
}

static void settings_mouse_down(int mx, int my)
{
    SHit h = settings_hit(mx, my);
    SW.press = h;
    SW.focus_visible = false;
    if (SW.rec.open) {
        if (h.type == HT_RECSAVE) rec_close(true);
        else if (h.type == HT_RECCANCEL || h.type == HT_RECOUT) rec_close(false);
        return;
    }
    if (SW.don.open) {
        if (SW.don.closing) return;
        if (h.idx == DON_OUT || h.idx == DON_CLOSE) {
            don_close();
        } else if (h.idx == DON_LINK) {
            open_path(DON_LINK_URL);
            confetti_burst();
        } else if (h.idx >= 0) {
            don_copy(h.idx);
            confetti_burst();
        }
        return;
    }
    if (h.type == HT_DONATE) {
        don_open();
        return;
    }
    if (h.type == HT_SBUPDATE) {
        upd_download();
        settings_invalidate();
        return;
    }
    if (SW.pop_open) {
        if (h.type == HT_POPITEM) sset(&k_sitems[SW.rows[SW.pop_row].item], h.idx);
        pop_close();
        return;
    }
    if (SW.editing >= 0 && !(h.type == HT_ROW && h.idx == SW.editing && h.part == PART_CONTROL)) end_edit(true);
    if (h.type == HT_NAV) {
        set_page(h.idx);
    } else if (h.type == HT_ROW) {
        SW.focus = h.idx;
        const SItem *it = &k_sitems[SW.rows[h.idx].item];
        if (is_seg(it) && h.part == PART_CONTROL) SW.seg_click = seg_index_at(it, SW.rows[h.idx].cx, (f32)mx);
        if ((it->kind == SK_TOGGLE || h.part != PART_ROW) && !(it->kind == SK_TEXT && SW.editing == h.idx)) activate_row(h.idx, h.part);
        else if (SW.armed) SW.armed = false;
    } else if (SW.armed) {
        SW.armed = false;
    }
    settings_invalidate();
}

static void settings_mouse_up(int mx, int my)
{
    SHit h = settings_hit(mx, my);
    if (SW.press.type == HT_WINCLOSE && h.type == HT_WINCLOSE) DestroyWindow(SW.hwnd);
    else if (SW.press.type == HT_WINMIN && h.type == HT_WINMIN) ShowWindow(SW.hwnd, SW_MINIMIZE);
    SW.press.type = HT_NONE;
    settings_invalidate();
}

static void settings_apply_theme(void)
{
    if (!SW.hwnd) return;
    BOOL d = U.th.dark;
    DwmSetWindowAttribute(SW.hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &d, sizeof d);
    settings_invalidate();
}

static LRESULT settings_nchittest(HWND h, LPARAM lp)
{
    POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    ScreenToClient(h, &p);
    f32 x = (f32)p.x, y = (f32)p.y;
    if (!IsZoomed(h)) {
        f32 b = SSR(6);
        bool L = x < b, R = x >= SW.W - b, T = y < b, B = y >= SW.H - b;
        if (T && L) return HTTOPLEFT;
        if (T && R) return HTTOPRIGHT;
        if (B && L) return HTBOTTOMLEFT;
        if (B && R) return HTBOTTOMRIGHT;
        if (T) return HTTOP;
        if (B) return HTBOTTOM;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
    }
    if (SW.rec.open || SW.pop_open || SW.don.open) return HTCLIENT;
    for (int i = 0; i < 2; i++) {
        f32 r[4];
        winbtn_rect(i, r);
        if (in_box(r, x, y)) return HTCLIENT;
    }
    if (y < top_h() && x >= sb_x() + sb_w()) return HTCAPTION;
    if (x < sb_x() + sb_w() && y < nav_y0() - SSR(6)) return HTCAPTION;
    return HTCLIENT;
}

static LRESULT CALLBACK settings_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp) {
            if (IsZoomed(h)) {
                NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
                int f = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                InflateRect(&p->rgrc[0], -f, -f);
            }
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return settings_nchittest(h, lp);
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        if (SW.s > 0) {
            mm->ptMinTrackSize.x = (LONG)SSR(720);
            mm->ptMinTrackSize.y = (LONG)SSR(460);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        settings_invalidate();
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(h, &rc);
        SW.W = MAX(1, rc.right - rc.left);
        SW.H = MAX(1, rc.bottom - rc.top);
        r_target_resize(&SW.target, SW.W, SW.H);
        SW.pop_open = false;
        settings_invalidate();
        settings_render();
        return 0;
    }
    case WM_DPICHANGED: {
        SW.dpi = LOWORD(wp);
        SW.s = (f32)SW.dpi / 96.f;
        RECT *r = (RECT *)lp;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        settings_invalidate();
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && SW.hover.type == HT_ROW && SW.hover.part == PART_CONTROL &&
            k_sitems[SW.rows[SW.hover.idx].item].kind == SK_TEXT) {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_IBEAM));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        bool was_hot = sb_hot();
        SW.mx = GET_X_LPARAM(lp);
        SW.my = GET_Y_LPARAM(lp);
        if (sb_hot() != was_hot || (SW.hover.type == HT_ROW && SW.hover.part == PART_CONTROL)) settings_invalidate();
        SHit hit = settings_hit(SW.mx, SW.my);
        if (hit.type != SW.hover.type || hit.idx != SW.hover.idx || hit.part != SW.hover.part) {
            SW.hover = hit;
            if (SW.pop_open && hit.type == HT_POPITEM) SW.pop_hover = hit.idx;
            settings_invalidate();
        }
        TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        SW.hover.type = HT_NONE;
        SW.mx = SW.my = -1;
        settings_invalidate();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetFocus(h);
        SetCapture(h);
        settings_mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        settings_mouse_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL:
        if (SW.rec.open) return 0;
        if (SW.don.open) {
            SW.don.scroll_target = CLAMP(SW.don.scroll_target - (f32)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * SSR(64), 0.f,
                                         MAX(0.f, SW.don.content_h - SW.don.view_h));
            settings_invalidate();
            return 0;
        }
        SW.pop_open = false;
        SW.scroll_target = CLAMP(SW.scroll_target - (f32)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * SSR(64), 0.f, max_scroll_s());
        settings_invalidate();
        return 0;
    case WM_APP_KEYCAP:
        if (SW.rec.open) rec_key((UINT)wp, lp != 0);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        if (SW.rec.open) {
            rec_key(sided_vk(wp, lp), down);
            return 0;
        }
        if ((wp == VK_LWIN || wp == VK_RWIN) && hotkey_is_win_tap()) return 0;
        if (!down) break;
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;
        settings_key((UINT)wp);
        return 0;
    }
    case WM_SYSCHAR:
        if (SW.rec.open) return 0;
        if (wp == ' ') break;
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_TASKLIST && hotkey_is_win_tap()) return 0;
        break;
    case WM_CHAR:
        if (SW.editing >= 0) {
            WCHAR c = (WCHAR)wp;
            edit_insert(&c, 1);
            SW.caret_on = true;
            settings_invalidate();
        }
        return 0;
    case WM_TIMER:
        if (wp == SETTINGS_TIMER_CARET) SW.caret_on = !SW.caret_on;
        if (wp == SETTINGS_TIMER_FLASH) KillTimer(h, SETTINGS_TIMER_FLASH);
        settings_invalidate();
        return 0;
    case WM_ACTIVATE:
        SW.active = LOWORD(wp) != WA_INACTIVE;
        if (SW.active) hook_watchdog_off();
        if (!SW.active) {
            end_edit(true);
            rec_close(false);
            SW.armed = false;
            SW.pop_open = false;
        } else {
            refresh_status();
            theme_update();
        }
        settings_invalidate();
        return 0;
    case WM_SETTINGCHANGE:
        if (lp && !wcscmp((const WCHAR *)lp, L"ImmersiveColorSet")) {
            theme_update();
            settings_apply_theme();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        rec_close(false);
        r_target_release(&SW.target);
        SW.hwnd = NULL;
        SW.active = false;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void settings_open(void)
{
    if (SW.hwnd) {
        hook_watchdog_off();
        if (IsIconic(SW.hwnd)) ShowWindow(SW.hwnd, SW_RESTORE);
        SetForegroundWindow(SW.hwnd);
        return;
    }
    static bool registered;
    if (!registered) {
        registered = true;
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = settings_wndproc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hIcon = g_icon_big;
        wc.hIconSm = g_icon_small;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = SETTINGS_CLASS;
        RegisterClassExW(&wc);
    }
    theme_update();
    refresh_status();
    SW.page = PG_GENERAL;
    SW.scroll = 0;
    SW.focus = -1;
    SW.hover.type = SW.press.type = HT_NONE;
    SW.editing = -1;
    SW.armed = false;
    SW.pop_open = false;
    SW.don.open = false;
    memset(&SW.rec, 0, sizeof SW.rec);

    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    if (g_pinned || g_other_monitor) {
        HMONITOR io[2] = { mon, NULL };
        EnumDisplayMonitors(NULL, NULL, find_other_monitor, (LPARAM)io);
        if (io[1]) mon = io[1];
    }
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    SW.dpi = monitor_dpi(mon);
    SW.s = (f32)SW.dpi / 96.f;
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD ex = WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW;
    RECT wa = mi.rcWork;
    int w = MIN((int)SSR(900), (int)(wa.right - wa.left) - 40), h = MIN((int)SSR(640), (int)(wa.bottom - wa.top) - 40);
    int x = wa.left + ((wa.right - wa.left) - w) / 2, y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    SW.hwnd = CreateWindowExW(ex, SETTINGS_CLASS, ss((Str){ L"Настройки MixLauncher", L"MixLauncher Settings" }), style, x, y, w, h, NULL, NULL,
                              GetModuleHandleW(NULL), NULL);
    if (!SW.hwnd) return;
    BOOL d = U.th.dark;
    DwmSetWindowAttribute(SW.hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &d, sizeof d);
    int corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(SW.hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof corner);
    int mica = 2;  // DWMSBT_MAINWINDOW
    MARGINS m = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(SW.hwnd, &m);
    SW.mica = SUCCEEDED(DwmSetWindowAttribute(SW.hwnd, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &mica, sizeof mica));
    SetWindowPos(SW.hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RECT cr;
    GetClientRect(SW.hwnd, &cr);
    SW.W = MAX(1, cr.right);
    SW.H = MAX(1, cr.bottom);
    if (!r_target_init(&SW.target, SW.hwnd, SW.W, SW.H)) {
        DestroyWindow(SW.hwnd);
        return;
    }
    settings_snap_motion();
    SW.last_frame = time_now();
    SW.dirty = true;
    settings_render();
    hook_watchdog_off();
    ShowWindow(SW.hwnd, SW_SHOW);
    SetForegroundWindow(SW.hwnd);
}

static void settings_device_lost(void) { r_target_release(&SW.target); }

static void settings_device_restored(void)
{
    if (SW.hwnd && r_target_init(&SW.target, SW.hwnd, SW.W, SW.H)) settings_invalidate();
}
