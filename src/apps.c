enum { APP_SHELL, APP_CMD };
enum { CMD_NONE, CMD_LOCK, CMD_SLEEP, CMD_RESTART, CMD_SHUTDOWN, CMD_SIGNOUT, CMD_RECYCLE };

#define APP_MAX_KW 6

typedef struct App {
    WCHAR *name;
    WCHAR *norm;
    u8 *ws;
    int len;
    WCHAR *id;
    WCHAR *path;
    u64 id_hash;
    u8 kind;
    u8 cmd;
    bool danger;
    u16 glyph;
    int kw_count;
    WCHAR *kw[APP_MAX_KW];
    u8 *kw_ws[APP_MAX_KW];
    int kw_len[APP_MAX_KW];
    f32 frec;
} App;

typedef struct AppList {
    Arena arena;
    App *apps;
    int count;
    int shell_count;
    u64 signature;
} AppList;

static void applist_free(AppList *l)
{
    if (!l) return;
    arena_release(&l->arena);
    free(l);
}

typedef struct RawApp {
    WCHAR *name, *id, *path;
} RawApp;

static int cmp_raw_name(const void *a, const void *b)
{
    const RawApp *x = (const RawApp *)a, *y = (const RawApp *)b;
    int r = CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS, x->name, -1, y->name, -1, NULL, NULL, 0);
    if (r) return r - 2;
    return _wcsicmp(x->name, y->name);
}

static void app_fill_match(Arena *a, App *app)
{
    app->len = wlen(app->name);
    app->norm = (WCHAR *)arena_push(a, (size_t)(app->len + 1) * sizeof(WCHAR));
    norm_str(app->norm, app->name, app->len);
    app->ws = (u8 *)arena_push(a, (size_t)app->len + 1);
    word_starts(app->name, app->len, app->ws);
}

static bool looks_like_noise(const WCHAR *norm, const WCHAR *path)
{
    static const WCHAR *bad_names[] = { L"uninstall", L"деинсталл", L"удалить", L"удаление", L"uninstaller" };
    for (int i = 0; i < countof(bad_names); i++)
        if (wcsstr(norm, bad_names[i])) return true;
    if (path) {
        if (wstarts_with_i(path, L"http://") || wstarts_with_i(path, L"https://")) return true;
        const WCHAR *file = PathFindFileNameW(path);
        if (wstarts_with_i(file, L"unins") || wstarts_with_i(file, L"uninst")) return true;
        static const WCHAR *doc_ext[] = { L".chm", L".txt", L".pdf", L".htm", L".html", L".rtf", L".md", L".url", L".hlp" };
        int n = wlen(path);
        for (int i = 0; i < countof(doc_ext); i++)
            if (wends_with_i(path, n, doc_ext[i])) return true;
        if (path[1] == ':' && !*PathFindExtensionW(path)) {
            DWORD attr = GetFileAttributesW(path);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
        }
    }
    return false;
}

// "{GUID}\sub\app.exe" -> "C:\Program Files\sub\app.exe"
static WCHAR *resolve_known_folder_path(Arena *a, const WCHAR *id)
{
    if (!id) return NULL;
    if (((id[0] >= 'A' && id[0] <= 'Z') || (id[0] >= 'a' && id[0] <= 'z')) && id[1] == ':' && id[2] == '\\') return wdup(a, id, -1);
    if (id[0] != '{') return NULL;
    const WCHAR *close = wcschr(id, '}');
    if (!close || close - id != 37) return NULL;
    WCHAR gs[40];
    memcpy(gs, id, 38 * sizeof(WCHAR));
    gs[38] = 0;
    GUID g;
    if (FAILED(CLSIDFromString(gs, &g))) return NULL;
    PWSTR base = NULL;
    if (FAILED(SHGetKnownFolderPath(&g, KF_FLAG_DONT_VERIFY, NULL, &base)) || !base) return NULL;
    WCHAR *r = wcat3(a, base, L"", close + 1);
    CoTaskMemFree(base);
    return r;
}

