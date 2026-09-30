/*
 * main.c —— QAQ-ETool 图形界面（P7）
 *
 * 本程序只是命令行程序 QAQ-ETool.exe 的外壳：
 *   - 工作线程用 CreateProcessW 调用 CLI，stdout+stderr 合并到一个管道；
 *   - UTF-8 输出按行解码后 PostMessage 到 UI 线程显示；
 *   - Per-Monitor V2 DPI、Common Controls v6、中文路径、拖放、键盘可达。
 *
 * 隐藏开关：
 *   --selftest <cli路径> <日志文件> [源 .e] [输出目录]   无窗口自检（见 selftest.c）
 *   --smoke                                             创建窗口 500ms 后自动关闭
 *
 * 普通参数：qaq-etool-gui.exe [路径]
 *   .e / .ec → 预选解包；文件夹或 .etprj（取其所在目录）→ 预选组包。
 *   可把 .etprj 拖到 exe 上，或用「打开方式」关联，无需注册表。
 */
#include "core.h"
#include "selftest.h"

#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <strsafe.h>

/* ---------- 控件 ID ---------- */
#define IDC_RADIO_E2T  1001
#define IDC_RADIO_T2E  1002
#define IDC_LBL_SRC    1003
#define IDC_EDIT_SRC   1004
#define IDC_BTN_SRC    1005
#define IDC_LBL_DST    1006
#define IDC_EDIT_DST   1007
#define IDC_BTN_DST    1008
#define IDC_CHK_NS     1009
#define IDC_BTN_START  IDOK   /* Enter 键（IsDialogMessage）触发 IDOK */
#define IDC_BTN_OPEN   1011
#define IDC_LBL_LOG    1012
#define IDC_EDIT_LOG   1013
#define IDC_STATUS     1014

#define WM_APP_LINE    (WM_APP + 1) /* lParam = wchar_t*（接收方 free） */
#define WM_APP_DONE    (WM_APP + 2) /* wParam = 退出码或错误码，lParam = 是否启动成功 */

#define TIMER_SMOKE    1

#ifndef WM_DPICHANGED
#define WM_DPICHANGED  0x02E0
#endif

enum { MODE_E2T = 0, MODE_T2E = 1 };
enum { BROWSE_OPEN_E = 0, BROWSE_FOLDER = 1, BROWSE_SAVE_E = 2 };

static const wchar_t kClassName[] = L"QAQEToolGuiWindow";
static const wchar_t kTitle[] = L"QAQ-ETool";

/* ---------- 全局状态（仅 UI 线程访问） ---------- */
static HINSTANCE g_hInst;
static HWND g_hwndMain;
static HWND g_hRadioE2T, g_hRadioT2E, g_hLblSrc, g_hEditSrc, g_hBtnSrc;
static HWND g_hLblDst, g_hEditDst, g_hBtnDst, g_hChkNs, g_hBtnStart, g_hBtnOpen;
static HWND g_hLblLog, g_hEditLog, g_hStatus;
static HWND g_hFocusSave;
static HFONT g_hFont;
static int g_fontH = 16;
static UINT g_dpi = 96;
static BOOL g_running, g_closePending, g_smoke;
static HANDLE g_hWorker;
static ULONGLONG g_startTick;
static wchar_t *g_succPath;   /* 本次运行 SUCC: 行给出的路径 */
static wchar_t *g_openTarget; /* 「打开目标」要打开的路径 */
static const wchar_t *g_initPath; /* 命令行给的初始源路径（指向 argv，勿 free） */

/* ---------- DPI API（动态加载，兼容旧系统） ---------- */
typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
typedef BOOL (WINAPI *PFN_SPIForDpi)(UINT, UINT, PVOID, UINT, UINT);
typedef BOOL (WINAPI *PFN_AdjustForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
static PFN_GetDpiForWindow pGetDpiForWindow;
static PFN_SPIForDpi pSPIForDpi;
static PFN_AdjustForDpi pAdjustForDpi;

static int IMax(int a, int b) { return a > b ? a : b; }

static void InitDpiApi(void)
{
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u)
        return;
    /* 经 void(*)(void) 中转，避免 -Wcast-function-type */
    pGetDpiForWindow = (PFN_GetDpiForWindow)(void (*)(void))GetProcAddress(u, "GetDpiForWindow");
    pSPIForDpi = (PFN_SPIForDpi)(void (*)(void))GetProcAddress(u, "SystemParametersInfoForDpi");
    pAdjustForDpi = (PFN_AdjustForDpi)(void (*)(void))GetProcAddress(u, "AdjustWindowRectExForDpi");
}

static UINT SystemDpi(void)
{
    HDC dc = GetDC(NULL);
    int d = 96;
    if (dc) {
        d = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(NULL, dc);
    }
    return d > 0 ? (UINT)d : 96;
}

static UINT DpiOf(HWND h)
{
    if (pGetDpiForWindow && h) {
        UINT d = pGetDpiForWindow(h);
        if (d)
            return d;
    }
    return SystemDpi();
}

/* 按当前 DPI 缩放 96 DPI 下的像素值 */
static int S(int v)
{
    return MulDiv(v, (int)g_dpi, 96);
}

static void AdjustRectDpi(RECT *r, DWORD style, DWORD ex)
{
    if (!(pAdjustForDpi && pAdjustForDpi(r, style, FALSE, ex, g_dpi)))
        AdjustWindowRectEx(r, style, FALSE, ex);
}

/* ---------- 字体 ---------- */

static BOOL CALLBACK SetFontProc(HWND h, LPARAM f)
{
    SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    return TRUE;
}

