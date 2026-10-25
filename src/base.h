#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef CINTERFACE
#define CINTERFACE
#endif
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#undef WINVER
#define WINVER 0x0A00
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <exdisp.h>
#include <shldisp.h>
#include <uiautomation.h>
#include <wincodec.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <winhttp.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>
#include <math.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "shell32")
#pragma comment(lib, "ole32")
#pragma comment(lib, "oleaut32")
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "advapi32")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "uuid")
#pragma comment(lib, "winhttp")
#endif

#include "../res/version.h"
#define ML_WIDEN2(x) L##x
#define ML_WIDEN(x) ML_WIDEN2(x)
#define ML_VER_WSTR ML_WIDEN(ML_VER_STR)

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef float    f32;
typedef double   f64;

#define countof(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define SAFE_RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

static bool g_lang_ru;
#define TR(ru, en) (g_lang_ru ? L##ru : L##en)

static HWND g_hwnd;           // the launcher window, the target of every worker's messages
static bool g_pinned;         // --pin: a test copy, no hotkey, tray, taskbar or single-instance lock
static bool g_other_monitor;  // --other-monitor: opens away from the cursor and traces keys to the log
static WCHAR g_data_dir[MAX_PATH];
static WCHAR g_exe_path[MAX_PATH];

// Other instances, the installer and tools\stop-running.ps1 post these by number: add new ones at the end.
enum {
    WM_APP_TOGGLE = WM_APP + 1,
    WM_APP_SHOW,
    WM_APP_APPS_READY,
    WM_APP_ICONS_READY,
    WM_APP_FILES_READY,
    WM_APP_TRAY,
    WM_APP_EXIT,
    WM_APP_LAUNCH_FAILED,
    WM_APP_CLICK_OUTSIDE,
    WM_APP_SETTINGS,
    WM_APP_KEYCAP,
    WM_APP_TASKBAR,
    WM_APP_MEDIA,
    WM_APP_UPDATE,
    WM_APP_AUTOSTART,
    WM_APP_PRTSC,
    WM_APP_SEARCH,
    WM_APP_SCREENSHOT,
};

// ---- memory

typedef struct Arena {
    u8    *base;
    size_t cap;
    size_t committed;
    size_t pos;
} Arena;

static void arena_init(Arena *a, size_t reserve)
{
    a->base = (u8 *)VirtualAlloc(NULL, reserve, MEM_RESERVE, PAGE_READWRITE);
    a->cap = a->base ? reserve : 0;
    a->committed = 0;
    a->pos = 0;
}

static void *arena_push(Arena *a, size_t size)
{
    size_t p = (a->pos + 15) & ~(size_t)15;
    size_t end = p + size;
    if (end > a->committed) {
        size_t nc = (end + 0xFFFF) & ~(size_t)0xFFFF;
        if (nc > a->cap || !VirtualAlloc(a->base + a->committed, nc - a->committed, MEM_COMMIT, PAGE_READWRITE))
            FatalAppExitW(0, L"MixLauncher: arena out of memory");
        a->committed = nc;
    }
    a->pos = end;
    return a->base + p;
}

static void *arena_push_zero(Arena *a, size_t size)
{
    void *p = arena_push(a, size);
    memset(p, 0, size);
    return p;
}

#define arena_array(a, T, n) ((T *)arena_push_zero((a), sizeof(T) * (size_t)(n)))

static void arena_reset(Arena *a) { a->pos = 0; }

static void arena_release(Arena *a)
{
    if (a->base) VirtualFree(a->base, 0, MEM_RELEASE);
    memset(a, 0, sizeof(*a));
}

// Grows a malloc'd array to room for `need` items, doubling from `first`. False when out of memory.
static bool grow_(void **p, int *cap, int need, size_t size, int first)
{
    if (need <= *cap) return true;
    int nc = *cap ? *cap : first;
    while (nc < need) nc *= 2;
    void *n = realloc(*p, size * (size_t)nc);
    if (!n) return false;
    *p = n;
    *cap = nc;
    return true;
}
#define GROW(arr, cap, need, first) grow_((void **)&(arr), &(cap), (need), sizeof *(arr), (first))

// ---- strings

static int wlen(const WCHAR *s) { return s ? (int)wcslen(s) : 0; }

static WCHAR *wdup(Arena *a, const WCHAR *s, int len)
{
    if (!s) return NULL;
    if (len < 0) len = wlen(s);
    WCHAR *d = (WCHAR *)arena_push(a, (size_t)(len + 1) * sizeof(WCHAR));
    memcpy(d, s, (size_t)len * sizeof(WCHAR));
    d[len] = 0;
    return d;
}

