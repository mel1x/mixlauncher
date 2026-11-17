// config.ini: one table (k_cfg) gives every setting its key, type, default, range and comment.

use crate::*;
use core::mem::offset_of;

pub const BACKDROP_BLUR: i32 = 0;
pub const BACKDROP_ACRYLIC: i32 = 1;
pub const BACKDROP_MICA: i32 = 2;
pub const BACKDROP_SOLID: i32 = 3;

// STYLE_START and STYLE_WIN10 lay the launcher out like the Windows 11 and Windows 10 Start menus.
pub const STYLE_STANDARD: i32 = 0;
pub const STYLE_RAYCAST: i32 = 1;
pub const STYLE_FLUENT: i32 = 2;
pub const STYLE_COMPACT: i32 = 3;
pub const STYLE_CAPSULE: i32 = 4;
pub const STYLE_START: i32 = 5;
pub const STYLE_WIN10: i32 = 6;
pub const STYLE__COUNT: i32 = 7;
pub const STYLES_START_MENU: i32 = (1 << STYLE_START) | (1 << STYLE_WIN10); // always at the taskbar

pub const POS_CENTER: i32 = 0;
pub const POS_BOTTOM_LEFT: i32 = 1;
pub const POS_BOTTOM_CENTER: i32 = 2;
pub const POS__COUNT: i32 = 3;

pub const TBI_DIAMOND: i32 = 0;
pub const TBI_GRID: i32 = 1;
pub const TBI_PETALS: i32 = 2;
pub const TBI_LAYERS: i32 = 3;
pub const TBI_DOTS: i32 = 4;
pub const TBI_CUSTOM: i32 = 5;
pub const TBI__COUNT: i32 = 6;

pub const HK_CTRL: u8 = 1;
pub const HK_ALT: u8 = 2;
pub const HK_SHIFT: u8 = 4;
pub const HK_WIN: u8 = 8;
pub const VK_ANYWIN: u32 = 0x07; // unassigned VK code, used for "either Win key"

#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq)]
pub struct Hotkey {
    pub mods: u8,
    pub vk: u8,
}

#[repr(C)]
pub struct Config {
    pub hotkey: [WCHAR; 64],
    pub hk: Hotkey, // parsed from hotkey
    pub language: i32, // 0 auto, 1 ru, 2 en
    pub theme: i32,    // 0 auto, 1 dark, 2 light
    pub background: i32,
    pub style: i32,
    pub position: i32,
    pub match_highlight: bool,
    pub width: i32,
    pub rows: i32,
    pub scale: i32, // the launcher's size in percent of the system's
    pub start10_width: i32,
    pub start10_height: i32, // the Windows 10 style's size (logical px)
    pub max_apps: i32,
    pub file_min_chars: i32,
    pub everything_filter: [WCHAR; 512],
    pub hide_uninstallers: bool,
    pub keep_query_seconds: i32,
    pub update_check: bool,
    pub taskbar_button: bool,
    pub taskbar_replace_icon: bool,
    pub taskbar_icon: i32,
    pub taskbar_icon_path: [WCHAR; MAX_PATH],
    pub taskbar_search: bool,
    pub media_widget: bool,
    pub media_position: i32, // 0 left, 1 right
    pub media_controls: bool,
    pub media_hide_paused: bool,
    pub media_marquee: bool,
    pub reduced_motion: bool,
    pub animations: bool,
    pub anim_speed: i32,
    pub anim_open_ms: i32,
    pub anim_close_ms: i32,
    pub anim_open_scale: i32,
    pub anim_cascade: i32,
    pub anim_row_ms: i32,
    pub anim_stagger_ms: i32,
    pub anim_select_ms: i32,
    pub anim_scroll_ms: i32,
    pub anim_menu_ms: i32,
    pub anim_page_ms: i32,
}

pub static mut g_cfg: Config = unsafe { zeroed() };

