// The hotkey: a low-level keyboard hook on a time-critical thread of its own. It swallows the bound key (and,
// for Win, masks the tap from the Start menu with a dummy vkE8), watches clicks outside the open launcher and
// on the taskbar's search, and keeps itself alive: a raw input watchdog re-installs a hook Windows dropped.

use crate::*;
use core::sync::atomic::{AtomicI32, AtomicPtr, Ordering::SeqCst};

// thread messages to the hook thread
const WM_HOOK_MASK: u32 = WM_APP + 100;
const WM_HOOK_REINSTALL: u32 = WM_APP + 101;
const WM_HOOK_MOUSE: u32 = WM_APP + 102;
const WM_HOOK_WATCHDOG: u32 = WM_APP + 103;
const WM_HOOK_VISIBLE: u32 = WM_APP + 104;
const WM_HOOK_SEARCH: u32 = WM_APP + 105;
pub const ML_INJECT_MAGIC: usize = 0x4D4C4E43; // our own injected keys

#[repr(C)]
pub struct Hook {
    pub tid: u32,
    pub hook: HHOOK,
    pub mouse_hook: HHOOK,
    pub watchdog: HWND,
    pub watchdog_on: bool,
    pub bind: AtomicI32,          // the Hotkey, mods << 8 | vk
    pub ui_visible: AtomicI32,
    pub search_clicks: AtomicI32, // the taskbar's search opens the launcher
    pub swallow_lup: bool,
    pub capture: AtomicPtr<c_void>, // the settings' hotkey recorder gets every key
    pub tap_down: bool,             // a modifier-only hotkey (Win, rctrl) is down; armed while nothing else came
    pub tap_armed: bool,
    pub tap_mouse: bool, // the mouse was used while it was down (Win, Alt): its release goes out masked
    pub tap_vk: u32,
    pub swallow_vk: u32,
    pub swallow_time: u32,
    pub toggle_pending: bool, // a tap toggles once our masking keys have gone through
    pub pending_vk: u32,
    pub fallback_timer: usize,
    pub hook_presses: u32, // presses of the bound key seen by the hook and by raw input
    pub raw_presses: u32,
    pub reinstalls: u32,
    pub prtsc_handoff: bool,
    pub prtsc_from: AtomicPtr<c_void>,
    pub prtsc_to: HWND,
}

pub static mut K: Hook = unsafe { zeroed() };

#[inline]
pub unsafe fn kk() -> &'static mut Hook {
    &mut *(&raw mut K)
}

#[derive(Clone, Copy)]
struct KeyTrace {
    time: u32,
    vk: u32,
    flags: u32,
    extra: usize,
    wp: WPARAM,
    what: u8,
}
static mut g_ktrace: [KeyTrace; 128] = unsafe { zeroed() };
static g_ktrace_n: AtomicI32 = AtomicI32::new(0);
pub static mut g_ktrace_on: bool = false;

unsafe fn ktrace(k: &KBDLLHOOKSTRUCT, wp: WPARAM, what: u8) {
    if !g_ktrace_on {
        return;
    }
    let i = g_ktrace_n.fetch_add(1, SeqCst);
    if i >= 128 {
        return;
    }
    let t = &mut (*(&raw mut g_ktrace))[i as usize];
    t.time = k.time;
    t.vk = k.vkCode;
    t.flags = k.flags;
    t.extra = k.dwExtraInfo;
    t.wp = wp;
    t.what = what;
}

pub unsafe fn ktrace_dump() {
    let n = min(g_ktrace_n.load(SeqCst), 128);
    for i in 0..n as usize {
        let t = (*(&raw const g_ktrace))[i];
        log_msg!(
            "{} t={} vk={:02x} {} flags={:02x} {} -> {}",
            if t.what == b'r' { "raw" } else { "key" },
            t.time,
            t.vk,
            if t.wp == WM_KEYDOWN as usize || t.wp == WM_SYSKEYDOWN as usize { "down" } else { "up  " },
            t.flags,
            if t.extra == ML_INJECT_MAGIC { "OURS" } else { "" },
            t.what as char
        );
    }
}

unsafe fn hook_bind() -> Hotkey {
    let b = K.bind.load(SeqCst);
    Hotkey { mods: (b >> 8) as u8, vk: b as u8 }
}

fn key_class(vk: u32) -> u32 {
    let m = vk_mod_bit(vk);
    if m != 0 { 0x100 | m as u32 } else { vk }
}

