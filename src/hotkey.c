// hotkey.c — the launcher hotkey, via a low-level keyboard hook on its own high-priority thread.
//
// Any key or combination can be the hotkey (see Hotkey in config.c), and it loses whatever it
// did before:
//  - a combination or a single key (Alt+Space, Win+D, F13): the key press is swallowed, so Win+D
//    no longer shows the desktop. If Win or Alt is held, an unassigned vkE8 is injected so their
//    release does not open the Start menu or a window menu either;
//  - a tap of a modifier alone (Win, Right Ctrl): the launcher toggles when the key is released
//    with nothing else pressed in between. That release is swallowed and replayed as
//    [vkE8 down/up], key-up: the shell sees a combination, so the Start menu stays closed, while
//    Win+E, Win+R, Win+Shift+S etc. pass through untouched.
// The hook thread never does any real work, so it cannot hit the system's hook timeout even if
// the UI thread is busy.
//
// Reliability next to other hook-based tools (AltSnap, PowerToys, AutoHotkey...):
//  - Our own synthetic input is tagged in dwExtraInfo; input injected by other tools (which often
//    swallow the physical Win key and replay it) is treated like real input.
//  - Windows silently unhooks a hook that once timed out. A raw-input watchdog sees every
//    physical press of the hotkey's key independently of the hook chain and re-installs the hook
//    the moment a press arrives that the hook did not see. The hook is also re-armed every minute,
//    which keeps it near the front of the chain.
//  - While a tap key is held, a mouse hook marks clicks/wheel as a combination (Win+drag,
//    Win+click). The same hook, while the launcher is open, closes it on a click anywhere outside
//    of it. Otherwise it is not installed, so the mouse path is untouched the rest of the time.
//  - Windows hides input aimed at elevated windows (games with anti-cheat, admin consoles) from
//    hooks of unelevated processes; that is why MixLauncher runs as administrator (see manifest).
//
// Recording: while the settings window records a new hotkey and is in the foreground, every key
// goes to it (WM_APP_KEYCAP) and nowhere else, so even Win, Alt+Tab or Win+D can be recorded.

#define WM_HOOK_MASK (WM_APP + 100)
#define WM_HOOK_REINSTALL (WM_APP + 101)
#define WM_HOOK_MOUSE (WM_APP + 102)
#define WM_HOOK_WATCHDOG (WM_APP + 104)
#define WM_HOOK_VISIBLE (WM_APP + 105)
#define ML_INJECT_MAGIC ((ULONG_PTR)0x4D4C4E43u)   // 'MLNC'

static struct {
    DWORD tid;
    HHOOK hook, mouse_hook;
    HWND watchdog;
    bool watchdog_on;
    volatile LONG bind;         // Hotkey: mods << 8 | vk; 0 = none
    volatile LONG ui_visible;   // launcher open: clicks outside of it close it
    HWND volatile capture;      // settings window recording a hotkey
    bool tap_down;              // the hotkey's tap key is held
    bool tap_armed;             // ... and nothing else happened since it went down
    DWORD tap_vk;
    DWORD swallow_vk;           // combination key we swallowed: its repeats and release go too
    DWORD swallow_time;
    bool toggle_pending;        // tap swallowed, waiting for our replayed release to go through
    DWORD pending_vk;
    UINT_PTR fallback_timer;
    u32 hook_presses;           // physical presses of the hotkey's key seen by the hook
    u32 raw_presses;            // ... and by raw input
    u32 reinstalls;
} K;

// Debug trace of hook decisions (enabled by --other-monitor), dumped to the log on exit.
typedef struct KeyTrace {
    DWORD time, vk, flags;
    ULONG_PTR extra;
    WPARAM wp;
    char what;   // p = pass, s = swallow, t = toggle posted, r = raw input
} KeyTrace;
static KeyTrace g_ktrace[128];
static volatile LONG g_ktrace_n;
static bool g_ktrace_on;