// Explorer sometimes hands out a packaged app's name unresolved ("ms-resource:AppName"), e.g. while the
// package is being updated. Resolve it against the package ourselves; NULL if that fails too.
static WCHAR *resolve_ms_resource(Arena *a, const WCHAR *res, const WCHAR *aumid)
{
    typedef LONG(WINAPI * PFN_GetPackagesByPackageFamily)(PCWSTR, UINT32 *, PWSTR *, UINT32 *, WCHAR *);
    static PFN_GetPackagesByPackageFamily get_packages;
    static bool loaded;
    if (!loaded) {
        loaded = true;
        get_packages = (PFN_GetPackagesByPackageFamily)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetPackagesByPackageFamily");
    }
    const WCHAR *bang = wcschr(aumid, '!');
    if (!get_packages || !bang || bang - aumid >= 256) return NULL;
    WCHAR family[256];
    memcpy(family, aumid, (size_t)(bang - aumid) * sizeof(WCHAR));
    family[bang - aumid] = 0;
    WCHAR *us = wcsrchr(family, '_');
    if (!us) return NULL;

    UINT32 count = 0, len = 0;
    if (get_packages(family, &count, NULL, &len, NULL) != ERROR_INSUFFICIENT_BUFFER || !count || !len) return NULL;
    PWSTR *names = (PWSTR *)malloc(count * sizeof(PWSTR));
    WCHAR *buf = (WCHAR *)malloc(len * sizeof(WCHAR));
    WCHAR *r = NULL;
    if (names && buf && get_packages(family, &count, names, &len, buf) == ERROR_SUCCESS && count) {
        *us = 0;  // family -> package name
        const WCHAR *key = res + 12;  // after "ms-resource:"
        WCHAR src[1024], out[512];
        for (int v = 0; v < 3 && !r; v++) {
            int k;
            if (v == 0) {
                if (key[0] == '/' || wcschr(key, '/')) continue;
                k = _snwprintf(src, countof(src), L"@{%s?ms-resource://%s/resources/%s}", names[0], family, key);
            } else if (v == 1) {
                if (key[0] == '/') continue;
                k = _snwprintf(src, countof(src), L"@{%s?ms-resource://%s/%s}", names[0], family, key);
            } else {
                k = _snwprintf(src, countof(src), L"@{%s?%s}", names[0], res);
            }
            if (k <= 0 || k >= countof(src)) continue;
            out[0] = 0;
            if (SUCCEEDED(SHLoadIndirectString(src, out, countof(out), NULL)) && out[0] && !wstarts_with_i(out, L"ms-resource:")) r = wdup(a, out, -1);
        }
    }
    free(names);
    free(buf);
    return r;
}

static const struct {
    u8 cmd;
    const WCHAR *ru, *en;
    const WCHAR *kw;
    u16 glyph;
    bool danger;
} k_commands[] = {
    { CMD_LOCK,     L"Заблокировать",      L"Lock",        L"lock|lock screen|блокировка|заблокировать компьютер", 0xE72E, false },
    { CMD_SLEEP,    L"Спящий режим",       L"Sleep",       L"sleep|suspend|сон|спящий режим", 0xE708, false },
    { CMD_RESTART,  L"Перезагрузка",       L"Restart",     L"restart|reboot|перезагрузить|перезагрузка|рестарт", 0xE777, true },
    { CMD_SHUTDOWN, L"Завершение работы",  L"Shut down",   L"shutdown|shut down|power off|выключить|выключение|завершение работы", 0xE7E8, true },
    { CMD_SIGNOUT,  L"Выйти из системы",   L"Sign out",    L"sign out|log off|logout|выйти|выход из системы", 0xF3B1, true },
    { CMD_RECYCLE,  L"Корзина",            L"Recycle Bin", L"recycle bin|trash|корзина", 0xE74D, false },
};

static void app_add_kw(Arena *a, App *app, const WCHAR *s, int len)
{
    if (len < 3 || app->kw_count >= APP_MAX_KW) return;
    WCHAR *orig = wdup(a, s, len);
    WCHAR *norm = (WCHAR *)arena_push(a, (size_t)(len + 1) * sizeof(WCHAR));
    norm_str(norm, orig, len);
    if (len == app->len && wmem_eq(norm, app->norm, len)) return;
    for (int i = 0; i < app->kw_count; i++)
        if (app->kw_len[i] == len && wmem_eq(app->kw[i], norm, len)) return;
    int j = app->kw_count++;
    app->kw[j] = norm;
    app->kw_len[j] = len;
    app->kw_ws[j] = (u8 *)arena_push(a, (size_t)len + 1);
    word_starts(orig, len, app->kw_ws[j]);
}

