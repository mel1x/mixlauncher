// Updates from GitHub releases: a periodic check of the latest release, then on request a download of the
// installer (installed copy) or the portable exe (portable copy) and a restart into the new version.
// Nothing but the release query and the download goes over the network.

#define UPD_HOST L"api.github.com"
#define UPD_PATH L"/repos/mel1x/mixlauncher/releases/latest"
#define UPD_SETUP_ASSET "MixLauncher-Setup.exe"
#define UPD_PORTABLE_ASSET "MixLauncher-portable.exe"
#define UPD_CHECK_EVERY_MS (6 * 3600 * 1000)
#define TIMER_UPDATE 3

static bool g_pinned;

enum { UPD_IDLE, UPD_CHECKING, UPD_LATEST, UPD_AVAILABLE, UPD_DOWNLOADING, UPD_INSTALLING, UPD_ERROR };

static struct {
    volatile LONG state;
    volatile LONG progress;  // 0..1000 while downloading
    volatile LONG busy;
    WCHAR latest[32];
    WCHAR url[1024];
    WCHAR file[MAX_PATH];
    int cur[3];
    bool dry_run;
} UPD;

static void upd_notify(void)
{
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_UPDATE, 0, 0);
}

static void upd_set(LONG st)
{
    InterlockedExchange(&UPD.state, st);
    upd_notify();
}

static int upd_state(void) { return (int)InterlockedCompareExchange(&UPD.state, 0, 0); }

