// The settings: the items of every page (k_sitems), their values and what changing one does. Layout and
// drawing of the rows are here too; the window, input and animation are in settings_wnd.rs.

use crate::*;
use windows::Win32::Graphics::Direct3D11::D3D11_BOX;
use windows::Win32::UI::Shell::Common::COMDLG_FILTERSPEC;
use windows::Win32::UI::Shell::{FileOpenDialog, IFileOpenDialog, SIGDN_FILESYSPATH};

pub const SK_TOGGLE: u8 = 0;
pub const SK_CHOICE: u8 = 1;
pub const SK_STEPPER: u8 = 2;
pub const SK_SLIDER: u8 = 3;
pub const SK_TEXT: u8 = 4;
pub const SK_HOTKEY: u8 = 5;
pub const SK_BUTTON: u8 = 6;
pub const SK_INFO: u8 = 7;
pub const SK_ABOUT: u8 = 8;
pub const SK_GALLERY: u8 = 9;

pub const SID_NONE: u8 = 0;
pub const SID_HOTKEY: u8 = 1;
pub const SID_AUTOSTART: u8 = 2;
pub const SID_KEEPQUERY: u8 = 3;
pub const SID_THEME: u8 = 4;
pub const SID_BACKDROP: u8 = 5;
pub const SID_ANIM: u8 = 6;
pub const SID_WIDTH: u8 = 7;
pub const SID_ROWS: u8 = 8;
pub const SID_MAXAPPS: u8 = 9;
pub const SID_HIDENOISE: u8 = 10;
pub const SID_REINDEX: u8 = 11;
pub const SID_MINCHARS: u8 = 12;
pub const SID_FILTER: u8 = 13;
pub const SID_CLEARHIST: u8 = 14;
pub const SID_CONFIG: u8 = 15;
pub const SID_DATADIR: u8 = 16;
pub const SID_ADMIN: u8 = 17;
pub const SID_EVERYTHING: u8 = 18;
pub const SID_ABOUT: u8 = 19;
pub const SID_ASPEED: u8 = 20;
pub const SID_AOPEN: u8 = 21;
pub const SID_ACLOSE: u8 = 22;
pub const SID_ASCALE: u8 = 23;
pub const SID_ACASCADE: u8 = 24;
pub const SID_AROW: u8 = 25;
pub const SID_ASTAGGER: u8 = 26;
pub const SID_ASELECT: u8 = 27;
pub const SID_ASCROLL: u8 = 28;
pub const SID_AMENU: u8 = 29;
pub const SID_APAGE: u8 = 30;
pub const SID_APREVIEW: u8 = 31;
pub const SID_ARESET: u8 = 32;
pub const SID_TASKBAR: u8 = 33;
pub const SID_TBREPLACE: u8 = 34;
pub const SID_TBICON: u8 = 35;
pub const SID_TBFILE: u8 = 36;
pub const SID_MEDIA: u8 = 37;
pub const SID_MEDIAPOS: u8 = 38;
pub const SID_MEDIACTL: u8 = 39;
pub const SID_MEDIAHIDE: u8 = 40;
pub const SID_MEDIASCROLL: u8 = 41;
pub const SID_LANG: u8 = 42;
pub const SID_STYLE: u8 = 43;
pub const SID_MATCHHL: u8 = 44;
pub const SID_UPDCHECK: u8 = 45;
pub const SID_UPDNOW: u8 = 46;
pub const SID_REDUCED: u8 = 47;
pub const SID_RMGOTO: u8 = 48;
pub const SID_POSITION: u8 = 49;
pub const SID_STARTPOS: u8 = 50;
pub const SID_TBSEARCH: u8 = 51;
pub const SID_SCALE: u8 = 52;
pub const SID_STYLEGO: u8 = 53;
pub const SID__COUNT: usize = 54;

pub const PG_GENERAL: u8 = 0;
pub const PG_TASKBAR: u8 = 1;
pub const PG_APPEARANCE: u8 = 2;
pub const PG_DESIGN: u8 = 3;
pub const PG_ANIM: u8 = 4;
pub const PG_SEARCH: u8 = 5;
pub const PG_ADVANCED: u8 = 6;
pub const PG_ABOUT: u8 = 7;
pub const PG__COUNT: i32 = 8;

#[derive(Clone, Copy)]
pub struct Str {
    pub ru: *const WCHAR,
    pub en: *const WCHAR,
}
unsafe impl Sync for Str {}
impl Str {
    pub const NONE: Str = Str { ru: core::ptr::null(), en: core::ptr::null() };
}

#[macro_export]
macro_rules! st {
    ($ru:literal, $en:literal) => {
        $crate::settings::Str { ru: windows_sys::w!($ru), en: windows_sys::w!($en) }
    };
}

pub unsafe fn ss(s: Str) -> *const WCHAR {
    if g_lang_ru { s.ru } else { s.en }
}

pub struct Page {
    pub name: Str,
    pub glyph: u32,
}

pub static k_pages: [Page; PG__COUNT as usize] = [
    Page { name: st!("Общие", "General"), glyph: 0xE713 },
    Page { name: st!("Панель задач", "Taskbar"), glyph: 0xE7C4 },
    Page { name: st!("Внешний вид", "Appearance"), glyph: 0xE790 },
    Page { name: st!("Дизайн", "Design"), glyph: 0xE80A },
    Page { name: st!("Анимация", "Animation"), glyph: 0xE916 },
    Page { name: st!("Поиск", "Search"), glyph: 0xE721 },
    Page { name: st!("Дополнительно", "Advanced"), glyph: 0xE90F },
    Page { name: st!("О программе", "About"), glyph: 0xE946 },
];

#[derive(Clone, Copy)]
pub struct SItem {
    pub page: u8,
    pub kind: u8,
    pub id: u8,
    pub group: Str,
    pub title: Str,
    pub desc: Str,
    pub lo: i32,
    pub hi: i32,
    pub step: i32, // a stepper or a slider; with no range given, its setting's range
    pub unit: Str,
    pub opt: [Str; 7],
    pub warn: u8, // options marked with a caution sign (bits), the reason in a tooltip (warn_tip)
    pub glyph: u32,
    pub dep: u8,
    pub dep_val: u8, // shown only while setting `dep` has the value `dep_val` (and is shown itself)
    pub dep_mask: u16, // ...or, with this, one of these values (bits)
    pub dep_not: bool, // ...or, with this, any other value
}

const SI: SItem = SItem {
    page: 0,
    kind: 0,
    id: 0,
    group: Str::NONE,
    title: Str::NONE,
    desc: Str::NONE,
    lo: 0,
    hi: 0,
    step: 0,
    unit: Str::NONE,
    opt: [Str::NONE; 7],
    warn: 0,
    glyph: 0,
    dep: 0,
    dep_val: 0,
    dep_mask: 0,
    dep_not: false,
};

const fn o1(a: Str) -> [Str; 7] {
    [a, Str::NONE, Str::NONE, Str::NONE, Str::NONE, Str::NONE, Str::NONE]
}
const fn o2(a: Str, b: Str) -> [Str; 7] {
    [a, b, Str::NONE, Str::NONE, Str::NONE, Str::NONE, Str::NONE]
}
const fn o3(a: Str, b: Str, c: Str) -> [Str; 7] {
    [a, b, c, Str::NONE, Str::NONE, Str::NONE, Str::NONE]
}

pub const SITEMS: usize = 54;
pub const SROWS_MAX: usize = 24;