/* 以当前 DPI 重建 NONCLIENTMETRICS 消息字体并应用到所有子控件 */
static void RecreateFont(HWND hwnd)
{
    NONCLIENTMETRICSW ncm;
    BOOL ok = FALSE;
    HFONT nf = NULL, old;

    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (pSPIForDpi)
        ok = pSPIForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, g_dpi);
    if (!ok) {
        ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ok)
            ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, (int)g_dpi, (int)SystemDpi());
    }
    if (ok)
        nf = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!nf) {
        LOGFONTW lf;
        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -MulDiv(9, (int)g_dpi, 72);
        lf.lfCharSet = DEFAULT_CHARSET;
        StringCchCopyW(lf.lfFaceName, LF_FACESIZE, L"Microsoft YaHei UI");
        nf = CreateFontIndirectW(&lf);
    }
    if (!nf)
        return; /* 保留旧字体 */

    old = g_hFont;
    g_hFont = nf;
    {
        HDC dc = GetDC(hwnd);
        if (dc) {
            TEXTMETRICW tm;
            HGDIOBJ of = SelectObject(dc, nf);
            if (GetTextMetricsW(dc, &tm))
                g_fontH = tm.tmHeight;
            SelectObject(dc, of);
            ReleaseDC(hwnd, dc);
        }
    }
    EnumChildWindows(hwnd, SetFontProc, (LPARAM)nf);
    if (old)
        DeleteObject(old);
}

/* 控件文字在当前字体下的像素宽度（& 助记符不计） */
static int TextWidth(HWND ctl)
{
    int n = GetWindowTextLengthW(ctl), w = 0;
    wchar_t *t = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
    HDC dc;

    if (!t)
        return 0;
    GetWindowTextW(ctl, t, n + 1);
    dc = GetDC(ctl);
    if (dc) {
        RECT r = { 0, 0, 0, 0 };
        HGDIOBJ of = SelectObject(dc, g_hFont);
        DrawTextW(dc, t, -1, &r, DT_CALCRECT | DT_SINGLELINE);
        w = r.right - r.left;
        SelectObject(dc, of);
        ReleaseDC(ctl, dc);
    }
    free(t);
    return w;
}

/* ---------- 小工具 ---------- */

static wchar_t *GetCtlText(HWND h)
{
    int n = GetWindowTextLengthW(h);
    wchar_t *b = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!b)
        return NULL;
    b[0] = 0;
    GetWindowTextW(h, b, n + 1);
    b[n] = 0;
    TrimPath(b);
    return b;
}

static void SetStatus(const wchar_t *s)
{
    SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)s);
}

static void LogAppend(const wchar_t *s)
{
    int len = GetWindowTextLengthW(g_hEditLog);
    SendMessageW(g_hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_hEditLog, EM_REPLACESEL, FALSE, (LPARAM)s);
    SendMessageW(g_hEditLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(g_hEditLog, EM_SCROLLCARET, 0, 0);
    SendMessageW(g_hEditLog, WM_VSCROLL, SB_BOTTOM, 0);
}

static void ErrorBox(HWND owner, const wchar_t *msg)
{
    MessageBoxW(owner, msg, kTitle, MB_OK | MB_ICONERROR);
}

static int GetMode(void)
{
    return SendMessageW(g_hRadioT2E, BM_GETCHECK, 0, 0) == BST_CHECKED ? MODE_T2E : MODE_E2T;
}

static void SetMode(int mode)
{
    SendMessageW(g_hRadioE2T, BM_SETCHECK, mode == MODE_E2T ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hRadioT2E, BM_SETCHECK, mode == MODE_T2E ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(g_hChkNs, mode == MODE_E2T); /* -ns 仅 e2t */
}

static BOOL HasExtEOrEc(const wchar_t *p)
{
    const wchar_t *dot = wcsrchr(p, L'.');
    const wchar_t *s1 = wcsrchr(p, L'\\'), *s2 = wcsrchr(p, L'/');
    if (!dot || (s1 && dot < s1) || (s2 && dot < s2))
        return FALSE;
    return _wcsicmp(dot, L".e") == 0 || _wcsicmp(dot, L".ec") == 0;
}

/*
 * 推荐目标：
 *   e2t：C:\a\x.e     → C:\a\x_src
 *   t2e：C:\a\folder\ → C:\a\folder.e
 */
static wchar_t *RecommendDst(int mode, const wchar_t *src)
{
    wchar_t *c = WDup(src), *sep, *r = NULL;
    size_t n;

    if (!c)
        return NULL;
    n = wcslen(c);
    while (n > 0 && (c[n - 1] == L'\\' || c[n - 1] == L'/'))
        c[--n] = 0;
    sep = wcsrchr(c, L'\\');
    if (!sep)
        sep = wcsrchr(c, L'/');
    if (sep && sep[1] && !wcschr(sep + 1, L':')) {
        if (mode == MODE_E2T) {
            wchar_t *dot = wcsrchr(sep + 1, L'.');
            if (dot && dot != sep + 1)
                *dot = 0;
            r = WCat(c, L"_src", NULL);
        } else {
            r = WCat(c, L".e", NULL);
        }
    }
    free(c);
    return r;
}

static void SuggestDstIfEmpty(const wchar_t *src)
{
    wchar_t *dst = GetCtlText(g_hEditDst);
    if (dst && dst[0] == 0) {
        wchar_t *rec = RecommendDst(GetMode(), src);
        if (rec) {
            SetWindowTextW(g_hEditDst, rec);
            free(rec);
        }
    }
    free(dst);
}

/* ---------- 设置（%APPDATA%\QAQ-ETool\gui.ini，UTF-16 保证中文路径） ---------- */

static wchar_t *IniPath(BOOL create)
{
    PWSTR appdata = NULL;
    wchar_t *dir, *ini = NULL;

    if (FAILED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &appdata)) || !appdata)
        return NULL;
    dir = WCat(appdata, L"\\QAQ-ETool", NULL);
    CoTaskMemFree(appdata);
    if (!dir)
        return NULL;
    ini = WCat(dir, L"\\gui.ini", NULL);
    if (create && ini) {
        if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            free(ini);
            ini = NULL;
        } else {
            /* 先写 UTF-16LE BOM，否则 WritePrivateProfileStringW 会建 ANSI 文件、丢失中文 */
            HANDLE f = CreateFileW(ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                static const BYTE bom[2] = { 0xFF, 0xFE };
                DWORD w;
                WriteFile(f, bom, sizeof(bom), &w, NULL);
                CloseHandle(f);
            }
        }
    }
    free(dir);
    return ini;
}