// Fields in config.ini order.
pub const CF_NONE: i32 = 0;
pub const CF_HOTKEY: i32 = 1;
pub const CF_LANGUAGE: i32 = 2;
pub const CF_THEME: i32 = 3;
pub const CF_BACKDROP: i32 = 4;
pub const CF_STYLE: i32 = 5;
pub const CF_POSITION: i32 = 6;
pub const CF_MATCH_HIGHLIGHT: i32 = 7;
pub const CF_WIDTH: i32 = 8;
pub const CF_ROWS: i32 = 9;
pub const CF_SCALE: i32 = 10;
pub const CF_START10_WIDTH: i32 = 11;
pub const CF_START10_HEIGHT: i32 = 12;
pub const CF_MAX_APPS: i32 = 13;
pub const CF_FILE_MIN_CHARS: i32 = 14;
pub const CF_EVERYTHING_FILTER: i32 = 15;
pub const CF_HIDE_UNINSTALLERS: i32 = 16;
pub const CF_KEEP_QUERY: i32 = 17;
pub const CF_UPDATE_CHECK: i32 = 18;
pub const CF_TASKBAR_BUTTON: i32 = 19;
pub const CF_TASKBAR_REPLACE_ICON: i32 = 20;
pub const CF_TASKBAR_ICON: i32 = 21;
pub const CF_TASKBAR_ICON_PATH: i32 = 22;
pub const CF_TASKBAR_SEARCH: i32 = 23;
pub const CF_MEDIA_WIDGET: i32 = 24;
pub const CF_MEDIA_POSITION: i32 = 25;
pub const CF_MEDIA_CONTROLS: i32 = 26;
pub const CF_MEDIA_HIDE_PAUSED: i32 = 27;
pub const CF_MEDIA_MARQUEE: i32 = 28;
pub const CF_REDUCED_MOTION: i32 = 29;
pub const CF_ANIMATIONS: i32 = 30;
pub const CF_ANIM_SPEED: i32 = 31;
pub const CF_ANIM_OPEN_MS: i32 = 32;
pub const CF_ANIM_CLOSE_MS: i32 = 33;
pub const CF_ANIM_OPEN_SCALE: i32 = 34;
pub const CF_ANIM_CASCADE: i32 = 35;
pub const CF_ANIM_ROW_MS: i32 = 36;
pub const CF_ANIM_STAGGER_MS: i32 = 37;
pub const CF_ANIM_SELECT_MS: i32 = 38;
pub const CF_ANIM_SCROLL_MS: i32 = 39;
pub const CF_ANIM_MENU_MS: i32 = 40;
pub const CF_ANIM_PAGE_MS: i32 = 41;
pub const CF__COUNT: i32 = 42;
pub const CF_ANIM_FIRST: i32 = CF_ANIM_SPEED;
pub const CF_ANIM_LAST: i32 = CF_ANIM_PAGE_MS;

pub const CK_BOOL: i32 = 0;
pub const CK_INT: i32 = 1;
pub const CK_ENUM: i32 = 2;
pub const CK_TEXT: i32 = 3;

pub struct CfgField {
    pub key: &'static str,
    pub kind: i32,
    pub off: usize, // in Config
    pub def: i32,
    pub lo: i32,
    pub hi: i32, // CK_INT: the range; CK_ENUM: hi names; CK_TEXT: hi WCHARs of room
    pub names: &'static [&'static str], // CK_ENUM
    pub text: &'static str,             // CK_TEXT: the default
    pub ru: Option<&'static str>,       // the comment above the key in config.ini, lines split by \n
    pub en: &'static str,
    pub gap: bool, // a blank line before it
}

pub static k_lang_names: [&str; 3] = ["auto", "ru", "en"];
pub static k_theme_names: [&str; 3] = ["auto", "dark", "light"];
pub static k_backdrop_names: [&str; 4] = ["blur", "acrylic", "mica", "solid"];
pub static k_style_names: [&str; STYLE__COUNT as usize] = ["standard", "raycast", "fluent", "compact", "capsule", "windows", "windows10"];
pub static k_pos_names: [&str; POS__COUNT as usize] = ["center", "bottom-left", "bottom-center"];
pub static k_tbi_names: [&str; TBI__COUNT as usize] = ["diamond", "grid", "petals", "layers", "dots", "custom"];
pub static k_side_names: [&str; 2] = ["left", "right"];