pub static k_sitems: [SItem; SITEMS] = [
    SItem {
        page: PG_GENERAL,
        kind: SK_HOTKEY,
        id: SID_HOTKEY,
        group: st!("Запуск", "Startup"),
        title: st!("Открыть MixLauncher", "Open MixLauncher"),
        desc: st!(
            "Клавиша или сочетание, которое открывает и закрывает лаунчер. Прежнее действие сочетания отключается: например, Win больше не открывает «Пуск», а Win+D не сворачивает окна.",
            "The key or shortcut that opens and closes the launcher. Whatever it did before is turned off: Win no longer opens Start, Win+D no longer shows the desktop."
        ),
        ..SI
    },
    SItem {
        page: PG_GENERAL,
        kind: SK_TOGGLE,
        id: SID_AUTOSTART,
        title: st!("Запускать при входе в Windows", "Start with Windows"),
        desc: st!(
            "Через планировщик заданий: с правами администратора и без запроса UAC при входе",
            "Through Task Scheduler: as administrator, without a UAC prompt at sign-in"
        ),
        ..SI
    },
    SItem {
        page: PG_GENERAL,
        kind: SK_CHOICE,
        id: SID_LANG,
        group: st!("Язык", "Language"),
        title: st!("Язык интерфейса", "Interface language"),
        desc: st!(
            "«Системный» берёт язык Windows: русский для русской Windows, иначе английский",
            "System follows Windows: Russian on a Russian Windows, English otherwise"
        ),
        opt: o3(st!("Системный", "System"), st!("Русский", "Русский"), st!("English", "English")),
        ..SI
    },
    SItem {
        page: PG_GENERAL,
        kind: SK_TOGGLE,
        id: SID_REDUCED,
        group: st!("Доступность", "Accessibility"),
        title: st!("Уменьшение движения", "Reduced motion"),
        desc: st!(
            "Без анимаций и задержек: окна, меню и списки появляются сразу. Настройки анимаций сохраняются до выключения",
            "No animations or delays: windows, menus and lists appear at once. Animation settings are kept until it is off"
        ),
        ..SI
    },
    SItem {
        page: PG_TASKBAR,
        kind: SK_TOGGLE,
        id: SID_TASKBAR,
        group: st!("Кнопка Пуск", "Start button"),
        title: st!("Кнопка Пуск открывает MixLauncher", "Start button opens MixLauncher"),
        desc: st!(
            "Клик по кнопке Пуск (Windows 11) открывает лаунчер. Правый клик по-прежнему открывает меню Win+X",
            "Clicking the Start button (Windows 11) opens the launcher. Right click still opens the Win+X menu"
        ),
        ..SI
    },
    SItem {
        page: PG_TASKBAR,
        kind: SK_TOGGLE,
        id: SID_TBREPLACE,
        dep: SID_TASKBAR,
        dep_val: 1,
        title: st!("Заменять значок меню Пуск", "Replace the Start button icon"),
        desc: st!("Наш значок вместо логотипа Windows. Выключено - кнопка выглядит как обычно", "Our icon instead of the Windows logo. Off keeps the button looking as usual"),
        ..SI
    },
    SItem {
        page: PG_TASKBAR,
        kind: SK_CHOICE,
        id: SID_TBICON,
        dep: SID_TBREPLACE,
        dep_val: 1,
        title: st!("Значок кнопки", "Button icon"),
        opt: [
            st!("Ромб", "Diamond"),
            st!("Сетка", "Grid"),
            st!("Лепестки", "Petals"),
            st!("Слои", "Layers"),
            st!("Точки", "Dots"),
            st!("Свой", "Custom"),
            Str::NONE,
        ],
        ..SI
    },
    SItem {
        page: PG_TASKBAR,
        kind: SK_BUTTON,
        id: SID_TBFILE,
        dep: SID_TBICON,
        dep_val: TBI_CUSTOM as u8,
        title: st!("Свой значок", "Custom icon"),
        desc: st!("PNG, ICO или JPG. Лучше квадратный, от 48×48, с прозрачным фоном", "PNG, ICO or JPG. Square, 48×48 or larger, with a transparent background works best"),
        opt: o1(st!("Выбрать…", "Choose…")),
        glyph: 0xE8E5,
        ..SI
    },
    SItem {
        page: PG_TASKBAR,
        kind: SK_TOGGLE,
        id: SID_TBSEARCH,
        group: st!("Поиск", "Search"),
        title: st!("Открывать поиск лаунчера по бейджу", "Taskbar search opens the launcher"),
        desc: st!(
            "Клик по поиску на панели задач Windows 10 и 11 - значку, значку с надписью или полю - открывает лаунчер вместо поиска Windows",
            "Clicking the search on the Windows 10 and 11 taskbar - an icon, an icon with a label or a box - opens the launcher instead of Windows search"
        ),
        ..SI
    },
    SItem {
        page: PG_TASKBAR,
        kind: SK_TOGGLE,
        id: SID_MEDIA,
        group: st!("Сейчас играет", "Now playing"),
        title: st!("Медиа на панели задач", "Media on the taskbar"),
        desc: st!(
            "Обложка, название и исполнитель из любого плеера: Spotify, браузера, Медиаплеера. Клик открывает плеер",
            "Cover, title and artist from any player: Spotify, a browser, Media Player. A click opens the player"
        ),
        ..SI
    },
    SItem {
        page: PG_TASKBAR,
        kind: SK_CHOICE,
        id: SID_MEDIAPOS,
        dep: SID_MEDIA,
        dep_val: 1,
        title: st!("Положение", "Position"),
        desc: st!(
            "Справа - у значков в трее. Если значки панели выровнены по левому краю, всегда справа",
            "Right sits next to the tray icons. Always right when taskbar icons are aligned left"
        ),
        opt: o2(st!("Слева", "Left"), st!("Справа", "Right")),
        ..SI
    },
    SItem {
        page: PG_TASKBAR,
        kind: SK_TOGGLE,
        id: SID_MEDIACTL,
        dep: SID_MEDIA,
        dep_val: 1,
        title: st!("Кнопки управления", "Playback buttons"),
        desc: st!("Предыдущий трек, пауза и следующий", "Previous track, pause and next"),
        ..SI
    },
    SItem {
        page: PG_TASKBAR,
        kind: SK_TOGGLE,
        id: SID_MEDIASCROLL,
        dep: SID_MEDIA,
        dep_val: 1,
        title: st!("Прокручивать длинные названия", "Scroll long titles"),
        desc: st!("Название, которое не помещается, медленно едет до конца и обратно", "A title that does not fit slowly rides to its end and back"),
        ..SI
    },
    SItem {
        page: PG_TASKBAR,
        kind: SK_TOGGLE,
        id: SID_MEDIAHIDE,
        dep: SID_MEDIA,
        dep_val: 1,
        title: st!("Скрывать на паузе", "Hide when paused"),
        desc: st!("Показывать виджет, только пока что-то играет", "Show the widget only while something plays"),
        ..SI
    },
    SItem {
        page: PG_APPEARANCE,
        kind: SK_CHOICE,
        id: SID_THEME,
        group: st!("Окно", "Window"),
        title: st!("Тема", "Theme"),
        desc: st!("«Системная» следует настройке Windows", "System follows the Windows setting"),
        opt: o3(st!("Системная", "System"), st!("Тёмная", "Dark"), st!("Светлая", "Light")),
        ..SI
    },
    SItem {
        page: PG_APPEARANCE,
        kind: SK_CHOICE,
        id: SID_BACKDROP,
        title: st!("Фон", "Background"),
        desc: st!(
            "Стекло - полупрозрачное размытие, акрил - как у системных меню, Mica - оттенок обоев, как у окон Windows 11",
            "Glass is a translucent blur, acrylic looks like system menus, Mica tints with the wallpaper like Windows 11 windows"
        ),
        opt: [st!("Стекло", "Glass"), st!("Акрил", "Acrylic"), st!("Mica", "Mica"), st!("Сплошной", "Solid"), Str::NONE, Str::NONE, Str::NONE],
        warn: (1 << BACKDROP_BLUR) | (1 << BACKDROP_ACRYLIC),
        ..SI
    },
    SItem {
        page: PG_APPEARANCE,
        kind: SK_BUTTON,
        id: SID_STYLEGO,
        group: st!("Лаунчер", "Launcher"),
        title: st!("Дизайн лаунчера", "Launcher design"),
        desc: st!("Стандарт, Капсула, Windows 11 и другие стили с превью", "Standard, Capsule, Windows 11 and other styles with previews"),
        opt: o1(st!("Выбрать", "Choose")),
        glyph: 0xE80A,
        ..SI
    },
    SItem {
        page: PG_APPEARANCE,
        kind: SK_CHOICE,
        id: SID_STARTPOS,
        dep: SID_STYLE,
        dep_mask: STYLES_START_MENU as u16,
        title: st!("Положение", "Position"),
        desc: st!("Как меню Пуск: окно выезжает из-под панели задач", "Like Start: the window slides up from under the taskbar"),
        opt: o2(st!("Слева снизу", "Bottom left"), st!("Снизу по центру", "Bottom center")),
        ..SI
    },
    SItem {
        page: PG_APPEARANCE,
        kind: SK_CHOICE,
        id: SID_POSITION,
        dep: SID_STYLE,
        dep_mask: STYLES_START_MENU as u16,
        dep_not: true,
        title: st!("Положение", "Position"),
        desc: st!("Снизу - как меню Пуск: окно выезжает из-под панели задач", "Bottom works like Start: the window slides up from under the taskbar"),
        opt: o3(st!("По центру", "Center"), st!("Слева снизу", "Bottom left"), st!("Снизу по центру", "Bottom center")),
        ..SI
    },
    SItem {
        page: PG_APPEARANCE,
        kind: SK_TOGGLE,
        id: SID_MATCHHL,
        title: st!("Подсветка совпадений", "Highlight matches"),
        desc: st!("Буквы, по которым нашлись приложение или файл, выделяются жирным", "The letters that matched the query are drawn in bold"),
        ..SI
    },
    SItem {
        page: PG_APPEARANCE,
        kind: SK_SLIDER,
        id: SID_SCALE,
        group: st!("Размер", "Size"),
        title: st!("Масштаб", "Scale"),
        desc: st!("Размер всего лаунчера: текста, значков и окна", "The size of the whole launcher: text, icons and the window"),
        step: 5,
        unit: st!("%", "%"),
        ..SI
    },
    SItem {
        page: PG_APPEARANCE,
        kind: SK_STEPPER,
        id: SID_WIDTH,
        dep: SID_STYLE,
        dep_mask: STYLES_START_MENU as u16,
        dep_not: true,
        title: st!("Ширина окна", "Window width"),
        step: 20,
        unit: st!("px", "px"),
        ..SI
    },
    SItem {
        page: PG_APPEARANCE,
        kind: SK_STEPPER,
        id: SID_ROWS,
        dep: SID_STYLE,
        dep_mask: STYLES_START_MENU as u16,
        dep_not: true,
        title: st!("Строк в списке", "Visible rows"),
        step: 1,
        ..SI
    },
    SItem {
        page: PG_APPEARANCE,
        kind: SK_BUTTON,
        id: SID_APREVIEW,
        title: st!("Проверить", "Try it"),
        desc: st!("Открыть лаунчер с текущими настройками. Esc вернёт сюда", "Open the launcher with the current settings. Esc comes back here"),
        opt: o1(st!("Открыть лаунчер", "Open launcher")),
        glyph: 0xE768,
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_BUTTON,
        id: SID_RMGOTO,
        dep: SID_REDUCED,
        dep_val: 1,
        title: st!("Включено уменьшение движения", "Reduced motion is on"),
        desc: st!(
            "Анимации выключены, их настройки сохранены. Выключить можно в «Общих»",
            "Animations are off and their settings are kept. It can be turned off in General"
        ),
        opt: o1(st!("Перейти", "Go there")),
        glyph: 0xE72A,
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_TOGGLE,
        id: SID_ANIM,
        group: st!("Общее", "General"),
        title: st!("Анимации", "Animations"),
        desc: st!(
            "Все анимации лаунчера и настроек. Выключаются и системной настройкой «Эффекты анимации»",
            "Every animation of the launcher and settings. Also off when Windows animation effects are off"
        ),
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_STEPPER,
        id: SID_ASPEED,
        dep: SID_ANIM,
        dep_val: 1,
        title: st!("Общая скорость", "Overall speed"),
        desc: st!(
            "Множитель для всех длительностей ниже: 200 % - всё вдвое быстрее, 50 % - вдвое медленнее",
            "Multiplies every duration below: 200% makes everything twice as fast, 50% twice as slow"
        ),
        step: 10,
        unit: st!("%", "%"),
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_BUTTON,
        id: SID_APREVIEW,
        dep: SID_ANIM,
        dep_val: 1,
        title: st!("Проверить", "Try it"),
        desc: st!("Открыть лаунчер с текущими настройками. Esc вернёт сюда", "Open the launcher with the current settings. Esc comes back here"),
        opt: o1(st!("Открыть лаунчер", "Open launcher")),
        glyph: 0xE768,
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_STEPPER,
        id: SID_AOPEN,
        dep: SID_ANIM,
        dep_val: 1,
        group: st!("Открытие и закрытие", "Open and close"),
        title: st!("Появление окна", "Window appears"),
        desc: st!("Проявление стекла и содержимого. 0 - сразу", "Fade-in of the glass and content. 0 is instant"),
        step: 10,
        unit: st!("мс", "ms"),
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_STEPPER,
        id: SID_ACLOSE,
        dep: SID_ANIM,
        dep_val: 1,
        title: st!("Закрытие", "Window closes"),
        desc: st!("Короче появления, чтобы Esc не ощущался задержкой", "Shorter than opening so Esc never feels delayed"),
        step: 10,
        unit: st!("мс", "ms"),
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_STEPPER,
        id: SID_ASCALE,
        dep: SID_ANIM,
        dep_val: 1,
        title: st!("Начальный масштаб", "Starting scale"),
        desc: st!("С какого размера содержимое «наплывает» при открытии. 100 % - без увеличения", "The size the content grows from when opening. 100% means no zoom"),
        step: 1,
        unit: st!("%", "%"),
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_TOGGLE,
        id: SID_ACASCADE,
        dep: SID_ANIM,
        dep_val: 1,
        title: st!("Каскад строк", "Row cascade"),
        desc: st!("После открытия строки появляются по очереди, сверху вниз", "After opening the rows appear one after another, top to bottom"),
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_STEPPER,
        id: SID_AROW,
        dep: SID_ACASCADE,
        dep_val: 1,
        title: st!("Появление строки", "Row fade-in"),
        step: 10,
        unit: st!("мс", "ms"),
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_STEPPER,
        id: SID_ASTAGGER,
        dep: SID_ACASCADE,
        dep_val: 1,
        title: st!("Задержка между строками", "Delay between rows"),
        step: 2,
        unit: st!("мс", "ms"),
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_STEPPER,
        id: SID_ASELECT,
        dep: SID_ANIM,
        dep_val: 1,
        group: st!("Движение", "Motion"),
        title: st!("Выделение", "Selection"),
        desc: st!(
            "За сколько подложка доезжает до выбранной строки (пружина: при быстром листании скорость не рвётся)",
            "How fast the highlight reaches the selected row (a spring: fast key repeat never jerks it)"
        ),
        step: 10,
        unit: st!("мс", "ms"),
        ..SI
    },
    SItem { page: PG_ANIM, kind: SK_STEPPER, id: SID_ASCROLL, dep: SID_ANIM, dep_val: 1, title: st!("Прокрутка", "Scrolling"), step: 10, unit: st!("мс", "ms"), ..SI },
    SItem {
        page: PG_ANIM,
        kind: SK_STEPPER,
        id: SID_AMENU,
        dep: SID_ANIM,
        dep_val: 1,
        title: st!("Меню", "Menus"),
        desc: st!("Контекстное меню лаунчера и выпадающие списки настроек", "The launcher context menu and the dropdowns in settings"),
        step: 10,
        unit: st!("мс", "ms"),
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_STEPPER,
        id: SID_APAGE,
        dep: SID_ANIM,
        dep_val: 1,
        title: st!("Смена раздела настроек", "Settings page switch"),
        step: 10,
        unit: st!("мс", "ms"),
        ..SI
    },
    SItem {
        page: PG_ANIM,
        kind: SK_BUTTON,
        id: SID_ARESET,
        dep: SID_ANIM,
        dep_val: 1,
        group: st!("Сброс", "Reset"),
        title: st!("Значения по умолчанию", "Default values"),
        desc: st!("Вернуть все тайминги этой страницы", "Restore every timing on this page"),
        opt: o1(st!("Сбросить", "Reset")),
        glyph: 0xE777,
        ..SI
    },
    SItem {
        page: PG_SEARCH,
        kind: SK_STEPPER,
        id: SID_KEEPQUERY,
        group: st!("Запрос", "Query"),
        title: st!("Помнить запрос", "Keep the query"),
        desc: st!(
            "Сколько секунд после закрытия хранить набранный текст. 0 - всегда начинать с пустого поля",
            "Seconds to keep the typed text after closing. 0 always starts empty"
        ),
        lo: 0,
        hi: 3600,
        step: 10,
        unit: st!("с", "s"),
        ..SI
    },
    SItem {
        page: PG_SEARCH,
        kind: SK_STEPPER,
        id: SID_MAXAPPS,
        group: st!("Приложения", "Applications"),
        title: st!("Приложений в результатах", "Apps in results"),
        step: 1,
        ..SI
    },
    SItem {
        page: PG_SEARCH,
        kind: SK_TOGGLE,
        id: SID_HIDENOISE,
        title: st!("Скрывать деинсталляторы и справку", "Hide uninstallers and help links"),
        desc: st!("Ярлыки «Удалить…», «Readme» и подобные из меню «Пуск»", "“Uninstall…”, “Readme” and similar Start menu shortcuts"),
        ..SI
    },
    SItem {
        page: PG_SEARCH,
        kind: SK_BUTTON,
        id: SID_REINDEX,
        title: st!("Список приложений", "App list"),
        desc: st!("Перечитать меню «Пуск» прямо сейчас. Обычно это происходит само", "Re-read the Start menu now. Normally this happens by itself"),
        opt: o1(st!("Обновить", "Refresh")),
        glyph: 0xE72C,
        ..SI
    },
    SItem {
        page: PG_SEARCH,
        kind: SK_INFO,
        id: SID_EVERYTHING,
        group: st!("Файлы (Everything)", "Files (Everything)"),
        title: st!("Everything", "Everything"),
        desc: st!(
            "Поиск файлов идёт через ваш Everything, а если его нет - через встроенный",
            "File search goes through your own Everything, or the built-in one when you have none"
        ),
        ..SI
    },
    SItem {
        page: PG_SEARCH,
        kind: SK_STEPPER,
        id: SID_MINCHARS,
        title: st!("Искать файлы с", "Search files from"),
        desc: st!("Минимальная длина запроса", "Minimum query length"),
        step: 1,
        unit: st!("симв.", "chars"),
        ..SI
    },
    SItem {
        page: PG_SEARCH,
        kind: SK_TEXT,
        id: SID_FILTER,
        title: st!("Фильтр Everything", "Everything filter"),
        desc: st!(
            "Добавляется к каждому запросу. Например, !C:\\Windows\\ исключает папку Windows",
            "Appended to every query. For example, !C:\\Windows\\ excludes the Windows folder"
        ),
        ..SI
    },
    SItem {
        page: PG_DESIGN,
        kind: SK_GALLERY,
        id: SID_STYLE,
        opt: [
            st!("Стандарт", "Standard"),
            st!("Raycast", "Raycast"),
            st!("Fluent", "Fluent"),
            st!("Компактный", "Compact"),
            st!("Капсула", "Capsule"),
            st!("Windows 11", "Windows 11"),
            st!("Windows 10", "Windows 10"),
        ],
        ..SI
    },
    SItem {
        page: PG_ADVANCED,
        kind: SK_BUTTON,
        id: SID_CLEARHIST,
        group: st!("Данные", "Data"),
        title: st!("История запусков", "Launch history"),
        desc: st!("Частота запусков и запомненные запросы, по которым строится порядок результатов", "Launch counts and remembered queries that order the results"),
        opt: o1(st!("Очистить", "Clear")),
        glyph: 0xE74D,
        ..SI
    },
    SItem {
        page: PG_ADVANCED,
        kind: SK_BUTTON,
        id: SID_CONFIG,
        title: st!("Файл настроек", "Settings file"),
        desc: st!("config.ini - все параметры с комментариями", "config.ini with every option and comments"),
        opt: o1(st!("Открыть", "Open")),
        glyph: 0xE8E5,
        ..SI
    },
    SItem {
        page: PG_ADVANCED,
        kind: SK_BUTTON,
        id: SID_DATADIR,
        title: st!("Папка данных", "Data folder"),
        desc: st!("Настройки, история, кэш списка приложений и журнал", "Settings, history, app list cache and log"),
        opt: o1(st!("Открыть", "Open")),
        glyph: 0xE838,
        ..SI
    },
    SItem {
        page: PG_ADVANCED,
        kind: SK_INFO,
        id: SID_ADMIN,
        group: st!("Права", "Permissions"),
        title: st!("Права администратора", "Administrator rights"),
        desc: st!(
            "Нужны, чтобы клавиша вызова работала поверх игр и окон администратора. Программы из лаунчера запускаются с обычными правами",
            "Needed for the hotkey to work over games and admin windows. Programs started from the launcher get normal rights"
        ),
        ..SI
    },
    SItem {
        page: PG_ABOUT,
        kind: SK_ABOUT,
        id: SID_ABOUT,
        desc: st!(
            "Быстрый лаунчер для Windows. Приложения, файлы и команды по одной клавише, музыка на панели задач. Всё работает на вашем компьютере: без ИИ и без телеметрии.",
            "A fast launcher for Windows. Apps, files and commands on one key, music on the taskbar. It all runs on your computer: no AI, no telemetry."
        ),
        ..SI
    },
    SItem {
        page: PG_ABOUT,
        kind: SK_TOGGLE,
        id: SID_UPDCHECK,
        group: st!("Обновления", "Updates"),
        title: st!("Проверять обновления", "Check for updates"),
        desc: st!(
            "Раз в несколько часов спрашивает GitHub о новой версии. Если она есть, в лаунчере появится кнопка «Обновить»",
            "Asks GitHub for a new version every few hours. When there is one, the launcher shows an Update button"
        ),
        ..SI
    },
    SItem {
        page: PG_ABOUT,
        kind: SK_BUTTON,
        id: SID_UPDNOW,
        title: st!("Версия 2.2.0", "Version 2.2.0"),
        desc: st!("Проверить сейчас или установить найденное обновление", "Check now or install the update that was found"),
        opt: o1(st!("Проверить", "Check now")),
        glyph: 0xE896,
        ..SI
    },
];

