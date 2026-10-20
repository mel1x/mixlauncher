// The settings: the items of every page (k_sitems), their values and what changing one does. Layout and
// drawing of the rows are here too; the window, input and animation are in settings_wnd.c.

enum { SK_TOGGLE, SK_CHOICE, SK_STEPPER, SK_TEXT, SK_HOTKEY, SK_BUTTON, SK_INFO, SK_ABOUT };
enum {
    SID_NONE, SID_HOTKEY, SID_AUTOSTART, SID_KEEPQUERY, SID_THEME, SID_BACKDROP, SID_ANIM, SID_WIDTH, SID_ROWS,
    SID_MAXAPPS, SID_HIDENOISE, SID_REINDEX, SID_MINCHARS, SID_FILTER, SID_CLEARHIST, SID_CONFIG,
    SID_DATADIR, SID_ADMIN, SID_EVERYTHING, SID_ABOUT,
    SID_ASPEED, SID_AOPEN, SID_ACLOSE, SID_ASCALE, SID_ACASCADE, SID_AROW, SID_ASTAGGER, SID_ASELECT, SID_ASCROLL, SID_AMENU,
    SID_APAGE, SID_APREVIEW, SID_ARESET, SID_TASKBAR, SID_TBREPLACE, SID_TBICON, SID_TBFILE, SID_MEDIA, SID_MEDIAPOS, SID_MEDIACTL, SID_MEDIAHIDE, SID_MEDIASCROLL, SID_LANG,
    SID_STYLE, SID_MATCHHL, SID_UPDCHECK, SID_UPDNOW, SID_REDUCED, SID_RMGOTO, SID_POSITION, SID_STARTPOS, SID_TBSEARCH, SID__COUNT
};
enum { PG_GENERAL, PG_TASKBAR, PG_APPEARANCE, PG_ANIM, PG_SEARCH, PG_ADVANCED, PG_ABOUT, PG__COUNT };

typedef struct Str { const WCHAR *ru, *en; } Str;
static const WCHAR *ss(Str s) { return g_lang_ru ? s.ru : s.en; }

static const struct { Str name; u32 glyph; } k_pages[PG__COUNT] = {
    { { L"Общие", L"General" }, 0xE713 },
    { { L"Панель задач", L"Taskbar" }, 0xE7C4 },
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
    int lo, hi, step;  // a stepper; with no range given, its setting's range
    Str unit;
    Str opt[6];
    u32 glyph;
    u8 dep, dep_val;  // shown only while setting `dep` has the value `dep_val` (and is shown itself)
    u16 dep_mask;     // ...or, with this, one of these values (bits)
    bool dep_not;     // ...or, with this, any other value
} SItem;