const fn fb(key: &'static str, off: usize, d: i32, gap: bool, ru: Option<&'static str>, en: &'static str) -> CfgField {
    CfgField { key, kind: CK_BOOL, off, def: d, lo: 0, hi: 1, names: &[], text: "", ru, en, gap }
}
const fn fi(key: &'static str, off: usize, d: i32, lo: i32, hi: i32, gap: bool, ru: Option<&'static str>, en: &'static str) -> CfgField {
    CfgField { key, kind: CK_INT, off, def: d, lo, hi, names: &[], text: "", ru, en, gap }
}
const fn fe(key: &'static str, off: usize, d: i32, names: &'static [&'static str], gap: bool, ru: Option<&'static str>, en: &'static str) -> CfgField {
    CfgField { key, kind: CK_ENUM, off, def: d, lo: 0, hi: names.len() as i32, names, text: "", ru, en, gap }
}
const fn ft(key: &'static str, off: usize, room: usize, d: &'static str, gap: bool, ru: Option<&'static str>, en: &'static str) -> CfgField {
    CfgField { key, kind: CK_TEXT, off, def: 0, lo: 0, hi: room as i32, names: &[], text: d, ru, en, gap }
}

macro_rules! off {
    ($f:ident) => {
        offset_of!(Config, $f)
    };
}

pub static k_cfg: [CfgField; CF__COUNT as usize] = [
    fb("", 0, 0, false, None, ""),
    ft("hotkey", off!(hotkey), 64, "win", true,
       Some("Клавиша или сочетание, которое открывает и закрывает лаунчер. Прежнее действие сочетания\n\
             отключается (Win больше не открывает «Пуск», Win+D не сворачивает окна).\n\
             Примеры: win, alt+space, ctrl+space, win+d, f13, rctrl (одиночное нажатие правого Ctrl)"),
       "The key or shortcut that opens and closes the launcher. Whatever it did before is turned off\n\
        (Win no longer opens Start, Win+D no longer shows the desktop).\n\
        Examples: win, alt+space, ctrl+space, win+d, f13, rctrl (a tap of the right Ctrl key)"),
    fe("language", off!(language), 0, &k_lang_names, true, Some("Язык: auto (как в Windows) | ru | en"), "Language: auto (follows Windows) | ru | en"),
    fe("theme", off!(theme), 0, &k_theme_names, true, Some("Тема: auto | dark | light"), "Theme: auto | dark | light"),
    fe("background", off!(background), BACKDROP_MICA, &k_backdrop_names, true,
       Some("Фон окна: blur (полупрозрачное стекло, как в macOS) | acrylic (акрил, как в Windows 11) |\n\
             mica (оттенок обоев, как у окон Windows 11) | solid (сплошной).\n\
             Стекло и акрил всё время следят за тем, что под окном, и нагружают видеокарту больше"),
       "Window background: blur (translucent glass, macOS-like) | acrylic (Windows 11 acrylic) |\n\
        mica (a tint of the wallpaper, like Windows 11 windows) | solid.\n\
        Glass and acrylic keep following what is behind the window and use more of the GPU"),
    fe("style", off!(style), STYLE_STANDARD, &k_style_names, true,
       Some("Стиль лаунчера: standard | raycast | fluent | compact | capsule (поиск и результаты отдельными панелями) |\n\
             windows | windows10 (как меню Пуск Windows 11 и 10)"),
       "Launcher style: standard | raycast | fluent | compact | capsule (the query and the results as separate panels) |\n\
        windows | windows10 (like the Windows 11 and 10 Start menus)"),
    fe("position", off!(position), POS_CENTER, &k_pos_names, false,
       Some("Положение: center (по центру экрана) | bottom-left (слева снизу, как Пуск) | bottom-center (снизу по центру).\n\
             Стили windows и windows10 всегда снизу."),
       "Position: center (screen center) | bottom-left (like Start on the left) | bottom-center (like Start in the center).\n\
        The windows and windows10 styles are always at the bottom."),
    fb("match_highlight", off!(match_highlight), 1, false, Some("Подсвечивать совпадения с запросом в названиях (1/0)"), "Highlight the query matches in names (1/0)"),
    fi("width", off!(width), 720, 480, 1600, true,
       Some("Ширина окна в логических пикселях и количество видимых строк списка"), "Window width in logical pixels and number of visible list rows"),
    fi("rows", off!(rows), 9, 4, 20, false, None, ""),
    fi("scale", off!(scale), 100, 70, 150, true,
       Some("Масштаб лаунчера в процентах: 100 - обычный размер, больше - крупнее"), "Launcher scale in percent: 100 is the normal size, more is bigger"),
    fi("start10_width", off!(start10_width), 648, 300, 4000, false,
       Some("Размер стиля windows10 (меняется и перетаскиванием края окна)"), "Size of the windows10 style (also set by dragging the window edge)"),
    fi("start10_height", off!(start10_height), 608, 420, 2000, false, None, ""),
    fi("max_apps", off!(max_apps), 8, 1, 50, true,
       Some("Сколько приложений показывать при поиске (файлы из Everything подгружаются при прокрутке)"),
       "How many apps to show while searching (files from Everything load as you scroll)"),
    fi("file_min_chars", off!(file_min_chars), 2, 1, 10, true, Some("Минимальная длина запроса для поиска файлов"), "Minimum query length for file search"),
    ft("everything_filter", off!(everything_filter), 512, "", true,
       Some("Фильтр Everything, добавляется к каждому запросу файлов. Пример: !C:\\Windows\\ !\\AppData\\"),
       "Everything filter appended to every file query. Example: !C:\\Windows\\ !\\AppData\\"),
    fb("hide_uninstallers", off!(hide_uninstallers), 1, true, Some("Скрывать деинсталляторы из списка приложений (1/0)"), "Hide uninstallers from the app list (1/0)"),
    fi("keep_query_seconds", off!(keep_query_seconds), 60, 0, 24 * 3600, true,
       Some("Через сколько секунд после закрытия окна сбрасывать введённый запрос"), "Seconds after closing before the typed query is cleared"),
    fb("update_check", off!(update_check), 1, true, Some("Проверять обновления на GitHub раз в несколько часов (1/0)"), "Check GitHub for updates every few hours (1/0)"),
    fb("taskbar_button", off!(taskbar_button), 1, true,
       Some("Кнопка Пуск на панели задач Windows 11 открывает лаунчер (1/0)"), "The Windows 11 taskbar Start button opens the launcher (1/0)"),
    fb("taskbar_replace_icon", off!(taskbar_replace_icon), 1, false,
       Some("Рисовать на ней наш значок вместо значка Windows (1/0)"), "Draw our icon on it instead of the Windows logo (1/0)"),
    fe("taskbar_icon", off!(taskbar_icon), TBI_DIAMOND, &k_tbi_names, false,
       Some("Её значок: diamond (ромб) | grid (сетка) | petals (лепестки) | layers (слои) | dots (точки) | \
             custom (свой файл PNG/ICO/JPG из taskbar_icon_path)"),
       "Its icon: diamond | grid | petals | layers | dots | custom (a PNG/ICO/JPG file from taskbar_icon_path)"),
    ft("taskbar_icon_path", off!(taskbar_icon_path), MAX_PATH, "", false, None, ""),
    fb("taskbar_search", off!(taskbar_search), 0, false,
       Some("Клик по поиску на панели задач Windows 10/11 открывает лаунчер (1/0)"), "Clicking the search on the Windows 10/11 taskbar opens the launcher (1/0)"),
    fb("media_widget", off!(media_widget), 1, true,
       Some("«Сейчас играет» на панели задач: обложка, название и кнопки управления музыкой и видео (1/0)"),
       "Now playing on the taskbar: cover, title and playback buttons for music and video (1/0)"),
    fe("media_position", off!(media_position), 0, &k_side_names, false,
       Some("Где: left (слева) | right (справа, у значков в трее). При значках, выровненных по левому краю, всегда справа"),
       "Where: left | right (next to the tray icons). Always right when taskbar icons are aligned left"),
    fb("media_controls", off!(media_controls), 1, false,
       Some("Кнопки «назад», «пауза», «вперёд» (1/0) и скрывать виджет, пока ничего не играет (1/0)"),
       "Previous / pause / next buttons (1/0) and hiding the widget while nothing plays (1/0)"),
    fb("media_hide_paused", off!(media_hide_paused), 0, false, None, ""),
    fb("media_marquee", off!(media_marquee), 1, false,
       Some("Прокручивать название и исполнителя, если они не помещаются (1/0)"), "Scroll the title and artist when they do not fit (1/0)"),
    fb("reduced_motion", off!(reduced_motion), 0, true,
       Some("Уменьшение движения (1/0): без анимаций и задержек, всё происходит сразу. Настройки анимаций ниже сохраняются."),
       "Reduced motion (1/0): no animations or delays, everything happens at once. The animation settings below are kept."),
    fb("animations", off!(animations), 1, true,
       Some("Анимации открытия и закрытия (1/0). Выключаются и системной настройкой «Эффекты анимации»."),
       "Open/close animations (1/0). Also off when Windows \"Animation effects\" are disabled."),
    fi("anim_speed", off!(anim_speed), 100, 30, 400, true,
       Some("Тонкая настройка анимаций (миллисекунды). anim_speed - общая скорость в процентах (200 = вдвое быстрее),\n\
             anim_open_scale - с какого масштаба (в процентах) проявляется окно, anim_cascade - каскад строк (1/0).\n\
             Выделение и прокрутка движутся на пружине: значение - примерное время, за которое она доезжает."),
       "Fine-tuning of animations (milliseconds). anim_speed is a global speed in percent (200 = twice as fast),\n\
        anim_open_scale is the scale (percent) the window opens from, anim_cascade cascades the rows in (1/0).\n\
        Selection and scrolling move on a spring: the value is roughly the time it takes to arrive."),
    fi("anim_open_ms", off!(anim_open_ms), 160, 0, 1000, false, None, ""),
    fi("anim_close_ms", off!(anim_close_ms), 90, 0, 1000, false, None, ""),
    fi("anim_open_scale", off!(anim_open_scale), 97, 80, 100, false, None, ""),
    fi("anim_cascade", off!(anim_cascade), 1, 0, 1, false, None, ""),
    fi("anim_row_ms", off!(anim_row_ms), 200, 0, 1000, false, None, ""),
    fi("anim_stagger_ms", off!(anim_stagger_ms), 12, 0, 60, false, None, ""),
    fi("anim_select_ms", off!(anim_select_ms), 120, 0, 600, false, None, ""),
    fi("anim_scroll_ms", off!(anim_scroll_ms), 160, 0, 600, false, None, ""),
    fi("anim_menu_ms", off!(anim_menu_ms), 140, 0, 600, false, None, ""),
    fi("anim_page_ms", off!(anim_page_ms), 220, 0, 800, false, None, ""),
];

pub unsafe fn cfg_ptr(id: i32) -> *mut u8 {
    (&raw mut g_cfg as *mut u8).add(k_cfg[id as usize].off)
}

// A bool, int or enum field as an int.
pub unsafe fn cfg_get(id: i32) -> i32 {
    let k = k_cfg[id as usize].kind;
    if k == CK_BOOL {
        return *(cfg_ptr(id) as *const bool) as i32;
    }
    if k == CK_TEXT {
        return 0;
    }
    *(cfg_ptr(id) as *const i32)
}

pub unsafe fn cfg_put(id: i32, v: i32) {
    let f = &k_cfg[id as usize];
    if f.kind == CK_BOOL {
        *(cfg_ptr(id) as *mut bool) = v != 0;
    } else if f.kind == CK_INT {
        *(cfg_ptr(id) as *mut i32) = clamp(v, f.lo, f.hi);
    } else if f.kind == CK_ENUM {
        *(cfg_ptr(id) as *mut i32) = clamp(v, 0, f.hi - 1);
    }
}

fn eq_i(a: &[u8], b: &str) -> bool {
    a.eq_ignore_ascii_case(b.as_bytes())
}

unsafe fn cstr<'a>(s: *const u8) -> &'a [u8] {
    core::slice::from_raw_parts(s, strlen(s))
}

pub unsafe fn cfg_find(key: *const u8) -> i32 {
    let k = cstr(key);
    for id in 1..CF__COUNT {
        if eq_i(k, k_cfg[id as usize].key) {
            return id;
        }
    }
    CF_NONE
}

pub unsafe fn parse_bool(v: *const u8) -> bool {
    let c = *v;
    c == b'1' || c == b'y' || c == b'Y' || c == b't' || c == b'T' || eq_i(cstr(v), "on")
}

// Sets a field from its config.ini text. An unknown enum name leaves the field as it is.
pub unsafe fn cfg_parse(id: i32, v: *const u8) {
    let f = &k_cfg[id as usize];
    match f.kind {
        CK_BOOL => cfg_put(id, parse_bool(v) as i32),
        CK_INT => cfg_put(id, atoi(v)),
        CK_ENUM => {
            for i in 0..f.hi {
                if eq_i(cstr(v), f.names[i as usize]) {
                    cfg_put(id, i);
                }
            }
        }
        CK_TEXT => {
            utf8_to_w(v, -1, cfg_ptr(id) as *mut WCHAR, f.hi);
        }
        _ => {}
    }
}

pub unsafe fn cfg_format(id: i32) -> String {
    let f = &k_cfg[id as usize];
    let v = cfg_get(id);
    match f.kind {
        CK_BOOL | CK_INT => format!("{}", v),
        CK_ENUM => f.names[clamp(v, 0, f.hi - 1) as usize].to_string(),
        _ => {
            let mut out = [0u8; 1100];
            w_to_utf8(cfg_ptr(id) as *const WCHAR, -1, out.as_mut_ptr(), out.len() as i32);
            String::from_utf8_lossy(cstr(out.as_ptr())).into_owned()
        }
    }
}

// The Start layouts always sit at the taskbar: center falls back to bottom center.
pub unsafe fn launcher_position() -> i32 {
    let p = clamp(g_cfg.position, 0, POS__COUNT - 1);
    if ((STYLES_START_MENU >> g_cfg.style) & 1) != 0 && p == POS_CENTER { POS_BOTTOM_CENTER } else { p }
}

// ---- keys and hotkeys: "ctrl+alt+k", "win", "rctrl"

pub struct KeyName {
    pub vk: u8,
    pub name: &'static str,
    pub label: &'static str,
}

const fn kn(vk: u16, name: &'static str, label: &'static str) -> KeyName {
    KeyName { vk: vk as u8, name, label }
}

pub static k_key_names: [KeyName; 49] = [
    kn(VK_ANYWIN as u16, "win", "Win"), kn(VK_CONTROL, "ctrl", "Ctrl"), kn(VK_MENU, "alt", "Alt"), kn(VK_SHIFT, "shift", "Shift"),
    kn(VK_LWIN, "lwin", "Win"), kn(VK_RWIN, "rwin", "Win"), kn(VK_LCONTROL, "lctrl", "Ctrl"), kn(VK_RCONTROL, "rctrl", "Ctrl"),
    kn(VK_LMENU, "lalt", "Alt"), kn(VK_RMENU, "ralt", "Alt"), kn(VK_LSHIFT, "lshift", "Shift"), kn(VK_RSHIFT, "rshift", "Shift"),
    kn(VK_SPACE, "space", "Space"), kn(VK_RETURN, "enter", "Enter"), kn(VK_TAB, "tab", "Tab"), kn(VK_ESCAPE, "esc", "Esc"),
    kn(VK_BACK, "backspace", "Backspace"), kn(VK_DELETE, "delete", "Del"), kn(VK_INSERT, "insert", "Ins"),
    kn(VK_HOME, "home", "Home"), kn(VK_END, "end", "End"), kn(VK_PRIOR, "pageup", "PgUp"), kn(VK_NEXT, "pagedown", "PgDn"),
    kn(VK_UP, "up", "\u{2191}"), kn(VK_DOWN, "down", "\u{2193}"), kn(VK_LEFT, "left", "\u{2190}"), kn(VK_RIGHT, "right", "\u{2192}"),
    kn(VK_CAPITAL, "capslock", "Caps Lock"), kn(VK_PAUSE, "pause", "Pause"), kn(VK_SCROLL, "scrolllock", "Scroll Lock"),
    kn(VK_SNAPSHOT, "printscreen", "PrtSc"), kn(VK_APPS, "menu", "Menu"), kn(VK_NUMLOCK, "numlock", "Num Lock"),
    kn(VK_OEM_3, "`", "`"), kn(VK_OEM_MINUS, "-", "-"), kn(VK_OEM_PLUS, "=", "="), kn(VK_OEM_4, "[", "["),
    kn(VK_OEM_6, "]", "]"), kn(VK_OEM_5, "\\", "\\"), kn(VK_OEM_1, ";", ";"), kn(VK_OEM_7, "'", "'"),
    kn(VK_OEM_COMMA, ",", ","), kn(VK_OEM_PERIOD, ".", "."), kn(VK_OEM_2, "/", "/"),
    kn(VK_MULTIPLY, "num*", "Num *"), kn(VK_ADD, "numplus", "Num +"), kn(VK_SUBTRACT, "num-", "Num -"),
    kn(VK_DECIMAL, "num.", "Num ."), kn(VK_DIVIDE, "num/", "Num /"),
];

pub fn key_entry(vk: u32) -> Option<&'static KeyName> {
    k_key_names.iter().find(|k| k.vk as u32 == vk)
}