pub unsafe fn held_mods() -> u8 {
    let mut m = 0u8;
    if (GetAsyncKeyState(VK_CONTROL as i32) as u16 & 0x8000) != 0 {
        m |= HK_CTRL;
    }
    if (GetAsyncKeyState(VK_MENU as i32) as u16 & 0x8000) != 0 {
        m |= HK_ALT;
    }
    if (GetAsyncKeyState(VK_SHIFT as i32) as u16 & 0x8000) != 0 {
        m |= HK_SHIFT;
    }
    if ((GetAsyncKeyState(VK_LWIN as i32) | GetAsyncKeyState(VK_RWIN as i32)) as u16 & 0x8000) != 0 {
        m |= HK_WIN;
    }
    m
}

fn vk_extended(vk: u32) -> bool {
    let v = vk as u16;
    v == VK_LWIN || v == VK_RWIN || v == VK_RCONTROL || v == VK_RMENU
}

unsafe fn inject_keys(inp: &mut [INPUT]) {
    for i in inp.iter_mut() {
        i.Anonymous.ki.dwExtraInfo = ML_INJECT_MAGIC;
    }
    SendInput(inp.len() as u32, inp.as_ptr(), size_of::<INPUT>() as i32);
}

unsafe fn own_foreground() -> bool {
    window_is_ours(GetForegroundWindow())
}

unsafe fn window_elevated(pid: u32) -> bool {
    let p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    let mut e: TOKEN_ELEVATION = zeroed();
    let mut sz = 0u32;
    if p.is_null() {
        return true;
    }
    let mut tok: HANDLE = null_mut();
    if OpenProcessToken(p, TOKEN_QUERY, &mut tok) != 0 {
        GetTokenInformation(tok, TokenElevation, &mut e as *mut _ as *mut c_void, size_of::<TOKEN_ELEVATION>() as u32, &mut sz);
        CloseHandle(tok);
    }
    CloseHandle(p);
    e.TokenIsElevated != 0
}

// The first ordinary window below ours (z-order), down to the desktop. Not the taskbar: while it is in front,
// PrtSc reaches no hook or hotkey of a screenshot tool either.
unsafe extern "system" fn prtsc_target_enum(h: HWND, lp: LPARAM) -> BOOL {
    if IsWindowVisible(h) == 0 || IsIconic(h) != 0 || !GetWindow(h, GW_OWNER).is_null() {
        return TRUE;
    }
    if (GetWindowLongW(h, GWL_EXSTYLE) as u32 & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) != 0 {
        return TRUE;
    }
    let mut cloaked = 0u32;
    let mut pid = 0u32;
    if succeeded(DwmGetWindowAttribute(h, DWMWA_CLOAKED as _, &mut cloaked as *mut u32 as *mut c_void, 4)) && cloaked != 0 {
        return TRUE;
    }
    GetWindowThreadProcessId(h, &mut pid);
    if pid == 0 || pid == GetCurrentProcessId() || window_elevated(pid) {
        return TRUE;
    }
    *(lp as *mut HWND) = h;
    FALSE
}

unsafe fn prtsc_target() -> HWND {
    let mut h: HWND = null_mut();
    EnumWindows(Some(prtsc_target_enum), &mut h as *mut HWND as LPARAM);
    if !h.is_null() { h } else { FindWindowW(w!("Progman"), null()) }
}

unsafe fn post_toggle() {
    watchdog_set(false);
    PostMessageW(g_hwnd, WM_APP_TOGGLE, 0, 0);
}

unsafe fn post_search() {
    watchdog_set(false);
    PostMessageW(g_hwnd, WM_APP_SEARCH, 0, 0);
}