static void LoadSettings(void)
{
    wchar_t *ini = IniPath(FALSE);
    wchar_t *buf;
    int mode;

    if (!ini || !IsFile(ini)) {
        free(ini);
        SetMode(MODE_E2T);
        return;
    }
    mode = (int)GetPrivateProfileIntW(L"gui", L"mode", MODE_E2T, ini);
    SetMode(mode == MODE_T2E ? MODE_T2E : MODE_E2T);
    SendMessageW(g_hChkNs, BM_SETCHECK,
                 GetPrivateProfileIntW(L"gui", L"ns", 0, ini) ? BST_CHECKED : BST_UNCHECKED, 0);
    buf = (wchar_t *)malloc(PATH_BUF * sizeof(wchar_t));
    if (buf) {
        GetPrivateProfileStringW(L"gui", L"src", L"", buf, PATH_BUF, ini);
        SetWindowTextW(g_hEditSrc, buf);
        GetPrivateProfileStringW(L"gui", L"dst", L"", buf, PATH_BUF, ini);
        SetWindowTextW(g_hEditDst, buf);
        free(buf);
    }
    free(ini);
}

static void WriteIniStr(const wchar_t *ini, const wchar_t *key, const wchar_t *val)
{
    /* 包一层引号，读取时 GetPrivateProfileString 会去掉，保证首尾空格不丢 */
    wchar_t *q = WCat(L"\"", val ? val : L"", L"\"");
    if (q) {
        WritePrivateProfileStringW(L"gui", key, q, ini);
        free(q);
    }
}

