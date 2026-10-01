// everything.c — file search through Everything's WM_COPYDATA IPC (no SDK DLL needed).

#define EV_COPYDATA_QUERY2W 18
#define EV_REQ_NAME 0x00000001
#define EV_REQ_PATH 0x00000002
#define EV_REQ_FULL_PATH 0x00000004
#define EV_REQ_EXTENSION 0x00000008
#define EV_REQ_SIZE 0x00000010
#define EV_REQ_DATE_CREATED 0x00000020
#define EV_REQ_DATE_MODIFIED 0x00000040
#define EV_REQ_DATE_ACCESSED 0x00000080
#define EV_REQ_ATTRIBUTES 0x00000100
#define EV_REQ_FILE_LIST_FILE_NAME 0x00000200
#define EV_REQ_RUN_COUNT 0x00000400
#define EV_REQ_DATE_RUN 0x00000800
#define EV_REQ_DATE_RECENTLY_CHANGED 0x00001000
#define EV_REQ_HIGHLIGHTED_NAME 0x00002000
#define EV_REQ_HIGHLIGHTED_PATH 0x00004000
#define EV_REQ_HIGHLIGHTED_FULL_PATH 0x00008000
#define EV_SORT_DATE_MODIFIED_DESC 14
#define EV_ITEM_FOLDER 0x1
#define EV_ITEM_DRIVE 0x2
#define EV_FIRST_PAGE 256
#define EV_PAGE 256         // each further page

enum { EV_OK, EV_NOT_RUNNING, EV_ERROR };
enum { FI_FOLDER = 1 };

typedef struct FileItem {
    WCHAR *name;
    WCHAR *dir;
    WCHAR *full;
    i64 mtime;         // FILETIME ticks
    u32 flags;
    f32 score;
    u64 icon_key;
} FileItem;

typedef struct FileResults {
    Arena arena;
    u32 id;
    int status;
    u32 total;         // files matching in Everything
    u32 fetched;
    bool page;
    int count;
    FileItem *items;
} FileResults;

static void fileresults_free(FileResults *r)
{
    if (!r) return;
    arena_release(&r->arena);
    free(r);
}

typedef struct RawFile {
    WCHAR *name, *path;
    i64 mtime;
    u32 flags;
} RawFile;

static struct {
    SRWLOCK lock;
    HANDLE event;
    u32 req_id;
    WCHAR req_query[1024];
    bool has_req;
    WCHAR filter[512];

    HWND reply;
    DWORD tag;
    DWORD waiting_tag;
    bool got;
    Arena scratch;
    RawFile *raw;
    int raw_count, raw_cap;
    u32 raw_total;
    WCHAR profile_norm[MAX_PATH];
    int profile_len;

    u32 more_id;
    bool has_more;
    u32 cur_id, cur_total, cur_fetched;
    WCHAR cur_search[2048], cur_qn[1024];
    int cur_qlen;
    bool cur_plain;
    u64 *seen;
    u32 seen_cap, seen_count;
} E;

static void ev_request(u32 id, const WCHAR *query)
{
    AcquireSRWLockExclusive(&E.lock);
    E.req_id = id;
    wcopy(E.req_query, countof(E.req_query), query);
    wcopy(E.filter, countof(E.filter), g_cfg.everything_filter);
    E.has_req = true;
    ReleaseSRWLockExclusive(&E.lock);
    SetEvent(E.event);
}

static void ev_request_more(u32 id)
{
    AcquireSRWLockExclusive(&E.lock);
    E.more_id = id;
    E.has_more = true;
    ReleaseSRWLockExclusive(&E.lock);
    SetEvent(E.event);
}

static bool ev_superseded(void)
{
    AcquireSRWLockShared(&E.lock);
    bool r = E.has_req;
    ReleaseSRWLockShared(&E.lock);
    return r;
}