// The config.ini field behind an item, when it is a plain setting (its value is the field's).
const fn sid_cfg() -> [u8; SID__COUNT] {
    let mut a = [0u8; SID__COUNT];
    a[SID_LANG as usize] = CF_LANGUAGE as u8;
    a[SID_REDUCED as usize] = CF_REDUCED_MOTION as u8;
    a[SID_THEME as usize] = CF_THEME as u8;
    a[SID_BACKDROP as usize] = CF_BACKDROP as u8;
    a[SID_STYLE as usize] = CF_STYLE as u8;
    a[SID_POSITION as usize] = CF_POSITION as u8;
    a[SID_MATCHHL as usize] = CF_MATCH_HIGHLIGHT as u8;
    a[SID_WIDTH as usize] = CF_WIDTH as u8;
    a[SID_ROWS as usize] = CF_ROWS as u8;
    a[SID_SCALE as usize] = CF_SCALE as u8;
    a[SID_TASKBAR as usize] = CF_TASKBAR_BUTTON as u8;
    a[SID_TBREPLACE as usize] = CF_TASKBAR_REPLACE_ICON as u8;
    a[SID_TBICON as usize] = CF_TASKBAR_ICON as u8;
    a[SID_TBSEARCH as usize] = CF_TASKBAR_SEARCH as u8;
    a[SID_MEDIA as usize] = CF_MEDIA_WIDGET as u8;
    a[SID_MEDIAPOS as usize] = CF_MEDIA_POSITION as u8;
    a[SID_MEDIACTL as usize] = CF_MEDIA_CONTROLS as u8;
    a[SID_MEDIAHIDE as usize] = CF_MEDIA_HIDE_PAUSED as u8;
    a[SID_MEDIASCROLL as usize] = CF_MEDIA_MARQUEE as u8;
    a[SID_ANIM as usize] = CF_ANIMATIONS as u8;
    a[SID_ASPEED as usize] = CF_ANIM_SPEED as u8;
    a[SID_AOPEN as usize] = CF_ANIM_OPEN_MS as u8;
    a[SID_ACLOSE as usize] = CF_ANIM_CLOSE_MS as u8;
    a[SID_ASCALE as usize] = CF_ANIM_OPEN_SCALE as u8;
    a[SID_ACASCADE as usize] = CF_ANIM_CASCADE as u8;
    a[SID_AROW as usize] = CF_ANIM_ROW_MS as u8;
    a[SID_ASTAGGER as usize] = CF_ANIM_STAGGER_MS as u8;
    a[SID_ASELECT as usize] = CF_ANIM_SELECT_MS as u8;
    a[SID_ASCROLL as usize] = CF_ANIM_SCROLL_MS as u8;
    a[SID_AMENU as usize] = CF_ANIM_MENU_MS as u8;
    a[SID_APAGE as usize] = CF_ANIM_PAGE_MS as u8;
    a[SID_KEEPQUERY as usize] = CF_KEEP_QUERY as u8;
    a[SID_MAXAPPS as usize] = CF_MAX_APPS as u8;
    a[SID_HIDENOISE as usize] = CF_HIDE_UNINSTALLERS as u8;
    a[SID_MINCHARS as usize] = CF_FILE_MIN_CHARS as u8;
    a[SID_UPDCHECK as usize] = CF_UPDATE_CHECK as u8;
    a
}
pub static k_sid_cfg: [u8; SID__COUNT] = sid_cfg();

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SRow {
    pub item: i32,
    pub first: bool,
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    pub ty: f32,
    pub cx: f32,
    pub cy: f32,
    pub cw: f32,
    pub ch: f32,
    pub vis: f32,
    pub ndesc: i32,
    pub desc_at: [i32; 4],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SBox {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

pub const HT_NONE: u8 = 0;
pub const HT_NAV: u8 = 1;
pub const HT_ROW: u8 = 2;
pub const HT_WINMIN: u8 = 3;
pub const HT_WINCLOSE: u8 = 4;
pub const HT_POPITEM: u8 = 5;
pub const HT_POPOUT: u8 = 6;
pub const HT_RECCANCEL: u8 = 7;
pub const HT_RECSAVE: u8 = 8;
pub const HT_RECPANEL: u8 = 9;
pub const HT_RECOUT: u8 = 10;
pub const HT_SBUPDATE: u8 = 11;
pub const HT_DONATE: u8 = 12;
pub const HT_DON: u8 = 13;
pub const DON_OUT: i32 = -1;
pub const DON_PANEL: i32 = -2;
pub const DON_CLOSE: i32 = -3;
pub const DON_LINK: i32 = -4;
pub const PART_ROW: u8 = 0;
pub const PART_CONTROL: u8 = 1;
pub const PART_MINUS: u8 = 2;
pub const PART_PLUS: u8 = 3;
pub const PART_CARD0: u8 = 4; // PART_CARD0 + k: the Design page's card k

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SHit {
    pub type_: u8,
    pub part: u8,
    pub idx: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KeyTok {
    pub label: [WCHAR; 24],
    pub side: *const WCHAR,
}

pub const SETTINGS_TIMER_CARET: usize = 1;
pub const SETTINGS_TIMER_FLASH: usize = 2;
pub const SETTINGS_TIMER_APPLY: usize = 3;
pub const SETTINGS_TIMER_TIP: usize = 4;

const RM_LCTRL: u8 = 1;
const RM_RCTRL: u8 = 2;
const RM_LALT: u8 = 4;
const RM_RALT: u8 = 8;
const RM_LSHIFT: u8 = 16;
const RM_RSHIFT: u8 = 32;
const RM_LWIN: u8 = 64;
const RM_RWIN: u8 = 128;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Rec {
    pub open: bool,
    pub held: u8,
    pub key: u8,
    pub used: bool,
    pub has: bool,
    pub hk: Hotkey,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Pend {
    pub on: bool,
    pub v: i32,
    pub due: f64,
}

#[repr(C)]
pub struct Don {
    pub open: bool,
    pub closing: bool,
    pub t: f32,
    pub scroll: f32,
    pub scroll_target: f32,
    pub scroll_v: f32,
    pub content_h: f32,
    pub view_h: f32,
    pub panel: [f32; 4],
    pub close: [f32; 4],
    pub link: [f32; 4],
    pub copy: [[f32; 4]; 8],
    pub hov: [f32; 10],
    pub cp: [f32; 8],
    pub sb_t: f32,
    pub sb_prev: f32,
    pub flash: i32,
    pub flash_until: f64,
    pub open_time: f64,
    pub sb_seen: f64,
}

#[repr(C)]
pub struct SettingsWnd {
    pub hwnd: HWND,
    pub target: RTarget,
    pub dirty: bool,
    pub mica: bool,
    pub active: bool,
    pub dpi: u32,
    pub s: f32,
    pub W: i32,
    pub H: i32,
    pub page: i32,
    pub scroll: f32,
    pub content_h: f32,
    pub rows: [SRow; SROWS_MAX],
    pub nrows: i32,
    pub cards: [SBox; 12],
    pub ncards: i32,
    pub heads: [SBox; 12],
    pub head_text: [Str; 12],
    pub head_a: [f32; 12],
    pub nheads: i32,
    pub focus: i32,
    pub hover: SHit,
    pub press: SHit,
    pub editing: i32,
    pub edit: [WCHAR; 512],
    pub edit_len: i32,
    pub edit_caret: i32,
    pub caret_on: bool,
    pub armed: bool,
    pub flash_id: i32,
    pub flash_until: f64,
    pub autostart: bool,
    pub everything: i32,
    pub pop_open: bool,
    pub pop_row: i32,
    pub pop_hover: i32,
    pub pop_x: f32,
    pub pop_y: f32,
    pub pop_w: f32,
    pub pop_h: f32,
    pub rec: Rec,
    pub rec_panel: [f32; 4],
    pub rec_btn: [[f32; 4]; 2],
    pub animating: bool,
    pub last_frame: f64,
    pub scroll_target: f32,
    pub scroll_v: f32,
    pub nav_y: f32,
    pub nav_v: f32,
    pub nav_hov: [f32; PG__COUNT as usize],
    pub win_hov: [f32; 2],
    pub tog: [f32; SITEMS],
    pub vis: [f32; SITEMS],
    // A change shows at once and applies a moment later; changing it back before then cancels it.
    pub pend: [Pend; SID__COUNT],
    pub page_t: f32,
    pub pop_t: f32,
    pub rec_t: f32,
    pub sb_t: f32,
    pub sb_prev: f32,
    pub row_hov: f32,
    pub row_hov_idx: i32,
    pub seg: [f32; SITEMS],
    pub seg_click: i32,
    pub card_hov: [f32; STYLE__COUNT as usize],
    pub card_sel: [f32; STYLE__COUNT as usize], // the Design page's cards: hovered, chosen (0..1)
    pub don: Don,
    pub sb_btn: [[f32; 4]; 2],
    pub sb_hov: [f32; 2],
    pub sb_seen: f64,
    pub mx: i32,
    pub my: i32,
    // Tooltip (a locked row, a marked option): armed when the mouse comes to it (tip_at's key), shown at
    // (tip_x, tip_y) after a pause.
    pub tip_row: i32,
    pub tip_x: i32,
    pub tip_y: i32,
    pub tip_on: bool,
}

pub static mut SW: SettingsWnd = unsafe { zeroed() };

#[inline]
pub unsafe fn sw() -> &'static mut SettingsWnd {
    &mut *(&raw mut SW)
}

// SW's non-zero starting values (called once at the start).
pub unsafe fn settings_static_init() {
    SW.focus = -1;
    SW.editing = -1;
    SW.pop_row = -1;
    SW.flash_id = -1;
    SW.page_t = 1.0;
    SW.pop_t = 1.0;
    SW.rec_t = 1.0;
    SW.seg_click = -1;
    SW.tip_row = -1;
    PM.hover = -1;
}

// The Design page's previews: the launcher itself, drawn in each style at the cards' size over the wallpaper of
// this monitor. R.pv holds them in a 3 x 3 grid of preview-sized cells: the styles, then the wallpaper.
#[repr(C)]
pub struct Previews {
    pub pw: i32,
    pub ph: i32,         // the cell: a card's preview, px
    pub tb: i32,         // the schematic taskbar's height
    pub tb_center: bool, // Windows 11 icons in the middle (TaskbarAl)
    pub ok: bool,
    pub dirty: bool,
    pub x: [f32; STYLE__COUNT as usize],
    pub y: [f32; STYLE__COUNT as usize], // where the launcher sits in its card
    pub w: [i32; STYLE__COUNT as usize],
    pub h: [i32; STYLE__COUNT as usize],
    pub rad: [f32; STYLE__COUNT as usize], // the corners DWM gives its window (0: the style draws its own)
}

pub static mut PV: Previews = unsafe { zeroed() };

const PV_WALL: i32 = STYLE__COUNT;
pub const SETTINGS_TIMER_PV: usize = 5;

pub unsafe fn SSC(v: f32) -> f32 {
    v * SW.s
}
pub unsafe fn SSR(v: f32) -> f32 {
    (v * SW.s + 0.5).floor()
}

pub unsafe fn settings_invalidate() {
    SW.dirty = true;
}
pub unsafe fn settings_needs_render() -> bool {
    !SW.hwnd.is_null() && (SW.dirty || SW.animating) && SW.target.ok
}

pub unsafe fn sb_x() -> f32 {
    SSR(10.0)
}
pub unsafe fn sb_w() -> f32 {
    SSR(228.0)
}
pub unsafe fn nav_y0() -> f32 {
    sb_x() + SSR(78.0)
}
pub unsafe fn nav_pitch() -> f32 {
    SSR(38.0)
}
pub unsafe fn top_h() -> f32 {
    SSR(60.0)
}
pub unsafe fn content_x0() -> f32 {
    sb_x() + sb_w() + SSR(26.0)
}
pub unsafe fn content_x1() -> f32 {
    SW.W as f32 - SSR(26.0)
}

pub unsafe fn winbtn_rect(i: i32, r: &mut [f32; 4]) {
    let w = SSR(46.0);
    let h = SSR(34.0);
    r[0] = SW.W as f32 - w * (2 - i) as f32;
    r[1] = 0.0;
    r[2] = r[0] + w;
    r[3] = h;
}

pub unsafe fn nav_rect(i: i32, r: &mut [f32; 4]) {
    r[0] = sb_x() + SSR(8.0);
    r[1] = nav_y0() + nav_pitch() * i as f32;
    r[2] = sb_x() + sb_w() - SSR(8.0);
    r[3] = r[1] + SSR(36.0);
}

unsafe fn sval_applied(id: u8) -> i32 {
    match id {
        SID_AUTOSTART => return SW.autostart as i32,
        SID_STARTPOS => return if launcher_position() == POS_BOTTOM_LEFT { 0 } else { 1 },
        _ => {}
    }
    let f = k_sid_cfg[id as usize];
    if f != 0 { cfg_get(f as i32) } else { 0 }
}

// The value the settings show: a pending change, or the applied one.
pub unsafe fn sval(id: u8) -> i32 {
    if SW.pend[id as usize].on { SW.pend[id as usize].v } else { sval_applied(id) }
}

pub fn sitem(id: u8) -> Option<&'static SItem> {
    k_sitems.iter().find(|i| i.id == id)
}

pub fn item_range(it: &SItem) -> (i32, i32) {
    let f = k_sid_cfg[it.id as usize];
    let own = it.lo != 0 || it.hi != 0 || f == 0;
    if own { (it.lo, it.hi) } else { (k_cfg[f as usize].lo, k_cfg[f as usize].hi) }
}

// Under reduced motion the animation settings keep their values but cannot be changed.
pub unsafe fn item_locked(it: &SItem) -> bool {
    sval(SID_REDUCED) != 0 && ((it.page == PG_ANIM && it.id != SID_APREVIEW && it.id != SID_RMGOTO) || it.id == SID_MEDIASCROLL)
}

pub unsafe fn item_shown(i: usize) -> bool {
    let it = &k_sitems[i];
    if it.dep == 0 {
        return true;
    }
    let p = k_sitems.iter().position(|x| x.id == it.dep);
    let v = sval(it.dep);
    let mat = if it.dep_mask != 0 { ((it.dep_mask as i32 >> v) & 1) != 0 } else { v == it.dep_val as i32 };
    mat != it.dep_not && p.map(|p| item_shown(p)).unwrap_or(true)
}

pub fn choice_count(it: &SItem) -> i32 {
    let mut n = 0;
    while n < 7 && !it.opt[n].ru.is_null() {
        n += 1;
    }
    max(n as i32, 1)
}

// Short option lists (up to four) are shown as a segmented control instead of a dropdown.
pub fn is_seg(it: &SItem) -> bool {
    it.kind == SK_CHOICE && choice_count(it) <= 4
}

// Why an option is marked with a caution sign.
unsafe fn warn_tip() -> *const WCHAR {
    tr!(
        "Нагружает видеокарту и процессор больше сплошного: фон всё время следит за тем, что под окном",
        "Uses more GPU and CPU than solid: the background keeps following what is behind the window"
    )
}

const WARN_GLYPH_W: f32 = 20.0; // the caution sign before a label, with its gap

pub unsafe fn seg_layout(it: &SItem, x: f32, xs: &mut [f32; 4], ws: &mut [f32; 4]) -> f32 {
    let fs = SSC(13.5);
    let x0 = x;
    let mut x = x;
    for k in 0..choice_count(it) as usize {
        xs[k] = x;
        ws[k] = (text_width(FONT_TEXT, fs, ss(it.opt[k]), -1) + SSR(28.0) + if it.warn & (1 << k) != 0 { SSR(WARN_GLYPH_W) } else { 0.0 }).floor();
        x += ws[k];
    }
    x - x0
}

pub unsafe fn seg_index_at(it: &SItem, cx: f32, mx: f32) -> i32 {
    let mut xs = [0.0; 4];
    let mut ws = [0.0; 4];
    seg_layout(it, cx + SSR(2.0), &mut xs, &mut ws);
    let mut k = choice_count(it) - 1;
    while k >= 0 {
        if mx >= xs[k as usize] {
            return k;
        }
        k -= 1;
    }
    0
}

// The custom Start button icon, from a file dialog. True when one was chosen.
unsafe fn pick_taskbar_icon() -> bool {
    let d: IFileOpenDialog = match windows::Win32::System::Com::CoCreateInstance(&FileOpenDialog, None, windows::Win32::System::Com::CLSCTX_INPROC_SERVER) {
        Ok(d) => d,
        Err(_) => return false,
    };
    let types = [COMDLG_FILTERSPEC {
        pszName: windows::core::PCWSTR(tr!("Изображения", "Images")),
        pszSpec: windows::core::PCWSTR(w!("*.png;*.ico;*.jpg;*.jpeg;*.bmp")),
    }];
    let _ = d.SetFileTypes(&types);
    let _ = d.SetTitle(windows::core::PCWSTR(tr!("Значок кнопки Пуск", "Start button icon")));
    let mut ok = false;
    if d.Show(Some(whwnd(SW.hwnd))).is_ok() {
        if let Ok(item) = d.GetResult() {
            if let Ok(path) = item.GetDisplayName(SIGDN_FILESYSPATH) {
                if !path.0.is_null() {
                    wcopy(g_cfg.taskbar_icon_path.as_mut_ptr(), MAX_PATH as i32, path.0);
                    cfg_save(CF_TASKBAR_ICON_PATH);
                    CoTaskMemFree(path.0 as *const c_void);
                    ok = true;
                }
            }
        }
    }
    ok
}

// Autostart (schtasks.exe, slow) runs on a thread; a value changed meanwhile is applied after it.
struct AutoStartState {
    busy: bool,
    want: i32,
}
static mut AS: AutoStartState = AutoStartState { busy: false, want: 0 };

unsafe extern "system" fn autostart_thread(param: *mut c_void) -> u32 {
    let v = param as isize as i32;
    autostart_set(v != 0);
    PostMessageW(g_hwnd, WM_APP_AUTOSTART, v as WPARAM, autostart_get() as LPARAM);
    0
}

unsafe fn autostart_request(v: i32) {
    AS.want = v;
    if AS.busy {
        return;
    }
    if thread_start(autostart_thread, v as isize as *mut c_void) {
        AS.busy = true;
    } else {
        autostart_set(v != 0);
        SW.autostart = autostart_get();
    }
}

pub unsafe fn settings_autostart_done(v: i32, on: bool) {
    AS.busy = false;
    if AS.want != v {
        autostart_request(AS.want);
        return;
    }
    SW.autostart = on; // a failed change flips the toggle back
    settings_invalidate();
}

// Applies a value: the setting and config.ini, then whatever has to follow it.
unsafe fn sapply(it: &SItem, v: i32) {
    if v == sval_applied(it.id) {
        return;
    }
    let f = k_sid_cfg[it.id as usize];
    if f != 0 {
        cfg_set(f as i32, v);
    }
    match it.id {
        SID_AUTOSTART => {
            SW.autostart = v != 0;
            autostart_request(v);
        }
        SID_STARTPOS => cfg_set(CF_POSITION, if v != 0 { POS_BOTTOM_CENTER } else { POS_BOTTOM_LEFT }),
        SID_LANG => {
            lang_apply();
            apps_relocalize(U.apps);
            SetWindowTextW(SW.hwnd, tr!("Настройки MixLauncher", "MixLauncher Settings"));
            if U.visible {
                rebuild_rows(true);
                ui_invalidate();
            }
        }
        SID_THEME | SID_BACKDROP => {
            theme_update();
            U.backdrop_ok = backdrop_apply(g_hwnd);
            bd_refresh();
            settings_apply_theme();
        }
        SID_STYLE => {
            U.dpi = 0; // the launcher's metrics are made again when it opens
            U.backdrop_ok = backdrop_apply(g_hwnd);
        }
        SID_WIDTH | SID_ROWS | SID_SCALE => U.dpi = 0,
        SID_UPDCHECK => upd_schedule(2000),
        SID_MATCHHL => {
            if U.visible {
                ui_invalidate();
            }
        }
        SID_TBICON | SID_TBREPLACE => tb_icon_changed(),
        SID_TASKBAR => tb_set_enabled(v != 0),
        SID_TBSEARCH => tb_set_search(v != 0),
        SID_MEDIA => media_set_enabled(v != 0),
        SID_REDUCED => {
            if !SW.hwnd.is_null() {
                dwm_set(SW.hwnd, DWA_NO_TRANSITIONS, v as u32);
            }
            media_settings_changed();
        }
        SID_MEDIAPOS | SID_MEDIACTL | SID_MEDIAHIDE | SID_MEDIASCROLL => media_settings_changed(),
        SID_HIDENOISE => apps_request_reindex(),
        _ => {}
    }
    PV.dirty = true; // the previews show the launcher as the settings make it
    settings_invalidate();
}

unsafe fn pend_schedule() {
    let mut due = 0.0;
    for id in 0..SID__COUNT {
        if SW.pend[id].on && (due == 0.0 || SW.pend[id].due < due) {
            due = SW.pend[id].due;
        }
    }
    if SW.hwnd.is_null() {
        return;
    }
    if due == 0.0 {
        KillTimer(SW.hwnd, SETTINGS_TIMER_APPLY);
    } else {
        SetTimer(SW.hwnd, SETTINGS_TIMER_APPLY, max(10.0, (due - time_now()) * 1000.0) as u32, None);
    }
}

// Applies the pending changes that are due (or all of them).
pub unsafe fn pend_flush(all: bool) {
    let now = time_now();
    for id in 0..SID__COUNT {
        if !SW.pend[id].on || (!all && SW.pend[id].due > now + 0.005) {
            continue;
        }
        SW.pend[id].on = false;
        if let Some(it) = sitem(id as u8) {
            sapply(it, SW.pend[id].v);
        }
    }
    pend_schedule();
}

// Shows a change at once and applies it after the animation; changing it back first cancels it.
pub unsafe fn sset(it: &SItem, v: i32) {
    let (lo, hi) = item_range(it);
    let mut v = v;
    if it.kind == SK_STEPPER || it.kind == SK_SLIDER {
        v = clamp(v, lo, hi);
    }
    if it.kind == SK_TOGGLE {
        v = (v != 0) as i32;
    }
    if it.kind == SK_CHOICE || it.kind == SK_GALLERY {
        v = clamp(v, 0, choice_count(it) - 1);
    }
    if v == sval(it.id) || item_locked(it) {
        return;
    }
    if it.id == SID_TBICON && v == TBI_CUSTOM && g_cfg.taskbar_icon_path[0] == 0 && !pick_taskbar_icon() {
        return;
    }
    let p = &mut sw().pend[it.id as usize];
    if g_cfg.reduced_motion || it.id == SID_REDUCED {
        // nothing to wait for without the animation
        p.on = false;
        sapply(it, v);
    } else if v == sval_applied(it.id) {
        p.on = false;
    } else {
        p.on = true;
        p.v = v;
        p.due = time_now() + if it.kind == SK_STEPPER || it.kind == SK_SLIDER { 0.45 } else { 0.25 };
    }
    pend_schedule();
    settings_invalidate();
}

// A button's label says "Done" for a moment.
unsafe fn button_flash(id: u8) {
    SW.flash_id = id as i32;
    SW.flash_until = time_now() + 1.6;
    SetTimer(SW.hwnd, SETTINGS_TIMER_FLASH, 1700, None);
}

pub unsafe fn button_action(it: &SItem) {
    match it.id {
        SID_REINDEX => {
            apps_request_reindex();
            button_flash(it.id);
        }
        SID_CLEARHIST => {
            if !SW.armed {
                // the first press asks
                SW.armed = true;
            } else {
                SW.armed = false;
                history_clear();
                refresh_frecency();
                refresh_empty_view();
                button_flash(it.id);
            }
        }
        SID_CONFIG => {
            let mut path = [0u16; MAX_PATH];
            config_path(path.as_mut_ptr());
            launch_open(path.as_ptr(), OPEN_PATH);
        }
        SID_DATADIR => launch_open(g_data_dir.as_ptr(), OPEN_PATH),
        SID_TBFILE => {
            if pick_taskbar_icon() {
                SW.pend[SID_TBICON as usize].on = false;
                cfg_set(CF_TASKBAR_ICON, TBI_CUSTOM);
                tb_icon_changed();
            }
        }
        SID_APREVIEW => ui_show(),
        SID_RMGOTO => settings_open_at(SID_REDUCED),
        SID_STYLEGO => settings_open_at(SID_STYLE),
        SID_UPDNOW => {
            if upd_state() == UPD_AVAILABLE || (upd_state() == UPD_ERROR && UPD.url[0] != 0) {
                upd_download();
            } else {
                upd_check();
            }
        }
        SID_ARESET => {
            for id in 0..SID__COUNT {
                let f = k_sid_cfg[id] as i32;
                if f < CF_ANIM_FIRST || f > CF_ANIM_LAST {
                    continue;
                }
                SW.pend[id].on = false;
                cfg_set(f, k_cfg[f as usize].def);
            }
            button_flash(it.id);
        }
        _ => {}
    }
    settings_invalidate();
}

pub unsafe fn refresh_status() {
    if !AS.busy {
        SW.autostart = autostart_get();
    }
    SW.everything = ev_state();
}

unsafe fn info_text(id: u8, good: &mut bool) -> *const WCHAR {
    if id == SID_ADMIN {
        *good = g_elevated;
        return if g_elevated { tr!("Есть", "Granted") } else { tr!("Нет", "No") };
    }
    *good = SW.everything != EVS_NONE;
    if SW.everything == EVS_OWN {
        return tr!("Встроенный, запущен", "Built-in, running");
    }
    if SW.everything == EVS_USER { tr!("Запущен", "Running") } else { tr!("Не запущен", "Not running") }
}

unsafe fn hk_tokens(h: Hotkey, out: &mut [KeyTok; 5]) -> i32 {
    let mut n = 0usize;
    let names = [w!("Ctrl"), w!("Alt"), w!("Shift"), w!("Win")];
    for b in 0..4 {
        if h.mods & (1 << b) != 0 {
            wcopy(out[n].label.as_mut_ptr(), 24, names[b]);
            out[n].side = null();
            n += 1;
        }
    }
    if h.vk != 0 {
        key_label(h.vk as u32, out[n].label.as_mut_ptr(), 24, &mut out[n].side);
        n += 1;
    }
    n as i32
}

fn rec_bit(vk: u32) -> u8 {
    match vk as u16 {
        VK_LCONTROL => RM_LCTRL,
        VK_RCONTROL => RM_RCTRL,
        VK_LMENU => RM_LALT,
        VK_RMENU => RM_RALT,
        VK_LSHIFT => RM_LSHIFT,
        VK_RSHIFT => RM_RSHIFT,
        VK_LWIN => RM_LWIN,
        VK_RWIN => RM_RWIN,
        _ => 0,
    }
}

fn rec_generic(held: u8) -> u8 {
    let mut m = 0;
    if held & (RM_LCTRL | RM_RCTRL) != 0 {
        m |= HK_CTRL;
    }
    if held & (RM_LALT | RM_RALT) != 0 {
        m |= HK_ALT;
    }
    if held & (RM_LSHIFT | RM_RSHIFT) != 0 {
        m |= HK_SHIFT;
    }
    if held & (RM_LWIN | RM_RWIN) != 0 {
        m |= HK_WIN;
    }
    m
}

static HKP_NONE: Str = st!("Нажмите клавишу или сочетание", "Press a key or a shortcut");
static HKP_TYPING: Str = st!("Эта клавиша нужна для набора текста: добавьте Ctrl, Alt или Win", "This key is needed for typing: add Ctrl, Alt or Win");
static HKP_RESERVED: Str = st!("Это сочетание Windows переназначить не даёт", "Windows does not allow reassigning this shortcut");

fn hotkey_problem(h: Hotkey) -> Option<&'static Str> {
    if h.vk == 0 {
        return Some(&HKP_NONE);
    }
    let vk = h.vk as u16;
    let typing_key = (b'A' as u16..=b'Z' as u16).contains(&vk)
        || (b'0' as u16..=b'9' as u16).contains(&vk)
        || vk == VK_SPACE
        || vk == VK_RETURN
        || vk == VK_TAB
        || vk == VK_ESCAPE
        || vk == VK_BACK
        || vk == VK_DELETE
        || vk == VK_INSERT
        || (vk >= VK_PRIOR && vk <= VK_DOWN)
        || (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE)
        || (vk >= VK_OEM_1 && vk <= VK_OEM_3)
        || (vk >= VK_OEM_4 && vk <= VK_OEM_8)
        || vk == VK_OEM_102;
    if typing_key && (h.mods & (HK_CTRL | HK_ALT | HK_WIN)) == 0 {
        return Some(&HKP_TYPING);
    }
    if (h.mods == HK_WIN && vk == b'L' as u16) || (vk == VK_DELETE && (h.mods & (HK_CTRL | HK_ALT)) == (HK_CTRL | HK_ALT)) {
        return Some(&HKP_RESERVED);
    }
    None
}

pub unsafe fn rec_open() {
    end_edit(true);
    SW.pop_open = false;
    SW.rec = zeroed();
    SW.rec.open = true;
    SW.rec_t = 0.0;
    SW.rec.has = true;
    SW.rec.hk = g_cfg.hk;
    hook_capture(SW.hwnd);
    settings_invalidate();
}

pub unsafe fn rec_close(save: bool) {
    if !SW.rec.open {
        return;
    }
    hook_capture(null_mut());
    if save && SW.rec.has && hotkey_problem(SW.rec.hk).is_none() {
        g_cfg.hk = SW.rec.hk;
        hotkey_format(g_cfg.hk, g_cfg.hotkey.as_mut_ptr(), 64);
        cfg_save(CF_HOTKEY);
        hook_set_hotkey(g_cfg.hk);
    }
    SW.rec.open = false;
    settings_invalidate();
}

pub unsafe fn rec_key(vk: u32, down: bool) {
    let bit = rec_bit(vk);
    let r = &mut sw().rec;
    if down {
        if if bit != 0 { (r.held & bit) != 0 } else { r.key as u32 == vk } {
            return;
        }
        let idle = r.held == 0 && r.key == 0;
        if idle && vk == VK_ESCAPE as u32 {
            rec_close(false);
            return;
        }
        if idle && vk == VK_RETURN as u32 && r.has && hotkey_problem(r.hk).is_none() {
            rec_close(true);
            return;
        }
        if idle {
            r.has = false;
            r.used = false;
        }
        if bit != 0 {
            r.held |= bit;
        } else {
            r.key = vk as u8;
            r.hk.mods = rec_generic(r.held);
            r.hk.vk = vk as u8;
            r.has = true;
            r.used = true;
        }
    } else if bit != 0 {
        if (r.held & bit) == 0 {
            return;
        }
        if !r.used {
            r.hk.mods = rec_generic(r.held & !bit) & !vk_mod_bit(vk);
            r.hk.vk = if vk == VK_LWIN as u32 || vk == VK_RWIN as u32 { VK_ANYWIN as u8 } else { vk as u8 };
            r.has = true;
            r.used = true;
        }
        r.held &= !bit;
    } else if r.key as u32 == vk {
        r.key = 0;
    }
    settings_invalidate();
}

pub unsafe fn sided_vk(vk: WPARAM, lp: LPARAM) -> u32 {
    let sc = ((lp >> 16) & 0xFF) as u32;
    let ext = ((lp >> 24) & 1) != 0;
    match vk as u16 {
        VK_SHIFT => MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX),
        VK_CONTROL => (if ext { VK_RCONTROL } else { VK_LCONTROL }) as u32,
        VK_MENU => (if ext { VK_RMENU } else { VK_LMENU }) as u32,
        _ => vk as u32,
    }
}