unsafe extern "system" fn ll_keyboard(code: i32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    let k_ = kk();
    if code != HC_ACTION as i32 {
        return CallNextHookEx(k_.hook, code, wp, lp);
    }
    let k = &*(lp as *const KBDLLHOOKSTRUCT);
    let down = wp == WM_KEYDOWN as usize || wp == WM_SYSKEYDOWN as usize;
    let vk = k.vkCode;

    if k.dwExtraInfo == ML_INJECT_MAGIC {
        if !down && k_.toggle_pending && vk == k_.pending_vk {
            k_.toggle_pending = false;
            post_toggle();
            ktrace(k, wp, b't');
        }
        return CallNextHookEx(k_.hook, code, wp, lp);
    }

    let cap = k_.capture.load(SeqCst) as HWND;
    if !cap.is_null() && GetForegroundWindow() == cap {
        if (k.scanCode & 0x200) == 0 {
            PostMessageW(cap, WM_APP_KEYCAP, vk as WPARAM, if down { 1 } else { 0 }); // 0x200: AltGr's fake Ctrl
        }
        return 1;
    }

    let b = hook_bind();
    let bit = vk_mod_bit(vk);
    let mut pass = true;
    if down {
        if vk == k_.swallow_vk && k.time.wrapping_sub(k_.swallow_time) < 1500 {
            k_.swallow_time = k.time;
            return 1;
        }
        if (k.flags & LLKHF_INJECTED) == 0 && b.vk != 0 && key_class(vk) == key_class(b.vk as u32) {
            k_.hook_presses = k_.hook_presses.wrapping_add(1);
        }
        // a screenshot: the launcher, left out of screen captures for its live backdrop, goes back in
        if k_.ui_visible.load(SeqCst) != 0 && (vk == VK_SNAPSHOT as u32 || (vk == 'S' as u32 && (held_mods() & (HK_WIN | HK_SHIFT)) == (HK_WIN | HK_SHIFT))) {
            PostMessageW(g_hwnd, WM_APP_SCREENSHOT, 0, 0);
        }
        if k_.tap_armed && vk != k_.tap_vk {
            k_.tap_armed = false;
        }
        // Our elevated window in front: Windows holds PrtSc back from the hooks and hotkeys of normal screenshot
        // tools (Lightshot, Snipaste). Hand the foreground to an ordinary window before the key goes on; a key
        // sent again from here would not reach them either, since it comes from an elevated process.
        if vk == VK_SNAPSHOT as u32 && k_.prtsc_handoff && own_foreground() {
            let mods = held_mods();
            let to = if (mods & HK_WIN) == 0 && !(hk_vk_match(b.vk as u32, vk) && mods == b.mods) { prtsc_target() } else { null_mut() };
            if !to.is_null() {
                k_.prtsc_from.store(GetForegroundWindow(), SeqCst);
                k_.prtsc_to = to;
                AllowSetForegroundWindow(ASFW_ANY); // the tool gets the key while the window is still taking over
                if SetForegroundWindow(to) != 0 {
                    PostMessageW(g_hwnd, WM_APP_PRTSC, 0, 0);
                } else {
                    k_.prtsc_from.store(null_mut(), SeqCst);
                }
            }
        }
        if b.vk != 0 {
            if bit != 0 && hk_vk_match(b.vk as u32, vk) {
                let repeat = k_.tap_down && vk == k_.tap_vk && (GetAsyncKeyState(vk as i32) as u16 & 0x8000) != 0;
                if !repeat {
                    k_.tap_down = true;
                    k_.tap_vk = vk;
                    k_.tap_armed = (held_mods() & !bit) == b.mods;
                    k_.tap_mouse = false;
                    PostThreadMessageW(k_.tid, WM_HOOK_MOUSE, 0, 0);
                }
            } else if bit == 0 && hk_vk_match(b.vk as u32, vk) && held_mods() == b.mods {
                k_.swallow_vk = vk;
                k_.swallow_time = k.time;
                if (b.mods & (HK_WIN | HK_ALT)) != 0 {
                    PostThreadMessageW(k_.tid, WM_HOOK_MASK, 0, 0);
                }
                post_toggle();
                ktrace(k, wp, b't');
                return 1;
            }
        }
    } else {
        if vk == k_.swallow_vk {
            k_.swallow_vk = 0;
            ktrace(k, wp, b's');
            return 1;
        }
        if k_.tap_down && vk == k_.tap_vk {
            k_.tap_down = false;
            PostThreadMessageW(k_.tid, WM_HOOK_MOUSE, 0, 0);
            let armed = k_.tap_armed;
            let mouse = k_.tap_mouse;
            k_.tap_armed = false;
            k_.tap_mouse = false;
            if armed && b.vk != 0 && hk_vk_match(b.vk as u32, vk) {
                k_.toggle_pending = true;
                k_.pending_vk = vk;
                PostThreadMessageW(k_.tid, WM_HOOK_MASK, vk as WPARAM, 0);
                ktrace(k, wp, b's');
                return 1;
            }
            // Only the mouse came between (a click, the wheel), which Windows does not count: let go as it is, Win
            // would open Start, Alt the menu bar. It goes out as a tap's does, after vkE8, just without the toggle.
            if mouse {
                PostThreadMessageW(k_.tid, WM_HOOK_MASK, vk as WPARAM, 0);
                ktrace(k, wp, b's');
                return 1;
            }
        }
    }
    let _ = &mut pass;
    ktrace(k, wp, b'p');
    CallNextHookEx(k_.hook, code, wp, lp)
}

