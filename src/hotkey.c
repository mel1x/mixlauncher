// hotkey.c — the launcher hotkey, via a low-level keyboard hook on its own high-priority thread.

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
    DWORD swallow_vk;
    DWORD swallow_time;
    bool toggle_pending;
    DWORD pending_vk;
    UINT_PTR fallback_timer;
    u32 hook_presses;
    u32 raw_presses;            // ... and by raw input
    u32 reinstalls;
} K;

typedef struct KeyTrace {
    DWORD time, vk, flags;
    ULONG_PTR extra;
    WPARAM wp;
    char what;
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

static u32 key_class(u32 vk)
{
    u8 m = vk_mod_bit(vk);
    return m ? 0x100u | m : vk;
}

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
    watchdog_set(false);
    PostMessageW(g_hwnd, WM_APP_TOGGLE, 0, 0);
}

static LRESULT CALLBACK ll_keyboard(int code, WPARAM wp, LPARAM lp)
{
    if (code != HC_ACTION) return CallNextHookEx(K.hook, code, wp, lp);
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
    bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
    DWORD vk = k->vkCode;

    if (k->dwExtraInfo == ML_INJECT_MAGIC) {
        if (!down && K.toggle_pending && vk == K.pending_vk) {
            K.toggle_pending = false;
            post_toggle();
            ktrace(k, wp, 't');
        }
        return CallNextHookEx(K.hook, code, wp, lp);
    }

    HWND cap = K.capture;
    if (cap && GetForegroundWindow() == cap) {
        if (!(k->scanCode & 0x200)) PostMessageW(cap, WM_APP_KEYCAP, vk, down ? 1 : 0);  // 0x200: AltGr's fake Ctrl
        return 1;
    }

    Hotkey b = hook_bind();
    u8 bit = vk_mod_bit(vk);
    if (down) {
        if (vk == K.swallow_vk && k->time - K.swallow_time < 1500) {
            K.swallow_time = k->time;
            return 1;
        }
        if (!(k->flags & LLKHF_INJECTED) && b.vk && key_class(vk) == key_class(b.vk)) K.hook_presses++;
        if (K.tap_armed && vk != K.tap_vk) K.tap_armed = false;  // something else pressed: a combination
        if (!b.vk) goto pass;
        if (bit && hk_vk_match(b.vk, vk)) {
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

static bool tb_hit(POINT pt);

static LRESULT CALLBACK ll_mouse(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp != WM_MOUSEMOVE) {
        if (K.tap_down) K.tap_armed = false;  // Win+click, Win+wheel
        bool press = wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN;
        if (press && K.ui_visible) {
            const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)lp;
            RECT r;
            if (GetWindowRect(g_hwnd, &r) && !PtInRect(&r, m->pt) && !tb_hit(m->pt)) PostMessageW(g_hwnd, WM_APP_CLICK_OUTSIDE, 0, 0);
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
            if ((i32)(K.raw_presses - K.hook_presses) > 0) {
                hook_install(true);
                K.hook_presses = K.raw_presses;
                K.tap_down = K.tap_armed = false;
            }
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// While our process is in front, Windows skips its LL hook for raw-input sinks: only on in the background.
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

static void CALLBACK foreground_changed(HWINEVENTHOOK h, DWORD ev, HWND hwnd, LONG obj, LONG child, DWORD tid, DWORD time)
{
    if (g_ktrace_on) log_msg("foreground event %p t=%lu", (void *)hwnd, GetTickCount());
    watchdog_sync(hwnd);
}

static void opt_out_of_throttling(HANDLE thread)
{
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
                hook_install(false);
                mouse_hook_update();
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

static void hook_capture(HWND h) { InterlockedExchangePointer((void *volatile *)&K.capture, h); }

static bool hook_running(void) { return K.tid && K.hook; }

static void hook_reinstall(void)
{
    if (K.tid) PostThreadMessageW(K.tid, WM_HOOK_REINSTALL, 0, 0);
}

static void hook_rearm(void)
{
    if (K.tid) PostThreadMessageW(K.tid, WM_HOOK_REINSTALL, 1, 0);
}

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

static bool hotkey_is_win_tap(void) { return vk_mod_bit(g_cfg.hk.vk) == HK_WIN && !K.capture; }