pub unsafe fn begin_edit(row_: i32) {
    SW.editing = row_;
    wcopy(SW.edit.as_mut_ptr(), 512, g_cfg.everything_filter.as_ptr());
    SW.edit_len = wlen(SW.edit.as_ptr());
    SW.edit_caret = SW.edit_len;
    SW.caret_on = true;
    SetTimer(SW.hwnd, SETTINGS_TIMER_CARET, GetCaretBlinkTime(), None);
}

pub unsafe fn end_edit(commit: bool) {
    if SW.editing < 0 {
        return;
    }
    if commit && wcscmp(SW.edit.as_ptr(), g_cfg.everything_filter.as_ptr()) != 0 {
        wcopy(g_cfg.everything_filter.as_mut_ptr(), 512, SW.edit.as_ptr());
        cfg_save(CF_EVERYTHING_FILTER);
    }
    SW.editing = -1;
    KillTimer(SW.hwnd, SETTINGS_TIMER_CARET);
    settings_invalidate();
}

pub unsafe fn edit_splice(a: i32, b: i32, s: *const WCHAR, n: i32) {
    SW.edit_caret = text_splice(SW.edit.as_mut_ptr(), &raw mut SW.edit_len, 512, a, b, s, n);
}

pub unsafe fn edit_key(vk: u32, ctrl: bool) {
    match vk as u16 {
        VK_RETURN => {
            end_edit(true);
            return;
        }
        VK_ESCAPE => {
            end_edit(false);
            return;
        }
        VK_LEFT => {
            if SW.edit_caret > 0 {
                SW.edit_caret -= 1;
            }
        }
        VK_RIGHT => {
            if SW.edit_caret < SW.edit_len {
                SW.edit_caret += 1;
            }
        }
        VK_HOME => SW.edit_caret = 0,
        VK_END => SW.edit_caret = SW.edit_len,
        VK_BACK => {
            if SW.edit_caret > 0 {
                let mut from = SW.edit_caret - 1;
                if ctrl {
                    while from > 0 && SW.edit[from as usize - 1] == ' ' as u16 {
                        from -= 1;
                    }
                    while from > 0 && SW.edit[from as usize - 1] != ' ' as u16 {
                        from -= 1;
                    }
                }
                edit_splice(from, SW.edit_caret, null(), 0);
            }
        }
        VK_DELETE => {
            if SW.edit_caret < SW.edit_len {
                edit_splice(SW.edit_caret, SW.edit_caret + 1, null(), 0);
            }
        }
        0x56 /*V*/ => {
            if ctrl {
                let mut tmp = [0u16; 512];
                let n = clipboard_get(tmp.as_mut_ptr(), 512);
                if n != 0 {
                    edit_splice(SW.edit_caret, SW.edit_caret, tmp.as_ptr(), n);
                }
            }
        }
        _ => {}
    }
    SW.caret_on = true;
    settings_invalidate();
}

unsafe fn wrap_lines(font: i32, size: f32, s: *const WCHAR, max_w: f32, at: &mut [i32; 4], max_lines: i32) -> i32 {
    let n = wlen(s);
    let mut count = 0;
    let mut pos = 0;
    while pos < n && count < max_lines {
        at[count as usize] = pos;
        count += 1;
        let mut w = 0.0;
        let mut i = pos;
        let mut space = -1;
        while i < n {
            if *s.add(i as usize) == ' ' as u16 {
                space = i;
            }
            w += text_width(font, size, s.add(i as usize), 1);
            if w > max_w {
                break;
            }
            i += 1;
        }
        if i >= n {
            break;
        }
        pos = if space > pos { space } else { max(i, pos + 1) };
        while pos < n && *s.add(pos as usize) == ' ' as u16 {
            pos += 1;
        }
    }
    count
}

unsafe fn toggle_w() -> f32 {
    SSR(40.0)
}

unsafe fn keycap_w(t: &KeyTok, font: i32, fs: f32, h: f32, pad: f32) -> f32 {
    let mut w = text_width(font, fs, t.label.as_ptr(), -1);
    if !t.side.is_null() {
        w += text_width(FONT_TEXT, fs * 0.7, t.side, -1) + fs * 0.35;
    }
    max(h, (w + pad * 2.0).floor())
}

unsafe fn keycaps_width(t: &[KeyTok], font: i32, fs: f32, h: f32, pad: f32, gap: f32) -> f32 {
    let mut w = 0.0;
    for (i, k) in t.iter().enumerate() {
        w += keycap_w(k, font, fs, h, pad) + if i != 0 { gap } else { 0.0 };
    }
    w
}

unsafe fn control_size(it: &SItem, w: &mut f32, h: &mut f32) {
    let fs = SSC(13.5);
    *h = SSR(32.0);
    match it.kind {
        SK_TOGGLE => {
            *w = toggle_w();
            *h = SSR(22.0);
        }
        SK_CHOICE => {
            if is_seg(it) {
                let mut xs = [0.0; 4];
                let mut ws = [0.0; 4];
                *w = seg_layout(it, 0.0, &mut xs, &mut ws) + SSR(4.0);
                return;
            }
            let mut tw: f32 = 0.0;
            for k in 0..choice_count(it) as usize {
                tw = max(tw, text_width(FONT_TEXT, fs, ss(it.opt[k]), -1));
            }
            *w = max(SSR(130.0), (tw + SSR(14.0) + SSR(10.0) + SSR(12.0) + SSR(14.0)).floor());
        }
        SK_STEPPER => *w = SSR(140.0),
        SK_SLIDER => *w = SSR(240.0),
        SK_TEXT => *w = SSR(250.0),
        SK_HOTKEY => {
            let mut t: [KeyTok; 5] = zeroed();
            let n = hk_tokens(g_cfg.hk, &mut t);
            *w = max(
                SSR(130.0),
                (SSR(6.0) + keycaps_width(&t[..n as usize], FONT_TEXT, SSC(12.5), SSR(22.0), SSR(7.0), SSR(4.0)) + SSR(12.0) + SSR(14.0) + SSR(12.0)).floor(),
            );
        }
        SK_BUTTON => {
            let mut tw = text_width(FONT_TEXT, fs, ss(it.opt[0]), -1);
            if it.id == SID_CLEARHIST {
                tw = max(tw, text_width(FONT_TEXT, fs, tr!("Точно очистить?", "Clear for sure?"), -1));
            }
            if it.id == SID_UPDNOW {
                tw = max(tw, text_width(FONT_TEXT, fs, tr!("Обновить до 10.10.10", "Update to 10.10.10"), -1));
            }
            *w = max(SSR(130.0), (tw + SSR(14.0) + SSR(8.0) + SSR(32.0)).floor());
        }
        SK_INFO => {
            let mut good = false;
            *w = (text_width(FONT_TEXT, fs, info_text(it.id, &mut good), -1) + SSR(16.0)).floor();
        }
        _ => {}
    }
}