static void ktrace(const KBDLLHOOKSTRUCT *k, WPARAM wp, char what)
{
    if (!g_ktrace_on) return;
    LONG i = InterlockedIncrement(&g_ktrace_n) - 1;
    if (i >= countof(g_ktrace)) return;
    KeyTrace *t = &g_ktrace[i];
    t->time = k->time;
    t->vk = k->vkCode;
    t->flags = k->flags;
    t->extra = k->dwExtraInfo;
    t->wp = wp;
    t->what = what;
}

static void ktrace_dump(void)
{
    LONG n = MIN(g_ktrace_n, countof(g_ktrace));
    for (LONG i = 0; i < n; i++) {
        KeyTrace *t = &g_ktrace[i];
        log_msg("%s t=%lu vk=%02lx %s flags=%02lx %s -> %c", t->what == 'r' ? "raw" : "key", t->time, t->vk,
                (t->wp == WM_KEYDOWN || t->wp == WM_SYSKEYDOWN) ? "down" : "up  ", t->flags,
                t->extra == ML_INJECT_MAGIC ? "OURS" : "", t->what);
    }
}

static Hotkey hook_bind(void)
{
    LONG b = K.bind;
    Hotkey h = { (u8)(b >> 8), (u8)b };
    return h;
}

// Keys that count as "the same key" for the watchdog: modifiers by kind, the rest by VK code
// (raw input reports Ctrl/Alt/Shift without the side).
static u32 key_class(u32 vk)
{
    u8 m = vk_mod_bit(vk);
    return m ? 0x100u | m : vk;
}

// Modifiers held right now. Inside the hook the state does not include the current event yet.
static u8 held_mods(void)
{
    u8 m = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= HK_CTRL;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) m |= HK_ALT;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) m |= HK_SHIFT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) m |= HK_WIN;
    return m;
}

static bool vk_extended(DWORD vk) { return vk == VK_LWIN || vk == VK_RWIN || vk == VK_RCONTROL || vk == VK_RMENU; }

static void inject_keys(INPUT *in, int n)
{
    for (int i = 0; i < n; i++) in[i].ki.dwExtraInfo = ML_INJECT_MAGIC;
    SendInput((UINT)n, in, sizeof(INPUT));
}

static void watchdog_set(bool on);

static void post_toggle(void)
{
    watchdog_set(false);  // must be off before our window takes the foreground (see watchdog_set)
    PostMessageW(g_hwnd, WM_APP_TOGGLE, 0, 0);
}