unsafe extern "system" fn ll_mouse(code: i32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    let k_ = kk();
    if code == HC_ACTION as i32 && wp != WM_MOUSEMOVE as usize {
        if k_.tap_down && k_.tap_armed {
            k_.tap_armed = false;
            k_.tap_mouse = (vk_mod_bit(k_.tap_vk) & (HK_WIN | HK_ALT)) != 0;
        }
        // A press on the taskbar's search toggles the launcher instead; its release goes nowhere either.
        if wp == WM_LBUTTONUP as usize && k_.swallow_lup {
            k_.swallow_lup = false;
            return 1;
        }
        let m = &*(lp as *const MSLLHOOKSTRUCT);
        if wp == WM_LBUTTONDOWN as usize && k_.search_clicks.load(SeqCst) != 0 && tb_search_hit(m.pt) {
            k_.swallow_lup = true;
            post_search();
            return 1;
        }
        let press = wp == WM_LBUTTONDOWN as usize || wp == WM_RBUTTONDOWN as usize || wp == WM_MBUTTONDOWN as usize || wp == WM_XBUTTONDOWN as usize;
        if press && k_.ui_visible.load(SeqCst) != 0 {
            let mut r: RECT = zeroed();
            if GetWindowRect(g_hwnd, &mut r) != 0 && PtInRect(&r, m.pt) == 0 && !tb_hit(m.pt) {
                PostMessageW(g_hwnd, WM_APP_CLICK_OUTSIDE, 0, 0);
            }
        }
    }
    CallNextHookEx(k_.mouse_hook, code, wp, lp)
}

unsafe fn mouse_hook_update() {
    let k_ = kk();
    let want = k_.tap_down || k_.ui_visible.load(SeqCst) != 0 || k_.search_clicks.load(SeqCst) != 0 || k_.swallow_lup;
    if want && k_.mouse_hook.is_null() {
        k_.mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, Some(ll_mouse), GetModuleHandleW(null()), 0);
    }
    if !want && !k_.mouse_hook.is_null() {
        UnhookWindowsHookEx(k_.mouse_hook);
        k_.mouse_hook = null_mut();
    }
}

unsafe fn hook_install(log: bool) {
    let nh = SetWindowsHookExW(WH_KEYBOARD_LL, Some(ll_keyboard), GetModuleHandleW(null()), 0);
    if nh.is_null() {
        log_msg!("SetWindowsHookEx failed {}", GetLastError());
        return;
    }
    let k_ = kk();
    if !k_.hook.is_null() {
        UnhookWindowsHookEx(k_.hook);
    }
    k_.hook = nh;
    if log {
        k_.reinstalls += 1;
        log_msg!("keyboard hook re-installed ({})", k_.reinstalls);
    }
}

unsafe extern "system" fn watchdog_proc(h: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    if msg == WM_INPUT {
        let mut ri: RAWINPUT = zeroed();
        let mut sz = size_of::<RAWINPUT>() as u32;
        if GetRawInputData(lp as HRAWINPUT, RID_INPUT, &mut ri as *mut RAWINPUT as *mut c_void, &mut sz, size_of::<RAWINPUTHEADER>() as u32) == u32::MAX {
            ri.header.dwType = RIM_TYPEMOUSE;
        }
        let kb = ri.data.keyboard;
        if ri.header.dwType == RIM_TYPEKEYBOARD && g_ktrace_on {
            let mut fake: KBDLLHOOKSTRUCT = zeroed();
            fake.time = GetTickCount();
            fake.vkCode = kb.VKey as u32;
            fake.flags = if ri.header.hDevice.is_null() { 0x110 } else { 0x100 };
            ktrace(&fake, if (kb.Flags as u32 & RI_KEY_BREAK) != 0 { WM_KEYUP as usize } else { WM_KEYDOWN as usize }, b'r');
        }
        let b = hook_bind();
        let k_ = kk();
        if ri.header.dwType == RIM_TYPEKEYBOARD
            && !ri.header.hDevice.is_null()
            && b.vk != 0
            && (kb.Flags as u32 & RI_KEY_BREAK) == 0
            && key_class(kb.VKey as u32) == key_class(b.vk as u32)
        {
            k_.raw_presses = k_.raw_presses.wrapping_add(1);
            if (k_.raw_presses.wrapping_sub(k_.hook_presses) as i32) > 0 {
                hook_install(true);
                k_.hook_presses = k_.raw_presses;
                k_.tap_down = false;
                k_.tap_armed = false;
            }
        }
    }
    DefWindowProcW(h, msg, wp, lp)
}