pub fn vk_mod_bit(vk: u32) -> u8 {
    match vk as u16 {
        VK_CONTROL | VK_LCONTROL | VK_RCONTROL => HK_CTRL,
        VK_MENU | VK_LMENU | VK_RMENU => HK_ALT,
        VK_SHIFT | VK_LSHIFT | VK_RSHIFT => HK_SHIFT,
        VK_LWIN | VK_RWIN => HK_WIN,
        _ if vk == VK_ANYWIN => HK_WIN,
        _ => 0,
    }
}

// The bound key matches the pressed one; a generic modifier matches either side.
pub fn hk_vk_match(bind: u32, vk: u32) -> bool {
    if bind == vk {
        return true;
    }
    let vk = vk as u16;
    match bind as u16 {
        VK_CONTROL => vk == VK_LCONTROL || vk == VK_RCONTROL,
        VK_MENU => vk == VK_LMENU || vk == VK_RMENU,
        VK_SHIFT => vk == VK_LSHIFT || vk == VK_RSHIFT,
        _ if bind == VK_ANYWIN => vk == VK_LWIN || vk == VK_RWIN,
        _ => false,
    }
}

pub fn key_by_name(name: &[u8]) -> u32 {
    for k in &k_key_names {
        if k.name.as_bytes() == name {
            return k.vk as u32;
        }
    }
    let n = name.len();
    if n == 1 && (name[0].is_ascii_lowercase() || name[0].is_ascii_digit()) {
        return name[0].to_ascii_uppercase() as u32;
    }
    let atoi_ = |s: &[u8]| -> i32 {
        let mut v = 0i32;
        for &c in s {
            if !c.is_ascii_digit() {
                break;
            }
            v = v * 10 + (c - b'0') as i32;
        }
        v
    };
    if n >= 2 && n <= 3 && name[0] == b'f' && name[1].is_ascii_digit() {
        let f = atoi_(&name[1..]);
        if (1..=24).contains(&f) {
            return VK_F1 as u32 + f as u32 - 1;
        }
    }
    if n == 4 && name.starts_with(b"num") && name[3].is_ascii_digit() {
        return VK_NUMPAD0 as u32 + (name[3] - b'0') as u32;
    }
    if n == 4 && name.starts_with(b"vk") {
        let s = core::str::from_utf8(&name[2..]).unwrap_or("");
        // strtol: leading hex digits
        let digits: String = s.chars().take_while(|c| c.is_ascii_hexdigit()).collect();
        let v = i32::from_str_radix(&digits, 16).unwrap_or(0);
        if v > 0 && v < 0xFF {
            return v as u32;
        }
    }
    match name {
        b"control" => VK_CONTROL as u32,
        b"escape" => VK_ESCAPE as u32,
        b"tilde" => VK_OEM_3 as u32,
        b"del" => VK_DELETE as u32,
        _ => 0,
    }
}