fn vis_ease(v: f32) -> f32 {
    let v = clamp(v, 0.0, 1.0);
    if v < 0.5 { 4.0 * v * v * v } else { 1.0 - (-2.0 * v + 2.0).powf(3.0) * 0.5 }
}

// How much of a group (its header and card) is shown: as much as its most visible row.
unsafe fn group_vis(i: usize) -> f32 {
    let mut g: f32 = 0.0;
    let mut j = i;
    while j < SITEMS && k_sitems[j].page == k_sitems[i].page && (j == i || k_sitems[j].group.ru.is_null()) {
        g = max(g, vis_ease(SW.vis[j]));
        j += 1;
    }
    g
}

// ---- the Design page: a card for every launcher style, with a preview drawn from shapes

static k_style_desc: [Str; STYLE__COUNT as usize] = [
    st!("Мягкая подложка с контуром", "A soft outlined highlight"),
    st!("Мелкие значки, яркий выбор", "Small icons, a solid highlight"),
    st!("Полоска акцента у выбранной", "An accent bar on the selection"),
    st!("Больше строк на экране", "More rows on screen"),
    st!("Поиск отдельно от списка", "Search apart from the list"),
    st!("Как Пуск: сетка значков", "Like Start: a grid of pins"),
    st!("Как Пуск: плитки и список", "Like Start: tiles and a list"),
];

const GAL_GAP: f32 = 14.0;
const GAL_INFO: f32 = 64.0; // under the preview: the name, a line about it and the button

fn gallery_cols(_r: &SRow) -> i32 {
    1 // one design per row, as wide as the page: big enough to read
}

pub unsafe fn gallery_preview_h(cw: f32) -> f32 {
    ((cw - SSR(16.0)) * 0.6).floor()
}

// The cards in page order: the Start look-alikes first.
pub static k_gal_order: [u8; STYLE__COUNT as usize] =
    [STYLE_START as u8, STYLE_WIN10 as u8, STYLE_STANDARD as u8, STYLE_RAYCAST as u8, STYLE_FLUENT as u8, STYLE_COMPACT as u8, STYLE_CAPSULE as u8];

pub fn gallery_pos(style: i32) -> i32 {
    for i in 0..STYLE__COUNT as usize {
        if k_gal_order[i] as i32 == style {
            return i as i32;
        }
    }
    0
}

// Style k's card (x0, y0, x1, y1) in the page's content.
pub unsafe fn gallery_card(r: &SRow, style: i32, b: &mut [f32; 4]) {
    let cols = gallery_cols(r);
    let k = gallery_pos(style);
    let g = SSR(GAL_GAP);
    let cw = ((r.w - g * (cols - 1) as f32) / cols as f32).floor();
    let ch = SSR(8.0) + gallery_preview_h(cw) + SSR(GAL_INFO);
    box_set(b, r.x + (cw + g) * (k % cols) as f32, r.y + (ch + g) * (k / cols) as f32, cw, ch);
}

pub unsafe fn gallery_at(r: &SRow, x: f32, y: f32) -> i32 {
    let mut b = [0.0; 4];
    for k in 0..STYLE__COUNT {
        gallery_card(r, k, &mut b);
        if in_box(&b, x, y) {
            return k;
        }
    }
    -1
}

pub unsafe fn settings_layout() {
    let s = sw();
    s.nrows = 0;
    s.ncards = 0;
    s.nheads = 0;
    let x0 = content_x0();
    let x1 = content_x1();
    let w = x1 - x0;
    let mut y = SSR(2.0);
    let mut card: i32 = -1;
    let mut lead: i32 = -1;
    let mut any = false;
    let mut card_first = false;
    let mut i = 0;
    while i < SITEMS && (s.nrows as usize) < SROWS_MAX {
        let it = &k_sitems[i];
        if it.page as i32 != s.page {
            i += 1;
            continue;
        }
        let ri = s.nrows as usize;
        s.nrows += 1;
        s.rows[ri] = zeroed();
        let r = &mut s.rows[ri];
        r.item = i as i32;
        r.x = x0;
        r.w = w;
        if it.kind == SK_GALLERY {
            let mut b = [0.0; 4];
            r.y = y + SSR(6.0);
            gallery_card(r, k_gal_order[STYLE__COUNT as usize - 1] as i32, &mut b);
            r.h = b[3] - y;
            r.vis = 1.0;
            y += r.h;
            card = -1;
            lead = -1;
            any = true;
            i += 1;
            continue;
        }
        if it.kind == SK_ABOUT {
            r.y = y;
            r.ndesc = wrap_lines(FONT_TEXT, SSC(13.5), ss(it.desc), w - SSR(4.0), &mut r.desc_at, 4);
            r.h = SSR(64.0) + SSR(22.0) + SSR(21.0) * r.ndesc as f32 + SSR(6.0);
            r.vis = 1.0;
            y += r.h;
            card = -1;
            lead = -1;
            any = true;
            i += 1;
            continue;
        }
        if !it.group.ru.is_null() || lead < 0 {
            lead = i as i32;
            let gv = group_vis(i);
            if any {
                y += (SSR(20.0) * gv + 0.5).floor();
            }
            if !it.group.ru.is_null() && (s.nheads as usize) < 12 {
                let hh = (SSR(28.0) * gv + 0.5).floor();
                s.heads[s.nheads as usize] = SBox { x: x0 + SSR(4.0), y: y + SSR(15.0), w: 0.0, h: hh };
                s.head_a[s.nheads as usize] = gv;
                s.head_text[s.nheads as usize] = it.group;
                s.nheads += 1;
                y += hh;
            }
            card = -1;
            if gv > 0.0 && (s.ncards as usize) < 12 {
                card = s.ncards;
                s.ncards += 1;
                s.cards[card as usize] = SBox { x: x0, y, w, h: 0.0 };
            }
            card_first = true;
        }
        let r = &mut s.rows[ri];
        control_size(it, &mut r.cw, &mut r.ch);
        r.cx = (x1 - SSR(18.0) - r.cw).floor();
        let tx = x0 + SSR(18.0);
        let tmax = r.cx - SSR(28.0) - tx;
        if !it.desc.ru.is_null() {
            r.ndesc = wrap_lines(FONT_TEXT, SSC(12.5), ss(it.desc), tmax, &mut r.desc_at, 4);
        }
        r.y = y;
        r.h = if r.ndesc != 0 { SSR(14.0) + SSR(20.0) + SSR(3.0) + SSR(18.0) * r.ndesc as f32 + SSR(14.0) } else { SSR(54.0) };
        r.ty = if r.ndesc != 0 { y + SSR(24.0) } else { y + (r.h * 0.5).floor() };
        r.cy = (r.ty - r.ch * 0.5).floor();
        r.vis = vis_ease(s.vis[i]);
        r.h = if r.vis >= 1.0 { r.h } else { (r.h * r.vis + 0.5).floor() };
        if r.h > 0.0 && card_first {
            r.first = true;
            card_first = false;
        }
        y += r.h;
        any |= r.h > 0.0;
        if card >= 0 {
            s.cards[card as usize].h = y - s.cards[card as usize].y;
        }
        i += 1;
    }
    s.content_h = y + SSR(28.0);
}

pub unsafe fn max_scroll_s() -> f32 {
    max(0.0, SW.content_h - (SW.H as f32 - top_h()))
}

pub unsafe fn row_focusable(r: i32) -> bool {
    if r < 0 || r >= SW.nrows || !item_shown(SW.rows[r as usize].item as usize) || item_locked(&k_sitems[SW.rows[r as usize].item as usize]) {
        return false;
    }
    let k = k_sitems[SW.rows[r as usize].item as usize].kind;
    k != SK_INFO && k != SK_ABOUT
}

#[derive(Clone, Copy)]
pub struct SColors {
    pub bg: u32,
    pub panel: u32,
    pub panel_border: u32,
    pub card: u32,
    pub card_border: u32,
    pub sep: u32,
    pub nav_sel: u32,
    pub nav_sel_border: u32,
    pub nav_hover: u32,
    pub ctl: u32,
    pub ctl_hover: u32,
    pub field: u32,
    pub field_border: u32,
    pub seg: u32,
    pub seg_border: u32,
    pub pop: u32,
    pub pop_border: u32,
    pub pop_hover: u32,
    pub shadow: u32,
    pub overlay: u32,
    pub knob: u32,
    pub cap: u32,
    pub cap_border: u32,
}

pub fn scolors(dark: bool) -> SColors {
    if dark {
        SColors {
            bg: RGBA(30, 30, 32, 255),
            panel: RGBA(255, 255, 255, 9),
            panel_border: RGBA(255, 255, 255, 15),
            card: RGBA(255, 255, 255, 10),
            card_border: RGBA(255, 255, 255, 6),
            sep: RGBA(255, 255, 255, 14),
            nav_sel: RGBA(255, 255, 255, 20),
            nav_sel_border: RGBA(255, 255, 255, 12),
            nav_hover: RGBA(255, 255, 255, 11),
            ctl: RGBA(255, 255, 255, 16),
            ctl_hover: RGBA(255, 255, 255, 28),
            field: RGBA(0, 0, 0, 56),
            field_border: RGBA(255, 255, 255, 26),
            seg: RGBA(255, 255, 255, 30),
            seg_border: RGBA(255, 255, 255, 14),
            pop: RGBA(44, 44, 47, 252),
            pop_border: RGBA(255, 255, 255, 22),
            pop_hover: RGBA(255, 255, 255, 20),
            shadow: RGBA(0, 0, 0, 120),
            overlay: RGBA(0, 0, 0, 120),
            knob: RGBA(255, 255, 255, 255),
            cap: RGBA(255, 255, 255, 24),
            cap_border: RGBA(255, 255, 255, 22),
        }
    } else {
        SColors {
            bg: RGBA(243, 243, 245, 255),
            panel: RGBA(255, 255, 255, 120),
            panel_border: RGBA(0, 0, 0, 14),
            card: RGBA(255, 255, 255, 185),
            card_border: RGBA(0, 0, 0, 12),
            sep: RGBA(0, 0, 0, 16),
            nav_sel: RGBA(0, 0, 0, 13),
            nav_sel_border: RGBA(0, 0, 0, 8),
            nav_hover: RGBA(0, 0, 0, 8),
            ctl: RGBA(0, 0, 0, 10),
            ctl_hover: RGBA(0, 0, 0, 18),
            field: RGBA(255, 255, 255, 230),
            field_border: RGBA(0, 0, 0, 30),
            seg: RGBA(255, 255, 255, 255),
            seg_border: RGBA(0, 0, 0, 16),
            pop: RGBA(251, 251, 252, 252),
            pop_border: RGBA(0, 0, 0, 22),
            pop_hover: RGBA(0, 0, 0, 10),
            shadow: RGBA(0, 0, 0, 60),
            overlay: RGBA(0, 0, 0, 60),
            knob: RGBA(255, 255, 255, 255),
            cap: RGBA(255, 255, 255, 230),
            cap_border: RGBA(0, 0, 0, 24),
        }
    }
}

const APP_LOGO_TILE: u32 = 0x1F1D26;
const APP_LOGO_FILL: f32 = 152.0 / 256.0;
const APP_LOGO_SQUASH: f32 = 0.53;
const k_logo_rgb: [u32; 3] = [0x5B4A8E, 0x8468D0, 0xB79CFF];
const k_logo_dy: [f32; 3] = [36.0, 0.0, -36.0];

pub unsafe fn draw_app_tile(x: f32, y: f32, sz: f32) {
    let x = x.floor();
    let y = y.floor();
    let rad = (sz * 0.225 + 0.5).floor();
    r_rect(x, y, sz, sz, RGBA(APP_LOGO_TILE >> 16, (APP_LOGO_TILE >> 8) & 255, APP_LOGO_TILE & 255, 255), rad);
    r_border(x, y, sz, sz, RGBA(255, 255, 255, 26), rad);
    let u = sz * APP_LOGO_FILL / 152.0;
    let m = [0.70710678, 0.70710678 * APP_LOGO_SQUASH, -0.70710678, 0.70710678 * APP_LOGO_SQUASH];
    for i in 0..3 {
        let c = k_logo_rgb[i];
        r_rect_affine(x + sz * 0.5, y + sz * 0.5 + k_logo_dy[i] * u, 112.0 * u, 112.0 * u, &m, RGBA(c >> 16, (c >> 8) & 255, c & 255, 255), 9.0 * u);
    }
}

unsafe fn draw_skeycaps(x: f32, cy: f32, t: &[KeyTok], font: i32, fs: f32, h: f32, pad: f32, gap: f32, rad: f32, bg: u32, border: u32, fg: u32, side_fg: u32) {
    let mut x = x;
    for k in t {
        let w = keycap_w(k, font, fs, h, pad);
        let y = (cy - h * 0.5).floor();
        r_rect(x, y, w, h, bg, rad);
        if border != 0 {
            r_border(x, y, w, h, border, rad);
        }
        let sfs = fs * 0.7;
        let lw = text_width(font, fs, k.label.as_ptr(), -1);
        let mut sw_ = 0.0;
        if !k.side.is_null() {
            sw_ = text_width(FONT_TEXT, sfs, k.side, -1) + fs * 0.35;
        }
        let mut tx = (x + (w - lw - sw_) * 0.5 + 0.5).floor();
        let base = text_baseline(font, fs, cy);
        if !k.side.is_null() {
            text_draw(FONT_TEXT, sfs, tx, base, k.side, -1, side_fg);
            tx += sw_;
        }
        text_draw(font, fs, tx, base, k.label.as_ptr(), -1, fg);
        x += w + gap;
    }
}

pub unsafe fn draw_sidebar(t: &Theme, c: &SColors) {
    let x = sb_x();
    let y = sb_x();
    let w = sb_w();
    let h = SW.H as f32 - sb_x() * 2.0;
    r_rect(x, y, w, h, c.panel, SSR(12.0));
    r_border(x, y, w, h, c.panel_border, SSR(12.0));

    let tile = SSR(36.0);
    let tx = x + SSR(16.0);
    let ty = y + SSR(18.0);
    draw_app_tile(tx, ty, tile);
    let nx = tx + tile + SSR(12.0);
    text_draw_fit(FONT_TEXT_SEMIBOLD, SSC(15.0), nx, (ty + SSR(16.0)).floor(), x + w - nx - SSR(10.0), w!("MixLauncher"), -1, t.text, false);
    text_draw_fit(FONT_TEXT, SSC(12.5), nx, (ty + SSR(33.0)).floor(), x + w - nx - SSR(10.0), tr!("Настройки", "Settings"), -1, t.dim, false);

    {
        let mut r = [0.0; 4];
        nav_rect(SW.page, &mut r);
        let py = (SW.nav_y + 0.5).floor();
        let ph = r[3] - r[1];
        r_rect(r[0], py, r[2] - r[0], ph, c.nav_sel, SSR(8.0));
        r_border(r[0], py, r[2] - r[0], ph, c.nav_sel_border, SSR(8.0));
        let bh = (min(SSR(16.0) + SW.nav_v.abs() * 0.02, ph - SSR(8.0)) + 0.5).floor();
        let bw = SSR(3.0);
        r_rect(r[0] + SSR(4.0), (py + (ph - bh) * 0.5 + 0.5).floor(), bw, bh, t.accent, bw * 0.5);
    }
    for i in 0..PG__COUNT {
        let mut r = [0.0; 4];
        nav_rect(i, &mut r);
        let sel = SW.page == i;
        let nh = SW.nav_hov[i as usize];
        if nh > 0.0 {
            r_rect(r[0], r[1], r[2] - r[0], r[3] - r[1], color_alpha(c.nav_hover, nh), SSR(8.0));
        }
        let cy = (r[1] + r[3]) * 0.5;
        let isz = SSR(20.0);
        let ix = r[0] + SSR(10.0);
        let fg = if sel { t.text } else { color_mix(t.dim, t.text, nh) };
        text_draw_icon(k_pages[i as usize].glyph, SSC(15.0), ix + isz * 0.5, cy, if sel { t.text } else { color_mix(t.faint, t.dim, nh) });
        let fs = SSC(14.0);
        text_draw_fit(
            if sel { FONT_TEXT_SEMIBOLD } else { FONT_TEXT },
            fs,
            ix + isz + SSR(12.0),
            text_baseline(FONT_TEXT, fs, cy),
            r[2] - ix - isz - SSR(18.0),
            ss(k_pages[i as usize].name),
            -1,
            fg,
            false,
        );
    }
}

const SLIDER_VALUE_W: f32 = 48.0; // the value at the slider's right