static void SaveSettings(void)
{
    wchar_t *ini, *src, *dst;

    if (g_smoke)
        return; /* 冒烟测试不改用户设置 */
    ini = IniPath(TRUE);
    if (!ini)
        return;
    src = GetCtlText(g_hEditSrc);
    dst = GetCtlText(g_hEditDst);
    WritePrivateProfileStringW(L"gui", L"mode", GetMode() == MODE_T2E ? L"1" : L"0", ini);
    WritePrivateProfileStringW(L"gui", L"ns",
        SendMessageW(g_hChkNs, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0", ini);
    WriteIniStr(ini, L"src", src);
    WriteIniStr(ini, L"dst", dst);
    WritePrivateProfileStringW(NULL, NULL, NULL, ini); /* 刷新缓存 */
    free(src);
    free(dst);
    free(ini);
}

/* ---------- 文件对话框（IFileDialog） ---------- */

static IShellItem *ItemFromPath(const wchar_t *path)
{
    IShellItem *it = NULL;
    if (FAILED(SHCreateItemFromParsingName(path, NULL, &IID_IShellItem, (void **)&it)))
        return NULL;
    return it;
}

static void ApplyInitialPath(IFileDialog *fd, int kind, const wchar_t *init)
{
    IShellItem *it;
    wchar_t *parent;

    if (!init || !*init)
        return;
    if (IsDir(init)) {
        it = ItemFromPath(init);
        if (it) {
            fd->lpVtbl->SetFolder(fd, it);
            it->lpVtbl->Release(it);
        }
        return;
    }
    parent = ParentDir(init);
    if (parent && IsDir(parent)) {
        it = ItemFromPath(parent);
        if (it) {
            fd->lpVtbl->SetFolder(fd, it);
            it->lpVtbl->Release(it);
        }
    }
    if (kind != BROWSE_FOLDER) {
        const wchar_t *name = wcsrchr(init, L'\\');
        name = name ? name + 1 : init;
        if (*name)
            fd->lpVtbl->SetFileName(fd, name);
    }
    free(parent);
}

/* 返回所选路径（调用者 free），取消或失败返回 NULL */
static wchar_t *BrowsePath(HWND owner, int kind, const wchar_t *init, const wchar_t *title)
{
    static const COMDLG_FILTERSPEC specOpen[] = {
        { L"易语言源码 (*.e;*.ec)", L"*.e;*.ec" },
        { L"所有文件 (*.*)", L"*.*" },
    };
    static const COMDLG_FILTERSPEC specSave[] = {
        { L"易语言源码 (*.e)", L"*.e" },
    };
    IFileDialog *fd = NULL;
    IShellItem *it = NULL;
    FILEOPENDIALOGOPTIONS opt = 0;
    wchar_t *res = NULL;
    HRESULT hr;

    hr = CoCreateInstance(kind == BROWSE_SAVE_E ? &CLSID_FileSaveDialog : &CLSID_FileOpenDialog,
                          NULL, CLSCTX_INPROC_SERVER, &IID_IFileDialog, (void **)&fd);
    if (FAILED(hr) || !fd) {
        ErrorBox(owner, L"无法创建文件对话框。");
        return NULL;
    }
    if (FAILED(fd->lpVtbl->GetOptions(fd, &opt)))
        opt = 0;
    opt |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
    if (kind == BROWSE_FOLDER)
        opt |= FOS_PICKFOLDERS;
    else if (kind == BROWSE_OPEN_E)
        opt |= FOS_FILEMUSTEXIST;
    else
        opt |= FOS_OVERWRITEPROMPT;
    fd->lpVtbl->SetOptions(fd, opt);
    if (kind == BROWSE_OPEN_E) {
        fd->lpVtbl->SetFileTypes(fd, 2, specOpen);
        fd->lpVtbl->SetFileTypeIndex(fd, 1);
    } else if (kind == BROWSE_SAVE_E) {
        fd->lpVtbl->SetFileTypes(fd, 1, specSave);
        fd->lpVtbl->SetFileTypeIndex(fd, 1);
        fd->lpVtbl->SetDefaultExtension(fd, L"e");
    }
    fd->lpVtbl->SetTitle(fd, title);
    ApplyInitialPath(fd, kind, init);

    hr = fd->lpVtbl->Show(fd, owner);
    if (SUCCEEDED(hr)) {
        if (SUCCEEDED(fd->lpVtbl->GetResult(fd, &it)) && it) {
            PWSTR p = NULL;
            if (SUCCEEDED(it->lpVtbl->GetDisplayName(it, SIGDN_FILESYSPATH, &p)) && p) {
                res = WDup(p);
                CoTaskMemFree(p);
            }
            it->lpVtbl->Release(it);
        }
    } else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        ErrorBox(owner, L"文件对话框出错。");
    }
    fd->lpVtbl->Release(fd);
    return res;
}

static void OnBrowseSrc(HWND hwnd)
{
    int mode = GetMode();
    wchar_t *cur = GetCtlText(g_hEditSrc);
    wchar_t *sel = BrowsePath(hwnd, mode == MODE_E2T ? BROWSE_OPEN_E : BROWSE_FOLDER, cur,
                              mode == MODE_E2T ? L"选择易语言源码 (.e / .ec)" : L"选择要组包的文本目录");
    if (sel) {
        SetWindowTextW(g_hEditSrc, sel);
        SuggestDstIfEmpty(sel);
        free(sel);
    }
    free(cur);
}

static void OnBrowseDst(HWND hwnd)
{
    int mode = GetMode();
    wchar_t *cur = GetCtlText(g_hEditDst);
    wchar_t *sel;

    if (cur && !*cur) { /* 目标为空时以推荐值作为初始位置 */
        wchar_t *src = GetCtlText(g_hEditSrc);
        if (src && *src) {
            free(cur);
            cur = RecommendDst(mode, src);
        }
        free(src);
    }
    sel = BrowsePath(hwnd, mode == MODE_E2T ? BROWSE_FOLDER : BROWSE_SAVE_E, cur,
                     mode == MODE_E2T ? L"选择输出文件夹" : L"保存为易语言源码 (.e)");
    if (sel) {
        SetWindowTextW(g_hEditDst, sel);
        free(sel);
    }
    free(cur);
}

/* ---------- 拖放 ---------- */

static BOOL HasExtEtprj(const wchar_t *p)
{
    const wchar_t *dot = wcsrchr(p, L'.');
    const wchar_t *s1 = wcsrchr(p, L'\\'), *s2 = wcsrchr(p, L'/');
    if (!dot || (s1 && dot < s1) || (s2 && dot < s2))
        return FALSE;
    return _wcsicmp(dot, L".etprj") == 0;
}

/*
 * 把路径归类为转换源：
 *   .e / .ec 文件 → e2t，源 = 该文件
 *   文件夹        → t2e，源 = 该文件夹
 *   .etprj 文件   → t2e，源 = 其所在文件夹（文本项目描述文件）
 * 成功返回新分配的源路径并写 *mode；无法识别返回 NULL。
 */
static wchar_t *ClassifySource(const wchar_t *path, int *mode)
{
    if (IsDir(path)) {
        *mode = MODE_T2E;
        return FullPath(path);
    }
    if (IsFile(path) && HasExtEOrEc(path)) {
        *mode = MODE_E2T;
        return FullPath(path);
    }
    if (IsFile(path) && HasExtEtprj(path)) {
        wchar_t *full = FullPath(path), *dir;
        dir = full ? ParentDir(full) : NULL;
        free(full);
        if (dir && IsDir(dir)) {
            *mode = MODE_T2E;
            return dir;
        }
        free(dir);
    }
    return NULL;
}

/* resetDst：TRUE 时总是按新源重新推荐目标（命令行启动用） */
static BOOL ApplySource(const wchar_t *path, BOOL resetDst)
{
    int newMode = MODE_E2T;
    wchar_t *src = ClassifySource(path, &newMode);

    if (!src)
        return FALSE;
    if (newMode != GetMode()) {
        SetMode(newMode);
        resetDst = TRUE; /* 模式变了，旧目标不再适用 */
    }
    if (resetDst)
        SetWindowTextW(g_hEditDst, L"");
    SetWindowTextW(g_hEditSrc, src);
    SuggestDstIfEmpty(src);
    free(src);
    return TRUE;
}

static void OnDropPath(HWND hwnd, const wchar_t *path)
{
    if (g_running) {
        SetStatus(L"运行中，已忽略拖放");
        return;
    }
    if (!ApplySource(path, FALSE))
        MessageBoxW(hwnd, L"只支持拖入 .e / .ec 文件（解包）、文件夹或 .etprj（组包）。", kTitle,
                    MB_OK | MB_ICONINFORMATION);
}

static void OnDropFiles(HWND hwnd, HDROP hd)
{
    UINT len = DragQueryFileW(hd, 0, NULL, 0);
    if (len > 0) {
        wchar_t *p = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
        if (p) {
            if (DragQueryFileW(hd, 0, p, len + 1) > 0)
                OnDropPath(hwnd, p);
            free(p);
        }
    }
    DragFinish(hd);
}

/* ---------- 运行 ---------- */

typedef struct {
    HWND hwnd;
    wchar_t *app, *src, *dst; /* 自有字符串 */
    const wchar_t *argv[10];
    int argc;
} Job;

static void FreeJob(Job *j)
{
    if (!j)
        return;
    free(j->app);
    free(j->src);
    free(j->dst);
    free(j);
}

/* 工作线程中的行回调：转交 UI 线程（所有权随消息转移） */
static void GuiLine(void *ctx, wchar_t *line)
{
    if (!PostMessageW((HWND)ctx, WM_APP_LINE, 0, (LPARAM)line))
        free(line);
}

static unsigned __stdcall WorkerProc(void *p)
{
    Job *j = (Job *)p;
    RunResult rr;
    BOOL ok = RunCli(j->app, j->argv, j->argc, GuiLine, j->hwnd, &rr);
    PostMessageW(j->hwnd, WM_APP_DONE, ok ? rr.exitCode : rr.winErr, ok);
    FreeJob(j);
    return 0;
}

/* 查找 CLI：先本 exe 目录，再环境变量 QAQ_ETOOL_CLI（可为文件或目录） */
static wchar_t *FindCli(void)
{
    wchar_t *dir = ExeDir(), *p;
    DWORD n;

    if (dir) {
        p = WCat(dir, L"\\QAQ-ETool.exe", NULL);
        free(dir);
        if (p && IsFile(p))
            return p;
        free(p);
    }
    n = GetEnvironmentVariableW(L"QAQ_ETOOL_CLI", NULL, 0);
    if (n > 0) {
        wchar_t *env = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
        if (env && GetEnvironmentVariableW(L"QAQ_ETOOL_CLI", env, n) < n) {
            TrimPath(env);
            p = IsDir(env) ? WCat(env, L"\\QAQ-ETool.exe", NULL) : WDup(env);
            if (p && IsFile(p)) {
                wchar_t *full = FullPath(p);
                free(p);
                free(env);
                return full;
            }
            free(p);
        }
        free(env);
    }
    return NULL;
}

static void SetRunningUi(BOOL running)
{
    g_running = running;
    if (running && GetFocus() == g_hBtnStart)
        SetFocus(g_hEditLog); /* 按钮禁用前移走焦点，保持键盘可用 */
    EnableWindow(g_hBtnStart, !running);
    if (running)
        EnableWindow(g_hBtnOpen, FALSE);
}

static void StartRun(HWND hwnd)
{
    int mode = GetMode();
    BOOL ns = SendMessageW(g_hChkNs, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t *srcRaw = NULL, *dstRaw = NULL;
    Job *j = NULL;
    uintptr_t th;

    if (g_running)
        return;
    srcRaw = GetCtlText(g_hEditSrc);
    dstRaw = GetCtlText(g_hEditDst);
    if (!srcRaw || !*srcRaw) {
        ErrorBox(hwnd, mode == MODE_E2T ? L"请先选择要解包的 .e / .ec 文件。" : L"请先选择要组包的文本目录。");
        SetFocus(g_hEditSrc);
        goto out;
    }
    if (!dstRaw || !*dstRaw) {
        ErrorBox(hwnd, mode == MODE_E2T ? L"请先指定输出文件夹。" : L"请先指定要生成的 .e 文件。");
        SetFocus(g_hEditDst);
        goto out;
    }
    j = (Job *)calloc(1, sizeof(Job));
    if (!j)
        goto out;
    j->hwnd = hwnd;
    j->src = FullPath(srcRaw);
    j->dst = FullPath(dstRaw);
    if (!j->src || !j->dst) {
        ErrorBox(hwnd, L"路径无效。");
        goto out;
    }

    SendMessageW(g_hEditLog, WM_SETTEXT, 0, (LPARAM)L"");
    free(g_succPath);
    g_succPath = NULL;
    free(g_openTarget);
    g_openTarget = NULL;

    j->app = FindCli();
    if (!j->app) {
        LogAppend(L"[失败] 找不到 QAQ-ETool.exe：请把它放在本程序同一目录，或设置环境变量 QAQ_ETOOL_CLI。");
        SetStatus(L"失败 (找不到 QAQ-ETool.exe)");
        EnableWindow(g_hBtnOpen, FALSE);
        goto out;
    }

    j->argv[j->argc++] = L"-mode";
    j->argv[j->argc++] = mode == MODE_E2T ? L"e2t" : L"t2e";
    j->argv[j->argc++] = L"-src";
    j->argv[j->argc++] = j->src;
    j->argv[j->argc++] = L"-dst";
    j->argv[j->argc++] = j->dst;
    if (mode == MODE_E2T && ns) {
        j->argv[j->argc++] = L"-ns";
        j->argv[j->argc++] = L"2";
    }
    {
        wchar_t *cmd = BuildCommandLine(j->app, j->argv, j->argc);
        wchar_t *line = cmd ? WCat(L"[命令] ", cmd, NULL) : NULL;
        if (line)
            LogAppend(line);
        free(line);
        free(cmd);
    }

    SaveSettings();
    Runner_ResetCancel();
    SetRunningUi(TRUE);
    SetStatus(L"运行中…");
    g_startTick = GetTickCount64();

    th = _beginthreadex(NULL, 0, WorkerProc, j, 0, NULL);
    if (th == 0) {
        LogAppend(L"[失败] 无法创建工作线程。");
        SetStatus(L"失败 (无法创建工作线程)");
        SetRunningUi(FALSE);
        goto out;
    }
    g_hWorker = (HANDLE)th;
    j = NULL; /* 所有权交给工作线程 */
out:
    FreeJob(j);
    free(srcRaw);
    free(dstRaw);
}

static void OnLine(wchar_t *line)
{
    if (StartsWith(line, L"SUCC:")) {
        wchar_t *p = WDup(line + 5);
        if (p) {
            TrimPath(p);
            free(g_succPath);
            g_succPath = p;
        }
    }
    if (IsErrorLine(line)) {
        wchar_t *s = WCat(L"[失败] ", line, NULL);
        LogAppend(s ? s : line);
        free(s);
    } else {
        LogAppend(line);
    }
    free(line);
}

static void OnDone(HWND hwnd, DWORD code, BOOL launched)
{
    wchar_t buf[256];
    ULONGLONG ms = GetTickCount64() - g_startTick;
    double sec = (double)ms / 1000.0;

    if (g_hWorker) {
        WaitForSingleObject(g_hWorker, INFINITE); /* 线程已在收尾 */
        CloseHandle(g_hWorker);
        g_hWorker = NULL;
    }
    SetRunningUi(FALSE);

    if (!launched) {
        wchar_t *msg = NULL;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, code, 0, (LPWSTR)&msg, 0, NULL);
        StringCchPrintfW(buf, 256, L"[失败] 无法启动 QAQ-ETool.exe（错误 %lu）：%s", code, msg ? msg : L"");
        if (msg)
            LocalFree(msg);
        LogAppend(buf);
        StringCchPrintfW(buf, 256, L"失败 (无法启动，错误 %lu)", code);
        SetStatus(buf);
    } else if (code == 0 || code == 2) {
        StringCchPrintfW(buf, 256, code == 0 ? L"成功 (耗时 %.1f s)" : L"成功但有无法解析内容 (耗时 %.1f s)", sec);
        SetStatus(buf);
        g_openTarget = g_succPath ? WDup(g_succPath) : GetCtlText(g_hEditDst);
        EnableWindow(g_hBtnOpen, g_openTarget != NULL);
    } else {
        StringCchPrintfW(buf, 256, L"失败 (退出码 %ld)", (long)code);
        SetStatus(buf);
    }
    if (g_closePending)
        DestroyWindow(hwnd);
}

/* 打开目标：目录直接打开；文件则打开所在目录并选中 */
static void OnOpenTarget(HWND hwnd)
{
    const wchar_t *p = g_openTarget;
    DWORD a;

    if (!p)
        return;
    a = GetFileAttributesW(p);
    if (a == INVALID_FILE_ATTRIBUTES) {
        ErrorBox(hwnd, L"目标不存在。");
        return;
    }
    if (a & FILE_ATTRIBUTE_DIRECTORY) {
        if ((INT_PTR)ShellExecuteW(hwnd, L"open", p, NULL, NULL, SW_SHOWNORMAL) <= 32)
            ErrorBox(hwnd, L"无法打开目标文件夹。");
        return;
    }
    {
        PIDLIST_ABSOLUTE pidl = NULL;
        HRESULT hr = SHParseDisplayName(p, NULL, &pidl, 0, NULL);
        if (SUCCEEDED(hr)) {
            hr = SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
            CoTaskMemFree(pidl);
        }
        if (FAILED(hr)) {
            wchar_t *dir = ParentDir(p);
            if (!dir || (INT_PTR)ShellExecuteW(hwnd, L"open", dir, NULL, NULL, SW_SHOWNORMAL) <= 32)
                ErrorBox(hwnd, L"无法打开目标所在文件夹。");
            free(dir);
        }
    }
}

/* ---------- 编辑框子类：Ctrl+A 全选 ---------- */

static LRESULT CALLBACK EditSubclassProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    if (m == WM_KEYDOWN && w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
        SendMessageW(h, EM_SETSEL, 0, -1);
        return 0;
    }
    if (m == WM_CHAR && w == 1) /* 吞掉 Ctrl+A 产生的 0x01，避免蜂鸣 */
        return 0;
    if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, EditSubclassProc, id);
    return DefSubclassProc(h, m, w, l);
}