static const SItem k_sitems[] = {
    { .page = PG_GENERAL, .kind = SK_HOTKEY, .id = SID_HOTKEY, .group = { L"Запуск", L"Startup" },
      .title = { L"Открыть MixLauncher", L"Open MixLauncher" },
      .desc = { L"Клавиша или сочетание, которое открывает и закрывает лаунчер. Прежнее действие сочетания отключается: "
                L"например, Win больше не открывает «Пуск», а Win+D не сворачивает окна.",
                L"The key or shortcut that opens and closes the launcher. Whatever it did before is turned off: "
                L"Win no longer opens Start, Win+D no longer shows the desktop." } },
    { .page = PG_GENERAL, .kind = SK_TOGGLE, .id = SID_AUTOSTART,
      .title = { L"Запускать при входе в Windows", L"Start with Windows" },
      .desc = { L"Через планировщик заданий: с правами администратора и без запроса UAC при входе",
                L"Through Task Scheduler: as administrator, without a UAC prompt at sign-in" } },
    { .page = PG_GENERAL, .kind = SK_CHOICE, .id = SID_LANG, .group = { L"Язык", L"Language" },
      .title = { L"Язык интерфейса", L"Interface language" },
      .desc = { L"«Системный» берёт язык Windows: русский для русской Windows, иначе английский",
                L"System follows Windows: Russian on a Russian Windows, English otherwise" },
      .opt = { { L"Системный", L"System" }, { L"Русский", L"Русский" }, { L"English", L"English" } } },
    { .page = PG_GENERAL, .kind = SK_TOGGLE, .id = SID_REDUCED, .group = { L"Доступность", L"Accessibility" },
      .title = { L"Уменьшение движения", L"Reduced motion" },
      .desc = { L"Без анимаций и задержек: окна, меню и списки появляются сразу. Настройки анимаций сохраняются до выключения",
                L"No animations or delays: windows, menus and lists appear at once. Animation settings are kept until it is off" } },

    { .page = PG_TASKBAR, .kind = SK_TOGGLE, .id = SID_TASKBAR, .group = { L"Кнопка Пуск", L"Start button" },
      .title = { L"Кнопка Пуск открывает MixLauncher", L"Start button opens MixLauncher" },
      .desc = { L"Клик по кнопке Пуск (Windows 11) открывает лаунчер. Правый клик по-прежнему открывает меню Win+X",
                L"Clicking the Start button (Windows 11) opens the launcher. Right click still opens the Win+X menu" } },
    { .page = PG_TASKBAR, .kind = SK_TOGGLE, .id = SID_TBREPLACE, .dep = SID_TASKBAR, .dep_val = 1, .title = { L"Заменять значок меню Пуск", L"Replace the Start button icon" },
      .desc = { L"Наш значок вместо логотипа Windows. Выключено - кнопка выглядит как обычно",
                L"Our icon instead of the Windows logo. Off keeps the button looking as usual" } },
    { .page = PG_TASKBAR, .kind = SK_CHOICE, .id = SID_TBICON, .dep = SID_TBREPLACE, .dep_val = 1, .title = { L"Значок кнопки", L"Button icon" },
      .opt = { { L"Ромб", L"Diamond" }, { L"Сетка", L"Grid" }, { L"Лепестки", L"Petals" }, { L"Слои", L"Layers" }, { L"Точки", L"Dots" }, { L"Свой", L"Custom" } } },
    { .page = PG_TASKBAR, .kind = SK_BUTTON, .id = SID_TBFILE, .dep = SID_TBICON, .dep_val = TBI_CUSTOM, .title = { L"Свой значок", L"Custom icon" },
      .desc = { L"PNG, ICO или JPG. Лучше квадратный, от 48×48, с прозрачным фоном", L"PNG, ICO or JPG. Square, 48×48 or larger, with a transparent background works best" },
      .opt = { { L"Выбрать…", L"Choose…" } }, .glyph = 0xE8E5 },
    { .page = PG_TASKBAR, .kind = SK_TOGGLE, .id = SID_TBSEARCH, .group = { L"Поиск", L"Search" },
      .title = { L"Открывать поиск лаунчера по бейджу", L"Taskbar search opens the launcher" },
      .desc = { L"Клик по поиску на панели задач Windows 10 и 11 - значку, значку с надписью или полю - открывает лаунчер вместо поиска Windows",
                L"Clicking the search on the Windows 10 and 11 taskbar - an icon, an icon with a label or a box - opens the launcher instead of Windows search" } },
    { .page = PG_TASKBAR, .kind = SK_TOGGLE, .id = SID_MEDIA, .group = { L"Сейчас играет", L"Now playing" },
      .title = { L"Медиа на панели задач", L"Media on the taskbar" },
      .desc = { L"Обложка, название и исполнитель из любого плеера: Spotify, браузера, Медиаплеера. Клик открывает плеер",
                L"Cover, title and artist from any player: Spotify, a browser, Media Player. A click opens the player" } },
    { .page = PG_TASKBAR, .kind = SK_CHOICE, .id = SID_MEDIAPOS, .dep = SID_MEDIA, .dep_val = 1, .title = { L"Положение", L"Position" },
      .desc = { L"Справа - у значков в трее. Если значки панели выровнены по левому краю, всегда справа",
                L"Right sits next to the tray icons. Always right when taskbar icons are aligned left" },
      .opt = { { L"Слева", L"Left" }, { L"Справа", L"Right" } } },
    { .page = PG_TASKBAR, .kind = SK_TOGGLE, .id = SID_MEDIACTL, .dep = SID_MEDIA, .dep_val = 1, .title = { L"Кнопки управления", L"Playback buttons" },
      .desc = { L"Предыдущий трек, пауза и следующий", L"Previous track, pause and next" } },
    { .page = PG_TASKBAR, .kind = SK_TOGGLE, .id = SID_MEDIASCROLL, .dep = SID_MEDIA, .dep_val = 1,
      .title = { L"Прокручивать длинные названия", L"Scroll long titles" },
      .desc = { L"Название, которое не помещается, медленно едет до конца и обратно", L"A title that does not fit slowly rides to its end and back" } },
    { .page = PG_TASKBAR, .kind = SK_TOGGLE, .id = SID_MEDIAHIDE, .dep = SID_MEDIA, .dep_val = 1, .title = { L"Скрывать на паузе", L"Hide when paused" },
      .desc = { L"Показывать виджет, только пока что-то играет", L"Show the widget only while something plays" } },

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
                L"Fluent - полоска акцента у выбранной строки. Компактный - больше строк на экране. "
                L"Windows 11 и Windows 10 - как меню Пуск: закреплённые приложения сеткой или плитками, учётная запись и питание",
                L"Standard has a soft outlined highlight. Raycast uses small icons and a solid highlight. "
                L"Fluent marks the selected row with an accent bar. Compact fits more rows on screen. "
                L"Windows 11 and Windows 10 look like Start: pinned apps as a grid or tiles, account and power" },
      .opt = { { L"Стандарт", L"Standard" }, { L"Raycast", L"Raycast" }, { L"Fluent", L"Fluent" }, { L"Компактный", L"Compact" },
               { L"Windows 11", L"Windows 11" }, { L"Windows 10", L"Windows 10" } } },
    { .page = PG_APPEARANCE, .kind = SK_CHOICE, .id = SID_STARTPOS, .dep = SID_STYLE, .dep_mask = STYLES_START_MENU, .title = { L"Положение", L"Position" },
      .desc = { L"Как меню Пуск: окно выезжает из-под панели задач", L"Like Start: the window slides up from under the taskbar" },
      .opt = { { L"Слева снизу", L"Bottom left" }, { L"Снизу по центру", L"Bottom center" } } },
    { .page = PG_APPEARANCE, .kind = SK_CHOICE, .id = SID_POSITION, .dep = SID_STYLE, .dep_mask = STYLES_START_MENU, .dep_not = true,
      .title = { L"Положение", L"Position" },
      .desc = { L"Снизу - как меню Пуск: окно выезжает из-под панели задач",
                L"Bottom works like Start: the window slides up from under the taskbar" },
      .opt = { { L"По центру", L"Center" }, { L"Слева снизу", L"Bottom left" }, { L"Снизу по центру", L"Bottom center" } } },
    { .page = PG_APPEARANCE, .kind = SK_TOGGLE, .id = SID_MATCHHL, .title = { L"Подсветка совпадений", L"Highlight matches" },
      .desc = { L"Буквы, по которым нашлись приложение или файл, выделяются жирным",
                L"The letters that matched the query are drawn in bold" } },
    { .page = PG_APPEARANCE, .kind = SK_STEPPER, .id = SID_WIDTH, .dep = SID_STYLE, .dep_mask = STYLES_START_MENU, .dep_not = true, .group = { L"Размер", L"Size" },
      .title = { L"Ширина окна", L"Window width" }, .step = 20, .unit = { L"px", L"px" } },
    { .page = PG_APPEARANCE, .kind = SK_STEPPER, .id = SID_ROWS, .dep = SID_STYLE, .dep_mask = STYLES_START_MENU, .dep_not = true, .title = { L"Строк в списке", L"Visible rows" },
      .step = 1 },
    { .page = PG_APPEARANCE, .kind = SK_BUTTON, .id = SID_APREVIEW, .title = { L"Проверить", L"Try it" },
      .desc = { L"Открыть лаунчер с текущими настройками. Esc вернёт сюда", L"Open the launcher with the current settings. Esc comes back here" },
      .opt = { { L"Открыть лаунчер", L"Open launcher" } }, .glyph = 0xE768 },

    { .page = PG_ANIM, .kind = SK_BUTTON, .id = SID_RMGOTO, .dep = SID_REDUCED, .dep_val = 1,
      .title = { L"Включено уменьшение движения", L"Reduced motion is on" },
      .desc = { L"Анимации выключены, их настройки сохранены. Выключить можно в «Общих»",
                L"Animations are off and their settings are kept. It can be turned off in General" },
      .opt = { { L"Перейти", L"Go there" } }, .glyph = 0xE72A },
    { .page = PG_ANIM, .kind = SK_TOGGLE, .id = SID_ANIM, .group = { L"Общее", L"General" }, .title = { L"Анимации", L"Animations" },
      .desc = { L"Все анимации лаунчера и настроек. Выключаются и системной настройкой «Эффекты анимации»",
                L"Every animation of the launcher and settings. Also off when Windows animation effects are off" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ASPEED, .dep = SID_ANIM, .dep_val = 1, .title = { L"Общая скорость", L"Overall speed" },
      .desc = { L"Множитель для всех длительностей ниже: 200 % - всё вдвое быстрее, 50 % - вдвое медленнее",
                L"Multiplies every duration below: 200% makes everything twice as fast, 50% twice as slow" },
      .step = 10, .unit = { L"%", L"%" } },
    { .page = PG_ANIM, .kind = SK_BUTTON, .id = SID_APREVIEW, .dep = SID_ANIM, .dep_val = 1, .title = { L"Проверить", L"Try it" },
      .desc = { L"Открыть лаунчер с текущими настройками. Esc вернёт сюда", L"Open the launcher with the current settings. Esc comes back here" },
      .opt = { { L"Открыть лаунчер", L"Open launcher" } }, .glyph = 0xE768 },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_AOPEN, .dep = SID_ANIM, .dep_val = 1, .group = { L"Открытие и закрытие", L"Open and close" },
      .title = { L"Появление окна", L"Window appears" }, .desc = { L"Проявление стекла и содержимого. 0 - сразу", L"Fade-in of the glass and content. 0 is instant" },
      .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ACLOSE, .dep = SID_ANIM, .dep_val = 1, .title = { L"Закрытие", L"Window closes" },
      .desc = { L"Короче появления, чтобы Esc не ощущался задержкой", L"Shorter than opening so Esc never feels delayed" },
      .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ASCALE, .dep = SID_ANIM, .dep_val = 1, .title = { L"Начальный масштаб", L"Starting scale" },
      .desc = { L"С какого размера содержимое «наплывает» при открытии. 100 % - без увеличения",
                L"The size the content grows from when opening. 100% means no zoom" },
      .step = 1, .unit = { L"%", L"%" } },
    { .page = PG_ANIM, .kind = SK_TOGGLE, .id = SID_ACASCADE, .dep = SID_ANIM, .dep_val = 1, .title = { L"Каскад строк", L"Row cascade" },
      .desc = { L"После открытия строки появляются по очереди, сверху вниз", L"After opening the rows appear one after another, top to bottom" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_AROW, .dep = SID_ACASCADE, .dep_val = 1, .title = { L"Появление строки", L"Row fade-in" },
      .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ASTAGGER, .dep = SID_ACASCADE, .dep_val = 1, .title = { L"Задержка между строками", L"Delay between rows" },
      .step = 2, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ASELECT, .dep = SID_ANIM, .dep_val = 1, .group = { L"Движение", L"Motion" },
      .title = { L"Выделение", L"Selection" },
      .desc = { L"За сколько подложка доезжает до выбранной строки (пружина: при быстром листании скорость не рвётся)",
                L"How fast the highlight reaches the selected row (a spring: fast key repeat never jerks it)" },
      .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_ASCROLL, .dep = SID_ANIM, .dep_val = 1, .title = { L"Прокрутка", L"Scrolling" },
      .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_AMENU, .dep = SID_ANIM, .dep_val = 1, .title = { L"Меню", L"Menus" },
      .desc = { L"Контекстное меню лаунчера и выпадающие списки настроек", L"The launcher context menu and the dropdowns in settings" },
      .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_STEPPER, .id = SID_APAGE, .dep = SID_ANIM, .dep_val = 1, .title = { L"Смена раздела настроек", L"Settings page switch" },
      .step = 10, .unit = { L"мс", L"ms" } },
    { .page = PG_ANIM, .kind = SK_BUTTON, .id = SID_ARESET, .dep = SID_ANIM, .dep_val = 1, .group = { L"Сброс", L"Reset" },
      .title = { L"Значения по умолчанию", L"Default values" }, .desc = { L"Вернуть все тайминги этой страницы", L"Restore every timing on this page" },
      .opt = { { L"Сбросить", L"Reset" } }, .glyph = 0xE777 },

    { .page = PG_SEARCH, .kind = SK_STEPPER, .id = SID_KEEPQUERY, .group = { L"Запрос", L"Query" },
      .title = { L"Помнить запрос", L"Keep the query" },
      .desc = { L"Сколько секунд после закрытия хранить набранный текст. 0 - всегда начинать с пустого поля",
                L"Seconds to keep the typed text after closing. 0 always starts empty" },
      .lo = 0, .hi = 3600, .step = 10, .unit = { L"с", L"s" } },
    { .page = PG_SEARCH, .kind = SK_STEPPER, .id = SID_MAXAPPS, .group = { L"Приложения", L"Applications" },
      .title = { L"Приложений в результатах", L"Apps in results" }, .step = 1 },
    { .page = PG_SEARCH, .kind = SK_TOGGLE, .id = SID_HIDENOISE, .title = { L"Скрывать деинсталляторы и справку", L"Hide uninstallers and help links" },
      .desc = { L"Ярлыки «Удалить…», «Readme» и подобные из меню «Пуск»", L"“Uninstall…”, “Readme” and similar Start menu shortcuts" } },
    { .page = PG_SEARCH, .kind = SK_BUTTON, .id = SID_REINDEX, .title = { L"Список приложений", L"App list" },
      .desc = { L"Перечитать меню «Пуск» прямо сейчас. Обычно это происходит само", L"Re-read the Start menu now. Normally this happens by itself" },
      .opt = { { L"Обновить", L"Refresh" } }, .glyph = 0xE72C },
    { .page = PG_SEARCH, .kind = SK_INFO, .id = SID_EVERYTHING, .group = { L"Файлы (Everything)", L"Files (Everything)" },
      .title = { L"Everything", L"Everything" },
      .desc = { L"Поиск файлов идёт через ваш Everything, а если его нет - через встроенный",
                L"File search goes through your own Everything, or the built-in one when you have none" } },
    { .page = PG_SEARCH, .kind = SK_STEPPER, .id = SID_MINCHARS,
      .title = { L"Искать файлы с", L"Search files from" },
      .desc = { L"Минимальная длина запроса", L"Minimum query length" }, .step = 1, .unit = { L"симв.", L"chars" } },
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
    { .page = PG_ADVANCED, .kind = SK_INFO, .id = SID_ADMIN, .group = { L"Права", L"Permissions" },
      .title = { L"Права администратора", L"Administrator rights" },
      .desc = { L"Нужны, чтобы клавиша вызова работала поверх игр и окон администратора. Программы из лаунчера запускаются с обычными правами",
                L"Needed for the hotkey to work over games and admin windows. Programs started from the launcher get normal rights" } },

    { .page = PG_ABOUT, .kind = SK_ABOUT, .id = SID_ABOUT,
      .desc = { L"Быстрый лаунчер для Windows. Приложения, файлы и команды по одной клавише, музыка на панели задач. "
                L"Всё работает на вашем компьютере: без ИИ и без телеметрии.",
                L"A fast launcher for Windows. Apps, files and commands on one key, music on the taskbar. "
                L"It all runs on your computer: no AI, no telemetry." } },
    { .page = PG_ABOUT, .kind = SK_TOGGLE, .id = SID_UPDCHECK, .group = { L"Обновления", L"Updates" },
      .title = { L"Проверять обновления", L"Check for updates" },
      .desc = { L"Раз в несколько часов спрашивает GitHub о новой версии. Если она есть, в лаунчере появится кнопка «Обновить»",
                L"Asks GitHub for a new version every few hours. When there is one, the launcher shows an Update button" } },
    { .page = PG_ABOUT, .kind = SK_BUTTON, .id = SID_UPDNOW, .title = { L"Версия " ML_VER_WSTR, L"Version " ML_VER_WSTR },
      .desc = { L"Проверить сейчас или установить найденное обновление", L"Check now or install the update that was found" },
      .opt = { { L"Проверить", L"Check now" } }, .glyph = 0xE896 },
};
#define SITEMS countof(k_sitems)
#define SROWS_MAX 24