static void app_add_hidden_keywords(Arena *a, App *app)
{
    if (app->path) {
        const WCHAR *file = PathFindFileNameW(app->path);
        const WCHAR *ext = PathFindExtensionW(file);
        if (!_wcsicmp(ext, L".exe") || !_wcsicmp(ext, L".msc") || !_wcsicmp(ext, L".cpl")) app_add_kw(a, app, file, (int)(ext - file));
    }
    const WCHAR *bang = wcschr(app->id, '!');
    const WCHAR *us = wcschr(app->id, '_');
    const WCHAR *end = us && bang && us < bang ? us : bang;
    if (end && end > app->id) {
        const WCHAR *start = end;
        while (start > app->id && start[-1] != '.') start--;
        app_add_kw(a, app, start, (int)(end - start));
    }
    static const struct { const WCHAR *id_prefix, *kw; } aliases[] = {
        { L"windows.immersivecontrolpanel", L"Settings" },
        { L"windows.immersivecontrolpanel", L"Параметры" },
        { L"Microsoft.Windows.Explorer", L"Explorer" },
        { L"Microsoft.Windows.ControlPanel", L"Control Panel" },
        { L"Microsoft.Windows.Shell.RunDialog", L"Run" },
        { L"Microsoft.WindowsTerminal", L"Terminal" },
    };
    for (int i = 0; i < countof(aliases); i++)
        if (wstarts_with_i(app->id, aliases[i].id_prefix)) app_add_kw(a, app, aliases[i].kw, wlen(aliases[i].kw));
}

static AppList *applist_build(RawApp *raw, int n)
{
    AppList *l = (AppList *)calloc(1, sizeof(AppList));
    if (!l) return NULL;
    arena_init(&l->arena, 256ull << 20);
    qsort(raw, n, sizeof(RawApp), cmp_raw_name);
    int ncmd = countof(k_commands);
    l->apps = arena_array(&l->arena, App, n + ncmd);
    u64 sig = 1469598103934665603ull;
    for (int i = 0; i < n; i++) {
        App *app = &l->apps[l->count++];
        app->kind = APP_SHELL;
        app->name = wdup(&l->arena, raw[i].name, -1);
        app->id = wdup(&l->arena, raw[i].id, -1);
        app->path = raw[i].path ? wdup(&l->arena, raw[i].path, -1) : NULL;
        app->id_hash = hash_wstr_i(app->id, -1);
        app_fill_match(&l->arena, app);
        app_add_hidden_keywords(&l->arena, app);
        sig = (sig ^ app->id_hash) * 1099511628211ull;
        sig = (sig ^ hash_wstr(app->name, -1)) * 1099511628211ull;
    }
    l->shell_count = l->count;
    for (int i = 0; i < ncmd; i++) {
        App *app = &l->apps[l->count++];
        app->kind = APP_CMD;
        app->cmd = k_commands[i].cmd;
        app->danger = k_commands[i].danger;
        app->glyph = k_commands[i].glyph;
        app->name = wdup(&l->arena, g_lang_ru ? k_commands[i].ru : k_commands[i].en, -1);
        WCHAR idbuf[32];
        _snwprintf(idbuf, countof(idbuf), L"cmd:%d", (int)app->cmd);
        idbuf[countof(idbuf) - 1] = 0;
        app->id = wdup(&l->arena, idbuf, -1);
        app->id_hash = hash_wstr_i(app->id, -1);
        app_fill_match(&l->arena, app);
        const WCHAR *k = k_commands[i].kw;
        while (*k && app->kw_count < APP_MAX_KW) {
            const WCHAR *e = k;
            while (*e && *e != '|') e++;
            int len = (int)(e - k);
            WCHAR *orig = wdup(&l->arena, k, len);
            int j = app->kw_count++;
            app->kw_len[j] = len;
            app->kw[j] = (WCHAR *)arena_push(&l->arena, (size_t)(len + 1) * sizeof(WCHAR));
            norm_str(app->kw[j], orig, len);
            app->kw_ws[j] = (u8 *)arena_push(&l->arena, (size_t)len + 1);
            word_starts(orig, len, app->kw_ws[j]);
            k = *e ? e + 1 : e;
        }
    }
    l->signature = sig;
    return l;
}

