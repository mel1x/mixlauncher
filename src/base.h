// base.h — types, arenas, strings, hashing, time, logging.
// Everything in this project is compiled as a single translation unit (see main.c).
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
#include <d3d11.h>
#include <dxgi1_3.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#endif

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

// Strings are UTF-16 everywhere: that is what Win32, the shell and Everything speak.
// TR() picks the UI language once at startup.
static bool g_lang_ru;
#define TR(ru, en) (g_lang_ru ? L##ru : L##en)

// ---------------------------------------------------------------------------------------------
// Arena: reserve a big virtual range once, commit on demand, free everything at once.

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
        if (nc > a->cap || !VirtualAlloc(a->base + a->committed, nc - a->committed, MEM_COMMIT, PAGE_READWRITE)) {
            // Out of reserved space: this is a programming error (reserves are huge), fail loudly.
            FatalAppExitW(0, L"MixLauncher: arena out of memory");
        }
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

// ---------------------------------------------------------------------------------------------
// Wide string helpers

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

static WCHAR *wcat3(Arena *a, const WCHAR *x, const WCHAR *y, const WCHAR *z)
{
    int lx = wlen(x), ly = wlen(y), lz = wlen(z);
    WCHAR *d = (WCHAR *)arena_push(a, (size_t)(lx + ly + lz + 1) * sizeof(WCHAR));
    if (lx) memcpy(d, x, lx * sizeof(WCHAR));
    if (ly) memcpy(d + lx, y, ly * sizeof(WCHAR));
    if (lz) memcpy(d + lx + ly, z, lz * sizeof(WCHAR));
    d[lx + ly + lz] = 0;
    return d;
}

// Heap copy, for strings crossing thread boundaries.
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
    int n = src ? wlen(src) : 0;
    if (n > cap - 1) n = cap - 1;
    if (n) memcpy(dst, src, (size_t)n * sizeof(WCHAR));
    dst[n] = 0;
}

static WCHAR wlower_ascii(WCHAR c) { return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c; }

static bool wstarts_with_i(const WCHAR *s, const WCHAR *prefix)
{
    for (; *prefix; s++, prefix++) {
        if (!*s) return false;
        if (towlower(*s) != towlower(*prefix)) return false;
    }
    return true;
}

static bool wends_with_i(const WCHAR *s, int n, const WCHAR *suffix)
{
    int m = wlen(suffix);
    if (m > n) return false;
    return _wcsnicmp(s + n - m, suffix, m) == 0;
}

// FNV-1a over UTF-16 code units. Case-insensitive variant folds with CharLower semantics.
static u64 hash_wstr(const WCHAR *s, int len)
{
    u64 h = 1469598103934665603ull;
    if (len < 0) len = wlen(s);
    for (int i = 0; i < len; i++) {
        h ^= (u64)s[i];
        h *= 1099511628211ull;
    }
    return h ? h : 1;
}

static u64 hash_wstr_i(const WCHAR *s, int len)
{
    u64 h = 1469598103934665603ull;
    if (len < 0) len = wlen(s);
    for (int i = 0; i < len; i++) {
        WCHAR c = s[i];
        if (c < 0x80) c = wlower_ascii(c);
        else c = (WCHAR)(uintptr_t)CharLowerW((LPWSTR)(uintptr_t)c);
        h ^= (u64)c;
        h *= 1099511628211ull;
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

// UTF-8 <-> UTF-16 into caller buffers.
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

// ---------------------------------------------------------------------------------------------
// Time

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

// Seconds since unix epoch.
static i64 unix_now(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    i64 t = ((i64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return t / 10000000 - 11644473600LL;
}

static i64 filetime_now(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((i64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

// ---------------------------------------------------------------------------------------------
// Paths & logging

static WCHAR g_data_dir[MAX_PATH];   // %LOCALAPPDATA%\MixLauncher
static WCHAR g_exe_path[MAX_PATH];

static void data_path(WCHAR *out, const WCHAR *file)
{
    _snwprintf(out, MAX_PATH, L"%s\\%s", g_data_dir, file);
    out[MAX_PATH - 1] = 0;
}

static SRWLOCK g_log_lock = SRWLOCK_INIT;

static void log_msg(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 3) n = (int)sizeof(buf) - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';

    WCHAR path[MAX_PATH];
    data_path(path, L"log.txt");
    AcquireSRWLockExclusive(&g_log_lock);
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz;
        // Keep the log small: start over when it grows past 256 KB.
        if (GetFileSizeEx(f, &sz) && sz.QuadPart > 256 * 1024) {
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
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}

// Read a whole file into malloc'ed memory (NUL-terminated). Caller frees.
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
    buf[got] = 0;
    buf[got + 1] = 0;
    if (out_size) *out_size = got;
    return buf;
}

// Atomic replace: write to .tmp then rename over the target.
static bool write_file_atomic(const WCHAR *path, const void *data, DWORD size)
{
    WCHAR tmp[MAX_PATH + 8];
    _snwprintf(tmp, countof(tmp), L"%s.tmp", path);
    tmp[countof(tmp) - 1] = 0;
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

// Growable byte buffer for building files.
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
    // w_to_utf8 with -1 includes the terminator in the count; drop it.
    if (n > 0 && tmp[n - 1] == 0) n--;
    buf_put(b, tmp, (size_t)n);
}

// Split a buffer into lines in place. Returns next line start or NULL at end.
static char *next_line(char **cursor)
{
    char *s = *cursor;
    if (!s || !*s) return NULL;
    char *e = s;
    while (*e && *e != '\n') e++;
    char *line = s;
    if (*e == '\n') {
        *cursor = e + 1;
    } else {
        *cursor = e;
    }
    if (e > s && e[-1] == '\r') e[-1] = 0;
    *e = 0;
    return line;
}

// ---------------------------------------------------------------------------------------------
// Messages posted to the main window by worker threads.

enum {
    WM_APP_TOGGLE = WM_APP + 1,  // hotkey / Win tap
    WM_APP_SHOW,                 // second instance asked us to show
    WM_APP_APPS_READY,           // lParam: AppList*
    WM_APP_ICONS_READY,
    WM_APP_FILES_READY,          // lParam: FileResults*
    WM_APP_TRAY,
    WM_APP_EXIT,
    WM_APP_LAUNCH_FAILED,        // lParam: heap WCHAR* key of item that failed
    WM_APP_CLICK_OUTSIDE,        // mouse button pressed outside the open launcher
    WM_APP_SETTINGS,             // open the settings window (second instance started with --settings)
    WM_APP_KEYCAP,               // hook -> settings window while recording a hotkey: wParam vk, lParam 1 down / 0 up
};

static HWND g_hwnd;   // main window, target of all worker notifications