// While our process is in front, Windows skips its LL hook for raw-input sinks: only on in the background.
unsafe fn watchdog_set(on: bool) {
    let k_ = kk();
    if k_.watchdog.is_null() || on == k_.watchdog_on {
        return;
    }
    let rid = RAWINPUTDEVICE { usUsagePage: 0x01, usUsage: 0x06, dwFlags: if on { RIDEV_INPUTSINK } else { RIDEV_REMOVE }, hwndTarget: if on { k_.watchdog } else { null_mut() } };
    if RegisterRawInputDevices(&rid, 1, size_of::<RAWINPUTDEVICE>() as u32) != 0 {
        k_.watchdog_on = on;
    } else {
        log_msg!("raw input watchdog {} failed ({})", if on { "register" } else { "remove" }, GetLastError());
    }
    if g_ktrace_on {
        log_msg!("watchdog {} t={}", if on { "on" } else { "off" }, GetTickCount());
    }
    k_.raw_presses = k_.hook_presses;
}

unsafe fn watchdog_sync(fg: HWND) {
    watchdog_set(!window_is_ours(fg));
}

unsafe extern "system" fn foreground_changed(_h: HWINEVENTHOOK, _ev: u32, hwnd: HWND, _obj: i32, _child: i32, _tid: u32, _time: u32) {
    if g_ktrace_on {
        log_msg!("foreground event {:p} t={}", hwnd, GetTickCount());
    }
    watchdog_sync(hwnd);
}

pub unsafe fn opt_out_of_throttling(thread: HANDLE) {
    #[repr(C)]
    struct PowerThrottlingState {
        version: u32,
        control_mask: u32,
        state_mask: u32,
    }
    let mut st = PowerThrottlingState { version: 1, control_mask: 0x1 /*EXECUTION_SPEED*/, state_mask: 0 };
    type Pfn = unsafe extern "system" fn(HANDLE, i32, *mut c_void, u32) -> BOOL;
    let k32 = GetModuleHandleW(w!("kernel32.dll"));
    let sti: Option<Pfn> = core::mem::transmute(GetProcAddress(k32, s!("SetThreadInformation")));
    let spi: Option<Pfn> = core::mem::transmute(GetProcAddress(k32, s!("SetProcessInformation")));
    let p = &mut st as *mut _ as *mut c_void;
    let n = size_of::<PowerThrottlingState>() as u32;
    if !thread.is_null() {
        if let Some(f) = sti {
            f(thread, 3 /*ThreadPowerThrottling*/, p, n);
        }
    }
    if thread.is_null() {
        if let Some(f) = spi {
            f(GetCurrentProcess(), 4 /*ProcessPowerThrottling*/, p, n);
        }
    }
}