// The config.ini field behind an item, when it is a plain setting (its value is the field's).
static const u8 k_sid_cfg[SID__COUNT] = {
    [SID_LANG] = CF_LANGUAGE, [SID_REDUCED] = CF_REDUCED_MOTION, [SID_THEME] = CF_THEME, [SID_BACKDROP] = CF_BACKDROP,
    [SID_STYLE] = CF_STYLE, [SID_POSITION] = CF_POSITION, [SID_MATCHHL] = CF_MATCH_HIGHLIGHT, [SID_WIDTH] = CF_WIDTH, [SID_ROWS] = CF_ROWS,
    [SID_TASKBAR] = CF_TASKBAR_BUTTON, [SID_TBREPLACE] = CF_TASKBAR_REPLACE_ICON, [SID_TBICON] = CF_TASKBAR_ICON,
    [SID_TBSEARCH] = CF_TASKBAR_SEARCH, [SID_MEDIA] = CF_MEDIA_WIDGET, [SID_MEDIAPOS] = CF_MEDIA_POSITION,
    [SID_MEDIACTL] = CF_MEDIA_CONTROLS, [SID_MEDIAHIDE] = CF_MEDIA_HIDE_PAUSED, [SID_MEDIASCROLL] = CF_MEDIA_MARQUEE,
    [SID_ANIM] = CF_ANIMATIONS, [SID_ASPEED] = CF_ANIM_SPEED, [SID_AOPEN] = CF_ANIM_OPEN_MS, [SID_ACLOSE] = CF_ANIM_CLOSE_MS,
    [SID_ASCALE] = CF_ANIM_OPEN_SCALE, [SID_ACASCADE] = CF_ANIM_CASCADE, [SID_AROW] = CF_ANIM_ROW_MS, [SID_ASTAGGER] = CF_ANIM_STAGGER_MS,
    [SID_ASELECT] = CF_ANIM_SELECT_MS, [SID_ASCROLL] = CF_ANIM_SCROLL_MS, [SID_AMENU] = CF_ANIM_MENU_MS, [SID_APAGE] = CF_ANIM_PAGE_MS,
    [SID_KEEPQUERY] = CF_KEEP_QUERY, [SID_MAXAPPS] = CF_MAX_APPS, [SID_HIDENOISE] = CF_HIDE_UNINSTALLERS, [SID_MINCHARS] = CF_FILE_MIN_CHARS,
    [SID_UPDCHECK] = CF_UPDATE_CHECK,
};