static LRESULT CALLBACK ll_keyboard(int code, WPARAM wp, LPARAM lp)
{
    if (code != HC_ACTION) return CallNextHookEx(K.hook, code, wp, lp);
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
    bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
    DWORD vk = k->vkCode;

    if (k->dwExtraInfo == ML_INJECT_MAGIC) {
        // Our replayed tap release went through: now the launcher may take the foreground.
        if (!down && K.toggle_pending && vk == K.pending_vk) {
            K.toggle_pending = false;
            post_toggle();
            ktrace(k, wp, 't');
        }
        return CallNextHookEx(K.hook, code, wp, lp);
    }

    // Recording a hotkey in the settings window: every key goes there, and only there.
    HWND cap = K.capture;
    if (cap && GetForegroundWindow() == cap) {
        if (!(k->scanCode & 0x200)) PostMessageW(cap, WM_APP_KEYCAP, vk, down ? 1 : 0);  // 0x200: AltGr's fake Ctrl
        return 1;
    }

    Hotkey b = hook_bind();
    u8 bit = vk_mod_bit(vk);
    if (down) {
        // Auto-repeat of a combination key we swallowed (the system never saw it go down).
        if (vk == K.swallow_vk && k->time - K.swallow_time < 1500) {
            K.swallow_time = k->time;
            return 1;
        }
        if (!(k->flags & LLKHF_INJECTED) && b.vk && key_class(vk) == key_class(b.vk)) K.hook_presses++;
        if (K.tap_armed && vk != K.tap_vk) K.tap_armed = false;  // something else pressed: a combination
        if (!b.vk) goto pass;
        if (bit && hk_vk_match(b.vk, vk)) {
            // The async state is not updated yet for this event: "already down" means auto-repeat.
            // Anything else is a new press, even if we missed the last release.
            bool repeat = K.tap_down && vk == K.tap_vk && (GetAsyncKeyState((int)vk) & 0x8000);
            if (!repeat) {
                K.tap_down = true;
                K.tap_vk = vk;
                K.tap_armed = (held_mods() & ~bit) == b.mods;  // other modifiers held: not our tap
                PostThreadMessageW(K.tid, WM_HOOK_MOUSE, 0, 0);
            }
        } else if (!bit && hk_vk_match(b.vk, vk) && held_mods() == b.mods) {
            K.swallow_vk = vk;
            K.swallow_time = k->time;
            if (b.mods & (HK_WIN | HK_ALT)) PostThreadMessageW(K.tid, WM_HOOK_MASK, 0, 0);
            post_toggle();
            ktrace(k, wp, 't');
            return 1;
        }
    } else {
        if (vk == K.swallow_vk) {
            K.swallow_vk = 0;
            ktrace(k, wp, 's');
            return 1;
        }
        if (K.tap_down && vk == K.tap_vk) {
            K.tap_down = false;
            PostThreadMessageW(K.tid, WM_HOOK_MOUSE, 0, 0);
            bool armed = K.tap_armed;
            K.tap_armed = false;
            if (armed && b.vk && hk_vk_match(b.vk, vk)) {
                // The launcher is toggled only once our replayed release has come through (above):
                // changing the foreground window while the shell still sees Win held would let it
                // open the Start menu.
                K.toggle_pending = true;
                K.pending_vk = vk;
                PostThreadMessageW(K.tid, WM_HOOK_MASK, vk, 0);
                ktrace(k, wp, 's');
                return 1;
            }
        }
    }
pass:
    ktrace(k, wp, 'p');
    return CallNextHookEx(K.hook, code, wp, lp);
}

static LRESULT CALLBACK ll_mouse(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp != WM_MOUSEMOVE) {
        if (K.tap_down) K.tap_armed = false;  // Win+click, Win+wheel
        bool press = wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN;
        if (press && K.ui_visible) {
            const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)lp;
            RECT r;
            if (GetWindowRect(g_hwnd, &r) && !PtInRect(&r, m->pt)) PostMessageW(g_hwnd, WM_APP_CLICK_OUTSIDE, 0, 0);
        }
    }
    return CallNextHookEx(K.mouse_hook, code, wp, lp);
}

static void mouse_hook_update(void)
{
    bool want = K.tap_down || K.ui_visible;
    if (want && !K.mouse_hook) K.mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, ll_mouse, GetModuleHandleW(NULL), 0);
    if (!want && K.mouse_hook) {
        UnhookWindowsHookEx(K.mouse_hook);
        K.mouse_hook = NULL;
    }
}

static void hook_install(bool log)
{
    HHOOK nh = SetWindowsHookExW(WH_KEYBOARD_LL, ll_keyboard, GetModuleHandleW(NULL), 0);
    if (!nh) {
        log_msg("SetWindowsHookEx failed %lu", GetLastError());
        return;
    }
    if (K.hook) UnhookWindowsHookEx(K.hook);
    K.hook = nh;
    if (log) log_msg("keyboard hook re-installed (%u)", ++K.reinstalls);
}