static bool upd_installed(void)
{
    WCHAR p[MAX_PATH];
    wcopy(p, MAX_PATH, g_exe_path);
    WCHAR *slash = wcsrchr(p, '\\');
    if (!slash) return false;
    wcopy(slash + 1, (int)(MAX_PATH - (slash + 1 - p)), L"unins000.exe");
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

static void upd_parse_ver(const char *s, int *v)
{
    v[0] = v[1] = v[2] = 0;
    if (*s == 'v' || *s == 'V') s++;
    for (int i = 0; i < 3 && *s; i++) {
        v[i] = atoi(s);
        while (*s >= '0' && *s <= '9') s++;
        if (*s != '.') break;
        s++;
    }
}

static bool upd_newer(const int *a, const int *b)
{
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

static HINTERNET upd_session(void)
{
    HINTERNET s = WinHttpOpen(L"MixLauncher/" ML_VER_WSTR, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"MixLauncher/" ML_VER_WSTR, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) {
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
        WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    }
    return s;
}

// GET https://host/path; the body goes to out (memory) or to the file (with progress). Follows redirects.
static bool upd_get(HINTERNET s, const WCHAR *url, Buf *out, HANDLE file, bool progress)
{
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    WCHAR host[256], path[2048];
    uc.lpszHostName = host;
    uc.dwHostNameLength = countof(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = countof(path);
    if (!WinHttpCrackUrl(url, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    if (!c) return false;
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    bool ok = false;
    const WCHAR *hdr = out ? L"Accept: application/vnd.github+json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
    if (r && WinHttpSendRequest(r, hdr, out ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, sz = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        DWORD total = 0;
        sz = sizeof total;
        if (!WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &total, &sz,
                                 WINHTTP_NO_HEADER_INDEX))
            total = 0;
        if (status == 200) {
            ok = true;
            u64 got = 0;
            char chunk[64 * 1024];
            for (;;) {
                DWORD n = 0;
                if (!WinHttpReadData(r, chunk, sizeof chunk, &n)) {
                    ok = false;
                    break;
                }
                if (!n) break;
                got += n;
                if (out) {
                    if (got > 4 * 1024 * 1024) {
                        ok = false;
                        break;
                    }
                    buf_put(out, chunk, n);
                } else {
                    DWORD w = 0;
                    if (!WriteFile(file, chunk, n, &w, NULL) || w != n) {
                        ok = false;
                        break;
                    }
                }
                if (progress && total) {
                    InterlockedExchange(&UPD.progress, (LONG)MIN(1000, got * 1000 / total));
                    upd_notify();
                }
            }
            if (ok && total && got != total) ok = false;
        }
    }
    if (r) WinHttpCloseHandle(r);
    WinHttpCloseHandle(c);
    return ok;
}

// Minimal field lookup in the release JSON: "key":"value" with no escapes we care about.
static bool json_str(const char *js, const char *key, const char *from, char *out, int cap)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(from ? from : js, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return false;
    p++;
    int n = 0;
    while (*p && *p != '"' && n < cap - 1) out[n++] = *p++;
    out[n] = 0;
    return *p == '"';
}

static DWORD WINAPI upd_check_thread(void *arg)
{
    (void)arg;
    HINTERNET s = upd_session();
    Buf b = { 0 };
    bool ok = s && upd_get(s, L"https://" UPD_HOST UPD_PATH, &b, NULL, false);
    if (s) WinHttpCloseHandle(s);
    LONG result = UPD_ERROR;
    if (ok) {
        buf_put(&b, "", 1);
        char tag[64], url[1024];
        const char *asset = upd_installed() ? UPD_SETUP_ASSET : UPD_PORTABLE_ASSET;
        bool found = false;
        if (json_str(b.data, "tag_name", NULL, tag, sizeof tag)) {
            for (const char *p = b.data; (p = strstr(p, "\"browser_download_url\"")) != NULL; p++) {
                if (json_str(b.data, "browser_download_url", p, url, sizeof url)) {
                    size_t n = strlen(url), m = strlen(asset);
                    if (n > m && url[n - m - 1] == '/' && !_stricmp(url + n - m, asset) && !strncmp(url, "https://github.com/", 19)) {
                        found = true;
                        break;
                    }
                }
            }
            int v[3];
            upd_parse_ver(tag, v);
            if (found && upd_newer(v, UPD.cur)) {
                WCHAR wtag[32];
                utf8_to_w(tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag, -1, wtag, countof(wtag));
                wcopy(UPD.latest, countof(UPD.latest), wtag);
                utf8_to_w(url, -1, UPD.url, countof(UPD.url));
                result = UPD_AVAILABLE;
            } else {
                result = UPD_LATEST;
            }
        }
    }
    if (!ok) log_msg("update check failed");
    free(b.data);
    InterlockedExchange(&UPD.busy, 0);
    upd_set(result);
    return 0;
}

static void upd_check(void)
{
    int st = upd_state();
    if (st == UPD_DOWNLOADING || st == UPD_INSTALLING) return;
    if (InterlockedCompareExchange(&UPD.busy, 1, 0)) return;
    if (st != UPD_AVAILABLE) upd_set(UPD_CHECKING);
    HANDLE t = CreateThread(NULL, 0, upd_check_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else {
        InterlockedExchange(&UPD.busy, 0);
        upd_set(UPD_ERROR);
    }
}

static bool upd_file_is_exe(const WCHAR *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    char mz[2] = { 0 };
    DWORD n = 0;
    LARGE_INTEGER sz;
    bool ok = ReadFile(f, mz, 2, &n, NULL) && n == 2 && mz[0] == 'M' && mz[1] == 'Z' && GetFileSizeEx(f, &sz) && sz.QuadPart > 64 * 1024;
    CloseHandle(f);
    return ok;
}

static DWORD WINAPI upd_download_thread(void *arg)
{
    (void)arg;
    WCHAR dir[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, dir);
    bool ok = n && n < MAX_PATH - 40;
    if (ok) {
        wcscat(dir, L"MixLauncher-update");
        CreateDirectoryW(dir, NULL);
        _snwprintf(UPD.file, MAX_PATH, L"%s\\%s", dir, upd_installed() ? L"" UPD_SETUP_ASSET : L"" UPD_PORTABLE_ASSET);
        UPD.file[MAX_PATH - 1] = 0;
        HANDLE f = CreateFileW(UPD.file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ok = f != INVALID_HANDLE_VALUE;
        if (ok) {
            HINTERNET s = upd_session();
            ok = s && upd_get(s, UPD.url, NULL, f, true);
            if (s) WinHttpCloseHandle(s);
            CloseHandle(f);
            ok = ok && upd_file_is_exe(UPD.file);
            if (!ok) DeleteFileW(UPD.file);
        }
    }
    if (!ok) log_msg("update download failed");
    InterlockedExchange(&UPD.busy, 0);
    upd_set(ok ? UPD_INSTALLING : UPD_ERROR);
    return 0;
}

static void upd_download(void)
{
    if (upd_state() != UPD_AVAILABLE && upd_state() != UPD_ERROR) return;
    if (!UPD.url[0]) {
        upd_check();
        return;
    }
    if (InterlockedCompareExchange(&UPD.busy, 1, 0)) return;
    InterlockedExchange(&UPD.progress, 0);
    upd_set(UPD_DOWNLOADING);
    HANDLE t = CreateThread(NULL, 0, upd_download_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else {
        InterlockedExchange(&UPD.busy, 0);
        upd_set(UPD_ERROR);
    }
}

static bool upd_run(const WCHAR *file, const WCHAR *args)
{
    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof sei);
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = file;
    sei.lpParameters = args;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != 0;
}

// Runs on the UI thread once the download is ready. The installer closes this instance itself
// (PrepareToInstall runs --shutdown) and starts the new one; the portable exe is swapped in place.
static void upd_apply(void)
{
    if (UPD.dry_run) {
        log_msg("update dry run: downloaded, not applied");
        upd_set(UPD_ERROR);
        return;
    }
    if (upd_installed()) {
        if (!upd_run(UPD.file, L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /update=1")) upd_set(UPD_ERROR);
        return;
    }
    WCHAR old[MAX_PATH + 8];
    _snwprintf(old, countof(old), L"%s.old", g_exe_path);
    old[countof(old) - 1] = 0;
    DeleteFileW(old);
    if (!MoveFileExW(g_exe_path, old, MOVEFILE_REPLACE_EXISTING)) {
        upd_set(UPD_ERROR);
        return;
    }
    if (!MoveFileExW(UPD.file, g_exe_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        MoveFileExW(old, g_exe_path, MOVEFILE_REPLACE_EXISTING);
        upd_set(UPD_ERROR);
        return;
    }
    WCHAR args[32];
    _snwprintf(args, countof(args), L"--after-update %lu", GetCurrentProcessId());
    args[countof(args) - 1] = 0;
    if (!upd_run(g_exe_path, args)) {
        MoveFileExW(g_exe_path, UPD.file, MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(old, g_exe_path, MOVEFILE_REPLACE_EXISTING);
        upd_set(UPD_ERROR);
        return;
    }
    PostMessageW(g_hwnd, WM_APP_EXIT, 0, 0);
}

static void upd_on_message(void)
{
    if (upd_state() == UPD_INSTALLING && UPD.file[0]) {
        upd_apply();
        UPD.file[0] = 0;
    }
}

static void upd_init(const WCHAR *pretend_version)
{
    char v[32];
    w_to_utf8(pretend_version ? pretend_version : L"" ML_VER_WSTR, -1, v, sizeof v);
    upd_parse_ver(v, UPD.cur);
    // A previous portable update leaves the old exe behind.
    WCHAR old[MAX_PATH + 8];
    _snwprintf(old, countof(old), L"%s.old", g_exe_path);
    old[countof(old) - 1] = 0;
    DeleteFileW(old);
}

static void upd_schedule(UINT first_ms)
{
    if (g_cfg.update_check && !g_pinned) SetTimer(g_hwnd, TIMER_UPDATE, first_ms, NULL);
    else KillTimer(g_hwnd, TIMER_UPDATE);
}

static void upd_on_timer(void)
{
    SetTimer(g_hwnd, TIMER_UPDATE, UPD_CHECK_EVERY_MS, NULL);
    if (g_cfg.update_check) upd_check();
}

static bool upd_visible(void)
{
    int st = upd_state();
    return st == UPD_AVAILABLE || st == UPD_DOWNLOADING || st == UPD_INSTALLING || (st == UPD_ERROR && UPD.url[0]);
}

static const WCHAR *upd_label(WCHAR *buf, int cap)
{
    switch (upd_state()) {
    case UPD_CHECKING: return TR("Проверка…", "Checking…");
    case UPD_LATEST: return TR("Последняя версия", "Up to date");
    case UPD_AVAILABLE: _snwprintf(buf, cap, L"%ls %ls", TR("Обновить до", "Update to"), UPD.latest); break;
    case UPD_DOWNLOADING: _snwprintf(buf, cap, L"%ls %d%%", TR("Загрузка", "Downloading"), (int)(UPD.progress / 10)); break;
    case UPD_INSTALLING: return TR("Установка…", "Installing…");
    case UPD_ERROR: return UPD.url[0] ? TR("Ошибка, повторить", "Failed, retry") : TR("Нет связи с GitHub", "GitHub unreachable");
    default: return TR("Проверить", "Check now");
    }
    buf[cap - 1] = 0;
    return buf;
}