pub fn key_name(vk: u32) -> String {
    if let Some(k) = key_entry(vk) {
        k.name.to_string()
    } else if (b'A' as u32..=b'Z' as u32).contains(&vk) || (b'0' as u32..=b'9' as u32).contains(&vk) {
        ((vk as u8).to_ascii_lowercase() as char).to_string()
    } else if vk >= VK_F1 as u32 && vk <= VK_F24 as u32 {
        format!("f{}", vk - VK_F1 as u32 + 1)
    } else if vk >= VK_NUMPAD0 as u32 && vk <= VK_NUMPAD9 as u32 {
        format!("num{}", vk - VK_NUMPAD0 as u32)
    } else {
        format!("vk{:02x}", vk & 0xFF)
    }
}

// What a keycap shows; *side is "L" or "R" for a sided modifier.
pub unsafe fn key_label(vk: u32, out: *mut WCHAR, cap: i32, side: *mut *const WCHAR) {
    *side = null();
    let v = vk as u16;
    if v == VK_LCONTROL || v == VK_LMENU || v == VK_LSHIFT || v == VK_LWIN {
        *side = w!("L");
    }
    if v == VK_RCONTROL || v == VK_RMENU || v == VK_RSHIFT || v == VK_RWIN {
        *side = w!("R");
    }
    if let Some(k) = key_entry(vk) {
        wfmt!(out, cap, "{}", k.label);
    } else if (b'A' as u32..=b'Z' as u32).contains(&vk) || (b'0' as u32..=b'9' as u32).contains(&vk) {
        wfmt!(out, cap, "{}", vk as u8 as char);
    } else if vk >= VK_F1 as u32 && vk <= VK_F24 as u32 {
        wfmt!(out, cap, "F{}", vk - VK_F1 as u32 + 1);
    } else if vk >= VK_NUMPAD0 as u32 && vk <= VK_NUMPAD9 as u32 {
        wfmt!(out, cap, "Num {}", vk - VK_NUMPAD0 as u32);
    } else {
        let sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
        let lp = (((sc & 0xFF) << 16) | if (sc & 0xE000) != 0 { 1 << 24 } else { 0 }) as i32;
        if sc == 0 || GetKeyNameTextW(lp, out, cap) <= 0 {
            wfmt!(out, cap, "VK {:02X}", vk & 0xFF);
        }
    }
    *out.add(cap as usize - 1) = 0;
}