typedef struct SRow {
    int item;
    bool first;
    f32 x, y, w, h;
    f32 ty;
    f32 cx, cy, cw, ch;
    f32 vis;
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
#define SETTINGS_TIMER_APPLY 3
#define SETTINGS_TIMER_TIP   4

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
    f32 head_a[12];
    int nheads;
    int focus;
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
    f32 vis[SITEMS];
    // A change shows at once and applies a moment later; changing it back before then cancels it.
    struct { bool on; int v; f64 due; } pend[SID__COUNT];
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
    // Tooltip of a locked row: armed when the mouse enters it, shown at (tip_x, tip_y) after a pause.
    int tip_row, tip_x, tip_y;
    bool tip_on;
} SW = { .focus = -1, .editing = -1, .pop_row = -1, .flash_id = -1, .page_t = 1, .pop_t = 1, .rec_t = 1, .seg_click = -1, .tip_row = -1 };

static f32 SSC(f32 v) { return v * SW.s; }
static f32 SSR(f32 v) { return floorf(v * SW.s + 0.5f); }

static void settings_invalidate(void) { SW.dirty = true; }
static bool settings_needs_render(void) { return SW.hwnd && (SW.dirty || SW.animating) && SW.target.ok; }


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

static int sval_applied(int id)
{
    switch (id) {
    case SID_AUTOSTART: return SW.autostart;
    case SID_STARTPOS: return launcher_position() == POS_BOTTOM_LEFT ? 0 : 1;
    }
    return k_sid_cfg[id] ? cfg_get(k_sid_cfg[id]) : 0;
}

// The value the settings show: a pending change, or the applied one.
static int sval(int id) { return SW.pend[id].on ? SW.pend[id].v : sval_applied(id); }

static const SItem *sitem(int id)
{
    for (int i = 0; i < (int)countof(k_sitems); i++)
        if (k_sitems[i].id == id) return &k_sitems[i];
    return NULL;
}

static void item_range(const SItem *it, int *lo, int *hi)
{
    bool own = it->lo || it->hi || !k_sid_cfg[it->id];
    *lo = own ? it->lo : k_cfg[k_sid_cfg[it->id]].lo;
    *hi = own ? it->hi : k_cfg[k_sid_cfg[it->id]].hi;
}

// Under reduced motion the animation settings keep their values but cannot be changed.
static bool item_locked(const SItem *it)
{
    return sval(SID_REDUCED) && ((it->page == PG_ANIM && it->id != SID_APREVIEW && it->id != SID_RMGOTO) || it->id == SID_MEDIASCROLL);
}

static bool item_shown(int i)
{
    const SItem *it = &k_sitems[i];
    if (!it->dep) return true;
    const SItem *p = sitem(it->dep);
    int v = sval(it->dep);
    bool match = it->dep_mask ? ((it->dep_mask >> v) & 1) != 0 : v == it->dep_val;
    return match != it->dep_not && (!p || item_shown((int)(p - k_sitems)));
}

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

// The custom Start button icon, from a file dialog. True when one was chosen.
static bool pick_taskbar_icon(void)
{
    IFileOpenDialog *d = NULL;
    if (FAILED(CoCreateInstance(&ML_CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &ML_IID_IFileOpenDialog, (void **)&d)) || !d) return false;
    COMDLG_FILTERSPEC types[] = { { TR("Изображения", "Images"), L"*.png;*.ico;*.jpg;*.jpeg;*.bmp" } };
    IFileOpenDialog_SetFileTypes(d, 1, types);
    IFileOpenDialog_SetTitle(d, TR("Значок кнопки Пуск", "Start button icon"));
    bool ok = false;
    if (SUCCEEDED(IFileOpenDialog_Show(d, SW.hwnd))) {
        IShellItem *item = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResult(d, &item)) && item) {
            WCHAR *path = NULL;
            if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path)) && path) {
                wcopy(g_cfg.taskbar_icon_path, countof(g_cfg.taskbar_icon_path), path);
                cfg_save(CF_TASKBAR_ICON_PATH);
                CoTaskMemFree(path);
                ok = true;
            }
            IShellItem_Release(item);
        }
    }
    IFileOpenDialog_Release(d);
    return ok;
}


// Autostart (schtasks.exe, slow) runs on a thread; a value changed meanwhile is applied after it.
static struct { bool busy; int want; } AS;

static DWORD WINAPI autostart_thread(void *param)
{
    int v = (int)(INT_PTR)param;
    autostart_set(v != 0);
    PostMessageW(g_hwnd, WM_APP_AUTOSTART, (WPARAM)v, autostart_get());
    return 0;
}

static void autostart_request(int v)
{
    AS.want = v;
    if (AS.busy) return;
    if (thread_start(autostart_thread, (void *)(INT_PTR)v)) {
        AS.busy = true;
    } else {
        autostart_set(v != 0);
        SW.autostart = autostart_get();
    }
}