// Where a slider's track runs: from x0 to x1.
unsafe fn slider_track(r: &SRow) -> (f32, f32) {
    let k = SSR(16.0);
    (r.cx + k * 0.5, r.cx + r.cw - SSR(SLIDER_VALUE_W) - k * 0.5)
}

// The value at x on a slider's track, on its steps.
pub unsafe fn slider_value_at(r: &SRow, x: f32) -> i32 {
    let it = &k_sitems[r.item as usize];
    let (lo, hi) = item_range(it);
    let (x0, x1) = slider_track(r);
    let f = clamp((x - x0) / max(x1 - x0, 1.0), 0.0, 1.0);
    let st = max(it.step, 1);
    lo + (f * (hi - lo) as f32 / st as f32 + 0.5).floor() as i32 * st
}

unsafe fn draw_toggle(x: f32, cy: f32, on: f32, hov: bool, t: &Theme, c: &SColors) {
    // Flat: a plain grey track when off, the accent when on; the knob keeps its size and only travels.
    let w = toggle_w();
    let h = SSR(22.0);
    let y = (cy - h * 0.5).floor();
    let off_bg = if t.dark { RGBA(255, 255, 255, if hov { 50 } else { 36 }) } else { RGBA(0, 0, 0, if hov { 44 } else { 32 }) };
    r_rect(x, y, w, h, color_mix(off_bg, t.accent, on), h * 0.5);
    let k = h - SSR(6.0);
    let kx = (x + h * 0.5 + (w - h) * on - k * 0.5 + 0.5).floor();
    let ky = (cy - k * 0.5 + 0.5).floor();
    let mut knob = c.knob;
    let lum = (0.299 * (t.accent & 255) as f32 + 0.587 * ((t.accent >> 8) & 255) as f32 + 0.114 * ((t.accent >> 16) & 255) as f32) / 255.0;
    if lum > 0.62 {
        knob = RGBA(24, 24, 26, 255);
    }
    let off_knob = if t.dark { RGBA(235, 235, 237, 255) } else { RGBA(255, 255, 255, 255) };
    if !t.dark {
        r_rect_ex(kx, ky + SSR(1.0), k, k, RGBA(0, 0, 0, 36), k * 0.5, 0.0, SSR(2.0)); // lifts the white knob off the light track
    }
    r_rect(kx, ky, k, k, color_mix(off_knob, knob, on), k * 0.5);
}

unsafe fn pv_cell(k: i32) -> (i32, i32) {
    ((k % 3) * PV.pw, (k / 3) * PV.ph)
}

// The wallpaper of the settings' monitor, covering a cell: into the grid, and as the backdrop's source.
unsafe fn pv_wallpaper() -> bool {
    let mut mi: MONITORINFO = zeroed();
    mi.cbSize = size_of::<MONITORINFO>() as u32;
    if GetMonitorInfoW(MonitorFromWindow(SW.hwnd, MONITOR_DEFAULTTONEAREST), &mut mi) == 0 {
        return false;
    }
    let mr = mi.rcMonitor;
    let mut full: Layer = zeroed();
    let mut cell: Layer = zeroed();
    let ok = wallpaper_paint(&mut full, mr) && layer_fit(&mut cell, PV.pw, PV.ph);
    if ok {
        let mw = mr.right - mr.left;
        let mh = mr.bottom - mr.top;
        let k = max(PV.pw as f32 / mw as f32, PV.ph as f32 / mh as f32); // cover
        let sw_ = (PV.pw as f32 / k) as i32;
        let sh = (PV.ph as f32 / k) as i32;
        SetStretchBltMode(cell.dc, HALFTONE);
        SetBrushOrgEx(cell.dc, 0, 0, null_mut());
        StretchBlt(cell.dc, 0, 0, PV.pw, PV.ph, full.dc, (mw - sw_) / 2, (mh - sh) / 2, sw_, sh, SRCCOPY);
        GdiFlush();
        for i in 0..(PV.pw * PV.ph) as usize {
            *cell.bits.add(i) |= 0xFF000000;
        }
        let (cx, cy) = pv_cell(PV_WALL);
        let bx = D3D11_BOX { left: cx as u32, top: cy as u32, front: 0, right: (cx + PV.pw) as u32, bottom: (cy + PV.ph) as u32, back: 1 };
        let r = rr();
        r.ctx.as_ref().unwrap().UpdateSubresource(r.pv.tex.as_ref().unwrap(), 0, Some(&bx), cell.bits as *const c_void, PV.pw as u32 * 4, 0);
        if r_bd_source(PV.pw, PV.ph) {
            r_bd_upload(cell.bits);
        }
    }
    layer_free(&mut full);
    layer_free(&mut cell);
    ok
}

// Draws the launcher in style s into its cell: its own layout and drawing, at the scale that fits the card.
unsafe fn pv_render_style(s: i32, bd: &mut [f32; 8]) {
    g_cfg.style = s;
    g_cfg.scale = 100;
    ui_metrics(96);
    let pos = launcher_position();
    // the capsule's window has a see-through margin around its panels: it is placed by the panels, as the others
    let cm = if s == STYLE_CAPSULE { CAP_MARGIN * 2.0 } else { 0.0 };
    let room = (PV.ph - PV.tb) as f32;
    let fit = if pos == POS_CENTER { 0.86 } else { 0.92 };
    let k = min(PV.pw as f32 * 0.86 / (U.W as f32 - cm), room * fit / (U.H as f32 - cm));
    g_cfg.scale = max(1, (k * 100.0) as i32);
    ui_metrics(96);
    let w = min(U.W, PV.pw);
    let h = min(U.H, PV.ph);
    let inn = if s == STYLE_CAPSULE { SR(CAP_MARGIN) } else { 0.0 };
    let vw = w as f32 - inn * 2.0;
    let vh = h as f32 - inn * 2.0;
    if !rtex_fit(&raw mut R.pv_tmp, w, h, true) {
        return;
    }
    refresh_empty_view();
    rebuild_rows(false);
    U.scroll = 0.0;
    U.scroll_target = 0.0;
    U.scroll_v = 0.0;
    snap_highlight();
    U.sub_t = 1.0;
    U.sub_text = null();
    // where it opens: as set (the Start styles at the taskbar), Windows 10 against the taskbar and the edge
    let m = if s == STYLE_WIN10 { 0.0 } else { (room * 0.03).floor() };
    let left = pos == POS_BOTTOM_LEFT || (s == STYLE_START && !PV.tb_center);
    let x = (if left { m } else { ((PV.pw as f32 - vw) * 0.5).floor() }) - inn;
    let y = (if pos == POS_CENTER { ((room - vh) * 0.42).floor() } else { room - vh - m }) - inn;
    PV.x[s as usize] = x;
    PV.y[s as usize] = y;
    PV.w[s as usize] = w;
    PV.h[s as usize] = h;
    PV.rad[s as usize] = if s == STYLE_CAPSULE { 0.0 } else { backdrop_radius() };
    let kind = backdrop_kind();
    R.bd_sigma = (if kind == BACKDROP_MICA { 60.0 } else { 30.0 }) * U.s;
    R.bd_dirty = true;
    bd[0] = x;
    bd[1] = y;
    rr().bd = *bd;
    let mut t = r_target_tex(&raw mut R.pv_tmp);
    r_use(&mut t);
    R.scale = 1.0;
    r_draw(ui_draw);
    r_end_and_present();
    r_use(&raw mut R.main);
    let (cx, cy) = pv_cell(s);
    let bx = D3D11_BOX { left: 0, top: 0, front: 0, right: w as u32, bottom: h as u32, back: 1 };
    let r = rr();
    r.ctx.as_ref().unwrap().CopySubresourceRegion(r.pv.tex.as_ref().unwrap(), 0, cx as u32, cy as u32, 0, r.pv_tmp.tex.as_ref().unwrap(), 0, Some(&bx));
}

static mut PV_AGAIN: i32 = 0;

// Renders every preview again when they are out of date. Not while the launcher shows: it is the same state.
pub unsafe fn pv_update(pw: i32, ph: i32) {
    if !R.ok || pw < 8 || ph < 8 || U.visible {
        return;
    }
    if PV.ok && !PV.dirty && pw == PV.pw && ph == PV.ph {
        return;
    }
    PV.pw = pw;
    PV.ph = ph;
    PV.tb = max(12, (ph as f32 * 0.075) as i32);
    let mut al = 1u32;
    reg_dword(HKEY_CURRENT_USER, w!("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"), w!("TaskbarAl"), &mut al);
    PV.tb_center = al != 0;
    PV.dirty = false;
    PV.ok = rtex_fit(&raw mut R.pv, pw * 3, ph * 3, false);
    if !PV.ok {
        return;
    }
    let wall = pv_wallpaper();
    // the launcher's state, given back after
    let style = g_cfg.style;
    let scale = g_cfg.scale;
    let qlen = U.qlen;
    let dpi = U.dpi;
    let search = U.search_mode;
    let bd_ok = BD.ok;
    let backdrop_ok = U.backdrop_ok;
    let open_time = U.open_time;
    U.qlen = 0;
    U.search_mode = false;
    U.open_time = 0.0;
    g_hide_ai = true;
    U.caret_on = true;
    let kind = backdrop_kind();
    BD.ok = wall && kind != BACKDROP_SOLID;
    U.backdrop_ok = BD.ok;
    let mut bd = [0.0, 0.0, 1.0 / pw as f32, 1.0 / ph as f32, bd_shading(kind), if TH.dark { 1.0 } else { 0.0 }, 0.0, 0.0];
    for s in 0..STYLE__COUNT {
        pv_render_style(s, &mut bd);
    }
    g_hide_ai = false;
    g_cfg.style = style;
    g_cfg.scale = scale;
    ui_metrics(if dpi != 0 { dpi } else { 96 });
    U.dpi = dpi; // 0 still asks the next open to measure (and load the icons) again
    U.qlen = qlen;
    U.search_mode = search;
    U.open_time = open_time;
    BD.ok = bd_ok;
    U.backdrop_ok = backdrop_ok;
    refresh_empty_view();
    rebuild_rows(false);
    snap_highlight();
    // icons still loading show as blank tiles: once more when they are in
    let a = PV_AGAIN;
    PV_AGAIN += 1;
    if !SW.hwnd.is_null() && a % 2 == 0 {
        SetTimer(SW.hwnd, SETTINGS_TIMER_PV, 700, None);
    }
}

// The apps on the previews' taskbar: the most used ones, then the first of the list.
unsafe fn pv_taskbar_apps(out: &mut [*mut App; 6]) -> i32 {
    let mx = out.len();
    let mut n = 0usize;
    g_hide_ai = true;
    let mut i = 0;
    while i < U.frequent_count as usize && n < mx {
        if !app_hidden(U.frequent[i]) {
            out[n] = U.frequent[i];
            n += 1;
        }
        i += 1;
    }
    if !U.apps.is_null() {
        let mut i = 0;
        while i < (*U.apps).shell_count as usize && n < mx {
            let a = (*U.apps).apps.add(i);
            let mut dup = (*a).kind == APP_CMD || app_hidden(a);
            for j in 0..n {
                if dup {
                    break;
                }
                dup = out[j] == a;
            }
            if !dup {
                out[n] = a;
                n += 1;
            }
            i += 1;
        }
    }
    g_hide_ai = false;
    n as i32
}

// A Windows logo: four squares, Windows 11's rounded and light blue, Windows 10's plain.
unsafe fn pv_logo(x: f32, cy: f32, sz: f32, w10: bool) {
    let g = max(1.0, (sz * 0.08).floor());
    let q = ((sz - g) * 0.5).floor();
    let c = if w10 {
        TH.text
    } else if TH.dark {
        RGBA(76, 194, 255, 255)
    } else {
        RGBA(0, 103, 192, 255)
    };
    for i in 0..4 {
        r_rect(x + (i % 2) as f32 * (q + g), (cy - sz * 0.5).floor() + (i / 2) as f32 * (q + g), q, q, c, if w10 { 0.0 } else { SQUIRCLE(q * 0.18) });
    }
}

// The taskbar along the preview's bottom, drawn over everything as the real one is: Windows 11's (icons in the middle
// or at the left, as Windows has them) or, under its own style, Windows 10's. The apps are yours, the clock is now.
unsafe fn pv_taskbar(k: i32, x: f32, y: f32, w: f32, h: f32, rad: f32) {
    let t = &*(&raw const TH);
    let w10 = k == STYLE_WIN10;
    let tb = PV.tb as f32;
    let ty = y + h - tb;
    let cy = (ty + tb * 0.5).floor();
    let saved = R.clip;
    r_set_clip(max(saved[0], x), max(saved[1], ty), min(saved[2], x + w), min(saved[3], y + h));
    let bg = if w10 {
        if t.dark { RGBA(16, 16, 18, 242) } else { RGBA(228, 228, 230, 242) }
    } else if t.dark {
        RGBA(32, 32, 34, 238)
    } else {
        RGBA(243, 243, 245, 238)
    };
    r_rect(x, y, w, h, bg, rad);
    r_set_clip(saved[0], saved[1], saved[2], saved[3]);
    r_rect(x, ty, w, 1.0, if t.dark { RGBA(255, 255, 255, 18) } else { RGBA(0, 0, 0, 14) }, 0.0);
    let dim = color_alpha(t.text, 0.8);
    let faint = color_alpha(t.text, 0.55);

    // at the right: the tray's chevron, network and sound, then the time over the date
    let mut st: SYSTEMTIME = zeroed();
    GetLocalTime(&mut st);
    let mut tm = [0u16; 16];
    let mut dt = [0u16; 16];
    wfmt!(tm.as_mut_ptr(), 16, "{}:{:02}", st.wHour, st.wMinute);
    wfmt!(dt.as_mut_ptr(), 16, "{:02}.{:02}.{:04}", st.wDay, st.wMonth, st.wYear);
    let cfs = (tb * 0.3).floor();
    let dw = text_width(FONT_TEXT, cfs, dt.as_ptr(), -1);
    let rx = x + w - (tb * 0.35).floor();
    let tw = text_width(FONT_TEXT, cfs, tm.as_ptr(), -1);
    let lh = cfs * 1.15;
    text_draw(FONT_TEXT, cfs, (rx - tw).floor(), (cy - lh * 0.12).floor(), tm.as_ptr(), -1, dim);
    text_draw(FONT_TEXT, cfs, (rx - dw).floor(), (cy + lh * 0.88).floor(), dt.as_ptr(), -1, dim);
    let gs = (tb * 0.36).floor();
    let gx = rx - dw - (tb * 0.55).floor();
    let tray: [u32; 3] = [0xE767, 0xE701, 0xE70E]; // Volume, Wifi, ChevronUp
    for i in 0..3 {
        text_draw_icon(tray[i], gs, gx - i as f32 * tb * 0.62, cy, if i == 2 { faint } else { dim });
    }

    let mut apps: [*mut App; 6] = [null_mut(); 6];
    let na = pv_taskbar_apps(&mut apps);
    let is = (tb * 0.56).floor();
    let pitch = (tb * 0.95).floor();
    if w10 {
        // the logo, the search box with its prompt, the apps
        let lx = x + (tb * 0.55).floor();
        pv_logo(lx, cy, (tb * 0.4).floor(), true);
        let bx = lx + (tb * 0.95).floor();
        let bw = (w * 0.26).floor();
        r_rect(bx, ty + 1.0, bw, tb - 1.0, if t.dark { RGBA(255, 255, 255, 34) } else { RGBA(255, 255, 255, 255) }, 0.0);
        text_draw_icon(0xE721, (tb * 0.32).floor(), bx + (tb * 0.45).floor(), cy, faint);
        let pfs = (tb * 0.3).floor();
        let px = bx + (tb * 0.85).floor();
        text_draw_fit(
            FONT_TEXT,
            pfs,
            px,
            text_baseline(FONT_TEXT, pfs, cy),
            bx + bw - px - SSR(4.0),
            tr!("Введите здесь текст для поиска", "Type here to search"),
            -1,
            faint,
            false,
        );
        let ix = bx + bw + (tb * 0.3).floor();
        for i in 0..na {
            let ax = ix + pitch * i as f32;
            draw_app_icon(apps[i as usize], (ax + (pitch - is) * 0.5).floor(), (cy - is * 0.5).floor(), is);
            if i < 2 {
                let dh = max(2.0, (tb * 0.07).floor());
                r_rect(ax + 1.0, y + h - dh, pitch - 2.0, dh, if i != 0 { faint } else { t.accent }, 0.0);
            }
        }
        return;
    }
    // Windows 11: Start, the search box, the apps (the first two running, the first in front)
    let sw_ = (tb * 3.3).floor();
    let sh = (tb * 0.66).floor();
    let gap = (tb * 0.25).floor();
    let total = pitch + gap + sw_ + gap + pitch * na as f32;
    let sx = if PV.tb_center { (x + (w - total) * 0.5).floor() } else { x + (tb * 0.25).floor() };
    pv_logo((sx + (pitch - tb * 0.42) * 0.5).floor(), cy, (tb * 0.42).floor(), false);
    let bx = sx + pitch + gap;
    r_rect(bx, cy - (sh * 0.5).floor(), sw_, sh, if t.dark { RGBA(255, 255, 255, 16) } else { RGBA(255, 255, 255, 230) }, sh * 0.5);
    r_border(bx, cy - (sh * 0.5).floor(), sw_, sh, if t.dark { RGBA(255, 255, 255, 14) } else { RGBA(0, 0, 0, 16) }, sh * 0.5);
    text_draw_icon(0xE721, (tb * 0.3).floor(), bx + (sh * 0.55).floor(), cy, dim);
    let pfs = (tb * 0.3).floor();
    text_draw(FONT_TEXT, pfs, bx + (sh * 0.55 + tb * 0.32).floor(), text_baseline(FONT_TEXT, pfs, cy), tr!("Поиск", "Search"), -1, faint);
    let ix = bx + sw_ + gap;
    for i in 0..na {
        let ax = ix + pitch * i as f32;
        if i == 0 {
            r_rect(
                ax + 1.0,
                ty + (tb * 0.1).floor(),
                pitch - 2.0,
                tb - (tb * 0.2).floor(),
                if t.dark { RGBA(255, 255, 255, 18) } else { RGBA(255, 255, 255, 200) },
                SQUIRCLE((tb * 0.2).floor()),
            );
        }
        draw_app_icon(apps[i as usize], (ax + (pitch - is) * 0.5).floor(), (cy - is * 0.5 - tb * 0.04).floor(), is);
        if i < 2 {
            let dw2 = if i != 0 { (tb * 0.12).floor() } else { (tb * 0.32).floor() };
            let dh = max(2.0, (tb * 0.08).floor());
            r_rect((ax + (pitch - dw2) * 0.5).floor(), y + h - (tb * 0.13).floor() - dh, dw2, dh, if i != 0 { faint } else { t.accent }, dh * 0.5);
        }
    }
}