static WCHAR *wdup_heap(const WCHAR *s)
{
    if (!s) return NULL;
    int n = wlen(s);
    WCHAR *d = (WCHAR *)malloc((size_t)(n + 1) * sizeof(WCHAR));
    if (d) memcpy(d, s, (size_t)(n + 1) * sizeof(WCHAR));
    return d;
}

static void wcopy(WCHAR *dst, int cap, const WCHAR *src)
{
    if (cap <= 0) return;
    int n = MIN(wlen(src), cap - 1);
    if (n) memcpy(dst, src, (size_t)n * sizeof(WCHAR));
    dst[n] = 0;
}

// swprintf into a fixed buffer, always terminated. Returns the length, or -1 when the text was cut.
static int wfmt(WCHAR *buf, int cap, const WCHAR *fmt, ...)
{
    if (cap <= 0) return -1;
    va_list ap;
    va_start(ap, fmt);
#ifdef _MSC_VER
    int n = _vsnwprintf_s(buf, (size_t)cap, _TRUNCATE, fmt, ap);
#else
    int n = _vsnwprintf(buf, (size_t)cap, fmt, ap);
    if (n < 0 || n >= cap) {
        buf[cap - 1] = 0;
        n = -1;
    }
#endif
    va_end(ap);
    return n;
}

static WCHAR wlower_ascii(WCHAR c) { return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c; }

static bool wstarts_with_i(const WCHAR *s, const WCHAR *prefix)
{
    for (; *prefix; s++, prefix++)
        if (!*s || towlower(*s) != towlower(*prefix)) return false;
    return true;
}

static bool wends_with_i(const WCHAR *s, int n, const WCHAR *suffix)
{
    int m = wlen(suffix);
    return m <= n && _wcsnicmp(s + n - m, suffix, m) == 0;
}

// Replaces [a, b) of a text field (len chars, room for cap - 1) with n typed or pasted chars: line breaks and
// tabs become spaces, other control chars are dropped, what does not fit is cut. Returns the caret after it.
static int text_splice(WCHAR *s, int *len, int cap, int a, int b, const WCHAR *ins, int n)
{
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    int room = MAX(cap - 1 - (*len - (b - a)), 0), k = 0, last = 0;
    for (; last < n && k < room; last++)
        if (ins[last] >= 0x20 ? ins[last] != 0x7F : ins[last] == '\r' || ins[last] == '\n' || ins[last] == '\t') k++;
    memmove(s + a + k, s + b, (size_t)(*len - b) * sizeof(WCHAR));
    for (int i = 0, j = a; i < last; i++) {
        WCHAR c = ins[i];
        if (c == '\r' || c == '\n' || c == '\t') s[j++] = ' ';
        else if (c >= 0x20 && c != 0x7F) s[j++] = c;
    }
    *len += k - (b - a);
    s[*len] = 0;
    return a + k;
}

static int utf8_to_w(const char *s, int n, WCHAR *out, int cap)
{
    int r = MultiByteToWideChar(CP_UTF8, 0, s, n, out, cap - 1);
    if (r < 0) r = 0;
    out[r] = 0;
    return r;
}

static int w_to_utf8(const WCHAR *s, int n, char *out, int cap)
{
    int r = WideCharToMultiByte(CP_UTF8, 0, s, n, out, cap - 1, NULL, NULL);
    if (r < 0) r = 0;
    out[r] = 0;
    return r;
}

// ---- hashing

#define FNV_BASIS 1469598103934665603ull
#define FNV_PRIME 1099511628211ull

static u64 hash_wstr(const WCHAR *s, int len)
{
    u64 h = FNV_BASIS;
    if (len < 0) len = wlen(s);
    for (int i = 0; i < len; i++) h = (h ^ (u64)s[i]) * FNV_PRIME;
    return h ? h : 1;
}

static u64 hash_wstr_i(const WCHAR *s, int len)
{
    u64 h = FNV_BASIS;
    if (len < 0) len = wlen(s);
    for (int i = 0; i < len; i++) {
        WCHAR c = s[i] < 0x80 ? wlower_ascii(s[i]) : (WCHAR)(uintptr_t)CharLowerW((LPWSTR)(uintptr_t)s[i]);
        h = (h ^ (u64)c) * FNV_PRIME;
    }
    return h ? h : 1;
}

static u64 hash_u64(u64 x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}

// ---- time

static f64 g_qpc_inv;

static void time_init(void)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_qpc_inv = 1.0 / (f64)f.QuadPart;
}