static void settings_autostart_done(int v, bool on)
{
    AS.busy = false;
    if (AS.want != v) {
        autostart_request(AS.want);
        return;
    }
    SW.autostart = on;  // a failed change flips the toggle back
    settings_invalidate();
}

// Applies a value: the setting and config.ini, then whatever has to follow it.
static void sapply(const SItem *it, int v)
{
    if (v == sval_applied(it->id)) return;
    if (k_sid_cfg[it->id]) cfg_set(k_sid_cfg[it->id], v);
    switch (it->id) {
    case SID_AUTOSTART:
        SW.autostart = v;
        autostart_request(v);
        break;
    case SID_STARTPOS:
        cfg_set(CF_POSITION, v ? POS_BOTTOM_CENTER : POS_BOTTOM_LEFT);
        break;
    case SID_LANG:
        lang_apply();
        apps_relocalize(U.apps);
        SetWindowTextW(SW.hwnd, TR("Настройки MixLauncher", "MixLauncher Settings"));
        if (U.visible) {
            rebuild_rows(true);
            ui_invalidate();
        }
        break;
    case SID_THEME:
    case SID_BACKDROP:
        theme_update();
        U.backdrop_ok = backdrop_apply(g_hwnd);
        settings_apply_theme();
        break;
    case SID_STYLE:
        U.dpi = 0;  // the launcher's metrics are made again when it opens
        U.backdrop_ok = backdrop_apply(g_hwnd);
        break;
    case SID_WIDTH:
    case SID_ROWS:
        U.dpi = 0;
        break;
    case SID_UPDCHECK:
        upd_schedule(2000);
        break;
    case SID_MATCHHL:
        if (U.visible) ui_invalidate();
        break;
    case SID_TBICON:
    case SID_TBREPLACE:
        tb_icon_changed();
        break;
    case SID_TASKBAR:
        tb_set_enabled(v != 0);
        break;
    case SID_TBSEARCH:
        tb_set_search(v != 0);
        break;
    case SID_MEDIA:
        media_set_enabled(v != 0);
        break;
    case SID_REDUCED:
        if (SW.hwnd) dwm_set(SW.hwnd, DWA_NO_TRANSITIONS, v);
        media_settings_changed();
        break;
    case SID_MEDIAPOS:
    case SID_MEDIACTL:
    case SID_MEDIAHIDE:
    case SID_MEDIASCROLL:
        media_settings_changed();
        break;
    case SID_HIDENOISE:
        apps_request_reindex();
        break;
    }
    settings_invalidate();
}

static void pend_schedule(void)
{
    f64 due = 0;
    for (int id = 0; id < SID__COUNT; id++)
        if (SW.pend[id].on && (due == 0 || SW.pend[id].due < due)) due = SW.pend[id].due;
    if (!SW.hwnd) return;
    if (due == 0) KillTimer(SW.hwnd, SETTINGS_TIMER_APPLY);
    else SetTimer(SW.hwnd, SETTINGS_TIMER_APPLY, (UINT)MAX(10.0, (due - time_now()) * 1000.0), NULL);
}

// Applies the pending changes that are due (or all of them).
static void pend_flush(bool all)
{
    f64 now = time_now();
    for (int id = 0; id < SID__COUNT; id++) {
        if (!SW.pend[id].on || (!all && SW.pend[id].due > now + 0.005)) continue;
        SW.pend[id].on = false;
        const SItem *it = sitem(id);
        if (it) sapply(it, SW.pend[id].v);
    }
    pend_schedule();
}

// Shows a change at once and applies it after the animation; changing it back first cancels it.
static void sset(const SItem *it, int v)
{
    int lo, hi;
    item_range(it, &lo, &hi);
    if (it->kind == SK_STEPPER) v = CLAMP(v, lo, hi);
    if (it->kind == SK_TOGGLE) v = v ? 1 : 0;
    if (it->kind == SK_CHOICE) v = CLAMP(v, 0, choice_count(it) - 1);
    if (v == sval(it->id) || item_locked(it)) return;
    if (it->id == SID_TBICON && v == TBI_CUSTOM && !g_cfg.taskbar_icon_path[0] && !pick_taskbar_icon()) return;
    if (g_cfg.reduced_motion || it->id == SID_REDUCED) {  // nothing to wait for without the animation
        SW.pend[it->id].on = false;
        sapply(it, v);
    } else if (v == sval_applied(it->id)) {
        SW.pend[it->id].on = false;
    } else {
        SW.pend[it->id].on = true;
        SW.pend[it->id].v = v;
        SW.pend[it->id].due = time_now() + (it->kind == SK_STEPPER ? 0.45 : 0.25);
    }
    pend_schedule();
    settings_invalidate();
}

// A button's label says "Done" for a moment.
static void button_flash(int id)
{
    SW.flash_id = id;
    SW.flash_until = time_now() + 1.6;
    SetTimer(SW.hwnd, SETTINGS_TIMER_FLASH, 1700, NULL);
}

static void button_action(const SItem *it)
{
    switch (it->id) {
    case SID_REINDEX:
        apps_request_reindex();
        button_flash(it->id);
        break;
    case SID_CLEARHIST:
        if (!SW.armed) {  // the first press asks
            SW.armed = true;
            break;
        }
        SW.armed = false;
        history_clear();
        refresh_frecency();
        refresh_empty_view();
        button_flash(it->id);
        break;
    case SID_CONFIG: {
        WCHAR path[MAX_PATH];
        config_path(path);
        launch_open(path, OPEN_PATH);
        break;
    }
    case SID_DATADIR:
        launch_open(g_data_dir, OPEN_PATH);
        break;
    case SID_TBFILE:
        if (pick_taskbar_icon()) {
            SW.pend[SID_TBICON].on = false;
            cfg_set(CF_TASKBAR_ICON, TBI_CUSTOM);
            tb_icon_changed();
        }
        break;
    case SID_APREVIEW:
        ui_show();
        break;
    case SID_RMGOTO:
        settings_open_at(SID_REDUCED);
        break;
    case SID_UPDNOW:
        if (upd_state() == UPD_AVAILABLE || (upd_state() == UPD_ERROR && UPD.url[0])) upd_download();
        else upd_check();
        break;
    case SID_ARESET:
        for (int id = 0; id < SID__COUNT; id++) {
            int f = k_sid_cfg[id];
            if (f < CF_ANIM_FIRST || f > CF_ANIM_LAST) continue;
            SW.pend[id].on = false;
            cfg_set(f, k_cfg[f].def);
        }
        button_flash(it->id);
        break;
    }
    settings_invalidate();
}

static void refresh_status(void)
{
    if (!AS.busy) SW.autostart = autostart_get();
    SW.everything = ev_state();
}

static const WCHAR *info_text(int id, bool *good)
{
    if (id == SID_ADMIN) {
        *good = g_elevated;
        return g_elevated ? TR("Есть", "Granted") : TR("Нет", "No");
    }
    *good = SW.everything != EVS_NONE;
    if (SW.everything == EVS_OWN) return TR("Встроенный, запущен", "Built-in, running");
    return SW.everything == EVS_USER ? TR("Запущен", "Running") : TR("Не запущен", "Not running");
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
        cfg_save(CF_HOTKEY);
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
        cfg_save(CF_EVERYTHING_FILTER);
    }
    SW.editing = -1;
    KillTimer(SW.hwnd, SETTINGS_TIMER_CARET);
    settings_invalidate();
}