/* ---------- 布局 ---------- */

static void Layout(HWND hwnd)
{
    RECT rc, sr;
    int cw, ch, m, gap, rowH, lblW, browseW, actW, r1W, r2W, chkW, sh, editW, y, lblH, logH;
    HDWP dw;

    if (!g_hEditLog)
        return;
    GetClientRect(hwnd, &rc);
    cw = rc.right;
    ch = rc.bottom;
    m = S(10);
    gap = S(6);
    rowH = IMax(S(23), g_fontH + S(8));
    lblW = IMax(TextWidth(g_hLblSrc), TextWidth(g_hLblDst)) + S(6);
    browseW = IMax(S(80), IMax(TextWidth(g_hBtnSrc), TextWidth(g_hBtnDst)) + S(24));
    actW = IMax(S(96), IMax(TextWidth(g_hBtnStart), TextWidth(g_hBtnOpen)) + S(24));
    r1W = TextWidth(g_hRadioE2T) + S(28);
    r2W = TextWidth(g_hRadioT2E) + S(28);
    chkW = TextWidth(g_hChkNs) + S(28);

    SendMessageW(g_hStatus, WM_SIZE, 0, 0);
    GetWindowRect(g_hStatus, &sr);
    sh = sr.bottom - sr.top;

    editW = IMax(S(60), cw - m * 2 - lblW - gap * 2 - browseW);
    lblH = g_fontH + S(4);

    dw = BeginDeferWindowPos(13);
#define MOVE(h, x, yy, w, hh) \
    do { if (dw) dw = DeferWindowPos(dw, (h), NULL, (x), (yy), (w), (hh), SWP_NOZORDER | SWP_NOACTIVATE); } while (0)
    y = m;
    MOVE(g_hRadioE2T, m, y, r1W, rowH);
    MOVE(g_hRadioT2E, m + r1W + S(16), y, r2W, rowH);
    y += rowH + gap;
    MOVE(g_hLblSrc, m, y, lblW, rowH);
    MOVE(g_hEditSrc, m + lblW + gap, y, editW, rowH);
    MOVE(g_hBtnSrc, cw - m - browseW, y, browseW, rowH);
    y += rowH + gap;
    MOVE(g_hLblDst, m, y, lblW, rowH);
    MOVE(g_hEditDst, m + lblW + gap, y, editW, rowH);
    MOVE(g_hBtnDst, cw - m - browseW, y, browseW, rowH);
    y += rowH + gap;
    MOVE(g_hChkNs, m, y, chkW, rowH);
    MOVE(g_hBtnStart, cw - m - actW * 2 - gap, y, actW, rowH);
    MOVE(g_hBtnOpen, cw - m - actW, y, actW, rowH);
    y += rowH + gap * 2;
    MOVE(g_hLblLog, m, y, cw - m * 2, lblH);
    y += lblH + S(2);
    logH = IMax(S(40), ch - sh - m - y);
    MOVE(g_hEditLog, m, y, cw - m * 2, logH);
#undef MOVE
    if (dw)
        EndDeferWindowPos(dw);
    InvalidateRect(hwnd, NULL, TRUE);
}