// The UI language changed: rename the commands of a loaded list.
static void apps_relocalize(AppList *l)
{
    if (!l) return;
    for (int i = l->shell_count; i < l->count; i++) {
        App *app = &l->apps[i];
        for (int k = 0; k < countof(k_commands); k++)
            if (k_commands[k].cmd == app->cmd) {
                app->name = wdup(&l->arena, g_lang_ru ? k_commands[k].ru : k_commands[k].en, -1);
                app_fill_match(&l->arena, app);
            }
    }
}

static void apps_cache_save(AppList *l)
{
    Buf b = { 0 };
    buf_put(&b, "# MixLauncher apps v1\n", 22);
    for (int i = 0; i < l->shell_count; i++) {
        App *a = &l->apps[i];
        buf_put_w(&b, a->name);
        buf_put(&b, "\t", 1);
        buf_put_w(&b, a->id);
        buf_put(&b, "\t", 1);
        if (a->path) buf_put_w(&b, a->path);
        buf_put(&b, "\n", 1);
    }
    WCHAR path[MAX_PATH];
    data_path(path, L"apps.cache");
    write_file_atomic(path, b.data, (DWORD)b.len);
    free(b.data);
}

static AppList *apps_cache_load(void)
{
    WCHAR path[MAX_PATH];
    data_path(path, L"apps.cache");
    char *text = read_file(path, NULL);
    if (!text) return NULL;
    Arena tmp;
    arena_init(&tmp, 64ull << 20);
    int cap = 1024, n = 0;
    RawApp *raw = (RawApp *)malloc(sizeof(RawApp) * cap);
    char *cur = text, *line;
    WCHAR w[4096];
    while (raw && (line = next_line(&cur)) != NULL) {
        if (line[0] == '#' || !line[0]) continue;
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2) continue;
        *t1 = *t2 = 0;
        if (n == cap) {
            cap *= 2;
            RawApp *nr = (RawApp *)realloc(raw, sizeof(RawApp) * cap);
            if (!nr) break;
            raw = nr;
        }
        RawApp *r = &raw[n];
        utf8_to_w(line, -1, w, countof(w));
        r->name = wdup(&tmp, w, -1);
        utf8_to_w(t1 + 1, -1, w, countof(w));
        r->id = wdup(&tmp, w, -1);
        utf8_to_w(t2 + 1, -1, w, countof(w));
        r->path = w[0] ? wdup(&tmp, w, -1) : NULL;
        if (r->name[0] && r->id[0] && !wstarts_with_i(r->name, L"ms-resource:")) n++;
    }
    free(text);
    AppList *l = (raw && n) ? applist_build(raw, n) : NULL;
    free(raw);
    arena_release(&tmp);
    return l;
}

static int g_apps_unresolved;