static f64 time_now(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (f64)c.QuadPart * g_qpc_inv;
}

static i64 filetime_now(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((i64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static i64 unix_now(void) { return filetime_now() / 10000000 - 11644473600LL; }

// ---- files and the log

static void data_path(WCHAR *out, const WCHAR *file) { wfmt(out, MAX_PATH, L"%s\\%s", g_data_dir, file); }

static SRWLOCK g_log_lock = SRWLOCK_INIT;

static void log_msg(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    n = MIN(n, (int)sizeof(buf) - 3);
    buf[n++] = '\r';
    buf[n++] = '\n';

    WCHAR path[MAX_PATH];
    data_path(path, L"log.txt");
    AcquireSRWLockExclusive(&g_log_lock);
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    LARGE_INTEGER sz;
    if (f != INVALID_HANDLE_VALUE && GetFileSizeEx(f, &sz) && sz.QuadPart > 256 * 1024) {
        CloseHandle(f);
        f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    }
    if (f != INVALID_HANDLE_VALUE) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char ts[64];
        int tn = snprintf(ts, sizeof ts, "%04d-%02d-%02d %02d:%02d:%02d ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        DWORD w;
        WriteFile(f, ts, (DWORD)tn, &w, NULL);
        WriteFile(f, buf, (DWORD)n, &w, NULL);
        CloseHandle(f);
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}

// The whole file, zero-terminated (two zeros, for UTF-16 text too).
static char *read_file(const WCHAR *path, DWORD *out_size)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(f);
        return NULL;
    }
    DWORD n = (DWORD)sz.QuadPart, got = 0;
    char *buf = (char *)malloc(n + 2);
    if (buf && !ReadFile(f, buf, n, &got, NULL)) got = 0;
    CloseHandle(f);
    if (!buf) return NULL;
    buf[got] = buf[got + 1] = 0;
    if (out_size) *out_size = got;
    return buf;
}

static bool write_file_atomic(const WCHAR *path, const void *data, DWORD size)
{
    WCHAR tmp[MAX_PATH + 8];
    wfmt(tmp, countof(tmp), L"%s.tmp", path);
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    BOOL ok = WriteFile(f, data, size, &w, NULL) && w == size;
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp);
        return false;
    }
    return MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
}

static char *data_read(const WCHAR *file)
{
    WCHAR path[MAX_PATH];
    data_path(path, file);
    return read_file(path, NULL);
}

// Splits text into lines in place: each call returns the next one without its line break, NULL at the end.
static char *next_line(char **cursor)
{
    char *s = *cursor;
    if (!s || !*s) return NULL;
    char *e = s;
    while (*e && *e != '\n') e++;
    *cursor = *e ? e + 1 : e;
    if (e > s && e[-1] == '\r') e[-1] = 0;
    *e = 0;
    return s;
}

static bool utf8_bom(const char *s) { return (u8)s[0] == 0xEF && (u8)s[1] == 0xBB && (u8)s[2] == 0xBF; }

// ---- a growing byte buffer, for files written in one go

typedef struct Buf {
    char *data;
    size_t len, cap;
} Buf;

static void buf_put(Buf *b, const void *p, size_t n)
{
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n) nc *= 2;
        char *nd = (char *)realloc(b->data, nc);
        if (!nd) return;
        b->data = nd;
        b->cap = nc;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void buf_str(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }

static void buf_printf(Buf *b, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0) buf_put(b, tmp, (size_t)MIN(n, (int)sizeof(tmp) - 1));
}

static void buf_put_w(Buf *b, const WCHAR *s)
{
    char tmp[4 * 1024];
    int n = w_to_utf8(s, -1, tmp, sizeof tmp);
    if (n > 0 && tmp[n - 1] == 0) n--;  // the count includes the terminator
    buf_put(b, tmp, (size_t)n);
}

// Writes the buffer (atomically) and frees it.
static bool buf_save(Buf *b, const WCHAR *path)
{
    bool ok = write_file_atomic(path, b->data ? b->data : "", (DWORD)b->len);
    free(b->data);
    memset(b, 0, sizeof *b);
    return ok;
}

static bool buf_save_data(Buf *b, const WCHAR *file)
{
    WCHAR path[MAX_PATH];
    data_path(path, file);
    bool ok = buf_save(b, path);
    if (!ok) log_msg("could not save %ls", file);
    return ok;
}

// ---- threads and windows

static bool thread_start(LPTHREAD_START_ROUTINE fn, void *arg)
{
    HANDLE t = CreateThread(NULL, 0, fn, arg, 0, NULL);
    if (t) CloseHandle(t);
    return t != NULL;
}

