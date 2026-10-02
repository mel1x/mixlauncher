enum { ACT_OPEN, ACT_REVEAL, ACT_RUNAS, ACT_PROPERTIES };

typedef struct LaunchJob {
    int act;
    bool is_app;
    u8 cmd;
    WCHAR *target;
    WCHAR *path;
    WCHAR *dir;
    u8 hist_type;
    WCHAR *hist_key;
} LaunchJob;

static struct {
    SRWLOCK lock;
    HANDLE event;
    LaunchJob *jobs;
    int count, cap;
} LQ;

static void job_free(LaunchJob *j)
{
    free(j->target);
    free(j->path);
    free(j->dir);
    free(j->hist_key);
}

static void enable_shutdown_privilege(void)
{
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return;
    TOKEN_PRIVILEGES tp;
    memset(&tp, 0, sizeof tp);
    tp.PrivilegeCount = 1;
    if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid)) {
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
    }
    CloseHandle(tok);
}

static bool shell_exec_ex(const WCHAR *verb, const WCHAR *file, const WCHAR *dir, DWORD *err, bool quiet)
{
    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof sei);
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_LOG_USAGE | (quiet ? SEE_MASK_FLAG_NO_UI : 0);
    sei.lpVerb = verb;
    sei.lpFile = file;
    sei.lpDirectory = (dir && dir[0]) ? dir : NULL;
    sei.nShow = SW_SHOWNORMAL;
    BOOL ok = ShellExecuteExW(&sei);
    if (err) *err = ok ? 0 : GetLastError();
    return ok != 0;
}

static bool shell_exec(const WCHAR *verb, const WCHAR *file, const WCHAR *dir, DWORD *err) { return shell_exec_ex(verb, file, dir, err, false); }

static void app_exec(const WCHAR *verb, const WCHAR *app_uri, const WCHAR *path)
{
    DWORD err = 0;
    if (shell_exec_ex(verb, app_uri, NULL, &err, true)) return;
    if (err == ERROR_CANCELLED) return;
    if (path && shell_exec(verb, path, NULL, &err)) return;
    if (!path) shell_exec(verb, app_uri, NULL, &err);
}

static void reveal_in_explorer(const WCHAR *path)
{
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path);
    if (pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
        ILFree(pidl);
    }
}

static void run_command(u8 cmd)
{
    switch (cmd) {
    case CMD_LOCK:
        LockWorkStation();
        break;
    case CMD_SLEEP: {
        enable_shutdown_privilege();
        typedef BOOLEAN (WINAPI *PFN_SetSuspendState)(BOOLEAN, BOOLEAN, BOOLEAN);
        HMODULE m = LoadLibraryW(L"powrprof.dll");
        PFN_SetSuspendState sss = m ? (PFN_SetSuspendState)(void *)GetProcAddress(m, "SetSuspendState") : NULL;
        if (sss) sss(FALSE, FALSE, FALSE);
        break;
    }
    case CMD_RESTART:
        enable_shutdown_privilege();
        ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
        break;
    case CMD_SHUTDOWN:
        enable_shutdown_privilege();
        ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF | 0x00400000 /*EWX_HYBRID_SHUTDOWN*/, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
        break;
    case CMD_SIGNOUT:
        ExitWindowsEx(EWX_LOGOFF, 0);
        break;
    case CMD_RECYCLE:
        if (!g_elevated || !shell_exec_unelevated(L"shell:RecycleBinFolder", NULL, NULL, NULL)) shell_exec(NULL, L"shell:RecycleBinFolder", NULL, NULL);
        break;
    }
}

static void launch_exec(LaunchJob *j)
{
    if (j->cmd) {
        run_command(j->cmd);
        return;
    }
    DWORD err = 0;
    WCHAR buf[2048];
    const WCHAR *app_uri = NULL;
    if (j->is_app) {
        _snwprintf(buf, countof(buf), L"shell:AppsFolder\\%s", j->target);
        buf[countof(buf) - 1] = 0;
        app_uri = buf;
    }
    switch (j->act) {
    case ACT_OPEN:
        if (j->is_app) {
            const WCHAR *what = wcsstr(j->target, L"://") ? j->target : app_uri;
            if (!g_elevated || !shell_exec_unelevated(what, NULL, NULL, NULL)) app_exec(NULL, app_uri, j->path);
        } else if (wcsstr(j->target, L"://")) {
            if (!g_elevated || !shell_exec_unelevated(j->target, NULL, NULL, NULL)) shell_exec(NULL, j->target, NULL, &err);
        } else if (GetFileAttributesW(j->target) == INVALID_FILE_ATTRIBUTES) {
            DWORD e = GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
                WCHAR *k = wdup_heap(j->hist_key);
                if (!PostMessageW(g_hwnd, WM_APP_LAUNCH_FAILED, (WPARAM)j->hist_type, (LPARAM)k)) free(k);
            }
        } else if (!g_elevated || !shell_exec_unelevated(j->target, NULL, j->dir, NULL)) {
            shell_exec(NULL, j->target, j->dir, &err);
        }
        break;
    case ACT_RUNAS:
        if (j->is_app) {
            app_exec(L"runas", app_uri, j->path);
        } else {
            shell_exec(L"runas", j->target, j->dir, &err);
        }
        break;
    case ACT_REVEAL: {
        const WCHAR *p = j->is_app ? j->path : j->target;
        if (p) reveal_in_explorer(p);
        break;
    }
    case ACT_PROPERTIES: {
        const WCHAR *p = j->is_app ? j->path : j->target;
        if (p) {
            SHELLEXECUTEINFOW sei;
            memset(&sei, 0, sizeof sei);
            sei.cbSize = sizeof sei;
            sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_NOASYNC;
            sei.lpVerb = L"properties";
            sei.lpFile = p;
            sei.nShow = SW_SHOWNORMAL;
            ShellExecuteExW(&sei);
        }
        break;
    }
    }
}

static DWORD WINAPI launch_thread(void *param)
{
    (void)param;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    for (;;) {
        // STA thread: keep pumping so shell components that post to us work.
        MsgWaitForMultipleObjects(1, &LQ.event, FALSE, INFINITE, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        for (;;) {
            LaunchJob j;
            AcquireSRWLockExclusive(&LQ.lock);
            bool has = LQ.count > 0;
            if (has) {
                j = LQ.jobs[0];
                memmove(LQ.jobs, LQ.jobs + 1, sizeof(LaunchJob) * (LQ.count - 1));
                LQ.count--;
            }
            ReleaseSRWLockExclusive(&LQ.lock);
            if (!has) break;
            launch_exec(&j);
            job_free(&j);
        }
    }
    return 0;
}

static void launch_start(void)
{
    InitializeSRWLock(&LQ.lock);
    LQ.event = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, launch_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

static void launch_submit(const LaunchJob *j)
{
    AcquireSRWLockExclusive(&LQ.lock);
    if (LQ.count == LQ.cap) {
        int nc = LQ.cap ? LQ.cap * 2 : 16;
        LaunchJob *nj = (LaunchJob *)realloc(LQ.jobs, sizeof(LaunchJob) * nc);
        if (nj) {
            LQ.jobs = nj;
            LQ.cap = nc;
        }
    }
    if (LQ.count < LQ.cap) LQ.jobs[LQ.count++] = *j;
    ReleaseSRWLockExclusive(&LQ.lock);
    SetEvent(LQ.event);
}