pub unsafe fn hotkey_parse(s: *const WCHAR, out: *mut Hotkey) -> bool {
    let mut buf = [0u8; 128];
    w_to_utf8(s, -1, buf.as_mut_ptr(), buf.len() as i32);
    let n = strlen(buf.as_ptr());
    let buf = &mut buf[..n];
    buf.make_ascii_lowercase();
    let mut h = Hotkey { mods: 0, vk: 0 };
    let mut last = 0u32;
    let mut cur = 0usize;
    loop {
        while cur < n && (buf[cur] == b' ' || buf[cur] == b'+') {
            cur += 1;
        }
        if cur >= n {
            break;
        }
        let tok = cur;
        while cur < n && buf[cur] != b'+' && buf[cur] != b' ' {
            cur += 1;
        }
        let vk = key_by_name(&buf[tok..cur]);
        if vk == 0 {
            return false;
        }
        if last != 0 {
            // every key but the last is a modifier
            let m = vk_mod_bit(last);
            if m == 0 {
                return false;
            }
            h.mods |= m;
        }
        last = vk;
    }
    if last == 0 {
        return false;
    }
    h.vk = last as u8;
    h.mods &= !vk_mod_bit(last);
    *out = h;
    true
}

pub unsafe fn hotkey_format(h: Hotkey, out: *mut WCHAR, cap: i32) {
    let s = format!(
        "{}{}{}{}{}",
        if h.mods & HK_CTRL != 0 { "ctrl+" } else { "" },
        if h.mods & HK_ALT != 0 { "alt+" } else { "" },
        if h.mods & HK_SHIFT != 0 { "shift+" } else { "" },
        if h.mods & HK_WIN != 0 { "win+" } else { "" },
        key_name(h.vk as u32)
    );
    wfmt!(out, cap, "{}", s);
}

