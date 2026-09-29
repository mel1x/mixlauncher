// elevation.c — living as an administrator process (see the manifest).

static bool g_elevated;

static bool process_is_elevated(void)
{
    HANDLE tok;
    TOKEN_ELEVATION e = { 0 };
    DWORD sz = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    bool r = GetTokenInformation(tok, TokenElevation, &e, sizeof e, &sz) && e.TokenIsElevated;
    CloseHandle(tok);
    return r;
}

static void uipi_allow(HWND h, UINT msg)
{
    if (h && msg) ChangeWindowMessageFilterEx(h, msg, MSGFLT_ALLOW, NULL);
}

ML_GUID(ML_CLSID_ShellWindows,         0x9ba05972, 0xf6a8, 0x11cf, 0xa4, 0x42, 0x00, 0xa0, 0xc9, 0x0a, 0x8f, 0x39);
ML_GUID(ML_IID_IShellWindows,          0x85cb6900, 0x4d95, 0x11cf, 0x96, 0x0c, 0x00, 0x80, 0xc7, 0xf4, 0xee, 0x85);
ML_GUID(ML_IID_IServiceProvider,       0x6d5140c1, 0x7436, 0x11ce, 0x80, 0x34, 0x00, 0xaa, 0x00, 0x60, 0x09, 0xfa);
ML_GUID(ML_SID_STopLevelBrowser,       0x4c96be40, 0x915c, 0x11cf, 0x99, 0xd3, 0x00, 0xaa, 0x00, 0x4a, 0xe8, 0x37);
ML_GUID(ML_IID_IShellBrowser,          0x000214e2, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
ML_GUID(ML_IID_IDispatch,              0x00020400, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
ML_GUID(ML_IID_IShellFolderViewDual,   0xe7a1af80, 0x4d96, 0x11cf, 0x96, 0x0c, 0x00, 0x80, 0xc7, 0xf4, 0xee, 0x85);
ML_GUID(ML_IID_IShellDispatch2,        0xa4c6892c, 0x3ba9, 0x11d2, 0x9d, 0xea, 0x00, 0xc0, 0x4f, 0xb1, 0x61, 0x62);

static bool shell_exec_unelevated_ex(const WCHAR *file, const WCHAR *args, const WCHAR *dir, const WCHAR *verb, int show)
{
    bool ok = false;
    IShellWindows *sw = NULL;
    IDispatch *desk = NULL, *bg = NULL, *app = NULL;
    IServiceProvider *sp = NULL;
    IShellBrowser *sb = NULL;
    IShellView *sv = NULL;
    IShellFolderViewDual *fvd = NULL;
    IShellDispatch2 *sd = NULL;
    VARIANT loc, root;
    VariantInit(&loc);
    VariantInit(&root);
    loc.vt = VT_I4;
    loc.lVal = CSIDL_DESKTOP;
    long hwnd = 0;
    if (SUCCEEDED(CoCreateInstance(&ML_CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER, &ML_IID_IShellWindows, (void **)&sw)) &&
        IShellWindows_FindWindowSW(sw, &loc, &root, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &desk) == S_OK && desk &&
        SUCCEEDED(IDispatch_QueryInterface(desk, &ML_IID_IServiceProvider, (void **)&sp)) &&
        SUCCEEDED(IServiceProvider_QueryService(sp, &ML_SID_STopLevelBrowser, &ML_IID_IShellBrowser, (void **)&sb)) &&
        SUCCEEDED(IShellBrowser_QueryActiveShellView(sb, &sv)) &&
        SUCCEEDED(IShellView_GetItemObject(sv, SVGIO_BACKGROUND, &ML_IID_IDispatch, (void **)&bg)) &&
        SUCCEEDED(IDispatch_QueryInterface(bg, &ML_IID_IShellFolderViewDual, (void **)&fvd)) &&
        SUCCEEDED(IShellFolderViewDual_get_Application(fvd, &app)) && app &&
        SUCCEEDED(IDispatch_QueryInterface(app, &ML_IID_IShellDispatch2, (void **)&sd))) {
        VARIANT va, vd, vo, vs;
        VariantInit(&va);
        VariantInit(&vd);
        VariantInit(&vo);
        VariantInit(&vs);
        if (args && *args) {
            va.vt = VT_BSTR;
            va.bstrVal = SysAllocString(args);
        }
        if (dir && *dir) {
            vd.vt = VT_BSTR;
            vd.bstrVal = SysAllocString(dir);
        }
        if (verb && *verb) {
            vo.vt = VT_BSTR;
            vo.bstrVal = SysAllocString(verb);
        }
        vs.vt = VT_I4;
        vs.lVal = show;
        BSTR bfile = SysAllocString(file);
        ok = bfile && SUCCEEDED(IShellDispatch2_ShellExecute(sd, bfile, va, vd, vo, vs));
        SysFreeString(bfile);
        VariantClear(&va);
        VariantClear(&vd);
        VariantClear(&vo);
    }
    SAFE_RELEASE(sd);
    SAFE_RELEASE(app);
    SAFE_RELEASE(fvd);
    SAFE_RELEASE(bg);
    SAFE_RELEASE(sv);
    SAFE_RELEASE(sb);
    SAFE_RELEASE(sp);
    SAFE_RELEASE(desk);
    SAFE_RELEASE(sw);
    return ok;
}

static bool shell_exec_unelevated(const WCHAR *file, const WCHAR *args, const WCHAR *dir, const WCHAR *verb)
{
    return shell_exec_unelevated_ex(file, args, dir, verb, SW_SHOWNORMAL);
}

// Run a console tool invisibly and wait for it.
static bool run_tool(const WCHAR *exe, const WCHAR *args, DWORD wait_ms, DWORD *exit_code)
{
    WCHAR cmd[2048];
    _snwprintf(cmd, countof(cmd), L"\"%s\" %s", exe, args);
    cmd[countof(cmd) - 1] = 0;
    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
    bool done = WaitForSingleObject(pi.hProcess, wait_ms) == WAIT_OBJECT_0;
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    if (exit_code) *exit_code = code;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return done;
}

// Start with Windows

#define AUTOSTART_TASK L"MixLauncher"
#define OLD_HELPER_TASK L"MixLauncher Admin Hook"
static const WCHAR k_run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static bool task_exists(const WCHAR *name)
{
    WCHAR path[MAX_PATH], sys[MAX_PATH];
    if (!GetSystemDirectoryW(sys, MAX_PATH)) return false;
    _snwprintf(path, MAX_PATH, L"%s\\Tasks\\%s", sys, name);
    path[MAX_PATH - 1] = 0;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static void task_delete(const WCHAR *name)
{
    WCHAR args[256];
    _snwprintf(args, countof(args), L"/delete /tn \"%s\" /f", name);
    args[countof(args) - 1] = 0;
    run_tool(L"schtasks.exe", args, 10000, NULL);
}

static void xml_put(Buf *b, const WCHAR *s) { buf_put(b, s, (size_t)wlen(s) * sizeof(WCHAR)); }

static void xml_put_escaped(Buf *b, const WCHAR *s)
{
    for (; *s; s++) {
        if (*s == '&') xml_put(b, L"&amp;");
        else if (*s == '<') xml_put(b, L"&lt;");
        else if (*s == '>') xml_put(b, L"&gt;");
        else if (*s == '"') xml_put(b, L"&quot;");
        else buf_put(b, s, sizeof(WCHAR));
    }
}

static bool task_create_autostart(void)
{
    WCHAR user[256], dom[128], name[128];
    if (!GetEnvironmentVariableW(L"USERDOMAIN", dom, countof(dom))) dom[0] = 0;
    if (!GetEnvironmentVariableW(L"USERNAME", name, countof(name))) return false;
    _snwprintf(user, countof(user), dom[0] ? L"%s\\%s" : L"%s%s", dom, name);
    user[countof(user) - 1] = 0;
    Buf b = { 0 };
    xml_put(&b, L"\xFEFF<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
                L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
                L"  <RegistrationInfo><Description>MixLauncher</Description></RegistrationInfo>\r\n"
                L"  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>");
    xml_put_escaped(&b, user);
    xml_put(&b, L"</UserId></LogonTrigger></Triggers>\r\n"
                L"  <Principals><Principal id=\"Author\"><UserId>");
    xml_put_escaped(&b, user);
    xml_put(&b, L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
                L"  <Settings>\r\n"
                L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
                L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
                L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
                L"    <AllowHardTerminate>true</AllowHardTerminate>\r\n"
                L"    <StartWhenAvailable>false</StartWhenAvailable>\r\n"
                L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\r\n"
                L"    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>\r\n"
                L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n"
                L"    <Enabled>true</Enabled>\r\n"
                L"    <Hidden>false</Hidden>\r\n"
                L"    <RunOnlyIfIdle>false</RunOnlyIfIdle>\r\n"
                L"    <WakeToRun>false</WakeToRun>\r\n"
                L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
                L"    <Priority>4</Priority>\r\n"
                L"  </Settings>\r\n"
                L"  <Actions Context=\"Author\"><Exec><Command>");
    xml_put_escaped(&b, g_exe_path);
    xml_put(&b, L"</Command></Exec></Actions>\r\n"
                L"</Task>\r\n");
    WCHAR xml[MAX_PATH];
    data_path(xml, L"autostart-task.xml");
    bool ok = write_file_atomic(xml, b.data, (DWORD)b.len);
    free(b.data);
    if (!ok) return false;
    WCHAR args[MAX_PATH + 96];
    _snwprintf(args, countof(args), L"/create /tn \"%s\" /xml \"%s\" /f", AUTOSTART_TASK, xml);
    args[countof(args) - 1] = 0;
    DWORD code = 1;
    ok = run_tool(L"schtasks.exe", args, 10000, &code) && code == 0;
    DeleteFileW(xml);
    if (!ok) log_msg("autostart task creation failed (exit %lu)", code);
    return ok;
}

static bool run_key_present(void)
{
    WCHAR val[MAX_PATH + 8];
    DWORD sz = sizeof val;
    return RegGetValueW(HKEY_CURRENT_USER, k_run_key, L"MixLauncher", RRF_RT_REG_SZ, NULL, val, &sz) == ERROR_SUCCESS;
}

static void run_key_set(bool on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, k_run_key, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        WCHAR val[MAX_PATH + 8];
        _snwprintf(val, countof(val), L"\"%s\"", g_exe_path);
        val[countof(val) - 1] = 0;
        RegSetValueExW(k, L"MixLauncher", 0, REG_SZ, (const BYTE *)val, (DWORD)(wlen(val) + 1) * sizeof(WCHAR));
    } else {
        RegDeleteValueW(k, L"MixLauncher");
    }
    RegCloseKey(k);
}

static bool autostart_get(void) { return g_elevated ? task_exists(AUTOSTART_TASK) : run_key_present(); }

static bool autostart_set(bool on)
{
    if (!g_elevated) {
        run_key_set(on);
        return true;
    }
    if (on) return task_create_autostart();
    task_delete(AUTOSTART_TASK);
    return !task_exists(AUTOSTART_TASK);
}

static DWORD WINAPI elevation_housekeeping(void *param)
{
    (void)param;
    if (!g_elevated) return 0;
    if (task_exists(OLD_HELPER_TASK)) task_delete(OLD_HELPER_TASK);
    if (run_key_present()) {
        if (task_create_autostart()) run_key_set(false);
    }
    return 0;
}

static void elevation_init(void) { g_elevated = process_is_elevated(); }

static void elevation_housekeeping_start(void)
{
    HANDLE t = CreateThread(NULL, 0, elevation_housekeeping, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