// Read a length-prefixed, NUL-terminated UTF-16 string.
static const u8 *ev_read_str(const u8 *p, const u8 *end, const WCHAR **out)
{
    if (!p || p + 4 > end) return NULL;
    DWORD len;
    memcpy(&len, p, 4);
    p += 4;
    if ((size_t)(end - p) < ((size_t)len + 1) * 2) return NULL;
    if (out) *out = (const WCHAR *)p;
    return p + ((size_t)len + 1) * 2;
}

static void ev_parse(const u8 *data, DWORD size)
{
    if (!data || size < 20) return;
    DWORD hdr[5];
    memcpy(hdr, data, 20);
    DWORD total = hdr[0], num = hdr[1], req = hdr[3];
    E.raw_total = MAX(E.raw_total, total);
    if ((u64)20 + (u64)num * 8 > size) return;
    const u8 *end = data + size;
    for (DWORD i = 0; i < num; i++) {
        DWORD it[2];
        memcpy(it, data + 20 + i * 8, 8);
        const u8 *p = data + it[1];
        if (it[1] >= size) continue;
        const WCHAR *name = NULL, *path = NULL;
        i64 mtime = 0;
        if (req & EV_REQ_NAME) p = ev_read_str(p, end, &name);
        if (req & EV_REQ_PATH) p = ev_read_str(p, end, &path);
        if (req & EV_REQ_FULL_PATH) p = ev_read_str(p, end, NULL);
        if (req & EV_REQ_EXTENSION) p = ev_read_str(p, end, NULL);
        if (p && (req & EV_REQ_SIZE)) p = (p + 8 <= end) ? p + 8 : NULL;
        if (p && (req & EV_REQ_DATE_CREATED)) p = (p + 8 <= end) ? p + 8 : NULL;
        if (p && (req & EV_REQ_DATE_MODIFIED)) {
            if (p + 8 <= end) {
                memcpy(&mtime, p, 8);
                p += 8;
            } else {
                p = NULL;
            }
        }
        if (!p || !name) continue;
        if (E.raw_count == E.raw_cap) {
            int nc = E.raw_cap ? E.raw_cap * 2 : 512;
            RawFile *nr = (RawFile *)realloc(E.raw, sizeof(RawFile) * nc);
            if (!nr) return;
            E.raw = nr;
            E.raw_cap = nc;
        }
        RawFile *r = &E.raw[E.raw_count++];
        r->name = wdup(&E.scratch, name, -1);
        r->path = wdup(&E.scratch, path ? path : L"", -1);
        r->mtime = (mtime < 0) ? 0 : mtime;
        r->flags = it[0];
    }
}