static void edit_splice(int a, int b, const WCHAR *s, int n) { SW.edit_caret = text_splice(SW.edit, &SW.edit_len, countof(SW.edit), a, b, s, n); }

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
            edit_splice(from, SW.edit_caret, NULL, 0);
        }
        break;
    case VK_DELETE:
        if (SW.edit_caret < SW.edit_len) edit_splice(SW.edit_caret, SW.edit_caret + 1, NULL, 0);
        break;
    case 'V':
        if (ctrl) {
            WCHAR tmp[countof(SW.edit)];
            int n = clipboard_get(tmp, countof(tmp));
            if (n) edit_splice(SW.edit_caret, SW.edit_caret, tmp, n);
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
        if (it->id == SID_CLEARHIST) tw = MAX(tw, text_width(FONT_TEXT, fs, TR("Точно очистить?", "Clear for sure?"), -1));
        if (it->id == SID_UPDNOW) tw = MAX(tw, text_width(FONT_TEXT, fs, TR("Обновить до 10.10.10", "Update to 10.10.10"), -1));
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

static f32 vis_ease(f32 v)
{
    v = CLAMP(v, 0.f, 1.f);
    return v < 0.5f ? 4.f * v * v * v : 1.f - powf(-2.f * v + 2.f, 3.f) * 0.5f;
}

// How much of a group (its header and card) is shown: as much as its most visible row.
static f32 group_vis(int i)
{
    f32 g = 0;
    for (int j = i; j < (int)SITEMS && k_sitems[j].page == k_sitems[i].page && (j == i || !k_sitems[j].group.ru); j++)
        g = MAX(g, vis_ease(SW.vis[j]));
    return g;
}

// Every item gets a row (stable indices): a hidden one is 0 tall, an appearing one partly.
static void settings_layout(void)
{
    SW.nrows = SW.ncards = SW.nheads = 0;
    f32 x0 = content_x0(), x1 = content_x1(), w = x1 - x0;
    f32 y = SSR(2);
    int card = -1, lead = -1;
    bool any = false, card_first = false;
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
            r->vis = 1.f;
            y += r->h;
            card = lead = -1;
            any = true;
            continue;
        }
        if (it->group.ru || lead < 0) {
            lead = i;
            f32 gv = group_vis(i);
            if (any) y += floorf(SSR(20) * gv + 0.5f);
            if (it->group.ru && SW.nheads < (int)countof(SW.heads)) {
                f32 hh = floorf(SSR(28) * gv + 0.5f);
                SW.heads[SW.nheads] = (SBox){ x0 + SSR(4), y + SSR(15), 0, hh };
                SW.head_a[SW.nheads] = gv;
                SW.head_text[SW.nheads++] = it->group;
                y += hh;
            }
            card = -1;
            if (gv > 0.f && SW.ncards < (int)countof(SW.cards)) {
                card = SW.ncards++;
                SW.cards[card] = (SBox){ x0, y, w, 0 };
            }
            card_first = true;
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
        r->vis = vis_ease(SW.vis[i]);
        r->h = r->vis >= 1.f ? r->h : floorf(r->h * r->vis + 0.5f);
        if (r->h > 0 && card_first) {
            r->first = true;
            card_first = false;
        }
        y += r->h;
        any |= r->h > 0;
        if (card >= 0) SW.cards[card].h = y - SW.cards[card].y;
    }
    SW.content_h = y + SSR(28);
}

static f32 max_scroll_s(void) { return MAX(0.f, SW.content_h - ((f32)SW.H - top_h())); }