// ---- config.ini

pub unsafe fn config_defaults() {
    zero(&raw mut g_cfg);
    for id in 1..CF__COUNT {
        let f = &k_cfg[id as usize];
        if f.kind == CK_TEXT {
            wfmt!(cfg_ptr(id) as *mut WCHAR, f.hi, "{}", f.text);
        } else {
            cfg_put(id, f.def);
        }
    }
    g_cfg.hk.vk = VK_ANYWIN as u8;
}

pub unsafe fn trim(mut s: *mut u8) -> *mut u8 {
    while *s == b' ' || *s == b'\t' {
        s = s.add(1);
    }
    let mut e = s.add(strlen(s));
    while e > s && (*e.sub(1) == b' ' || *e.sub(1) == b'\t' || *e.sub(1) == b'\r') {
        e = e.sub(1);
        *e = 0;
    }
    s
}

pub unsafe fn config_path(out: *mut WCHAR) {
    data_path(out, w!("config.ini"));
}

fn buf_comment(b: &mut Buf, text: &str) {
    for line in text.split('\n') {
        b.str("; ");
        b.str(line);
        b.str("\r\n");
    }
}

// Writes the whole file, with comments, in the UI language.
pub unsafe fn config_write_all() {
    let mut b = Buf::new();
    b.put(b"\xEF\xBB\xBF");
    buf_comment(
        &mut b,
        if g_lang_ru {
            "MixLauncher - настройки. Удобнее менять в окне настроек (кнопка «Настройки» в лаунчере или Ctrl+,)."
        } else {
            "MixLauncher settings. Easier to change in the settings window (Settings button in the launcher or Ctrl+,)."
        },
    );
    for id in 1..CF__COUNT {
        let f = &k_cfg[id as usize];
        let val = cfg_format(id);
        if f.gap {
            b.str("\r\n");
        }
        if let Some(ru) = f.ru {
            buf_comment(&mut b, if g_lang_ru { ru } else { f.en });
        }
        if !val.is_empty() {
            buf_printf!(b, "{} = {}\r\n", f.key, val);
        } else {
            buf_printf!(b, "{} =\r\n", f.key);
        }
    }
    let mut path = [0u16; MAX_PATH];
    config_path(path.as_mut_ptr());
    buf_save(&mut b, path.as_ptr());
}