// Card k's preview at (x, y): the wallpaper, its taskbar, and the launcher with the shadow and outline DWM gives it.
unsafe fn pv_draw(k: i32, x: f32, y: f32, w: f32, h: f32) {
    let rad = SQUIRCLE(SSR(16.0));
    if !PV.ok || PV.pw != w as i32 || PV.ph != h as i32 {
        r_rect(x, y, w, h, if TH.dark { RGBA(0, 0, 0, 64) } else { RGBA(0, 0, 0, 12) }, rad);
        return;
    }
    let aw = rr().pv.w as f32;
    let ah = rr().pv.h as f32;
    let (cx, cy) = pv_cell(PV_WALL);
    r_image(x, y, w, h, cx as f32 / aw, cy as f32 / ah, (cx + PV.pw) as f32 / aw, (cy + PV.ph) as f32 / ah, rad);
    let ku = k as usize;
    let lx = x + PV.x[ku];
    let ly = y + PV.y[ku];
    let lw = PV.w[ku] as f32;
    let lh = PV.h[ku] as f32;
    let saved = R.clip;
    r_set_clip(max(saved[0], x), max(saved[1], y), min(saved[2], x + w), min(saved[3], y + h));
    if PV.rad[ku] > 0.0 || k == STYLE_WIN10 {
        r_rect_ex(lx, ly + SSR(3.0), lw, lh, RGBA(0, 0, 0, if TH.dark { 110 } else { 60 }), PV.rad[ku], 0.0, SSR(10.0));
    }
    let (cx, cy) = pv_cell(k);
    r_image(lx, ly, lw, lh, cx as f32 / aw, cy as f32 / ah, (cx + PV.w[ku]) as f32 / aw, (cy + PV.h[ku]) as f32 / ah, 0.0);
    if PV.rad[ku] > 0.0 || k == STYLE_WIN10 {
        r_border(lx, ly, lw, lh, if TH.dark { RGBA(255, 255, 255, 30) } else { RGBA(0, 0, 0, 40) }, PV.rad[ku]);
    }
    r_set_clip(saved[0], saved[1], saved[2], saved[3]);
    pv_taskbar(k, x, y, w, h, rad); // last, over the launcher's shadow, as the real taskbar is
}

// The card's button, sel (0..1) of the way from "Select" to "Selected".
unsafe fn gallery_button(b: &[f32; 4], sel: f32, out: &mut [f32; 4]) {
    let fs = SSC(13.0);
    let w0 = text_width(FONT_TEXT, fs, tr!("Выбрать", "Select"), -1) + SSR(32.0);
    let w1 = text_width(FONT_TEXT, fs, tr!("Выбрано", "Selected"), -1) + SSR(44.0);
    let w = (w0 + (w1 - w0) * sel + 0.5).floor();
    let h = SSR(30.0);
    box_set(out, b[2] - SSR(12.0) - w, (b[3] - SSR(GAL_INFO) * 0.5 - h * 0.5 + 0.5).floor(), w, h);
}

unsafe fn draw_gallery(r: &SRow, off: f32, t: &Theme, c: &SColors) {
    let it = &k_sitems[r.item as usize];
    for k in 0..STYLE__COUNT {
        let mut b = [0.0; 4];
        gallery_card(r, k, &mut b);
        b[1] += off;
        b[3] += off;
        if b[3] < top_h() || b[1] > SW.H as f32 {
            continue;
        }
        let hov = SW.card_hov[k as usize];
        let sel = SW.card_sel[k as usize];
        let cw = b[2] - b[0];
        let ch = b[3] - b[1];
        let cr = SQUIRCLE(SSR(22.0));
        r_rect(b[0], b[1], cw, ch, color_mix(c.card, c.ctl_hover, 0.35 * hov * (1.0 - sel)), cr);
        let edge = color_mix(color_mix(c.card_border, c.ctl_hover, hov), t.accent, sel);
        r_rect_ex(b[0], b[1], cw, ch, edge, cr, 1.0 + (SSR(1.5) - 1.0) * sel, 0.0);
        let px = b[0] + SSR(8.0);
        let py = b[1] + SSR(8.0);
        let pw = cw - SSR(16.0);
        let ph = gallery_preview_h(cw);
        pv_draw(k, px, py, pw, ph);
        let mut bt = [0.0; 4];
        gallery_button(&b, sel, &mut bt);
        let ib = py + ph;
        let tx = b[0] + SSR(14.0);
        let tmax = bt[0] - SSR(10.0) - tx;
        text_draw_fit(FONT_TEXT_SEMIBOLD, SSC(14.0), tx, (ib + SSR(29.0)).floor(), tmax, ss(it.opt[k as usize]), -1, t.text, false);
        text_draw_fit(FONT_TEXT, SSC(12.5), tx, (ib + SSR(48.0)).floor(), tmax, ss(k_style_desc[k as usize]), -1, t.dim, false);
        let bw = bt[2] - bt[0];
        let bh = bt[3] - bt[1];
        let bcy = bt[1] + bh * 0.5;
        let fs = SSC(13.0);
        let lum = (0.299 * (t.accent & 255) as f32 + 0.587 * ((t.accent >> 8) & 255) as f32 + 0.114 * ((t.accent >> 16) & 255) as f32) / 255.0;
        let fg = if lum > 0.62 { RGBA(24, 24, 26, 255) } else { RGBA(255, 255, 255, 255) };
        r_rect(bt[0], bt[1], bw, bh, color_mix(color_mix(c.ctl, c.ctl_hover, hov), t.accent, sel), SQUIRCLE(SSR(13.0)));
        // the labels cross-fade, the check slides in from the left
        let o = R.opacity;
        let base = text_baseline(FONT_TEXT, fs, bcy);
        if sel < 1.0 {
            let l0 = tr!("Выбрать", "Select");
            R.opacity = o * (1.0 - sel);
            text_draw(FONT_TEXT, fs, (bt[0] + (bw - text_width(FONT_TEXT, fs, l0, -1)) * 0.5 + 0.5).floor(), base, l0, -1, t.text);
        }
        if sel > 0.0 {
            let l1 = tr!("Выбрано", "Selected");
            let lx = (bt[0] + (bw - text_width(FONT_TEXT, fs, l1, -1) - SSR(18.0)) * 0.5 + 0.5).floor();
            R.opacity = o * sel;
            text_draw_icon(0xE73E, SSC(11.0), lx + SSR(6.0) - SSR(6.0) * (1.0 - sel), bcy, fg); // CheckMark
            text_draw(FONT_TEXT, fs, lx + SSR(18.0), base, l1, -1, fg);
        }
        R.opacity = o;
    }
}

static ABOUT_VER: Str = st!("Версия 2.2.0", "Version 2.2.0");

pub unsafe fn draw_srow(ri: i32, off: f32, t: &Theme, c: &SColors) {
    let r = SW.rows[ri as usize];
    let it = &k_sitems[r.item as usize];
    let y = r.y + off;
    let ty = r.ty + off;
    let locked = item_locked(it);
    let hov = SW.hover.type_ == HT_ROW && SW.hover.idx == ri && !locked;
    let fs = SSC(14.0);
    let dfs = SSC(12.5);
    let cfs = SSC(13.5);

    if it.kind == SK_GALLERY {
        draw_gallery(&r, off, t, c);
        return;
    }
    if it.kind == SK_ABOUT {
        let tile = SSR(64.0);
        draw_app_tile(r.x, y + SSR(2.0), tile);
        let nx = r.x + tile + SSR(18.0);
        text_draw(FONT_TEXT_SEMIBOLD, SSC(22.0), nx, (y + SSR(32.0)).floor(), w!("MixLauncher"), -1, t.text);
        text_draw(FONT_TEXT, SSC(13.0), nx, (y + SSR(54.0)).floor(), ss(ABOUT_VER), -1, t.dim);
        let d = ss(it.desc);
        let n = wlen(d);
        for k in 0..r.ndesc {
            let a = r.desc_at[k as usize];
            let mut b = if k + 1 < r.ndesc { r.desc_at[k as usize + 1] } else { n };
            while b > a && *d.add(b as usize - 1) == ' ' as u16 {
                b -= 1;
            }
            text_draw_fit(FONT_TEXT, SSC(13.5), r.x + SSR(2.0), (y + SSR(64.0) + SSR(34.0) + SSR(21.0) * k as f32).floor(), r.w - SSR(4.0), d.add(a as usize), b - a, t.dim, false);
        }
        return;
    }

    if !r.first {
        r_rect(r.x + SSR(18.0), y, r.w - SSR(36.0), 1.0, c.sep, 0.0);
    }
    let opacity = R.opacity;
    if locked {
        R.opacity *= 0.4;
    }
    if SW.row_hov_idx == ri && SW.row_hov > 0.0 {
        let g = SSR(4.0);
        r_rect(r.x + g, y + g, r.w - g * 2.0, r.h - g * 2.0, color_alpha(c.nav_hover, SW.row_hov), SSR(7.0));
    }
    let tx = r.x + SSR(18.0);
    let tmax = r.cx - SSR(28.0) - tx;
    text_draw_fit(FONT_TEXT, fs, tx, text_baseline(FONT_TEXT, fs, ty), tmax, ss(it.title), -1, t.text, false);
    if r.ndesc != 0 {
        let d = ss(it.desc);
        let n = wlen(d);
        for k in 0..r.ndesc {
            let a = r.desc_at[k as usize];
            let mut b = if k + 1 < r.ndesc { r.desc_at[k as usize + 1] } else { n };
            while b > a && *d.add(b as usize - 1) == ' ' as u16 {
                b -= 1;
            }
            let lc = y + SSR(14.0) + SSR(20.0) + SSR(3.0) + SSR(18.0) * k as f32 + SSR(9.0);
            text_draw_fit(FONT_TEXT, dfs, tx, text_baseline(FONT_TEXT, dfs, lc), tmax, d.add(a as usize), b - a, t.dim, false);
        }
    }

    let cx = r.cx;
    let cy = r.cy + off;
    let cw = r.cw;
    let ch = r.ch;
    let mid = cy + ch * 0.5;
    let chov = hov && SW.hover.part != PART_ROW;
    match it.kind {
        SK_TOGGLE => draw_toggle(cx, mid, SW.tog[r.item as usize], hov, t, c),
        SK_CHOICE => {
            if is_seg(it) {
                let mut xs = [0.0; 4];
                let mut ws = [0.0; 4];
                let n = choice_count(it);
                let cur = sval(it.id);
                seg_layout(it, cx + SSR(2.0), &mut xs, &mut ws);
                r_rect(cx, cy, cw, ch, c.ctl, SSR(8.0));
                let p = clamp(SW.seg[r.item as usize], 0.0, (n - 1) as f32);
                let k0 = p.floor() as usize;
                let k1 = min(k0 + 1, n as usize - 1);
                let fr = p - k0 as f32;
                let px = (xs[k0] + (xs[k1] - xs[k0]) * fr + 0.5).floor();
                let pw = (ws[k0] + (ws[k1] - ws[k0]) * fr + 0.5).floor();
                if !t.dark {
                    r_rect_ex(px, cy + SSR(3.0), pw, ch - SSR(4.0), RGBA(0, 0, 0, 22), SSR(6.0), 0.0, SSR(2.0));
                }
                r_rect(px, cy + SSR(2.0), pw, ch - SSR(4.0), c.seg, SSR(6.0));
                r_border(px, cy + SSR(2.0), pw, ch - SSR(4.0), c.seg_border, SSR(6.0));
                let hk = if chov { seg_index_at(it, cx, SW.mx as f32) } else { -1 };
                for k in 0..n {
                    let lbl = ss(it.opt[k as usize]);
                    let lw = text_width(FONT_TEXT, cfs, lbl, -1);
                    let gw = if it.warn & (1 << k) != 0 { SSR(WARN_GLYPH_W) } else { 0.0 };
                    let col = if k == cur {
                        t.text
                    } else if k == hk {
                        color_mix(t.dim, t.text, 0.6)
                    } else {
                        t.dim
                    };
                    let lx = (xs[k as usize] + (ws[k as usize] - lw - gw) * 0.5 + 0.5).floor();
                    if gw > 0.0 {
                        text_draw_icon(0xE7BA, SSC(12.0), lx + SSR(7.0), mid, col); // Warning
                    }
                    text_draw(FONT_TEXT, cfs, lx + gw, text_baseline(FONT_TEXT, cfs, mid), lbl, -1, col);
                }
            } else {
                let open = SW.pop_open && SW.pop_row == ri;
                r_rect(cx, cy, cw, ch, if chov || open { c.ctl_hover } else { c.ctl }, SSR(7.0));
                let lbl = ss(it.opt[sval(it.id) as usize]);
                let gx = cx + cw - SSR(14.0) - SSR(5.0);
                text_draw_fit(FONT_TEXT, cfs, cx + SSR(14.0), text_baseline(FONT_TEXT, cfs, mid), gx - SSR(10.0) - cx - SSR(14.0), lbl, -1, t.text, false);
                text_draw_icon(0xE70D, SSC(10.0), gx, mid, t.dim);
            }
        }
        SK_STEPPER => {
            let v = sval(it.id);
            let (lo, hi) = item_range(it);
            let bw = SSR(34.0);
            let can_dec = v > lo;
            let can_inc = v < hi;
            r_rect(cx, cy, cw, ch, c.ctl, SSR(7.0));
            if hov && SW.hover.part == PART_MINUS && can_dec {
                r_rect(cx, cy, bw, ch, c.ctl_hover, SSR(7.0));
            }
            if hov && SW.hover.part == PART_PLUS && can_inc {
                r_rect(cx + cw - bw, cy, bw, ch, c.ctl_hover, SSR(7.0));
            }
            r_rect(cx + bw, cy + SSR(7.0), 1.0, ch - SSR(14.0), c.sep, 0.0);
            r_rect(cx + cw - bw, cy + SSR(7.0), 1.0, ch - SSR(14.0), c.sep, 0.0);
            text_draw_icon(0xE738, SSC(11.0), cx + bw * 0.5, mid, if can_dec { t.text } else { t.faint });
            text_draw_icon(0xE710, SSC(11.0), cx + cw - bw * 0.5, mid, if can_inc { t.text } else { t.faint });
            let mut num = [0u16; 16];
            wfmt!(num.as_mut_ptr(), 16, "{}", v);
            let unit = if it.unit.ru.is_null() { null() } else { ss(it.unit) };
            let ufs = SSC(12.0);
            let nw = text_width(FONT_TEXT, cfs, num.as_ptr(), -1);
            let uw = if unit.is_null() { 0.0 } else { text_width(FONT_TEXT, ufs, unit, -1) + SSR(4.0) };
            let nx = (cx + (cw - nw - uw) * 0.5 + 0.5).floor();
            let base = text_baseline(FONT_TEXT, cfs, mid);
            text_draw(FONT_TEXT, cfs, nx, base, num.as_ptr(), -1, t.text);
            if !unit.is_null() {
                text_draw(FONT_TEXT, ufs, nx + nw + SSR(4.0), base, unit, -1, t.dim);
            }
        }
        SK_SLIDER => {
            let v = sval(it.id);
            let (lo, hi) = item_range(it);
            let f = k_sid_cfg[it.id as usize];
            let def = if f != 0 { k_cfg[f as usize].def } else { lo };
            let (x0, x1) = slider_track(&r);
            let th = SSR(4.0);
            let k = SSR(16.0);
            let span = max((hi - lo) as f32, 1.0);
            let kx = (x0 + (x1 - x0) * (v - lo) as f32 / span + 0.5).floor();
            let dx = (x0 + (x1 - x0) * (def - lo) as f32 / span + 0.5).floor();
            let track = if t.dark { RGBA(255, 255, 255, if chov { 50 } else { 36 }) } else { RGBA(0, 0, 0, if chov { 44 } else { 32 }) };
            r_rect(x0, (mid - th * 0.5).floor(), x1 - x0, th, track, th * 0.5);
            r_rect(dx - SSR(1.0), (mid - SSR(6.0)).floor(), SSR(2.0), SSR(12.0), track, SSR(1.0)); // the default
            r_rect(x0, (mid - th * 0.5).floor(), kx - x0, th, t.accent, th * 0.5);
            let ky = (mid - k * 0.5 + 0.5).floor();
            if !t.dark {
                r_rect_ex(kx - k * 0.5, ky + SSR(1.0), k, k, RGBA(0, 0, 0, 40), k * 0.5, 0.0, SSR(2.0));
            }
            r_rect(kx - k * 0.5, ky, k, k, if t.dark { RGBA(235, 235, 237, 255) } else { RGBA(255, 255, 255, 255) }, k * 0.5);
            r_rect(kx - k * 0.25, (mid - k * 0.25 + 0.5).floor(), k * 0.5, k * 0.5, t.accent, k * 0.25);
            let mut num = [0u16; 16];
            wfmt!(num.as_mut_ptr(), 16, "{}{}", v, Ws(if it.unit.ru.is_null() { w!("") } else { ss(it.unit) }));
            let nw = text_width(FONT_TEXT, cfs, num.as_ptr(), -1);
            text_draw(FONT_TEXT, cfs, (cx + cw - nw).floor(), text_baseline(FONT_TEXT, cfs, mid), num.as_ptr(), -1, t.text);
        }
        SK_TEXT => {
            let editing = SW.editing == ri;
            r_rect(cx, cy, cw, ch, c.field, SSR(7.0));
            r_rect_ex(
                cx,
                cy,
                cw,
                ch,
                if editing {
                    t.accent
                } else if chov {
                    color_alpha(c.field_border, 1.8)
                } else {
                    c.field_border
                },
                SSR(7.0),
                if editing { SSR(1.5) } else { 1.0 },
                0.0,
            );
            let s = if editing { SW.edit.as_ptr() } else { g_cfg.everything_filter.as_ptr() };
            let ix = cx + SSR(10.0);
            let iw = cw - SSR(20.0);
            let base = text_baseline(FONT_TEXT, cfs, mid);
            let clip = R.clip;
            r_set_clip(max(ix - SSR(1.0), clip[0]), max(cy, clip[1]), min(ix + iw + SSR(1.0), clip[2]), min(cy + ch, clip[3]));
            if *s == 0 && !editing {
                text_draw(FONT_TEXT, cfs, ix, base, tr!("Нет", "None"), -1, t.faint);
            } else {
                let mut offs = [0.0f32; 513];
                let n = wlen(s);
                text_offsets(FONT_TEXT, cfs, s, n, offs.as_mut_ptr());
                let caret_x = if editing { offs[SW.edit_caret as usize] } else { 0.0 };
                let shift = if caret_x > iw - SSR(2.0) { caret_x - iw + SSR(2.0) } else { 0.0 };
                text_draw(FONT_TEXT, cfs, ix - shift, base, s, n, t.text);
                if editing && SW.caret_on {
                    r_rect((ix - shift + caret_x).floor(), (mid - cfs * 0.6).floor(), max(1.0, SSC(1.25).floor()), (cfs * 1.2).floor(), t.text, 0.0);
                }
            }
            r_set_clip(clip[0], clip[1], clip[2], clip[3]);
        }
        SK_HOTKEY => {
            r_rect(cx, cy, cw, ch, if chov { c.ctl_hover } else { c.ctl }, SSR(7.0));
            let mut tk: [KeyTok; 5] = zeroed();
            let n = hk_tokens(g_cfg.hk, &mut tk);
            let kfs = SSC(12.5);
            let kh = SSR(22.0);
            draw_skeycaps(cx + SSR(6.0), mid, &tk[..n as usize], FONT_TEXT, kfs, kh, SSR(7.0), SSR(4.0), SSR(5.0), c.cap, 0, t.text, t.faint);
            text_draw_icon(0xE70F, SSC(12.0), cx + cw - SSR(12.0) - SSR(7.0), mid, if chov { t.text } else { t.dim });
        }
        SK_BUTTON => {
            let armed = it.id == SID_CLEARHIST && SW.armed;
            let flashing = SW.flash_id == it.id as i32 && time_now() < SW.flash_until;
            let bg = if armed {
                color_alpha(t.danger, 0.2)
            } else if chov {
                c.ctl_hover
            } else {
                c.ctl
            };
            r_rect(cx, cy, cw, ch, bg, SSR(7.0));
            let mut lbl = if armed {
                tr!("Точно очистить?", "Clear for sure?")
            } else if flashing {
                tr!("Готово", "Done")
            } else {
                ss(it.opt[0])
            };
            let mut glyph = if flashing { 0xE73E } else { it.glyph };
            let mut col = if armed {
                t.danger
            } else if flashing {
                t.accent
            } else {
                t.text
            };
            let mut ub = [0u16; 64];
            if it.id == SID_UPDNOW && upd_state() != UPD_IDLE {
                lbl = upd_label(ub.as_mut_ptr(), 64);
                let st = upd_state();
                glyph = if st == UPD_LATEST {
                    0xE73E
                } else if st == UPD_ERROR {
                    0xE72C
                } else {
                    0xE896
                };
                col = if st == UPD_ERROR { t.danger } else { t.text };
                if st == UPD_AVAILABLE {
                    r_rect(cx, cy, cw, ch, color_alpha(t.accent, if chov { 0.30 } else { 0.20 }), SSR(7.0));
                }
            }
            let lw = text_width(FONT_TEXT, cfs, lbl, -1);
            let gw = if glyph != 0 { SSR(14.0) + SSR(8.0) } else { 0.0 };
            let bx = (cx + (cw - lw - gw) * 0.5 + 0.5).floor();
            if glyph != 0 {
                text_draw_icon(glyph, SSC(13.0), bx + SSR(7.0), mid, col);
            }
            text_draw(FONT_TEXT, cfs, bx + gw, text_baseline(FONT_TEXT, cfs, mid), lbl, -1, col);
        }
        SK_INFO => {
            let mut good = false;
            let s = info_text(it.id, &mut good);
            let dot = SSR(8.0);
            r_rect(cx, (mid - dot * 0.5).floor(), dot, dot, if good { RGBA(48, 176, 99, 255) } else { t.danger }, dot * 0.5);
            text_draw(FONT_TEXT, cfs, cx + dot + SSR(8.0), text_baseline(FONT_TEXT, cfs, mid), s, -1, t.text);
        }
        _ => {}
    }
    R.opacity = opacity;
}