/* ---------- 创建 ---------- */

static HWND MakeCtl(HWND parent, DWORD ex, const wchar_t *cls, const wchar_t *text, DWORD style, int id)
{
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                           parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
}

static BOOL OnCreate(HWND hwnd)
{
    g_hwndMain = hwnd;
    g_dpi = DpiOf(hwnd);

    /* 创建顺序即 Tab 顺序；标签紧挨在对应编辑框之前，助记符会把焦点交给下一个可停留控件 */
    g_hRadioE2T = MakeCtl(hwnd, 0, WC_BUTTONW, L"解包(&E) .e → 文本 (e2t)",
                          BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, IDC_RADIO_E2T);
    g_hRadioT2E = MakeCtl(hwnd, 0, WC_BUTTONW, L"组包(&T) 文本 → .e (t2e)",
                          BS_AUTORADIOBUTTON, IDC_RADIO_T2E);
    g_hLblSrc = MakeCtl(hwnd, 0, WC_STATICW, L"来源(&S):", SS_LEFT | SS_CENTERIMAGE | WS_GROUP, IDC_LBL_SRC);
    g_hEditSrc = MakeCtl(hwnd, WS_EX_CLIENTEDGE, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_EDIT_SRC);
    g_hBtnSrc = MakeCtl(hwnd, 0, WC_BUTTONW, L"浏览(&B)…", BS_PUSHBUTTON | WS_TABSTOP, IDC_BTN_SRC);
    g_hLblDst = MakeCtl(hwnd, 0, WC_STATICW, L"目标(&D):", SS_LEFT | SS_CENTERIMAGE, IDC_LBL_DST);
    g_hEditDst = MakeCtl(hwnd, WS_EX_CLIENTEDGE, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_EDIT_DST);
    g_hBtnDst = MakeCtl(hwnd, 0, WC_BUTTONW, L"浏览(&R)…", BS_PUSHBUTTON | WS_TABSTOP, IDC_BTN_DST);
    g_hChkNs = MakeCtl(hwnd, 0, WC_BUTTONW, L"中文目录名 (-ns 2)(&N)", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_CHK_NS);
    g_hBtnStart = MakeCtl(hwnd, 0, WC_BUTTONW, L"开始(&G)", BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_BTN_START);
    g_hBtnOpen = MakeCtl(hwnd, 0, WC_BUTTONW, L"打开目标(&O)", BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, IDC_BTN_OPEN);
    g_hLblLog = MakeCtl(hwnd, 0, WC_STATICW, L"日志(&L):", SS_LEFT, IDC_LBL_LOG);
    g_hEditLog = MakeCtl(hwnd, WS_EX_CLIENTEDGE, WC_EDITW, L"",
                         ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, IDC_EDIT_LOG);
    g_hStatus = MakeCtl(hwnd, 0, STATUSCLASSNAMEW, L"", SBARS_SIZEGRIP, IDC_STATUS);

    if (!g_hRadioE2T || !g_hRadioT2E || !g_hLblSrc || !g_hEditSrc || !g_hBtnSrc || !g_hLblDst ||
        !g_hEditDst || !g_hBtnDst || !g_hChkNs || !g_hBtnStart || !g_hBtnOpen || !g_hLblLog ||
        !g_hEditLog || !g_hStatus)
        return FALSE;

    SendMessageW(g_hEditSrc, EM_SETLIMITTEXT, PATH_BUF - 1, 0);
    SendMessageW(g_hEditDst, EM_SETLIMITTEXT, PATH_BUF - 1, 0);
    SendMessageW(g_hEditLog, EM_SETLIMITTEXT, 0, 0); /* 0 = 多行编辑框最大容量 */
    SetWindowSubclass(g_hEditSrc, EditSubclassProc, 1, 0);
    SetWindowSubclass(g_hEditDst, EditSubclassProc, 2, 0);
    SetWindowSubclass(g_hEditLog, EditSubclassProc, 3, 0);

    RecreateFont(hwnd);
    LoadSettings();
    SetStatus(L"就绪");
    if (g_initPath && !ApplySource(g_initPath, TRUE))
        SetStatus(L"启动参数不是 .e / .ec / .etprj 或文件夹，已忽略");
    DragAcceptFiles(hwnd, TRUE);
    g_hFocusSave = g_hEditSrc;
    Layout(hwnd);
    return TRUE;
}