pub unsafe fn lang_apply() {
    g_lang_ru = g_cfg.language == 1 || (g_cfg.language == 0 && primarylangid(GetUserDefaultUILanguage()) == LANG_RUSSIAN);
}

pub unsafe fn config_load() {
    config_defaults();
    let mut path = [0u16; MAX_PATH];
    config_path(path.as_mut_ptr());
    let text = read_file(path.as_ptr(), null_mut());
    if text.is_null() {
        // First run: reduced motion follows the Windows "Animation effects" setting.
        let mut anim: BOOL = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &mut anim as *mut BOOL as *mut c_void, 0);
        g_cfg.reduced_motion = anim == 0;
        config_write_all();
        return;
    }
    let mut old_win_key = -1;
    let mut old_backdrop = false;
    let mut has_scale = false;
    let mut cur = if utf8_bom(text) { text.add(3) } else { text };
    loop {
        let mut line = next_line(&mut cur);
        if line.is_null() {
            break;
        }
        line = trim(line);
        if *line == 0 || *line == b';' || *line == b'#' || *line == b'[' {
            continue;
        }
        let eq = strchr(line, b'=' as i32);
        if eq.is_null() {
            continue;
        }
        *eq = 0;
        let k = trim(line);
        let v = trim(eq.add(1));
        let id = cfg_find(k);
        if id != 0 {
            cfg_parse(id, v);
        }
        if id == CF_SCALE {
            has_scale = true;
        } else if eq_i(cstr(k), "win_key") {
            old_win_key = parse_bool(v) as i32; // before hotkey existed
        } else if eq_i(cstr(k), "backdrop") {
            old_backdrop = true; // blur was the default then: everyone starts on solid
        }
    }
    free(text as *mut c_void);
    lang_apply();
    if old_win_key == 1 {
        wcopy(g_cfg.hotkey.as_mut_ptr(), 64, w!("win"));
    }
    if !hotkey_parse(g_cfg.hotkey.as_ptr(), &raw mut g_cfg.hk) || g_cfg.hk.vk == 0 {
        wcopy(g_cfg.hotkey.as_mut_ptr(), 64, w!("win"));
        g_cfg.hk.mods = 0;
        g_cfg.hk.vk = VK_ANYWIN as u8;
    }
    // Before scale (2.1.2) solid was the default: everyone on it moves to Mica once.
    let to_mica = !has_scale && g_cfg.background == BACKDROP_SOLID;
    if to_mica {
        g_cfg.background = BACKDROP_MICA;
    }
    if old_win_key >= 0 || old_backdrop || to_mica {
        config_write_all();
    }
}

// Rewrites one key in the file, keeping everything else (comments, the user's own lines) as it is.
pub unsafe fn config_set(key: &str, value: &str) {
    let mut path = [0u16; MAX_PATH];
    config_path(path.as_mut_ptr());
    let text = read_file(path.as_ptr(), null_mut());
    let mut b = Buf::new();
    let mut found = false;
    let kl = key.len();
    let mut cur = text;
    if !text.is_null() {
        loop {
            let mut line = next_line(&mut cur);
            if line.is_null() {
                break;
            }
            if utf8_bom(line) {
                b.put(core::slice::from_raw_parts(line, 3));
                line = line.add(3);
            }
            let mut t = line;
            while *t == b' ' || *t == b'\t' {
                t = t.add(1);
            }
            let ts = cstr(t);
            if !found && ts.len() >= kl && ts[..kl].eq_ignore_ascii_case(key.as_bytes()) && {
                let c = *t.add(kl);
                c == b' ' || c == b'=' || c == b'\t'
            } {
                buf_printf!(b, "{} = {}\r\n", key, value);
                found = true;
            } else {
                b.put(cstr(line));
                b.str("\r\n");
            }
        }
    }
    free(text as *mut c_void);
    if !found {
        buf_printf!(b, "{} = {}\r\n", key, value);
    }
    buf_save(&mut b, path.as_ptr());
}

pub unsafe fn cfg_save(id: i32) {
    let val = cfg_format(id);
    config_set(k_cfg[id as usize].key, &val);
}

pub unsafe fn cfg_set(id: i32, v: i32) {
    cfg_put(id, v);
    cfg_save(id);
}