static bool row_focusable(int r)
{
    if (r < 0 || r >= SW.nrows || !item_shown(SW.rows[r].item) || item_locked(&k_sitems[SW.rows[r].item])) return false;
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

static void draw_app_tile(f32 x, f32 y, f32 sz)
{
    x = floorf(x);
    y = floorf(y);
    f32 rad = floorf(sz * 0.225f + 0.5f);
    r_rect(x, y, sz, sz, RGBA(APP_LOGO_TILE >> 16, (APP_LOGO_TILE >> 8) & 255, APP_LOGO_TILE & 255, 255), rad);
    r_border(x, y, sz, sz, RGBA(255, 255, 255, 26), rad);
    f32 u = sz * APP_LOGO_FILL / 152.f, m[4] = { 0.70710678f, 0.70710678f * APP_LOGO_SQUASH, -0.70710678f, 0.70710678f * APP_LOGO_SQUASH };
    for (int i = 0; i < 3; i++) {
        u32 c = k_logo_rgb[i];
        r_rect_affine(x + sz * 0.5f, y + sz * 0.5f + k_logo_dy[i] * u, 112.f * u, 112.f * u, m, RGBA(c >> 16, (c >> 8) & 255, c & 255, 255), 9.f * u);
    }
}

static void draw_skeycaps(f32 x, f32 cy, const KeyTok *t, int n, int font, f32 fs, f32 h, f32 pad, f32 gap, f32 rad, u32 bg, u32 border,
                         u32 fg, u32 side_fg)
{
    for (int i = 0; i < n; i++) {
        f32 w = keycap_w(&t[i], font, fs, h, pad), y = floorf(cy - h * 0.5f);
        r_rect(x, y, w, h, bg, rad);
        if (border) r_border(x, y, w, h, border, rad);
        f32 sfs = fs * 0.7f, lw = text_width(font, fs, t[i].label, -1), sw = 0;
        if (t[i].side) sw = text_width(FONT_TEXT, sfs, t[i].side, -1) + fs * 0.35f;
        f32 tx = floorf(x + (w - lw - sw) * 0.5f + 0.5f), base = text_baseline(font, fs, cy);
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
    r_border(x, y, w, h, c->panel_border, SSR(12));

    f32 tile = SSR(36), tx = x + SSR(16), ty = y + SSR(18);
    draw_app_tile(tx, ty, tile);
    f32 nx = tx + tile + SSR(12);
    text_draw_fit(FONT_TEXT_SEMIBOLD, SSC(15), nx, floorf(ty + SSR(16)), x + w - nx - SSR(10), L"MixLauncher", -1, t->text, false);
    text_draw_fit(FONT_TEXT, SSC(12.5f), nx, floorf(ty + SSR(33)), x + w - nx - SSR(10), TR("Настройки", "Settings"), -1, t->dim, false);

    {
        f32 r[4];
        nav_rect(SW.page, r);
        f32 py = floorf(SW.nav_y + 0.5f), ph = r[3] - r[1];
        r_rect(r[0], py, r[2] - r[0], ph, c->nav_sel, SSR(8));
        r_border(r[0], py, r[2] - r[0], ph, c->nav_sel_border, SSR(8));
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
        text_draw_fit(sel ? FONT_TEXT_SEMIBOLD : FONT_TEXT, fs, ix + isz + SSR(12), text_baseline(FONT_TEXT, fs, cy), r[2] - ix - isz - SSR(18),
                      ss(k_pages[i].name), -1, fg, false);
    }
}

static void draw_toggle(f32 x, f32 cy, f32 on, bool hov, const Theme *t, const SColors *c)
{
    // Flat: a plain grey track when off, the accent when on; the knob keeps its size and only travels.
    f32 w = toggle_w(), h = SSR(22), y = floorf(cy - h * 0.5f);
    u32 off_bg = t->dark ? RGBA(255, 255, 255, hov ? 50 : 36) : RGBA(0, 0, 0, hov ? 44 : 32);
    r_rect(x, y, w, h, color_mix(off_bg, t->accent, on), h * 0.5f);
    f32 k = h - SSR(6);
    f32 kx = floorf(x + h * 0.5f + (w - h) * on - k * 0.5f + 0.5f), ky = floorf(cy - k * 0.5f + 0.5f);
    u32 knob = c->knob;
    f32 lum = (0.299f * (f32)(t->accent & 255) + 0.587f * (f32)((t->accent >> 8) & 255) + 0.114f * (f32)((t->accent >> 16) & 255)) / 255.f;
    if (lum > 0.62f) knob = RGBA(24, 24, 26, 255);
    u32 off_knob = t->dark ? RGBA(235, 235, 237, 255) : RGBA(255, 255, 255, 255);
    if (!t->dark) r_rect_ex(kx, ky + SSR(1), k, k, RGBA(0, 0, 0, 36), k * 0.5f, 0, SSR(2));  // lifts the white knob off the light track
    r_rect(kx, ky, k, k, color_mix(off_knob, knob, on), k * 0.5f);
}

static void draw_srow(SRow *r, f32 off, const Theme *t, const SColors *c)
{
    const SItem *it = &k_sitems[r->item];
    f32 y = r->y + off, ty = r->ty + off;
    int ri = (int)(r - SW.rows);
    bool locked = item_locked(it);
    bool hov = SW.hover.type == HT_ROW && SW.hover.idx == ri && !locked;
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
    f32 opacity = R.opacity;
    if (locked) R.opacity *= 0.4f;
    if (SW.row_hov_idx == ri && SW.row_hov > 0.f) {
        f32 g = SSR(4);
        r_rect(r->x + g, y + g, r->w - g * 2, r->h - g * 2, color_alpha(c->nav_hover, SW.row_hov), SSR(7));
    }
    f32 tx = r->x + SSR(18), tmax = r->cx - SSR(28) - tx;
    text_draw_fit(FONT_TEXT, fs, tx, text_baseline(FONT_TEXT, fs, ty), tmax, ss(it->title), -1, t->text, false);
    if (r->ndesc) {
        const WCHAR *d = ss(it->desc);
        int n = wlen(d);
        for (int k = 0; k < r->ndesc; k++) {
            int a = r->desc_at[k], b = k + 1 < r->ndesc ? r->desc_at[k + 1] : n;
            while (b > a && d[b - 1] == ' ') b--;
            f32 lc = y + SSR(14) + SSR(20) + SSR(3) + SSR(18) * (f32)k + SSR(9);
            text_draw_fit(FONT_TEXT, dfs, tx, text_baseline(FONT_TEXT, dfs, lc), tmax, d + a, b - a, t->dim, false);
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
            r_border(px, cy + SSR(2), pw, ch - SSR(4), c->seg_border, SSR(6));
            int hk = chov ? seg_index_at(it, cx, (f32)SW.mx) : -1;
            for (int k = 0; k < n; k++) {
                const WCHAR *lbl = ss(it->opt[k]);
                f32 lw = text_width(FONT_TEXT, cfs, lbl, -1);
                u32 col = k == cur ? t->text : k == hk ? color_mix(t->dim, t->text, 0.6f) : t->dim;
                text_draw(FONT_TEXT, cfs, floorf(xs[k] + (ws[k] - lw) * 0.5f + 0.5f), text_baseline(FONT_TEXT, cfs, mid), lbl, -1, col);
            }
            break;
        }
        bool open = SW.pop_open && SW.pop_row == ri;
        r_rect(cx, cy, cw, ch, chov || open ? c->ctl_hover : c->ctl, SSR(7));
        const WCHAR *lbl = ss(it->opt[sval(it->id)]);
        f32 gx = cx + cw - SSR(14) - SSR(5);
        text_draw_fit(FONT_TEXT, cfs, cx + SSR(14), text_baseline(FONT_TEXT, cfs, mid), gx - SSR(10) - cx - SSR(14), lbl, -1, t->text, false);
        text_draw_icon(0xE70D, SSC(10), gx, mid, t->dim);
        break;
    }
    case SK_STEPPER: {
        int v = sval(it->id), lo, hi;
        item_range(it, &lo, &hi);
        f32 bw = SSR(34);
        bool can_dec = v > lo, can_inc = v < hi;
        r_rect(cx, cy, cw, ch, c->ctl, SSR(7));
        if (hov && SW.hover.part == PART_MINUS && can_dec) r_rect(cx, cy, bw, ch, c->ctl_hover, SSR(7));
        if (hov && SW.hover.part == PART_PLUS && can_inc) r_rect(cx + cw - bw, cy, bw, ch, c->ctl_hover, SSR(7));
        r_rect(cx + bw, cy + SSR(7), 1, ch - SSR(14), c->sep, 0);
        r_rect(cx + cw - bw, cy + SSR(7), 1, ch - SSR(14), c->sep, 0);
        text_draw_icon(0xE738, SSC(11), cx + bw * 0.5f, mid, can_dec ? t->text : t->faint);
        text_draw_icon(0xE710, SSC(11), cx + cw - bw * 0.5f, mid, can_inc ? t->text : t->faint);
        WCHAR num[16];
        wfmt(num, countof(num), L"%d", v);
        const WCHAR *unit = it->unit.ru ? ss(it->unit) : NULL;
        f32 ufs = SSC(12), nw = text_width(FONT_TEXT, cfs, num, -1), uw = unit ? text_width(FONT_TEXT, ufs, unit, -1) + SSR(4) : 0;
        f32 nx = floorf(cx + (cw - nw - uw) * 0.5f + 0.5f), base = text_baseline(FONT_TEXT, cfs, mid);
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
        f32 ix = cx + SSR(10), iw = cw - SSR(20), base = text_baseline(FONT_TEXT, cfs, mid);
        f32 clip[4];
        memcpy(clip, R.clip, sizeof clip);
        r_set_clip(MAX(ix - SSR(1), clip[0]), MAX(cy, clip[1]), MIN(ix + iw + SSR(1), clip[2]), MIN(cy + ch, clip[3]));
        if (!s[0] && !editing) {
            text_draw(FONT_TEXT, cfs, ix, base, TR("Нет", "None"), -1, t->faint);
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
        r_set_clip(clip[0], clip[1], clip[2], clip[3]);
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
        const WCHAR *lbl = armed ? TR("Точно очистить?", "Clear for sure?") : flashing ? TR("Готово", "Done") : ss(it->opt[0]);
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
        text_draw(FONT_TEXT, cfs, bx + gw, text_baseline(FONT_TEXT, cfs, mid), lbl, -1, col);
        break;
    }
    case SK_INFO: {
        bool good;
        const WCHAR *s = info_text(it->id, &good);
        f32 dot = SSR(8);
        r_rect(cx, floorf(mid - dot * 0.5f), dot, dot, good ? RGBA(48, 176, 99, 255) : t->danger, dot * 0.5f);
        text_draw(FONT_TEXT, cfs, cx + dot + SSR(8), text_baseline(FONT_TEXT, cfs, mid), s, -1, t->text);
        break;
    }
    }
    R.opacity = opacity;
}

static void draw_tip(const Theme *t, const SColors *c)
{
    const WCHAR *s = TR("Изменение недоступно при включённом уменьшении движения", "Can't be changed while reduced motion is on");
    f32 fs = SSC(12.5f), pad = SSR(10), h = SSR(30), w = floorf(text_width(FONT_TEXT, fs, s, -1) + pad * 2);
    f32 x = floorf(MAX(SSR(8), MIN((f32)SW.tip_x, (f32)SW.W - w - SSR(8)))), y = (f32)SW.tip_y + SSR(22);
    if (y + h > (f32)SW.H - SSR(8)) y = (f32)SW.tip_y - h - SSR(8);
    r_panel(x, y, w, h, SSR(7), c->pop, c->pop_border, c->shadow, SSR(3), SSR(8));
    text_draw(FONT_TEXT, fs, x + pad, text_baseline(FONT_TEXT, fs, y + h * 0.5f), s, -1, t->text);
}

static void draw_popover(const Theme *t, const SColors *c)
{
    const SItem *it = &k_sitems[SW.rows[SW.pop_row].item];
    f32 x = SW.pop_x, y = SW.pop_y - floorf((1.f - ease_out_cubic(SW.pop_t)) * SSR(6) + 0.5f), w = SW.pop_w, h = SW.pop_h;
    r_panel(x, y, w, h, SSR(10), c->pop, c->pop_border, c->shadow, SSR(6), SSR(14));
    int sel = sval(it->id);
    f32 fs = SSC(13.5f);
    for (int k = 0; k < choice_count(it); k++) {
        f32 iy = y + SSR(5) + SSR(32) * (f32)k, cy = iy + SSR(16);
        if (SW.pop_hover == k) r_rect(x + SSR(5), iy, w - SSR(10), SSR(32), c->pop_hover, SSR(6));
        if (k == sel) text_draw_icon(0xE73E, SSC(12), x + SSR(5) + SSR(16), cy, t->text);
        text_draw(FONT_TEXT, fs, x + SSR(5) + SSR(32), text_baseline(FONT_TEXT, fs, cy), ss(it->opt[k]), -1, t->text);
    }
}

static void draw_recorder(const Theme *t, const SColors *c)
{
    r_rect(0, 0, (f32)SW.W, (f32)SW.H, c->overlay, 0);
    f32 pw = SSR(460), ph = SSR(260);
    f32 px = floorf(((f32)SW.W - pw) * 0.5f), py = floorf(((f32)SW.H - ph) * 0.5f + (1.f - ease_out_cubic(SW.rec_t)) * SSR(12));
    box_set(SW.rec_panel, px, py, pw, ph);
    r_panel(px, py, pw, ph, SSR(16), t->dark ? RGBA(36, 36, 39, 255) : RGBA(252, 252, 253, 255), c->pop_border, c->shadow, SSR(12), SSR(26));

    text_draw(FONT_TEXT, SSC(16), px + SSR(26), py + SSR(42), TR("Задайте сочетание…", "Set Hotkey…"), -1, t->faint);

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
        const WCHAR *s = TR("Нажмите клавишу или сочетание", "Press a key or a shortcut");
        f32 fs = SSC(15), w = text_width(FONT_TEXT, fs, s, -1);
        text_draw(FONT_TEXT, fs, floorf(px + (pw - w) * 0.5f), text_baseline(FONT_TEXT, fs, kcy), s, -1, t->faint);
    }
    const WCHAR *msg = NULL;
    u32 msg_col = t->faint;
    if (!live && problem && SW.rec.has) {
        msg = ss(*problem);
        msg_col = t->danger;
    } else if (!n || live) {
        msg = TR("Одна клавиша (Win, F1-F24, правый Ctrl…) или сочетание с Ctrl, Alt, Shift, Win",
                 "A single key (Win, F1-F24, right Ctrl…) or a shortcut with Ctrl, Alt, Shift, Win");
    }
    if (msg) {
        f32 fs = SSC(12.5f), w = MIN(text_width(FONT_TEXT, fs, msg, -1), pw - SSR(40));
        text_draw_fit(FONT_TEXT, fs, floorf(px + (pw - w) * 0.5f), text_baseline(FONT_TEXT, fs, py + SSR(180)), pw - SSR(40), msg, -1, msg_col, false);
    }

    f32 by = py + ph - SSR(56), bcy = by + SSR(28);
    r_rect(px, by, pw, 1, c->sep, 0);
    draw_app_tile(px + SSR(22), bcy - SSR(11), SSR(22));
    f32 fs = SSC(14);
    text_draw(FONT_TEXT, fs, px + SSR(22) + SSR(22) + SSR(10), text_baseline(FONT_TEXT, fs, bcy), TR("Открыть MixLauncher", "Open MixLauncher"), -1,
              t->text);

    bool can_save = SW.rec.has && !problem;
    f32 xr = px + pw - SSR(14);
    for (int b = 1; b >= 0; b--) {
        const WCHAR *lbl = b ? TR("Сохранить", "Save") : TR("Отмена", "Cancel");
        KeyTok cap;
        wcopy(cap.label, countof(cap.label), b ? L"Enter" : L"Esc");
        cap.side = NULL;
        f32 cfs = SSC(12), ch = SSR(22), cw = keycap_w(&cap, FONT_TEXT, cfs, ch, SSR(6));
        f32 lw = text_width(FONT_TEXT, fs, lbl, -1);
        f32 bw = SSR(10) + lw + SSR(8) + cw + SSR(6), bh = SSR(34);
        f32 bx = xr - bw, byy = floorf(bcy - bh * 0.5f);
        box_set(SW.rec_btn[b], bx, byy, bw, bh);
        bool hov = SW.hover.type == (b ? HT_RECSAVE : HT_RECCANCEL);
        bool enabled = !b || can_save;
        if (b && can_save) r_rect(bx, byy, bw, bh, color_alpha(t->accent, hov ? 0.32f : 0.22f), SSR(8));
        else if (hov && enabled) r_rect(bx, byy, bw, bh, c->ctl, SSR(8));
        u32 col = !enabled ? t->faint : t->text;
        text_draw(FONT_TEXT, fs, bx + SSR(10), text_baseline(FONT_TEXT, fs, bcy), lbl, -1, col);
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
    const SItem *it = &k_sitems[SW.rows[SW.hover.idx].item];
    return it->kind == SK_TOGGLE && !item_locked(it) ? SW.hover.idx : -1;
}