/* ---------- 窗口过程 ---------- */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        return OnCreate(hwnd) ? 0 : -1;

    case WM_SIZE:
        Layout(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lParam;
        RECT r = { 0, 0, S(560), S(400) };
        AdjustRectDpi(&r, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        mm->ptMinTrackSize.x = r.right - r.left;
        mm->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }

    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)lParam;
        g_dpi = HIWORD(wParam);
        RecreateFont(hwnd);
        SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Layout(hwnd);
        return 0;
    }

    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETNONCLIENTMETRICS) {
            RecreateFont(hwnd);
            Layout(hwnd);
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            HWND f = GetFocus();
            if (f && IsChild(hwnd, f))
                g_hFocusSave = f;
        } else if (g_hFocusSave && IsWindow(g_hFocusSave) && IsWindowEnabled(g_hFocusSave)) {
            SetFocus(g_hFocusSave);
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC:
        /* 只读日志框用窗口底色，便于阅读 */
        if ((HWND)lParam == g_hEditLog) {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;

    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        switch (id) {
        case IDC_BTN_START:
            if (!g_running && IsWindowEnabled(g_hBtnStart))
                StartRun(hwnd);
            return 0;
        case IDC_RADIO_E2T:
        case IDC_RADIO_T2E:
            if (code == BN_CLICKED)
                SetMode(id == IDC_RADIO_T2E ? MODE_T2E : MODE_E2T);
            return 0;
        case IDC_BTN_SRC:
            OnBrowseSrc(hwnd);
            return 0;
        case IDC_BTN_DST:
            OnBrowseDst(hwnd);
            return 0;
        case IDC_BTN_OPEN:
            OnOpenTarget(hwnd);
            return 0;
        case IDCANCEL: /* Esc 不关闭窗口 */
            return 0;
        }
        break;
    }

    case WM_DROPFILES:
        OnDropFiles(hwnd, (HDROP)wParam);
        return 0;

    case WM_APP_LINE:
        OnLine((wchar_t *)lParam);
        return 0;

    case WM_APP_DONE:
        OnDone(hwnd, (DWORD)wParam, (BOOL)lParam);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_SMOKE) {
            KillTimer(hwnd, TIMER_SMOKE);
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_CLOSE:
        if (g_running) {
            if (g_closePending)
                return 0;
            if (MessageBoxW(hwnd, L"转换仍在运行，是否终止并退出？", kTitle,
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                g_closePending = TRUE;
                SetStatus(L"正在终止…");
                Runner_RequestCancel(); /* 等 WM_APP_DONE 后再销毁窗口 */
            }
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        SaveSettings();
        DragAcceptFiles(hwnd, FALSE);
        g_hwndMain = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ---------- GUI 入口 ---------- */

static int RunGui(int nShow)
{
    WNDCLASSEXW wc;
    INITCOMMONCONTROLSEX icc;
    HWND hwnd;
    MSG msg;
    BOOL r;
    int rc = 0;
    HRESULT hrCo;

    hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    if (!InitCommonControlsEx(&icc)) {
        rc = 4;
        goto out;
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        rc = 4;
        goto out;
    }

    g_dpi = SystemDpi();
    hwnd = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, 700, 520, NULL, NULL, g_hInst, NULL);
    if (!hwnd) {
        rc = 4;
        goto out;
    }
    /* 按窗口所在显示器的 DPI 设定初始大小 */
    {
        RECT wr = { 0, 0, S(700), S(500) };
        g_dpi = DpiOf(hwnd);
        wr.right = S(700);
        wr.bottom = S(500);
        AdjustRectDpi(&wr, WS_OVERLAPPEDWINDOW, 0);
        SetWindowPos(hwnd, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ShowWindow(hwnd, g_smoke ? SW_SHOWNOACTIVATE : nShow);
    UpdateWindow(hwnd);
    if (g_smoke && !SetTimer(hwnd, TIMER_SMOKE, 500, NULL)) {
        DestroyWindow(hwnd);
        rc = 4;
    }

    while ((r = GetMessageW(&msg, NULL, 0, 0)) != 0) {
        if (r == -1) {
            rc = 4;
            break;
        }
        if (g_hwndMain && IsDialogMessageW(g_hwndMain, &msg))
            continue; /* Tab / 助记符 / Enter */
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (r != -1 && rc == 0)
        rc = (int)msg.wParam;

out:
    if (g_hFont) {
        DeleteObject(g_hFont);
        g_hFont = NULL;
    }
    free(g_succPath);
    free(g_openTarget);
    g_succPath = g_openTarget = NULL;
    if (SUCCEEDED(hrCo))
        CoUninitialize();
    return rc;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR pCmd, int nShow)
{
    int argc = 0, rc;
    LPWSTR *argv;

    (void)hPrev;
    (void)pCmd;
    g_hInst = hInst;
    SetDllDirectoryW(L""); /* 从搜索路径中移除当前目录，防 DLL 劫持 */
    InitDpiApi();
    Runner_Init();

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 4 && wcscmp(argv[1], L"--selftest") == 0) {
        rc = SelfTest(argv[2], argv[3], argc > 4 ? argv[4] : NULL, argc > 5 ? argv[5] : NULL);
    } else {
        int i;
        for (i = 1; argv && i < argc; i++) {
            if (wcscmp(argv[i], L"--smoke") == 0)
                g_smoke = TRUE;
            else if (!g_initPath && argv[i][0])
                g_initPath = argv[i]; /* 第一个非开关参数 = 初始源路径 */
        }
        rc = RunGui(nShow);
    }
    if (argv)
        LocalFree(argv);
    Runner_Shutdown();
    return rc;
}