// What the tooltip under the pointer says (none: NULL), and *key for its place: a new place arms it again.
pub unsafe fn tip_at(key: &mut i32) -> *const WCHAR {
    *key = -1;
    if SW.hover.type_ != HT_ROW || SW.hover.idx < 0 || SW.hover.idx >= SW.nrows {
        return null();
    }
    let it = &k_sitems[SW.rows[SW.hover.idx as usize].item as usize];
    if item_locked(it) {
        *key = SW.hover.idx * 8;
        return tr!("Изменение недоступно при включённом уменьшении движения", "Can't be changed while reduced motion is on");
    }
    if it.warn != 0 && is_seg(it) && SW.hover.part == PART_CONTROL {
        let k = seg_index_at(it, SW.rows[SW.hover.idx as usize].cx, SW.mx as f32);
        if it.warn & (1 << k) != 0 {
            *key = SW.hover.idx * 8 + 1 + k;
            return warn_tip();
        }
    }
    null()
}

pub unsafe fn draw_tip(t: &Theme, c: &SColors, s: *const WCHAR) {
    let fs = SSC(12.5);
    let pad = SSR(10.0);
    let h = SSR(30.0);
    let w = (text_width(FONT_TEXT, fs, s, -1) + pad * 2.0).floor();
    let x = max(SSR(8.0), min(SW.tip_x as f32, SW.W as f32 - w - SSR(8.0))).floor();
    let mut y = SW.tip_y as f32 + SSR(22.0);
    if y + h > SW.H as f32 - SSR(8.0) {
        y = SW.tip_y as f32 - h - SSR(8.0);
    }
    r_panel(x, y, w, h, SSR(7.0), c.pop, c.pop_border, c.shadow, SSR(3.0), SSR(8.0));
    text_draw(FONT_TEXT, fs, x + pad, text_baseline(FONT_TEXT, fs, y + h * 0.5), s, -1, t.text);
}

pub unsafe fn draw_popover(t: &Theme, c: &SColors) {
    let it = &k_sitems[SW.rows[SW.pop_row as usize].item as usize];
    let x = SW.pop_x;
    let y = SW.pop_y - ((1.0 - ease_out_cubic(SW.pop_t)) * SSR(6.0) + 0.5).floor();
    let w = SW.pop_w;
    let h = SW.pop_h;
    r_panel(x, y, w, h, SSR(10.0), c.pop, c.pop_border, c.shadow, SSR(6.0), SSR(14.0));
    let sel = sval(it.id);
    let fs = SSC(13.5);
    for k in 0..choice_count(it) {
        let iy = y + SSR(5.0) + SSR(32.0) * k as f32;
        let cy = iy + SSR(16.0);
        if SW.pop_hover == k {
            r_rect(x + SSR(5.0), iy, w - SSR(10.0), SSR(32.0), c.pop_hover, SSR(6.0));
        }
        if k == sel {
            text_draw_icon(0xE73E, SSC(12.0), x + SSR(5.0) + SSR(16.0), cy, t.text);
        }
        text_draw(FONT_TEXT, fs, x + SSR(5.0) + SSR(32.0), text_baseline(FONT_TEXT, fs, cy), ss(it.opt[k as usize]), -1, t.text);
    }
}

pub unsafe fn draw_recorder(t: &Theme, c: &SColors) {
    r_rect(0.0, 0.0, SW.W as f32, SW.H as f32, c.overlay, 0.0);
    let pw = SSR(460.0);
    let ph = SSR(260.0);
    let px = ((SW.W as f32 - pw) * 0.5).floor();
    let py = ((SW.H as f32 - ph) * 0.5 + (1.0 - ease_out_cubic(SW.rec_t)) * SSR(12.0)).floor();
    box_set(&mut SW.rec_panel, px, py, pw, ph);
    r_panel(px, py, pw, ph, SSR(16.0), if t.dark { RGBA(36, 36, 39, 255) } else { RGBA(252, 252, 253, 255) }, c.pop_border, c.shadow, SSR(12.0), SSR(26.0));

    text_draw(FONT_TEXT, SSC(16.0), px + SSR(26.0), py + SSR(42.0), tr!("Задайте сочетание…", "Set Hotkey…"), -1, t.faint);

    let mut tk: [KeyTok; 5] = zeroed();
    let mut n = 0;
    let rec = SW.rec;
    let live = (rec.held != 0 || rec.key != 0) && !rec.used;
    if live {
        let h = Hotkey { mods: rec_generic(rec.held), vk: 0 };
        n = hk_tokens(h, &mut tk);
    } else if rec.has {
        n = hk_tokens(rec.hk, &mut tk);
    }
    let kcy = py + SSR(122.0);
    let kfs = SSC(20.0);
    let kh = SSR(54.0);
    let kpad = SSR(16.0);
    let kgap = SSR(10.0);
    let problem = if rec.has { hotkey_problem(rec.hk) } else { None };
    if n != 0 {
        let kw = keycaps_width(&tk[..n as usize], FONT_TEXT_SEMIBOLD, kfs, kh, kpad, kgap);
        let fg = if problem.is_some() && !live { t.danger } else { t.text };
        draw_skeycaps(
            (px + (pw - kw) * 0.5).floor(),
            kcy,
            &tk[..n as usize],
            FONT_TEXT_SEMIBOLD,
            kfs,
            kh,
            kpad,
            kgap,
            SSR(12.0),
            if t.dark { RGBA(255, 255, 255, 24) } else { RGBA(0, 0, 0, 9) },
            c.cap_border,
            fg,
            t.faint,
        );
    } else {
        let s = tr!("Нажмите клавишу или сочетание", "Press a key or a shortcut");
        let fs = SSC(15.0);
        let w = text_width(FONT_TEXT, fs, s, -1);
        text_draw(FONT_TEXT, fs, (px + (pw - w) * 0.5).floor(), text_baseline(FONT_TEXT, fs, kcy), s, -1, t.faint);
    }
    let mut msg: *const WCHAR = null();
    let mut msg_col = t.faint;
    if !live && problem.is_some() && rec.has {
        msg = ss(*problem.unwrap());
        msg_col = t.danger;
    } else if n == 0 || live {
        msg = tr!(
            "Одна клавиша (Win, F1-F24, правый Ctrl…) или сочетание с Ctrl, Alt, Shift, Win",
            "A single key (Win, F1-F24, right Ctrl…) or a shortcut with Ctrl, Alt, Shift, Win"
        );
    }
    if !msg.is_null() {
        let fs = SSC(12.5);
        let w = min(text_width(FONT_TEXT, fs, msg, -1), pw - SSR(40.0));
        text_draw_fit(FONT_TEXT, fs, (px + (pw - w) * 0.5).floor(), text_baseline(FONT_TEXT, fs, py + SSR(180.0)), pw - SSR(40.0), msg, -1, msg_col, false);
    }

    let by = py + ph - SSR(56.0);
    let bcy = by + SSR(28.0);
    r_rect(px, by, pw, 1.0, c.sep, 0.0);
    draw_app_tile(px + SSR(22.0), bcy - SSR(11.0), SSR(22.0));
    let fs = SSC(14.0);
    text_draw(FONT_TEXT, fs, px + SSR(22.0) + SSR(22.0) + SSR(10.0), text_baseline(FONT_TEXT, fs, bcy), tr!("Открыть MixLauncher", "Open MixLauncher"), -1, t.text);

    let can_save = rec.has && problem.is_none();
    let mut xr = px + pw - SSR(14.0);
    for b in (0..2).rev() {
        let lbl = if b != 0 { tr!("Сохранить", "Save") } else { tr!("Отмена", "Cancel") };
        let mut cap: KeyTok = zeroed();
        wcopy(cap.label.as_mut_ptr(), 24, if b != 0 { w!("Enter") } else { w!("Esc") });
        cap.side = null();
        let cfs = SSC(12.0);
        let ch = SSR(22.0);
        let cw = keycap_w(&cap, FONT_TEXT, cfs, ch, SSR(6.0));
        let lw = text_width(FONT_TEXT, fs, lbl, -1);
        let bw = SSR(10.0) + lw + SSR(8.0) + cw + SSR(6.0);
        let bh = SSR(34.0);
        let bx = xr - bw;
        let byy = (bcy - bh * 0.5).floor();
        box_set(&mut SW.rec_btn[b], bx, byy, bw, bh);
        let hov = SW.hover.type_ == if b != 0 { HT_RECSAVE } else { HT_RECCANCEL };
        let enabled = b == 0 || can_save;
        if b != 0 && can_save {
            r_rect(bx, byy, bw, bh, color_alpha(t.accent, if hov { 0.32 } else { 0.22 }), SSR(8.0));
        } else if hov && enabled {
            r_rect(bx, byy, bw, bh, c.ctl, SSR(8.0));
        }
        let col = if !enabled { t.faint } else { t.text };
        text_draw(FONT_TEXT, fs, bx + SSR(10.0), text_baseline(FONT_TEXT, fs, bcy), lbl, -1, col);
        draw_skeycaps(
            bx + SSR(10.0) + lw + SSR(8.0),
            bcy,
            core::slice::from_ref(&cap),
            FONT_TEXT,
            cfs,
            ch,
            SSR(6.0),
            0.0,
            SSR(5.0),
            0,
            c.cap_border,
            if !enabled { t.faint } else { t.dim },
            t.faint,
        );
        xr = bx - SSR(6.0);
    }
}

pub unsafe fn sb_hot() -> bool {
    SW.hover.type_ != HT_WINCLOSE && SW.hover.type_ != HT_WINMIN && SW.mx >= SW.W - SSR(16.0) as i32 && SW.my >= top_h() as i32 && SW.my < SW.H
}

pub unsafe fn row_hover_target() -> i32 {
    if SW.hover.type_ != HT_ROW || SW.pop_open || SW.rec.open || SW.hover.idx < 0 || SW.hover.idx >= SW.nrows {
        return -1;
    }
    let it = &k_sitems[SW.rows[SW.hover.idx as usize].item as usize];
    if it.kind == SK_TOGGLE && !item_locked(it) { SW.hover.idx } else { -1 }
}