// Raw input arrives for every physical key regardless of the hook chain. If a press of the
// hotkey's key got here but not to our hook, the hook is gone (timed out and removed).
static LRESULT CALLBACK watchdog_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INPUT) {
        RAWINPUT ri;
        UINT sz = sizeof ri;
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) == (UINT)-1) ri.header.dwType = RIM_TYPEMOUSE;
        if (ri.header.dwType == RIM_TYPEKEYBOARD) {
            if (g_ktrace_on) {
                KBDLLHOOKSTRUCT fake;
                memset(&fake, 0, sizeof fake);
                fake.time = GetTickCount();
                fake.vkCode = ri.data.keyboard.VKey;
                fake.flags = ri.header.hDevice ? 0x100 : 0x110;  // 0x100 marks "raw input" in the trace
                ktrace(&fake, (ri.data.keyboard.Flags & RI_KEY_BREAK) ? WM_KEYUP : WM_KEYDOWN, 'r');
            }
        }
        Hotkey b = hook_bind();
        if (ri.header.dwType == RIM_TYPEKEYBOARD && ri.header.hDevice && b.vk && !(ri.data.keyboard.Flags & RI_KEY_BREAK) &&
            key_class(ri.data.keyboard.VKey) == key_class(b.vk)) {
            K.raw_presses++;
            // The hook always runs before raw input is generated, so any lead means a miss.
            if ((i32)(K.raw_presses - K.hook_presses) > 0) {
                hook_install(true);
                K.hook_presses = K.raw_presses;
                K.tap_down = K.tap_armed = false;
            }
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// Raw input sink on/off. Important: while our process is in the foreground, Windows does not
// call the low-level hook of a process that has a raw keyboard registration. So the watchdog
// runs only while none of our windows (launcher, settings, menus) is in the foreground.
static void watchdog_set(bool on)
{
    if (!K.watchdog || on == K.watchdog_on) return;
    RAWINPUTDEVICE rid = { 0x01, 0x06, on ? RIDEV_INPUTSINK : RIDEV_REMOVE, on ? K.watchdog : NULL };  // keyboard
    if (RegisterRawInputDevices(&rid, 1, sizeof rid)) K.watchdog_on = on;
    else log_msg("raw input watchdog %s failed (%lu)", on ? "register" : "remove", GetLastError());
    if (g_ktrace_on) log_msg("watchdog %s t=%lu", on ? "on" : "off", GetTickCount());
    // Presses that happened while it was off must not look like misses.
    K.raw_presses = K.hook_presses;
}

static void watchdog_sync(HWND fg)
{
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    watchdog_set(pid != GetCurrentProcessId());
}

// The event can arrive before GetForegroundWindow() reports the new window: use the event's.
static void CALLBACK foreground_changed(HWINEVENTHOOK h, DWORD ev, HWND hwnd, LONG obj, LONG child, DWORD tid, DWORD time)
{
    if (g_ktrace_on) log_msg("foreground event %p t=%lu", (void *)hwnd, GetTickCount());
    watchdog_sync(hwnd);
}

static void opt_out_of_throttling(HANDLE thread)
{
    // EcoQoS / efficiency mode would stretch the hook's response time past the system timeout.
    typedef struct { ULONG Version, ControlMask, StateMask; } PowerThrottlingState;
    PowerThrottlingState st = { 1, 0x1 /*EXECUTION_SPEED*/, 0 };
    typedef BOOL (WINAPI *PFN_SetThreadInformation)(HANDLE, int, LPVOID, DWORD);
    typedef BOOL (WINAPI *PFN_SetProcessInformation)(HANDLE, int, LPVOID, DWORD);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    PFN_SetThreadInformation sti = (PFN_SetThreadInformation)(void *)GetProcAddress(k32, "SetThreadInformation");
    PFN_SetProcessInformation spi = (PFN_SetProcessInformation)(void *)GetProcAddress(k32, "SetProcessInformation");
    if (thread && sti) sti(thread, 3 /*ThreadPowerThrottling*/, &st, sizeof st);
    if (!thread && spi) spi(GetCurrentProcess(), 4 /*ProcessPowerThrottling*/, &st, sizeof st);
}

static DWORD WINAPI hook_thread(void *param)
{
    HANDLE ready = (HANDLE)param;
    MSG msg;
    PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);  // create the message queue
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    opt_out_of_throttling(GetCurrentThread());
    hook_install(false);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = watchdog_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MixLauncherKeyWatchdog";
    RegisterClassW(&wc);
    // A hidden top-level window (not message-only): input sinks need a real target window.
    K.watchdog = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    watchdog_sync(GetForegroundWindow());
    if (!SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, foreground_changed, 0, 0, WINEVENT_OUTOFCONTEXT))
        log_msg("SetWinEventHook failed %lu", GetLastError());
    SetEvent(ready);
    SetTimer(NULL, 0, 60 * 1000, NULL);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        switch (msg.message) {
        case WM_HOOK_MASK: {
            // vkE8 down/up, then (for a tap) the key's release we swallowed.
            INPUT in[3];
            memset(in, 0, sizeof in);
            in[0].type = in[1].type = in[2].type = INPUT_KEYBOARD;
            in[0].ki.wVk = 0xE8;
            in[1].ki.wVk = 0xE8;
            in[1].ki.dwFlags = KEYEVENTF_KEYUP;
            DWORD vk = (DWORD)msg.wParam;
            in[2].ki.wVk = (WORD)vk;
            in[2].ki.dwFlags = KEYEVENTF_KEYUP | (vk_extended(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
            inject_keys(in, vk ? 3 : 2);
            // Normally the hook sees our release within a millisecond. If a tool earlier in the
            // hook chain eats injected input, toggle anyway after a short while.
            if (vk) K.fallback_timer = SetTimer(NULL, K.fallback_timer, 80, NULL);
            break;
        }
        case WM_HOOK_MOUSE:
            mouse_hook_update();
            break;
        case WM_HOOK_VISIBLE:
            InterlockedExchange(&K.ui_visible, msg.wParam ? 1 : 0);
            mouse_hook_update();
            break;
        case WM_TIMER:
            if (K.fallback_timer && msg.wParam == K.fallback_timer) {
                KillTimer(NULL, K.fallback_timer);
                K.fallback_timer = 0;
                if (K.toggle_pending) {
                    K.toggle_pending = false;
                    post_toggle();
                }
                break;
            }
            if (!K.tap_down) {
                hook_install(false);  // never while a tap key is held: keep its state intact
                mouse_hook_update();  // a lost release must not leave the mouse hook behind
            }
            break;
        case WM_HOOK_REINSTALL:
            hook_install(false);
            if (!msg.wParam) K.tap_down = K.tap_armed = false;  // wParam 1: re-arm only, keep key state
            break;
        case WM_HOOK_WATCHDOG:
            if (msg.wParam) watchdog_sync(GetForegroundWindow());
            else watchdog_set(false);
            break;
        default:
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            break;
        }
    }
    return 0;
}