static AppList *apps_enumerate(void)
{
    g_apps_unresolved = 0;
    IShellItem *folder = NULL;
    IEnumShellItems *en = NULL;
    if (FAILED(SHGetKnownFolderItem(&ML_FOLDERID_AppsFolder, KF_FLAG_DEFAULT, NULL, &ML_IID_IShellItem, (void **)&folder)) || !folder) {
        log_msg("AppsFolder: SHGetKnownFolderItem failed");
        return NULL;
    }
    HRESULT hr = IShellItem_BindToHandler(folder, NULL, &ML_BHID_EnumItems, &ML_IID_IEnumShellItems, (void **)&en);
    IShellItem_Release(folder);
    if (FAILED(hr) || !en) {
        log_msg("AppsFolder: BindToHandler failed 0x%08lx", hr);
        return NULL;
    }
    Arena tmp;
    arena_init(&tmp, 64ull << 20);
    int cap = 1024, n = 0;
    RawApp *raw = (RawApp *)malloc(sizeof(RawApp) * cap);
    IShellItem *it = NULL;
    ULONG fetched = 0;
    while (raw && IEnumShellItems_Next(en, 1, &it, &fetched) == S_OK && it) {
        LPWSTR name = NULL, parse = NULL, target = NULL;
        IShellItem_GetDisplayName(it, SIGDN_NORMALDISPLAY, &name);
        IShellItem_GetDisplayName(it, SIGDN_PARENTRELATIVEPARSING, &parse);
        IShellItem2 *it2 = NULL;
        if (SUCCEEDED(IShellItem_QueryInterface(it, &ML_IID_IShellItem2, (void **)&it2)) && it2) {
            IShellItem2_GetString(it2, &ML_PKEY_Link_TargetParsingPath, &target);
            IShellItem2_Release(it2);
        }
        const WCHAR *disp = name;
        if (name && parse && wstarts_with_i(name, L"ms-resource:")) {
            disp = resolve_ms_resource(&tmp, name, parse);
            if (!disp) g_apps_unresolved++;
        }
        if (disp && parse && disp[0] && parse[0]) {
            WCHAR *path = target && target[0] ? wdup(&tmp, target, -1) : resolve_known_folder_path(&tmp, parse);
            if (path && path[0] == ':' && path[1] == ':') path = NULL;
            WCHAR norm[512];
            int nl = MIN(wlen(disp), 511);
            norm_str(norm, disp, nl);
            if (!(g_cfg.hide_uninstallers && looks_like_noise(norm, path))) {
                if (n == cap) {
                    cap *= 2;
                    RawApp *nr = (RawApp *)realloc(raw, sizeof(RawApp) * cap);
                    if (!nr) break;
                    raw = nr;
                }
                raw[n].name = wdup(&tmp, disp, -1);
                raw[n].id = wdup(&tmp, parse, -1);
                raw[n].path = path;
                n++;
            }
        }
        CoTaskMemFree(name);
        CoTaskMemFree(parse);
        CoTaskMemFree(target);
        IShellItem_Release(it);
        it = NULL;
    }
    IEnumShellItems_Release(en);
    AppList *l = raw ? applist_build(raw, n) : NULL;
    free(raw);
    arena_release(&tmp);
    return l;
}

static HANDLE g_index_event;
static volatile LONG g_index_busy;

static void apps_request_reindex(void)
{
    if (g_index_event) SetEvent(g_index_event);
}

static DWORD WINAPI indexer_thread(void *param)
{
    u64 last_sig = (u64)(uintptr_t)param;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    int retries = 0;
    HANDLE waits[3];
    int nw = 0;
    waits[nw++] = g_index_event;
    const GUID *dirs[2] = { &ML_FOLDERID_Programs, &ML_FOLDERID_CommonPrograms };
    for (int i = 0; i < 2; i++) {
        PWSTR p = NULL;
        if (SUCCEEDED(SHGetKnownFolderPath(dirs[i], 0, NULL, &p)) && p) {
            HANDLE h = FindFirstChangeNotificationW(p, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
            if (h != INVALID_HANDLE_VALUE) waits[nw++] = h;
        }
        CoTaskMemFree(p);
    }

    for (;;) {
        InterlockedExchange(&g_index_busy, 1);
        f64 t0 = time_now();
        AppList *l = apps_enumerate();
        InterlockedExchange(&g_index_busy, 0);
        if (l) {
            if (l->signature != last_sig) {
                last_sig = l->signature;
                apps_cache_save(l);
                log_msg("apps indexed: %d in %.0f ms", l->shell_count, (time_now() - t0) * 1000.0);
                if (!PostMessageW(g_hwnd, WM_APP_APPS_READY, 0, (LPARAM)l)) applist_free(l);
            } else {
                applist_free(l);
            }
        }
        // Unresolved names usually sort themselves out once the package update finishes.
        if (g_apps_unresolved) log_msg("apps: %d unresolved names skipped", g_apps_unresolved);
        retries = g_apps_unresolved ? retries + 1 : 0;
        DWORD wait = g_apps_unresolved && retries <= 10 ? 30 * 1000 : 20 * 60 * 1000;
        DWORD r = WaitForMultipleObjects((DWORD)nw, waits, FALSE, wait);
        if (r >= WAIT_OBJECT_0 + 1 && r < WAIT_OBJECT_0 + (DWORD)nw) {
            FindNextChangeNotification(waits[r - WAIT_OBJECT_0]);
            for (;;) {
                DWORD r2 = WaitForMultipleObjects((DWORD)nw - 1, waits + 1, FALSE, 1500);
                if (r2 == WAIT_TIMEOUT || r2 == WAIT_FAILED) break;
                FindNextChangeNotification(waits[1 + r2 - WAIT_OBJECT_0]);
            }
        }
    }
    return 0;
}