unsafe extern "system" fn hook_thread(param: *mut c_void) -> u32 {
    let ready = param as HANDLE;
    let mut msg: MSG = zeroed();
    PeekMessageW(&mut msg, null_mut(), 0, 0, PM_NOREMOVE);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    opt_out_of_throttling(GetCurrentThread());
    hook_install(false);

    let mut wc: WNDCLASSW = zeroed();
    wc.lpfnWndProc = Some(watchdog_proc);
    wc.hInstance = GetModuleHandleW(null());
    wc.lpszClassName = w!("MixLauncherKeyWatchdog");
    RegisterClassW(&wc);
    let k_ = kk();
    k_.watchdog = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, w!(""), WS_POPUP, 0, 0, 0, 0, null_mut(), null_mut(), wc.hInstance, null());
    watchdog_sync(GetForegroundWindow());
    if SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, null_mut(), Some(foreground_changed), 0, 0, WINEVENT_OUTOFCONTEXT).is_null() {
        log_msg!("SetWinEventHook failed {}", GetLastError());
    }
    SetEvent(ready);
    SetTimer(null_mut(), 0, 60 * 1000, None);
    while GetMessageW(&mut msg, null_mut(), 0, 0) > 0 {
        match msg.message {
            WM_HOOK_MASK => {
                // vkE8 down/up, then (for a tap) the key's release we swallowed.
                let mut inp: [INPUT; 3] = zeroed();
                for i in inp.iter_mut() {
                    i.r#type = INPUT_KEYBOARD;
                }
                inp[0].Anonymous.ki.wVk = 0xE8;
                inp[1].Anonymous.ki.wVk = 0xE8;
                inp[1].Anonymous.ki.dwFlags = KEYEVENTF_KEYUP;
                let vk = msg.wParam as u32;
                inp[2].Anonymous.ki.wVk = vk as u16;
                inp[2].Anonymous.ki.dwFlags = KEYEVENTF_KEYUP | if vk_extended(vk) { KEYEVENTF_EXTENDEDKEY } else { 0 };
                inject_keys(&mut inp[..if vk != 0 { 3 } else { 2 }]);
                if vk != 0 {
                    k_.fallback_timer = SetTimer(null_mut(), k_.fallback_timer, 80, None);
                }
            }
            WM_HOOK_MOUSE => mouse_hook_update(),
            WM_HOOK_VISIBLE => {
                k_.ui_visible.store(if msg.wParam != 0 { 1 } else { 0 }, SeqCst);
                mouse_hook_update();
            }
            WM_HOOK_SEARCH => {
                k_.search_clicks.store(if msg.wParam != 0 { 1 } else { 0 }, SeqCst);
                mouse_hook_update();
            }
            WM_TIMER => {
                if k_.fallback_timer != 0 && msg.wParam == k_.fallback_timer {
                    KillTimer(null_mut(), k_.fallback_timer);
                    k_.fallback_timer = 0;
                    if k_.toggle_pending {
                        k_.toggle_pending = false;
                        post_toggle();
                    }
                    continue;
                }
                if !k_.tap_down {
                    hook_install(false);
                    // Windows silently drops a hook that once took too long: renew it every minute, the mouse hook too
                    // while it only watches the search clicks
                    if !k_.mouse_hook.is_null() && k_.search_clicks.load(SeqCst) != 0 && !k_.swallow_lup {
                        UnhookWindowsHookEx(k_.mouse_hook);
                        k_.mouse_hook = null_mut();
                    }
                    mouse_hook_update();
                }
            }
            WM_HOOK_REINSTALL => {
                hook_install(false);
                if msg.wParam == 0 {
                    k_.tap_down = false;
                    k_.tap_armed = false;
                }
            }
            WM_HOOK_WATCHDOG => {
                if msg.wParam != 0 {
                    watchdog_sync(GetForegroundWindow());
                } else {
                    watchdog_set(false);
                }
            }
            _ => {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
    0
}

pub unsafe fn hook_set_hotkey(h: Hotkey) {
    K.bind.store(((h.mods as i32) << 8) | h.vk as i32, SeqCst);
}

unsafe fn hook_post(msg: u32, wp: WPARAM) {
    if K.tid != 0 {
        PostThreadMessageW(K.tid, msg, wp, 0);
    }
}

pub unsafe fn hook_start(h: Hotkey) {
    hook_set_hotkey(h);
    if K.tid != 0 {
        return;
    }
    let ready = CreateEventW(null(), TRUE, FALSE, null());
    let t = CreateThread(null(), 0, Some(hook_thread), ready as *mut c_void, 0, &raw mut K.tid);
    if !t.is_null() {
        WaitForSingleObject(ready, 2000);
        CloseHandle(t);
    }
    CloseHandle(ready);
}

pub unsafe fn hook_capture(h: HWND) {
    K.capture.store(h, SeqCst);
}

pub unsafe fn hook_reinstall() {
    hook_post(WM_HOOK_REINSTALL, 0); // after sleep: a fresh hook, tap state reset
}
pub unsafe fn hook_rearm() {
    hook_post(WM_HOOK_REINSTALL, 1); // a fresh hook, a tap in progress kept
}
pub unsafe fn hook_watchdog_off() {
    hook_post(WM_HOOK_WATCHDOG, 0);
}
pub unsafe fn hook_watchdog_sync() {
    hook_post(WM_HOOK_WATCHDOG, 1);
}
pub unsafe fn hook_set_visible(visible: bool) {
    hook_post(WM_HOOK_VISIBLE, visible as WPARAM);
}
pub unsafe fn hook_search_clicks(on: bool) {
    hook_post(WM_HOOK_SEARCH, on as WPARAM);
}

pub unsafe fn hotkey_is_win_tap() -> bool {
    vk_mod_bit(g_cfg.hk.vk as u32) == HK_WIN && K.capture.load(SeqCst).is_null()
}