static void hook_set_hotkey(Hotkey h) { InterlockedExchange(&K.bind, (LONG)((h.mods << 8) | h.vk)); }

static void hook_start(Hotkey h)
{
    hook_set_hotkey(h);
    if (K.tid) return;
    HANDLE ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, hook_thread, ready, 0, &K.tid);
    if (t) {
        WaitForSingleObject(ready, 2000);
        CloseHandle(t);
    }
    CloseHandle(ready);
}

// The settings window records a new hotkey (NULL: stop). Keys go to it only while it is in the
// foreground, so clicking another window always gives the keyboard back.
static void hook_capture(HWND h) { InterlockedExchangePointer((void *volatile *)&K.capture, h); }

static bool hook_running(void) { return K.tid && K.hook; }

static void hook_reinstall(void)
{
    if (K.tid) PostThreadMessageW(K.tid, WM_HOOK_REINSTALL, 0, 0);
}

// Move our hook back to the front of the chain without touching the key state.
static void hook_rearm(void)
{
    if (K.tid) PostThreadMessageW(K.tid, WM_HOOK_REINSTALL, 1, 0);
}

// The launcher is about to take the foreground (off), or has just gone (re-check).
static void hook_watchdog_off(void)
{
    if (K.tid) PostThreadMessageW(K.tid, WM_HOOK_WATCHDOG, 0, 0);
}

static void hook_watchdog_sync(void)
{
    if (K.tid) PostThreadMessageW(K.tid, WM_HOOK_WATCHDOG, 1, 0);
}

// Launcher open/closed: while open, a click outside of it closes it.
static void hook_set_visible(bool visible)
{
    if (K.tid) PostThreadMessageW(K.tid, WM_HOOK_VISIBLE, visible ? 1 : 0, 0);
}

// The hotkey is a tap of a Win key: then the Win key belongs to us inside our windows as well.
static bool hotkey_is_win_tap(void) { return vk_mod_bit(g_cfg.hk.vk) == HK_WIN && !K.capture; }