static LRESULT CALLBACK ev_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_COPYDATA) {
        COPYDATASTRUCT *cds = (COPYDATASTRUCT *)l;
        if (cds && cds->dwData == E.waiting_tag && !E.got) {
            ev_parse((const u8 *)cds->lpData, cds->cbData);
            E.got = true;
        }
        return TRUE;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ev_pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// Our own copy of Everything: Everything\Everything.exe next to our exe (the installer puts it there).
// It runs as a separate hidden instance with its settings and index in our data folder, and only while
// the user has no Everything of their own: theirs is always preferred, so the disk is never indexed twice.
#define EV_INSTANCE L"MixLauncher"
#define EV_OWN_CLASS L"EVERYTHING_TASKBAR_NOTIFICATION_(" EV_INSTANCE L")"

enum { EVS_NONE, EVS_USER, EVS_OWN };

static bool g_ev_own_allowed;  // set by main: only an elevated launcher can give it NTFS access
static HANDLE g_ev_own_proc;
static f64 g_ev_own_started;

static HWND ev_user_window(void)
{
    HWND h = FindWindowW(L"EVERYTHING_TASKBAR_NOTIFICATION", NULL);
    if (!h) h = FindWindowW(L"EVERYTHING_TASKBAR_NOTIFICATION_(1.5a)", NULL);
    return h;
}

static int ev_state(void) { return ev_user_window() ? EVS_USER : FindWindowW(EV_OWN_CLASS, NULL) ? EVS_OWN : EVS_NONE; }

static bool ev_own_exe(WCHAR *out)
{
    WCHAR dir[MAX_PATH];
    wcopy(dir, MAX_PATH, g_exe_path);
    PathRemoveFileSpecW(dir);
    _snwprintf(out, MAX_PATH, L"%s\\Everything\\Everything.exe", dir);
    out[MAX_PATH - 1] = 0;
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

static HANDLE ev_own_run(const WCHAR *args)
{
    WCHAR exe[MAX_PATH], cmd[3 * MAX_PATH + 128];
    if (!ev_own_exe(exe)) return NULL;
    _snwprintf(cmd, countof(cmd), L"\"%s\" -instance " EV_INSTANCE L" %s", exe, args);
    cmd[countof(cmd) - 1] = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        log_msg("everything: could not run our copy (%lu)", GetLastError());
        return NULL;
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static void ev_own_start(void)
{
    if (!g_ev_own_allowed) return;
    if (g_ev_own_proc && WaitForSingleObject(g_ev_own_proc, 0) == WAIT_TIMEOUT) return;  // still coming up
    if (g_ev_own_started && time_now() - g_ev_own_started < 30.0) return;              // it quit: don't hammer
    WCHAR exe[MAX_PATH];
    if (!ev_own_exe(exe)) return;
    WCHAR dir[MAX_PATH], ini[MAX_PATH], db[MAX_PATH], args[2 * MAX_PATH + 64];
    data_path(dir, L"Everything");
    CreateDirectoryW(dir, NULL);
    _snwprintf(ini, MAX_PATH, L"%s\\Everything.ini", dir);
    _snwprintf(db, MAX_PATH, L"%s\\Everything.db", dir);
    ini[MAX_PATH - 1] = db[MAX_PATH - 1] = 0;
    if (GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES) {
        static const char k_ini[] = "[Everything]\r\n"
                                    "show_tray_icon=0\r\n"
                                    "run_in_background=1\r\n"
                                    "check_for_updates_on_startup=0\r\n"
                                    "auto_include_fixed_volumes=1\r\n";
        write_file_atomic(ini, k_ini, (DWORD)(sizeof k_ini - 1));
    }
    _snwprintf(args, countof(args), L"-startup -config \"%s\" -db \"%s\"", ini, db);
    args[countof(args) - 1] = 0;
    if (g_ev_own_proc) CloseHandle(g_ev_own_proc);
    g_ev_own_proc = ev_own_run(args);
    g_ev_own_started = time_now();
    if (g_ev_own_proc) log_msg("everything: started our own copy");
}

// Asks our copy to save its index and quit; wait_ms > 0 waits for it.
static void ev_own_stop(DWORD wait_ms)
{
    if (!FindWindowW(EV_OWN_CLASS, NULL)) return;
    HANDLE p = ev_own_run(wait_ms ? L"-exit -wait" : L"-exit");
    if (p) {
        if (wait_ms) WaitForSingleObject(p, wait_ms);
        CloseHandle(p);
    }
}

static HWND ev_find_window(void)
{
    HWND h = ev_user_window();
    if (h) {
        if (g_ev_own_allowed && FindWindowW(EV_OWN_CLASS, NULL)) ev_own_stop(0);  // the user started theirs
        return h;
    }
    h = FindWindowW(EV_OWN_CLASS, NULL);
    if (h) return h;
    ev_own_start();
    // A fresh start creates its window within a moment; the index loads behind it.
    for (int i = 0; !h && i < 40 && g_ev_own_proc && WaitForSingleObject(g_ev_own_proc, 50) == WAIT_TIMEOUT; i++) h = FindWindowW(EV_OWN_CLASS, NULL);
    return h;
}

// 1 = ok, 0 = error, -1 = cancelled by a newer request
static int ev_send_query(HWND ev, const WCHAR *search, DWORD offset, DWORD max_results, DWORD sort)
{
    int len = wlen(search);
    DWORD size = 28 + (DWORD)(len + 1) * 2;
    u8 *buf = (u8 *)arena_push(&E.scratch, size);
    DWORD q[7];
    E.tag = (E.tag + 1) & 0xFFFF;
    E.waiting_tag = 0x4D4C0000u | E.tag;
    q[0] = (DWORD)(uintptr_t)E.reply;
    q[1] = E.waiting_tag;
    q[2] = 0;  // search flags: case-insensitive, no regex
    q[3] = offset;
    q[4] = max_results;
    q[5] = EV_REQ_NAME | EV_REQ_PATH | EV_REQ_DATE_MODIFIED;
    q[6] = sort;
    memcpy(buf, q, 28);
    memcpy(buf + 28, search, (size_t)(len + 1) * 2);
    COPYDATASTRUCT cds;
    cds.dwData = EV_COPYDATA_QUERY2W;
    cds.cbData = size;
    cds.lpData = buf;
    E.got = false;
    DWORD_PTR res = 0;
    if (!SendMessageTimeoutW(ev, WM_COPYDATA, (WPARAM)E.reply, (LPARAM)&cds, SMTO_ABORTIFHUNG | SMTO_NORMAL, 3000, &res) || !res) return 0;
    f64 t0 = time_now();
    // Our copy builds its index on the very first run and answers once it is done.
    f64 limit = g_ev_own_started && t0 - g_ev_own_started < 60.0 ? 30.0 : 5.0;
    while (!E.got) {
        if (ev_superseded()) {
            E.waiting_tag = 0;
            return -1;
        }
        f64 el = time_now() - t0;
        if (el > limit) {
            E.waiting_tag = 0;
            return 0;
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 15, QS_ALLINPUT);
        ev_pump();
    }
    E.waiting_tag = 0;
    return 1;
}

static bool query_is_simple(const WCHAR *q)
{
    for (; *q; q++)
        if (wcschr(L":\"<>|*?!\\/", *q)) return false;
    return true;
}

typedef struct Penalty {
    const WCHAR *needle;
    int score;
} Penalty;

static const Penalty k_path_penalties[] = {
    { L"\\$recycle.bin", -6000 }, { L"\\system volume information", -6000 }, { L"\\winsxs\\", -6000 },
    { L"\\windowsapps\\", -4000 }, { L"\\node_modules\\", -3500 }, { L"\\__pycache__", -3000 },
    { L"\\appdata\\", -2000 }, { L"\\site-packages\\", -2000 }, { L"\\programdata\\", -1500 },
    { L"\\temp\\", -1500 }, { L"\\cache", -1200 }, { L"\\msys64\\", -1500 }, { L"\\steamapps\\", -1500 },
    { L"\\tmp\\", -1000 }, { L"\\obj\\", -800 }, { L"\\build\\", -300 },
};

static const WCHAR *k_internal_ext[] = {
    L".dll", L".sys", L".mui", L".xbf", L".pri", L".pak", L".dat", L".bin", L".pdb", L".obj", L".o", L".lib",
    L".a", L".class", L".pyc", L".pyd", L".pom", L".cat", L".manifest", L".etl", L".tmp", L".cache", L".idx",
    L".lock", L".map", L".d", L".tlog", L".ilk", L".exp", L".nls", L".db-journal", L".db-wal", L".db-shm", L".ldb",
    L".sha1", L".sha256", L".mo", L".qm", L".winmd", L".node", L".so", L".jsonl", L".sig",
};

static f32 ev_rank(const RawFile *r, const WCHAR *full, int full_len, const WCHAR *qn, int qlen, bool plain, i64 now_ft)
{
    WCHAR nn[1024];
    u8 ws[1024];
    int nl = MIN(wlen(r->name), 1023);
    norm_str(nn, r->name, nl);
    word_starts(r->name, nl, ws);
    f32 s;
    if (plain) {
        int tier = score_query(nn, nl, ws, qn, qlen);
        // exact stem: "report" == "report.docx"
        const WCHAR *dot = NULL;
        for (int i = nl - 1; i > 0; i--)
            if (nn[i] == '.') {
                dot = nn + i;
                break;
            }
        if (dot && (int)(dot - nn) == qlen && wmem_eq(nn, qn, qlen)) tier = SCORE_EXACT;
        s = tier ? (f32)tier : 1500.f;  // matched by path only
    } else {
        s = 3000.f;
    }
    f64 age_days = (f64)(now_ft - r->mtime) / 864000000000.0;
    if (r->mtime > 0 && age_days >= 0) s += (f32)(1200.0 / (1.0 + age_days / 3.0));

    WCHAR fn[2048];
    int fl = MIN(full_len, 2047);
    norm_str(fn, full, fl);
    for (int i = 0; i < countof(k_path_penalties); i++)
        if (wcsstr(fn, k_path_penalties[i].needle)) s += (f32)k_path_penalties[i].score;
    if (fl >= 11 && !wcsncmp(fn + 1, L":\\windows\\", 10)) s -= 3000;
    bool folder = (r->flags & EV_ITEM_FOLDER) != 0;
    int dir_len = fl - nl;
    for (int i = 0; i + 1 < dir_len; i++)
        if (fn[i] == '\\' && fn[i + 1] == '.') {
            s -= 2500;
            break;
        }
    if (wcsstr(fn, L"\\program files")) s -= folder ? 300 : 1500;
    if (!folder) {
        const WCHAR *ext = PathFindExtensionW(nn);
        if (!*ext) s -= 800;
        else
            for (int i = 0; i < countof(k_internal_ext); i++)
                if (!wcscmp(ext, k_internal_ext[i])) {
                    s -= 1500;
                    break;
                }
    }
    if (E.profile_len && !wcsncmp(fn, E.profile_norm, E.profile_len) && !wcsstr(fn, L"\\appdata\\")) {
        s += 300;
        const WCHAR *rest = fn + E.profile_len;
        if (wstarts_with_i(rest, L"\\desktop\\") || wstarts_with_i(rest, L"\\documents\\") || wstarts_with_i(rest, L"\\downloads\\")) s += 300;
    }
    int depth = 0;
    for (int i = 0; i < fl; i++)
        if (fn[i] == '\\') depth++;
    if (depth > 4) s -= (f32)((depth - 4) * 60);
    if (folder) s += 150;
    return s;
}

static int cmp_file_score(const void *a, const void *b)
{
    const FileItem *x = (const FileItem *)a, *y = (const FileItem *)b;
    return x->score < y->score ? 1 : x->score > y->score ? -1 : 0;
}

// Adds a path hash; false if it was handed out already.
static bool seen_add(u64 h)
{
    if (!h) h = 1;
    if ((E.seen_count + 1) * 2 > E.seen_cap) {
        u32 nc = E.seen_cap ? E.seen_cap * 2 : 1024;
        u64 *ns = (u64 *)calloc(nc, sizeof(u64));
        if (!ns) return true;
        for (u32 i = 0; i < E.seen_cap; i++)
            if (E.seen[i]) {
                u32 slot = (u32)hash_u64(E.seen[i]) & (nc - 1);
                while (ns[slot]) slot = (slot + 1) & (nc - 1);
                ns[slot] = E.seen[i];
            }
        free(E.seen);
        E.seen = ns;
        E.seen_cap = nc;
    }
    u32 slot = (u32)hash_u64(h) & (E.seen_cap - 1);
    while (E.seen[slot]) {
        if (E.seen[slot] == h) return false;
        slot = (slot + 1) & (E.seen_cap - 1);
    }
    E.seen[slot] = h;
    E.seen_count++;
    return true;
}

// Turns E.raw into ranked, de-duplicated items of `res`.
static void ev_build(FileResults *res, size_t reserve)
{
    arena_init(&res->arena, reserve);
    res->items = arena_array(&res->arena, FileItem, MAX(E.raw_count, 1));
    i64 now_ft = filetime_now();
    for (int i = 0; i < E.raw_count; i++) {
        RawFile *r = &E.raw[i];
        WCHAR full[2048];
        if (r->path[0]) _snwprintf(full, countof(full), L"%s\\%s", r->path, r->name);
        else _snwprintf(full, countof(full), L"%s\\", r->name);
        full[countof(full) - 1] = 0;
        int fl = wlen(full);
        if (!seen_add(hash_wstr_i(full, fl))) continue;
        // Start menu shortcuts are already covered by the Applications section.
        if (wends_with_i(full, fl, L".lnk") && StrStrIW(full, L"\\Start Menu\\Programs\\")) continue;
        FileItem *fi = &res->items[res->count++];
        fi->full = wdup(&res->arena, full, fl);
        fi->name = fi->full + (r->path[0] ? wlen(r->path) + 1 : 0);
        fi->dir = r->path[0] ? wdup(&res->arena, r->path, -1) : wdup(&res->arena, L"", 0);
        fi->mtime = r->mtime;
        fi->flags = (r->flags & (EV_ITEM_FOLDER | EV_ITEM_DRIVE)) ? FI_FOLDER : 0;
        fi->score = ev_rank(r, full, fl, E.cur_qn, E.cur_qlen, E.cur_plain, now_ft);
        fi->icon_key = icon_key_for_file(full, fi->flags & FI_FOLDER);
    }
    qsort(res->items, res->count, sizeof(FileItem), cmp_file_score);
}

static void ev_run(u32 id, const WCHAR *query, const WCHAR *filter)
{
    FileResults *res = (FileResults *)calloc(1, sizeof(FileResults));
    if (!res) return;
    res->id = id;
    HWND ev = ev_find_window();
    if (!ev) {
        res->status = EV_NOT_RUNNING;
        if (!PostMessageW(g_hwnd, WM_APP_FILES_READY, 0, (LPARAM)res)) free(res);
        return;
    }
    arena_reset(&E.scratch);
    E.raw_count = 0;
    E.raw_total = 0;
    E.cur_id = 0;  // pages of the previous query are no longer wanted

    WCHAR search[2048];
    if (filter[0]) _snwprintf(search, countof(search), L"<%s> %s", query, filter);
    else _snwprintf(search, countof(search), L"%s", query);
    search[countof(search) - 1] = 0;

    int ok = ev_send_query(ev, search, 0, EV_FIRST_PAGE, EV_SORT_DATE_MODIFIED_DESC);
    if (ok < 0) {
        free(res);
        return;
    }
    u32 main_count = (u32)E.raw_count;
    bool plain = query_is_simple(query);
    if (ok > 0 && plain && E.raw_total > (u32)E.raw_count && !ev_superseded()) {
        if (filter[0]) _snwprintf(search, countof(search), L"startwith:<%s> %s", query, filter);
        else _snwprintf(search, countof(search), L"startwith:%s", query);
        search[countof(search) - 1] = 0;
        u32 total = E.raw_total;
        int ok2 = ev_send_query(ev, search, 0, 128, EV_SORT_DATE_MODIFIED_DESC);
        E.raw_total = total;
        if (ok2 < 0) {
            free(res);
            return;
        }
    }
    if (ok == 0) {
        res->status = EV_ERROR;
        if (!PostMessageW(g_hwnd, WM_APP_FILES_READY, 0, (LPARAM)res)) free(res);
        return;
    }

    E.cur_qlen = MIN(wlen(query), 1023);
    norm_str(E.cur_qn, query, E.cur_qlen);
    while (E.cur_qlen && E.cur_qn[E.cur_qlen - 1] == ' ') E.cur_qn[--E.cur_qlen] = 0;
    E.cur_plain = plain;
    if (filter[0]) _snwprintf(E.cur_search, countof(E.cur_search), L"<%s> %s", query, filter);
    else _snwprintf(E.cur_search, countof(E.cur_search), L"%s", query);
    E.cur_search[countof(E.cur_search) - 1] = 0;
    if (E.seen) memset(E.seen, 0, sizeof(u64) * E.seen_cap);
    E.seen_count = 0;

    res->total = E.raw_total;
    ev_build(res, 32ull << 20);
    E.cur_id = id;
    E.cur_total = E.raw_total;
    E.cur_fetched = res->fetched = main_count;
    if (!PostMessageW(g_hwnd, WM_APP_FILES_READY, 0, (LPARAM)res)) fileresults_free(res);
}

static void ev_run_more(u32 id)
{
    if (id != E.cur_id || E.cur_fetched >= E.cur_total) return;
    HWND ev = ev_find_window();
    if (!ev) return;
    arena_reset(&E.scratch);
    E.raw_count = 0;
    E.raw_total = 0;
    int ok = ev_send_query(ev, E.cur_search, E.cur_fetched, EV_PAGE, EV_SORT_DATE_MODIFIED_DESC);
    if (ok < 0) return;  // a new query: it resets everything
    FileResults *res = (FileResults *)calloc(1, sizeof(FileResults));
    if (!res) return;
    res->id = id;
    res->page = true;
    if (ok == 0) {
        res->status = EV_ERROR;
        E.cur_fetched = E.cur_total;  // stop asking
    } else {
        if (E.raw_total) E.cur_total = E.raw_total;
        E.cur_fetched = E.raw_count ? E.cur_fetched + (u32)E.raw_count : E.cur_total;
        ev_build(res, 8ull << 20);
    }
    res->total = E.cur_total;
    res->fetched = E.cur_fetched;
    if (!PostMessageW(g_hwnd, WM_APP_FILES_READY, 0, (LPARAM)res)) fileresults_free(res);
}

static DWORD WINAPI ev_thread(void *param)
{
    (void)param;
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = ev_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MixLauncherEverythingReply";
    RegisterClassW(&wc);
    E.reply = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    // We run elevated and Everything does not: its replies must pass UIPI.
    if (E.reply) ChangeWindowMessageFilterEx(E.reply, WM_COPYDATA, MSGFLT_ALLOW, NULL);
    arena_init(&E.scratch, 64ull << 20);

    PWSTR prof = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(&ML_FOLDERID_Profile, 0, NULL, &prof)) && prof) {
        E.profile_len = MIN(wlen(prof), MAX_PATH - 1);
        norm_str(E.profile_norm, prof, E.profile_len);
    }
    CoTaskMemFree(prof);
    ev_find_window();  // start our copy of Everything now, so it is ready by the first search

    for (;;) {
        MsgWaitForMultipleObjects(1, &E.event, FALSE, INFINITE, QS_ALLINPUT);
        ev_pump();
        u32 id;
        WCHAR q[1024], filter[512];
        AcquireSRWLockExclusive(&E.lock);
        bool has = E.has_req, more = E.has_more;
        u32 more_id = E.more_id;
        E.has_req = E.has_more = false;
        id = E.req_id;
        wcopy(q, countof(q), E.req_query);
        wcopy(filter, countof(filter), E.filter);
        ReleaseSRWLockExclusive(&E.lock);
        if (has) ev_run(id, q, filter);
        else if (more) ev_run_more(more_id);
    }
    return 0;
}

static void ev_start(void)
{
    InitializeSRWLock(&E.lock);
    E.event = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, ev_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
