enum { APP_SHELL, APP_CMD };
enum { CMD_NONE, CMD_LOCK, CMD_SLEEP, CMD_RESTART, CMD_SHUTDOWN, CMD_SIGNOUT, CMD_RECYCLE, CMD_UNINSTALL, CMD_ADDREMOVE, CMD_HIBERNATE };

#define APP_MAX_KW 12

typedef struct App {
    WCHAR *name;
    WCHAR *norm;  // name for matching (norm_str) and its word starts
    u8 *ws;
    int len;
    WCHAR *id;    // the Apps folder parsing name (AUMID or path), "cmd:N" for a command
    WCHAR *path;  // the target file, when there is one
    u64 id_hash;
    u8 kind;
    u8 cmd;       // APP_CMD: CMD_*, its glyph, and whether it asks for a second Enter
    bool danger;
    u16 glyph;
    int kw_count;  // extra words it is found by: the exe name, the package, aliases, command synonyms
    WCHAR *kw[APP_MAX_KW];
    u8 *kw_ws[APP_MAX_KW];
    int kw_len[APP_MAX_KW];
    f32 frec;     // launch frequency bonus, refreshed on show
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

// An app as read from the Apps folder or the cache, before applist_build.
typedef struct RawApp {
    WCHAR *name, *id, *path;
} RawApp;

typedef struct RawApps {
    Arena arena;
    RawApp *items;
    int n, cap;
} RawApps;

static void raw_init(RawApps *r)
{
    memset(r, 0, sizeof *r);
    arena_init(&r->arena, 64ull << 20);
}

static void raw_free(RawApps *r)
{
    free(r->items);
    arena_release(&r->arena);
}

static bool raw_add(RawApps *r, const WCHAR *name, const WCHAR *id, const WCHAR *path)
{
    if (!GROW(r->items, r->cap, r->n + 1, 1024)) return false;
    RawApp *a = &r->items[r->n++];
    a->name = wdup(&r->arena, name, -1);
    a->id = wdup(&r->arena, id, -1);
    a->path = path && path[0] ? wdup(&r->arena, path, -1) : NULL;
    return true;
}

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
    static const WCHAR *bad_names[] = { L"uninstall", L"деинсталл", L"удалить", L"удаление" };
    for (int i = 0; i < countof(bad_names); i++)
        if (wcsstr(norm, bad_names[i])) return true;
    if (path) {
        if (wstarts_with_i(path, L"http://") || wstarts_with_i(path, L"https://")) return true;
        const WCHAR *file = PathFindFileNameW(path);
        if (wstarts_with_i(file, L"unins")) return true;
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

// "{GUID}\sub\app.exe" -> "C:\Program Files\sub\app.exe". False when the id is no path.
static bool resolve_known_folder_path(const WCHAR *id, WCHAR *out, int cap)
{
    out[0] = 0;
    if (!id) return false;
    if (((id[0] >= 'A' && id[0] <= 'Z') || (id[0] >= 'a' && id[0] <= 'z')) && id[1] == ':' && id[2] == '\\') {
        wcopy(out, cap, id);
        return true;
    }
    const WCHAR *close = id[0] == '{' ? wcschr(id, '}') : NULL;
    if (!close || close - id != 37) return false;
    WCHAR gs[40];
    memcpy(gs, id, 38 * sizeof(WCHAR));
    gs[38] = 0;
    GUID g;
    PWSTR base = NULL;
    bool ok = SUCCEEDED(CLSIDFromString(gs, &g)) && SUCCEEDED(SHGetKnownFolderPath(&g, KF_FLAG_DONT_VERIFY, NULL, &base)) && base;
    if (ok) wfmt(out, cap, L"%s%s", base, close + 1);
    CoTaskMemFree(base);
    return ok;
}

// Explorer may hand out a packaged app's name unresolved ("ms-resource:...", e.g. mid-update): resolve it ourselves.
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
                k = wfmt(src, countof(src), L"@{%s?ms-resource://%s/resources/%s}", names[0], family, key);
            } else if (v == 1) {
                if (key[0] == '/') continue;
                k = wfmt(src, countof(src), L"@{%s?ms-resource://%s/%s}", names[0], family, key);
            } else {
                k = wfmt(src, countof(src), L"@{%s?%s}", names[0], res);
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
    { CMD_SLEEP,    L"Спящий режим",       L"Sleep",       L"sleep|suspend|сон|спящий режим", 0xE708, true },
    { CMD_RESTART,  L"Перезагрузка",       L"Restart",     L"restart|reboot|перезагрузить|перезагрузка|рестарт", 0xE777, true },
    { CMD_SHUTDOWN, L"Завершение работы",  L"Shut down",   L"shutdown|shut down|power off|выключить|выключение|завершение работы", 0xE7E8, true },
    { CMD_SIGNOUT,  L"Выйти из системы",   L"Sign out",    L"sign out|log off|logout|выйти|выход из системы", 0xF3B1, true },
    { CMD_RECYCLE,  L"Корзина",            L"Recycle Bin", L"recycle bin|trash|корзина", 0xE74D, false },
    { CMD_UNINSTALL, L"Установленные приложения", L"Installed apps",
      L"installed programs|apps and features|установленные программы|приложения и возможности", 0xE71D, false },
    // Start's classic name for the same page: "удаление" / "uninstall" find this one
    { CMD_ADDREMOVE, L"Установка и удаление программ", L"Add or remove programs",
      L"uninstall|remove program|programs and features|appwiz|удаление программ|удалить программу|программы и компоненты|деинсталляция",
      0xECAA, false },
};

static void app_push_kw(Arena *a, App *app, const WCHAR *s, int len)
{
    if (app->kw_count >= APP_MAX_KW) return;
    int j = app->kw_count++;
    app->kw[j] = (WCHAR *)arena_push(a, (size_t)(len + 1) * sizeof(WCHAR));
    norm_str(app->kw[j], s, len);
    app->kw_len[j] = len;
    app->kw_ws[j] = (u8 *)arena_push(a, (size_t)len + 1);
    word_starts(s, len, app->kw_ws[j]);
}

// A hidden keyword, unless it is too short or repeats the name or another keyword.
static void app_add_kw(Arena *a, App *app, const WCHAR *s, int len)
{
    WCHAR norm[256];
    if (len < 3 || len >= countof(norm)) return;
    norm_str(norm, s, len);
    if (len == app->len && wmem_eq(norm, app->norm, len)) return;
    for (int i = 0; i < app->kw_count; i++)
        if (app->kw_len[i] == len && wmem_eq(app->kw[i], norm, len)) return;
    app_push_kw(a, app, s, len);
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

// Sorted by name, then the commands. The signature tells whether anything changed since the last list.
static AppList *applist_build(RawApps *r)
{
    RawApp *raw = r->items;
    int n = r->n;
    AppList *l = (AppList *)calloc(1, sizeof(AppList));
    if (!l) return NULL;
    arena_init(&l->arena, 256ull << 20);
    qsort(raw, n, sizeof(RawApp), cmp_raw_name);
    int ncmd = countof(k_commands);
    l->apps = arena_array(&l->arena, App, n + ncmd);
    u64 sig = FNV_BASIS;
    for (int i = 0; i < n; i++) {
        App *app = &l->apps[l->count++];
        app->kind = APP_SHELL;
        app->name = wdup(&l->arena, raw[i].name, -1);
        app->id = wdup(&l->arena, raw[i].id, -1);
        app->path = raw[i].path ? wdup(&l->arena, raw[i].path, -1) : NULL;
        app->id_hash = hash_wstr_i(app->id, -1);
        app_fill_match(&l->arena, app);
        app_add_hidden_keywords(&l->arena, app);
        sig = (sig ^ app->id_hash) * FNV_PRIME;
        sig = (sig ^ hash_wstr(app->name, -1)) * FNV_PRIME;
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
        wfmt(idbuf, countof(idbuf), L"cmd:%d", (int)app->cmd);
        app->id = wdup(&l->arena, idbuf, -1);
        app->id_hash = hash_wstr_i(app->id, -1);
        app_fill_match(&l->arena, app);
        for (const WCHAR *k = k_commands[i].kw; *k;) {
            const WCHAR *e = k;
            while (*e && *e != '|') e++;
            app_push_kw(&l->arena, app, k, (int)(e - k));  // all of them: the name changes with the language
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

// name, id, path: one app per line (the cache, --dump-apps)
static void apps_write(Buf *b, AppList *l, int n)
{
    for (int i = 0; i < n; i++) {
        App *a = &l->apps[i];
        buf_put_w(b, a->name);
        buf_put(b, "\t", 1);
        buf_put_w(b, a->id);
        buf_put(b, "\t", 1);
        if (a->path) buf_put_w(b, a->path);
        buf_put(b, "\n", 1);
    }
}

static void apps_cache_save(AppList *l)
{
    Buf b = { 0 };
    buf_str(&b, "# MixLauncher apps v1\n");
    apps_write(&b, l, l->shell_count);
    buf_save_data(&b, L"apps.cache");
}

static AppList *apps_cache_load(void)
{
    char *text = data_read(L"apps.cache");
    if (!text) return NULL;
    RawApps raw;
    raw_init(&raw);
    char *cur = text, *line;
    WCHAR name[1024], id[1024], path[2048];
    while ((line = next_line(&cur)) != NULL) {
        char *t1 = line[0] == '#' ? NULL : strchr(line, '\t');
        char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (!t2) continue;
        *t1 = *t2 = 0;
        utf8_to_w(line, -1, name, countof(name));
        utf8_to_w(t1 + 1, -1, id, countof(id));
        utf8_to_w(t2 + 1, -1, path, countof(path));
        if (name[0] && id[0] && !wstarts_with_i(name, L"ms-resource:") && !raw_add(&raw, name, id, path)) break;
    }
    free(text);
    AppList *l = raw.n ? applist_build(&raw) : NULL;
    raw_free(&raw);
    return l;
}

// Everything in the Apps folder (Start's "All apps"): shortcuts, packaged apps, system entries.
static AppList *apps_enumerate(int *unresolved)
{
    if (unresolved) *unresolved = 0;
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
    RawApps raw;
    raw_init(&raw);
    IShellItem *it = NULL;
    ULONG fetched = 0;
    bool ok = true;
    while (ok && IEnumShellItems_Next(en, 1, &it, &fetched) == S_OK && it) {
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
            disp = resolve_ms_resource(&raw.arena, name, parse);
            if (!disp && unresolved) (*unresolved)++;
        }
        if (disp && parse && disp[0] && parse[0]) {
            WCHAR path[2048];
            if (target && target[0]) wcopy(path, countof(path), target);
            else resolve_known_folder_path(parse, path, countof(path));
            if (path[0] == ':' && path[1] == ':') path[0] = 0;  // a shell namespace item, no file
            WCHAR norm[512];
            int nl = MIN(wlen(disp), 511);
            norm_str(norm, disp, nl);
            if (!(g_cfg.hide_uninstallers && looks_like_noise(norm, path[0] ? path : NULL))) ok = raw_add(&raw, disp, parse, path);
        }
        CoTaskMemFree(name);
        CoTaskMemFree(parse);
        CoTaskMemFree(target);
        IShellItem_Release(it);
        it = NULL;
    }
    IEnumShellItems_Release(en);
    AppList *l = applist_build(&raw);
    raw_free(&raw);
    return l;
}

static HANDLE g_index_event;

static void apps_request_reindex(void)
{
    if (g_index_event) SetEvent(g_index_event);
}

// Lists the apps again on request, when the Start menu folders change, and every 20 minutes. A list that
// differs from the last one is cached and posted to the window (WM_APP_APPS_READY).
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
        f64 t0 = time_now();
        int unresolved = 0;
        AppList *l = apps_enumerate(&unresolved);
        if (l && l->signature != last_sig) {
            last_sig = l->signature;
            apps_cache_save(l);
            log_msg("apps indexed: %d in %.0f ms", l->shell_count, (time_now() - t0) * 1000.0);
            if (!PostMessageW(g_hwnd, WM_APP_APPS_READY, 0, (LPARAM)l)) applist_free(l);
        } else {
            applist_free(l);
        }
        // Unresolved names usually sort themselves out once the package update finishes.
        if (unresolved) log_msg("apps: %d unresolved names skipped", unresolved);
        retries = unresolved ? retries + 1 : 0;
        DWORD wait = unresolved && retries <= 10 ? 30 * 1000 : 20 * 60 * 1000;
        DWORD r = WaitForMultipleObjects((DWORD)nw, waits, FALSE, wait);
        if (r >= WAIT_OBJECT_0 + 1 && r < WAIT_OBJECT_0 + (DWORD)nw) {
            // a folder changed: wait for the burst of changes (an installer at work) to settle
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

static void apps_index_start(u64 cached_signature)
{
    g_index_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    thread_start(indexer_thread, (void *)(uintptr_t)cached_signature);
}