static void pump_messages(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// Registering a class that exists already just fails, so callers need no guard.
static void window_class(const WCHAR *name, WNDPROC proc, UINT style, HICON icon, HICON icon_sm)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = style;
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hIcon = icon;
    wc.hIconSm = icon_sm;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
}

static HWND message_window(const WCHAR *cls, WNDPROC proc)
{
    window_class(cls, proc, 0, NULL, NULL);
    return CreateWindowExW(0, cls, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
}

static bool window_is_ours(HWND h)
{
    DWORD pid = 0;
    if (h) GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

static void track_mouse_leave(HWND h)
{
    TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, h, 0 };
    TrackMouseEvent(&tme);
}

static bool reg_dword(HKEY root, const WCHAR *key, const WCHAR *name, DWORD *out)
{
    DWORD sz = sizeof(DWORD);
    return RegGetValueW(root, key, name, RRF_RT_REG_DWORD, NULL, out, &sz) == ERROR_SUCCESS;
}

// A 32-bit top-down DIB.
static BITMAPINFO dib32(int w, int h)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return bi;
}

// DWM attributes and values by name: older SDKs lack them.
enum { DWA_NO_TRANSITIONS = 3, DWA_DARK = 20, DWA_CORNER = 33, DWA_BORDER_COLOR = 34, DWA_BACKDROP = 38 };
enum { DWC_SQUARE = 1, DWC_ROUND = 2, DWC_ROUND_SMALL = 3 };
enum { DWB_NONE = 1, DWB_MICA = 2, DWB_ACRYLIC = 3 };
#define DW_COLOR_NONE 0xFFFFFFFEu
#define DW_COLOR_DEFAULT 0xFFFFFFFFu

static bool dwm_set(HWND h, DWORD attr, DWORD v) { return SUCCEEDED(DwmSetWindowAttribute(h, attr, &v, sizeof v)); }

// A system backdrop under the whole window (the frame extended over the client area).
static bool dwm_backdrop(HWND h, DWORD type)
{
    MARGINS m = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(h, &m);
    return dwm_set(h, DWA_BACKDROP, type);
}

// ---- monitors

static UINT monitor_dpi(HMONITOR m)
{
    typedef HRESULT (WINAPI *PFN_GetDpiForMonitor)(HMONITOR, int, UINT *, UINT *);
    static PFN_GetDpiForMonitor f;
    static bool tried;
    if (!tried) {
        tried = true;
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) f = (PFN_GetDpiForMonitor)(void *)GetProcAddress(shcore, "GetDpiForMonitor");
    }
    UINT x = 96, y = 96;
    if (!f || FAILED(f(m, 0, &x, &y))) {
        HDC dc = GetDC(NULL);
        x = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    return x;
}

static RECT monitor_work(HMONITOR m)
{
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(m, &mi);
    return mi.rcWork;
}

static BOOL CALLBACK find_other_monitor(HMONITOR m, HDC dc, LPRECT r, LPARAM lp)
{
    HMONITOR *io = (HMONITOR *)lp;
    if (m == io[0]) return TRUE;
    io[1] = m;
    return FALSE;
}

// Where our windows open: the cursor's monitor, or another one for a test copy.
static HMONITOR open_monitor(void)
{
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR io[2] = { MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), NULL };
    if (g_pinned || g_other_monitor) EnumDisplayMonitors(NULL, NULL, find_other_monitor, (LPARAM)io);
    return io[1] ? io[1] : io[0];
}

// ---- the clipboard

static void clipboard_set(const WCHAR *s, int n)
{
    if (n < 0) n = wlen(s);
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (size_t)(n + 1) * sizeof(WCHAR));
    WCHAR *p = g ? (WCHAR *)GlobalLock(g) : NULL;
    if (p) {
        memcpy(p, s, (size_t)n * sizeof(WCHAR));
        p[n] = 0;
        GlobalUnlock(g);
        if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
    } else if (g) {
        GlobalFree(g);
    }
    CloseClipboard();
}

// The clipboard text, up to cap - 1 characters. Returns its length.
static int clipboard_get(WCHAR *out, int cap)
{
    int n = 0;
    if (cap <= 0) return 0;
    out[0] = 0;
    if (!OpenClipboard(g_hwnd)) return 0;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    const WCHAR *p = h ? (const WCHAR *)GlobalLock(h) : NULL;
    if (p) {
        while (p[n] && n < cap - 1) n++;
        memcpy(out, p, (size_t)n * sizeof(WCHAR));
        GlobalUnlock(h);
    }
    out[n] = 0;
    CloseClipboard();
    return n;
}
