/* ============================================================================
 * mbk_webview.cpp —�?edgewebview（Microsoft Edge WebView2）内核后�? * ----------------------------------------------------------------------------
 * 【定位�? *   MbKit 的第二个 wke 内核。node.dll 那条路（mbkit_fwd.c �?bind_all 后半段）
 *   一个字不动；本文件只负责「把 mbk_slots[] 的一部分换成自己的实现」�? *   编组（mbk_marshal_exec）、属主线程判定（mbk_can_kernel）、回调存活校�? *   （mbkit_cb.c �?wrapper）全部原样复�?—�?这正是「干净接入点」的意思�? *
 * 【线程模型（硬约束）�? *   所�?mbkwv_* 实现只会�?mbk_marshal_exec 派到**内核属主线程**上执行；
 *   WebView2 的异步完成回调也投回同一条线程的消息队列�? *   因此本文件里所有「同步等 WebView2 结果」的地方都必�?*自己泵消�?*�? *   不能�?mbk_wait_pump（它默认�?WaitForSingleObject，泵是关的）�? *
 * 【wkeString / jsValue�? *   模块的事件回调签名里�?wkeString（标�?URL/弹窗消息），`WKE取文本` �? *   wkeGetStringW 取�?—�?所以本层必须真的实现一对可读写的字符串句柄�? *   不能返回 0。jsValue 同理：运行JS 返回的长整数要能�?jsToStringW 读回来�? *
 * 【为什么不做成独立 DLL�? *   mbkit.c �?bind_symbols 是按 GetProcAddress(g_hNode, name) 取符号的�? *   独立 DLL 能省一次槽表绑定，但代价是**多一个要释放到磁盘的文件**（模�? *   现在只释�?MbKit144.dll）。改为把 g_hNode 指向 MbKit.dll 自己�? *   bind_symbols 取到的是 gen_thunk.c 的编�?thunk，thunk �?marshal �? *   本层的槽�?—�?多一跳，但语义完全一致，且保持「单文件」契约�? * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <WebView2.h>

/* MinGW �?__uuidof �?__mingw_uuidof<T>() 模板，需�?__CRT_UUID_DECL 显式特化�? * WebView2.h �?MIDL_INTERFACE("guid") 声明接口，但 MinGW 不会自动从那里提�? * UUID —�?必须手动补齐，否�?QueryInterface 里的 __uuidof 编译不过�?*/
#ifndef __CRT_UUID_DECL
#define __CRT_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    extern "C++" { \
        template<> inline const GUID& __mingw_uuidof<type>() { \
            static const GUID __uuid_inst = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }; \
            return __uuid_inst; \
        } \
        template<> inline const GUID& __mingw_uuidof<type*>() { \
            return __mingw_uuidof<type>(); \
        } \
    }
#endif

__CRT_UUID_DECL(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
    0x4e8a3389, 0xc9d8, 0x4bd2, 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d)
__CRT_UUID_DECL(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
    0x6c4819f3, 0xc9b7, 0x4260, 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c)
__CRT_UUID_DECL(ICoreWebView2DocumentTitleChangedEventHandler,
    0xf5f2b923, 0x953e, 0x4042, 0x9f, 0x95, 0xf3, 0xa1, 0x18, 0xe1, 0xaf, 0xd4)
__CRT_UUID_DECL(ICoreWebView2SourceChangedEventHandler,
    0x3c067f9f, 0x5388, 0x4772, 0x8b, 0x48, 0x79, 0xf7, 0xef, 0x1a, 0xb3, 0x7c)
__CRT_UUID_DECL(ICoreWebView2NavigationStartingEventHandler,
    0x9adbe429, 0xf36d, 0x432b, 0x9d, 0xdc, 0xf8, 0x88, 0x1f, 0xbd, 0x76, 0xe3)
__CRT_UUID_DECL(ICoreWebView2NavigationCompletedEventHandler,
    0xd33a35bf, 0x1c49, 0x4f98, 0x93, 0xab, 0x00, 0x6e, 0x05, 0x33, 0xfe, 0x1c)
__CRT_UUID_DECL(ICoreWebView2ContentLoadingEventHandler,
    0x364471e7, 0xf2be, 0x4910, 0xbd, 0xba, 0xd7, 0x20, 0x77, 0xd5, 0x1c, 0x4b)
__CRT_UUID_DECL(ICoreWebView2ScriptDialogOpeningEventHandler,
    0xef381bf9, 0xafa8, 0x4e37, 0x91, 0xc4, 0x8a, 0xc4, 0x85, 0x24, 0xbd, 0xfb)
__CRT_UUID_DECL(ICoreWebView2NewWindowRequestedEventHandler,
    0xd4c185fe, 0xc81c, 0x4989, 0x97, 0xaf, 0x2d, 0x3f, 0xa7, 0xab, 0x56, 0x51)
__CRT_UUID_DECL(ICoreWebView2ExecuteScriptCompletedHandler,
    0x49511172, 0xcc67, 0x4bca, 0x99, 0x23, 0x13, 0x71, 0x12, 0xf4, 0xc4, 0xcc)
__CRT_UUID_DECL(ICoreWebView2Controller2,
    0xc979903e, 0xd4ca, 0x4228, 0x92, 0xeb, 0x47, 0xee, 0x3f, 0xa9, 0x6e, 0xab)
__CRT_UUID_DECL(ICoreWebView2WebResourceRequestedEventHandler,
    0xab00b74c, 0x15f1, 0x4646, 0x80, 0xe8, 0xe7, 0x63, 0x41, 0xd2, 0x5d, 0x71)
__CRT_UUID_DECL(ICoreWebView2Settings2,
    0xee9a0f68, 0xf46c, 0x4e32, 0xac, 0x23, 0xef, 0x8c, 0xac, 0x22, 0x4d, 0x2a)
__CRT_UUID_DECL(ICoreWebView2DownloadStartingEventHandler,
    0xefedc989, 0xc396, 0x41ca, 0x83, 0xf7, 0x07, 0xf8, 0x45, 0xa5, 0x57, 0x24)
__CRT_UUID_DECL(ICoreWebView2StateChangedEventHandler,
    0x81336594, 0x7ede, 0x4ba9, 0xbf, 0x71, 0xac, 0xf0, 0xa9, 0x5b, 0x58, 0xdd)

#include "mbk_webview.h"
#include "gen_fwd.h"
#include "gen_webview_bind.h"

extern "C" int __stdcall MbKit_DebugEnabled(void);

/* ==========================================================================
 * 0. 小工�? * ==========================================================================*/
static char g_diag[65536];
static int  g_diagLen = 0;
/* 无条件落盘：先写 <exe目录>\mbk_diag.log，失败再写 %TEMP%\mbk_diag.log。
 * 用 CreateFileA/WriteFile，绕开 CRT fopen（实测 fopen 分支从不产生文件）。 */
static DWORD diag_file(const char* line, int n)
{
    char path[MAX_PATH + 32];
    char head[64];
    int hn;
    DWORD wrote = 0;
    HANDLE hf = INVALID_HANDLE_VALUE;
    DWORD e1 = 0, e2 = 0, e3 = 0;
    DWORD dl = GetModuleFileNameA(NULL, path, MAX_PATH);

    if (dl > 0 && dl < MAX_PATH) {
        char* p = strrchr(path, '\\');
        if (p) {
            *(p + 1) = 0;
            _snprintf(p + 1, sizeof(path) - (size_t)(p + 1 - path), "mbk_diag.log");
            path[sizeof(path) - 1] = 0;
            hf = CreateFileA(path, FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hf == INVALID_HANDLE_VALUE) e1 = GetLastError();
        }
    } else {
        e1 = GetLastError();
    }
    if (hf == INVALID_HANDLE_VALUE) {
        char tmp[MAX_PATH];
        DWORD tl = GetTempPathA(MAX_PATH, tmp);
        if (tl > 0 && tl < MAX_PATH) {
            _snprintf(tmp + tl, sizeof(tmp) - (size_t)tl, "mbk_diag.log");
            tmp[sizeof(tmp) - 1] = 0;
            hf = CreateFileA(tmp, FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        if (hf == INVALID_HANDLE_VALUE) e2 = GetLastError();
    }
    if (hf == INVALID_HANDLE_VALUE) {
        hf = CreateFileA("C:\\Users\\Public\\mbk_diag.log", FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf == INVALID_HANDLE_VALUE) e3 = GetLastError();
    }
    if (hf == INVALID_HANDLE_VALUE) {
        char msg[160];
        int mn = _snprintf(msg, sizeof(msg) - 1,
                           "[logfile fail exe=%lu temp=%lu pub=%lu]", 
                           (unsigned long)e1, (unsigned long)e2, (unsigned long)e3);
        if (mn > 0) {
            if (g_diagLen > 0) g_diag[g_diagLen++] = '\n';
            if (mn > (int)sizeof(g_diag) - 1 - g_diagLen)
                mn = (int)sizeof(g_diag) - 1 - g_diagLen;
            memcpy(g_diag + g_diagLen, msg, (size_t)mn);
            g_diagLen += mn;
            g_diag[g_diagLen] = 0;
        }
        return e3 ? e3 : (e2 ? e2 : e1);
    }
    hn = _snprintf(head, sizeof(head) - 1, "[%lu t=%lu] ",
                   (unsigned long)GetCurrentProcessId(),
                   (unsigned long)GetCurrentThreadId());
    if (hn > 0) WriteFile(hf, head, (DWORD)hn, &wrote, NULL);
    if (n > 0)  WriteFile(hf, line, (DWORD)n, &wrote, NULL);
    WriteFile(hf, "\r\n", 2, &wrote, NULL);
    CloseHandle(hf);
    return 0;
}

static void diagf(const char* fmt, ...)
{
    char line[4096];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = _vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)sizeof(line) - 1;
    if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
    line[n] = 0;

    diag_file(line, n);

    if (g_diagLen > 0 && g_diagLen < (int)sizeof(g_diag) - 3) {
        g_diag[g_diagLen++] = '\n';
    }
    {   int room = (int)sizeof(g_diag) - 1 - g_diagLen;
        int m = n;
        if (room < 0) room = 0;
        if (m > room) m = room;
        if (m > 0) memcpy(g_diag + g_diagLen, line, (size_t)m);
        g_diagLen += m;
    }
    if (g_diagLen > (int)sizeof(g_diag) - 1) g_diagLen = (int)sizeof(g_diag) - 1;
    g_diag[g_diagLen] = 0;
    /* 缓冲区将满时丢掉前 3/4，保证最新的诊断（下载、事件）一定留在尾部 */
    if (g_diagLen > (int)sizeof(g_diag) - 1024) {
        int cut = (int)sizeof(g_diag) * 3 / 4;
        memmove(g_diag, g_diag + cut, (size_t)(g_diagLen - cut));
        g_diagLen -= cut;
        g_diag[g_diagLen] = 0;
    }
    /* forced-diag: 临时强制写文件日志 */
    if (IsDebuggerPresent()) {
        OutputDebugStringA("[MbKit/edgewebview] ");
        OutputDebugStringA(g_diag + (g_diagLen - n > 0 ? g_diagLen - n : 0));
        OutputDebugStringA("\n");
    }
    wchar_t logPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, logPath)) {
        wchar_t leaf[64];
        _snwprintf(leaf, 64, L"mbkit-webview-%lu.log", (unsigned long)GetCurrentProcessId());
        if (wcslen(logPath) + wcslen(leaf) < MAX_PATH) {
            wcscat(logPath, leaf);
            FILE* f = _wfopen(logPath, L"ab");
            if (f) {
                va_start(ap, fmt);
                fprintf(f, "[%lu tid=%lu] ", (unsigned long)GetTickCount(), (unsigned long)GetCurrentThreadId());
                vfprintf(f, fmt, ap);
                va_end(ap);
                fputc('\n', f); fclose(f);
            }
        }
    }
}

static std::wstring utf8w(const char* s)
{
    std::wstring r;
    int n;
    if (!s || !*s) return r;
    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return r;
    r.resize((size_t)n - 1);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &r[0], n);
    return r;
}

/* 易语言 文本�?= GBK/ANSI（wkeLoadURL / wkeLoadHTML 这类「文本型」参数） */
static std::wstring ansiw(const char* s)
{
    std::wstring r;
    int n;
    if (!s || !*s) return r;
    n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (n <= 0) return r;
    r.resize((size_t)n - 1);
    MultiByteToWideChar(CP_ACP, 0, s, -1, &r[0], n);
    return r;
}

static std::string w2utf8(const wchar_t* s, int len)
{
    std::string r;
    int n;
    if (!s) return r;
    if (len < 0) len = (int)wcslen(s);
    if (len <= 0) return r;
    n = WideCharToMultiByte(CP_UTF8, 0, s, len, NULL, 0, NULL, NULL);
    if (n <= 0) return r;
    r.resize((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, s, len, &r[0], n, NULL, NULL);
    return r;
}

/* WebView2 的 get_XXX 返回的都是 **LPWSTR（CoTaskMemAlloc 分配）**，不是 BSTR：
 * 用 SysStringLen 会读到指针前 4 字节的「垃圾长度」（本轮实测 117459558 → 拷贝越界崩溃），
 * 用 SysFreeString 释放则会串了堆。统一：wcslen 取长度 + CoTaskMemFree 释放。*/
static std::wstring bstrw(LPCWSTR b)
{
    std::wstring r;
    if (!b) return r;
    r.assign(b, wcslen(b));
    return r;
}

/* ANSI(CP_ACP) 出参：易语言 文本型 是 char*，且指针必须在整个 fire() 期间有效。
 * 用法：std::string a = w2ansi(s); 然后 fire(..., (void*)a.c_str()); —— 局部变量
 * 活到 fire 返回之后，所以安全。 */
static std::string w2ansi(const std::wstring& s)
{
    std::string r;
    int n;
    if (s.empty()) return r;
    n = WideCharToMultiByte(CP_ACP, 0, s.c_str(), (int)s.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return r;
    r.resize((size_t)n);
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), (int)s.size(), &r[0], n, NULL, NULL);
    return r;
}

/* 系统「下载」文件夹；取不到就退到 %TEMP% */
static __attribute__((unused)) std::wstring dl_default_dir()
{
    PWSTR p = 0;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &p)) && p) {
        r = bstrw(p);
        CoTaskMemFree(p);
    }
    if (r.empty()) {
        wchar_t buf[MAX_PATH + 2] = {0};
        DWORD n = GetTempPathW(MAX_PATH, buf);
        if (n) r.assign(buf, n);
    }
    return r;
}

/* 取路径/URL 的最后一段当文件名，把 Windows 非法字符换成 '_' */
static std::wstring dl_file_name(const std::wstring& src)
{
    std::wstring s = src;
    size_t q = s.find_first_of(L"?#");
    if (q != std::wstring::npos) s.erase(q);
    size_t b = s.find_last_of(L"/\\");
    if (b != std::wstring::npos) s = s.substr(b + 1);
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c < 32 || c == L'<' || c == L'>' || c == L':' || c == L'"' ||
            c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*')
            s[i] = L'_';
    }
    if (s.empty()) s = L"download.bin";
    return s;
}

/* 下载目录必须真的存在，否则 put_ResultFilePath 会失败、文件悄悄落到
 * 系统「下载」目录（2026-10-19 只读验收 F1）。返回 1 = 可用。 */
static int ensure_dir_w(const std::wstring& dir)
{
    int rc;
    DWORD a;
    if (dir.empty()) return 1;
    a = GetFileAttributesW(dir.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return 1;
    rc = SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    if (rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS ||
        rc == ERROR_FILE_EXISTS) return 1;
    diagf("创建下载目录失败 rc=%d: %s", rc, w2utf8(dir.c_str(), -1).c_str());
    return 0;
}

/* 同步等一个事件，期间**一�?*泵本线程消息（WebView2 的完成回调靠消息队列）�? * �?mbk_wait_pump 更强：后者默认泵是关的，会直接等到超时�?*/
static int wv_wait(HANDLE evt, DWORD ms)
{
    DWORD t0 = GetTickCount();
    std::vector<MSG> deferred;
    struct Restore {
        std::vector<MSG>& messages;
        ~Restore() {
            for (const MSG& m : messages) {
                if (m.hwnd) PostMessageW(m.hwnd, m.message, m.wParam, m.lParam);
                else PostThreadMessageW(GetCurrentThreadId(), m.message, m.wParam, m.lParam);
            }
        }
    } restore{deferred};
    if (!evt) return 0;
    for (;;) {
        DWORD r, left;
        MSG  m;
        if (WaitForSingleObject(evt, 0) == WAIT_OBJECT_0) {
            return 1;
        }
        if ((int)(GetTickCount() - t0) >= (int)ms) return 0;
        left = ms - (DWORD)(GetTickCount() - t0);
        r = MsgWaitForMultipleObjects(1, &evt, FALSE, left > 20 ? 20 : left,
                                      QS_POSTMESSAGE | QS_SENDMESSAGE);
        if (r == WAIT_FAILED) return 0;
        /* WM_PAINT is generated again until validated. Removing it without
           dispatching it made the old inner loop infinite, even after timeout.
           Only consume posted work and bound every batch. */
        for (unsigned batch = 0; batch < 64 &&
             PeekMessageW(&m, NULL, 0, 0, PM_REMOVE | PM_QS_POSTMESSAGE); ++batch) {
            if (m.message == WM_QUIT) { deferred.push_back(m); return 0; }
            wchar_t cls[128] = {};
            if (m.hwnd) GetClassNameW(m.hwnd, cls, 128);
            bool browser = wcsncmp(cls, L"Chrome_", 7) == 0 ||
                wcscmp(cls, L"OleMainThreadWndClass") == 0 ||
                wcsstr(cls, L"WebView") != NULL;
            if (m.hwnd == NULL || (browser && m.message != WM_APP + 75)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            } else {
                deferred.push_back(m);
            }
            if (WaitForSingleObject(evt, 0) == WAIT_OBJECT_0) return 1;
            if (GetTickCount() - t0 >= ms) return 0;
        }
        if (r == WAIT_TIMEOUT) {
            /* 只是 20ms 切片到了，不是总时长到了 —— 回到 for 顶部重新判总时长。
             * （原实现这里直接 return 0，等于最多只等一个切片就报超时。）*/
            continue;
        }
    }
}

/* ==========================================================================
 * 1. COM handler 样板（手工实现，不依�?WRL —�?mingw 没有 wrl.h�? * ==========================================================================*/
#define WV_IUNKNOWN(cls, iface)                                              \
public:                                                                      \
    cls() : m_ref(1) {}                                                      \
    ULONG STDMETHODCALLTYPE AddRef()                                        \
    { return (ULONG)InterlockedIncrement(&m_ref); }                          \
    ULONG STDMETHODCALLTYPE Release()                                       \
    { ULONG r = (ULONG)InterlockedDecrement(&m_ref); if (!r) delete this;    \
      return r; }                                                            \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv)        \
    {                                                                        \
        if (!ppv) return E_POINTER;                                          \
        if (IsEqualIID(riid, IID_IUnknown) ||                                \
            IsEqualIID(riid, __uuidof(iface))) {                             \
            *ppv = static_cast<iface*>(this); AddRef(); return S_OK;         \
        }                                                                    \
        *ppv = 0; return E_NOINTERFACE;                                      \
    }                                                                        \
private:                                                                     \
    volatile LONG m_ref = 1;                                                 \
public:

/* ==========================================================================
 * 2. wkeString / jsValue 的本地实�? * ==========================================================================*/
#define WV_STR_MAGIC 0x53545231u   /* 'STR1' */
#define WV_JSV_MAGIC 0x4A535631u   /* 'JSV1' */

struct WStr {
    unsigned magic;
    std::wstring ws;
    mutable std::string u8;        /* 缓存：wkeGetString �?*/
    mutable int         u8Valid;
    WStr() : magic(WV_STR_MAGIC), u8Valid(0) {}
};

static WStr* str_new(const wchar_t* s, int cch)
{
    WStr* p = new WStr();
    if (s && cch > 0) p->ws.assign(s, (size_t)cch);
    else if (s)       p->ws.assign(s);
    return p;
}
static WStr* str_of(unsigned h)
{
    WStr* p = (WStr*)(UINT_PTR)h;
    return (p && p->magic == WV_STR_MAGIC) ? p : 0;
}

struct Jsv {
    unsigned magic;
    int      kind;                 /* 0=undefined 1=null 2=bool 3=int 4=num 5=string */
    int      b;
    double   d;
    std::string u8;
    std::wstring ws;
    Jsv() : magic(WV_JSV_MAGIC), kind(0), b(0), d(0) {}
};

/* jsValue 句柄的有限池：模块可能反�?运行JS，无�?new 会漏�? * 超过上限就丢最老的（丢了也不至于崩 —�?读回去最多是空串）�?*/
static std::vector<Jsv*>* g_jsv = 0;
static Jsv* jsv_new(int kind)
{
    Jsv* p = new Jsv();
    p->kind = kind;
    if (!g_jsv) g_jsv = new std::vector<Jsv*>();
    if (g_jsv->size() >= 512) {
        Jsv* old = (*g_jsv)[0];
        g_jsv->erase(g_jsv->begin());
        old->magic = 0;
        delete old;
    }
    g_jsv->push_back(p);
    return p;
}
static Jsv* jsv_of(unsigned h)
{
    Jsv* p = (Jsv*)(UINT_PTR)h;
    return (p && p->magic == WV_JSV_MAGIC) ? p : 0;
}

/* ==========================================================================
 * 3. 视图对象
 * ==========================================================================*/
struct WV;

typedef int (__cdecl *mb_cb10)(void*, void*, void*, void*, void*, void*,
                               void*, void*, void*, void*);

struct CbRef { void* fn; void* param; CbRef() : fn(0), param(0) {} };

#define WV_VIEWS_MAGIC 0x57565731u /* 'WVW1' */

struct WV {
    unsigned magic;
    ICoreWebView2Controller* ctl;
    ICoreWebView2*           core;
    HWND   host;
    int    ctlPending;
    int    wired;
    volatile LONG asyncRefs;
    int    retired;
    int    width, height;
    int    transparent, headless, editable;
    double zoom;
    int    alive;
    int    visState;   /* 上一次 put_IsVisible 的结果；-1=未知（可见性轮询去抖用） */
    int    slept;      /* wkeSleep/wkeWake 的显式休眠，轮询不许把它顶掉 */

    /* 导航状态（wkeIsLoading* 一族读它） */
    int    loading, completed, failed;
    int    readyEligible;
    std::wstring title, url;
    std::wstring pendingNav;      /* 控制器还没起来时先记着 */
    int    pendingIsHtml;
    int    firstNavDone;
    std::wstring queuedHtml;
    std::wstring virtualHtml;
    std::wstring dlDir;           /* wkeSetDownloadDir 指定的下载目录；空=用系统「下载」 */

    /* 回调登记（键 = MBK_IDX_*，�?= mbkit_cb.c 传进来的 wrapper + MBCB*�?*/
    std::map<int, CbRef> cbs;
    /* 用户键值（wkeSetUserKeyValue / wkeGetUserKeyValue�?*/
    std::map<std::wstring, void*> kv;

    /* 运行JS 的结果（�?jsToStringW / wkeGetSource 读回�?*/
    std::string jsOut;
    std::wstring jsOutW;

    /* wkeGlobalExec 给的执行态句�?*/
    void* exec;

    WV() : magic(WV_VIEWS_MAGIC), ctl(0), core(0), host(0),
           ctlPending(0), wired(0), asyncRefs(0), retired(0),
           width(0), height(0), transparent(0), headless(0), editable(0),
           zoom(1.0), alive(1), visState(-1), slept(0),
           loading(0), completed(0), failed(0), readyEligible(0),
           pendingIsHtml(0), firstNavDone(0), exec(0) {}
};

static void wv_async_release(WV* w)
{
    if (w && InterlockedDecrement(&w->asyncRefs) == 0 && w->retired) {
        diagf("view freed view=%p (last lease dropped)", (void*)w);
        delete w;
    }
}

/* Event handlers can outlive Close() while COM deliveries are in flight. */
struct WVLease {
    WV* value;
    WVLease() : value(nullptr) {}
    explicit WVLease(WV* w) : value(w) { InterlockedIncrement(&w->asyncRefs); }
    ~WVLease() { wv_async_release(value); }
    operator WV*() const { return value && value->alive ? value : nullptr; }
    WV* operator->() const { return value; }
};

struct ExecState { unsigned magic; WV* wv; std::vector<Jsv> args; };
#define WV_EXEC_MAGIC 0x45585331u

/* Monotonic IDs keep queued work and COM host objects from addressing a reused view. */
static std::map<unsigned, WV*> g_liveViews;
static unsigned g_nextViewId = 0;
static unsigned view_id(WV* w)
{
    for (const auto& entry : g_liveViews) if (entry.second == w) return entry.first;
    return 0;
}
static WV* view_by_id(unsigned id)
{
    auto it = g_liveViews.find(id);
    return it == g_liveViews.end() ? nullptr : it->second;
}

#include "mbk_jsbridge.inl"

static WV* wv_of(unsigned h)
{
    WV* w = (WV*)(UINT_PTR)h;
    for (const auto& entry : g_liveViews)
        if (entry.second == w && w->alive) return w;
    return nullptr;
}

/* ==========================================================================
 * 4. 事件回调派发
 * ==========================================================================*/
static int fire(WV* w, int idx, void* a3 = 0, void* a4 = 0, void* a5 = 0,
                void* a6 = 0, void* a7 = 0)
{
    std::map<int, CbRef>::iterator it;
    CbRef c;
    if (!w || !w->alive) return 0;
    it = w->cbs.find(idx);
    if (it == w->cbs.end() || !it->second.fn) return 0;
    c = it->second;                       /* 拷出来再调：回调里可能改�?*/
    return ((mb_cb10)c.fn)((void*)(UINT_PTR)w, c.param, a3, a4, a5, a6, a7, 0, 0, 0);
}

/* 回调里要交出去的 wkeString：临时建、调完删 */
static void fire_str(WV* w, int idx, const std::wstring& s)
{
    WStr* p = str_new(s.c_str(), (int)s.size());
    fire(w, idx, (void*)(UINT_PTR)p);
    p->magic = 0;
    delete p;
}

/* ==========================================================================
 * 5. WebView2 Runtime 探测
 * ==========================================================================*/
static char  g_rtVer[128];
static LONG  g_rtState = -1;               /* -1=未测 0=没有 1=�?*/

static int dir_has_rt(const wchar_t* base, char* ver, size_t verCap)
{
    std::wstring pat(base);
    WIN32_FIND_DATAW fd;
    HANDLE h;
    std::wstring best;
    if (pat.empty()) return 0;
    if (pat[pat.size() - 1] == L'\\' || pat[pat.size() - 1] == L'/') pat.resize(pat.size() - 1);
    h = FindFirstFileW((pat + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.cFileName[0] == L'.') continue;
            if (best.empty() || wcscmp(fd.cFileName, best.c_str()) > 0) best = fd.cFileName;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (best.empty()) return 0;
    {   /* 版本目录里必须真的有 msedgewebview2.exe，避免空目录误判 */
        DWORD a = GetFileAttributesW((pat + L"\\" + best + L"\\msedgewebview2.exe").c_str());
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    }
    if (ver && verCap) { lstrcpynA(ver, "", (int)verCap);
        { int n = WideCharToMultiByte(CP_ACP, 0, best.c_str(), -1, ver, (int)verCap, 0, 0);
          if (n <= 0) ver[0] = 0; } }
    return 1;
}

/* WebView2 Runtime 的固�?Client GUID */
static const wchar_t* kWv2ClientGuid =
    L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";

static int reg_has_rt(char* ver, size_t verCap)
{
    static const wchar_t* keys[] = {
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients",
    };
    static const REGSAM views[] = { KEY_READ, KEY_READ | KEY_WOW64_64KEY,
                                    KEY_READ | KEY_WOW64_32KEY };
    int i, j;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 3; j++) {
            HKEY h = 0;
            std::wstring path(keys[i]);
            path += L"\\";
            path += kWv2ClientGuid;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, views[j], &h) != ERROR_SUCCESS)
                continue;
            {
                wchar_t buf[64]; DWORD cb = sizeof(buf); DWORD t = 0;
                if (RegQueryValueExW(h, L"pv", 0, &t, (LPBYTE)buf, &cb) == ERROR_SUCCESS &&
                    t == REG_SZ && buf[0] && wcscmp(buf, L"0.0.0.0") != 0) {
                    if (ver && verCap) WideCharToMultiByte(CP_ACP, 0, buf, -1, ver, (int)verCap, 0, 0);
                    RegCloseKey(h);
                    return 1;
                }
            }
            RegCloseKey(h);
        }
    }
    return 0;
}

static int rt_detect(void)
{
    LONG s = g_rtState;
    int  ok;
    if (s >= 0) return (int)s;
    if (InterlockedCompareExchange(&g_rtState, -2, -1) != -1) {   /* 别人正在�?*/
        while ((s = g_rtState) == -1 || s == -2) Sleep(0);
        return (s == 1);
    }
    g_rtVer[0] = 0;
    ok = 0;
    {
        wchar_t pf[MAX_PATH];
        if (!ok && SUCCEEDED(SHGetFolderPathW(NULL, 0x0026 /*CSIDL_PROGRAM_FILESX86*/, NULL, 0, pf))) {
            std::wstring p(pf); p += L"\\Microsoft\\EdgeWebView\\Application";
            ok = dir_has_rt(p.c_str(), g_rtVer, sizeof(g_rtVer));
        }
        if (!ok) {
            char p[MAX_PATH] = {0};
            DWORD n = GetEnvironmentVariableA("ProgramFiles", p, MAX_PATH);
            if (n > 0 && n < MAX_PATH) {
                std::wstring w = ansiw(p); w += L"\\Microsoft\\EdgeWebView\\Application";
                ok = dir_has_rt(w.c_str(), g_rtVer, sizeof(g_rtVer));
            }
        }
        if (!ok) ok = reg_has_rt(g_rtVer, sizeof(g_rtVer));
    }
    InterlockedExchange(&g_rtState, ok ? 1 : 0);
    if (ok) diagf("detected WebView2 Runtime %s", g_rtVer[0] ? g_rtVer : "(version unknown)");
    else    diagf("本机未检测到 WebView2 Runtime，回退 node.dll");
    return ok;
}

/* ==========================================================================
 * 6. WebView2Loader.dll —�?作为资源内嵌，首次使用时释放到磁盘再加载
 *
 * 为什么内嵌：模块只释�?MbKit144.dll 一个文件，加不了第二个�? * �?WebView2 的入�?API（CreateCoreWebView2EnvironmentWithOptions）只�? * 这个 116 KB �?loader 里。资�?id �?webview2.rc（MBKWV_RID_LOADER）�? * ==========================================================================*/
#define MBKWV_RID_LOADER 101

/* �?必须�?WebView2.h 的声明逐字一致（4 参）。曾误写�?3 参，导致 handler
 * 被当�?options、真正的 handler 位拿到栈垃圾 —�?CreateCoreWebView2EnvironmentWithOptions
 * 返回 E_POINTER(0x80004003)，环境永远起不来�?*/
typedef HRESULT (STDAPICALLTYPE *PFN_CreateEnv)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);

static HMODULE   g_loader = NULL;
static PFN_CreateEnv pCreateEnv = NULL;
static HMODULE   g_self = NULL;

static void hide_file(const wchar_t* p)
{
    DWORD a = GetFileAttributesW(p);
    if (a != INVALID_FILE_ATTRIBUTES)
        SetFileAttributesW(p, a | FILE_ATTRIBUTE_HIDDEN);
}

/* 把资源写�?base 目录；失败退�?%TEMP%\MbKit。返回实际写到的全路径�?*/
static int extract_loader(wchar_t* out, size_t cap)
{
    HRSRC  hr; HGLOBAL hg; DWORD sz; const void* data;
    HANDLE f; DWORD wr = 0;
    wchar_t tmp[MAX_PATH];

    hr = FindResourceW(g_self, MAKEINTRESOURCEW(MBKWV_RID_LOADER),
                       (LPCWSTR)(UINT_PTR)RT_RCDATA);
    if (!hr) return 0;
    hg = LoadResource(g_self, hr);
    if (!hg) return 0;
    sz = SizeofResource(g_self, hr);
    data = LockResource(hg);
    if (!data || !sz) return 0;

    /* 首选：MbKit.dll 同目录（不写就走系统加载序也未必找得到） */
    if (out && cap) {
        if (GetModuleFileNameW(g_self, tmp, MAX_PATH)) {
            wchar_t* sl = wcsrchr(tmp, L'\\');
            if (sl) {
                *(sl + 1) = 0;
                wcsncat_s(tmp, MAX_PATH, L"WebView2Loader.dll", _TRUNCATE);
                f = CreateFileW(tmp, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (f != INVALID_HANDLE_VALUE) {
                    WriteFile(f, data, sz, &wr, NULL);
                    CloseHandle(f);
                    if (wr == sz) {
                        hide_file(tmp);
                        lstrcpynW(out, tmp, (int)cap);
                        return 1;
                    }
                }
            }
        }
    }
    /* 退路：用户临时目录 */
    if (GetTempPathW(MAX_PATH, tmp)) {
        wcscat_s(tmp, MAX_PATH, L"MbKit\\");
        CreateDirectoryW(tmp, NULL);
        wcscat_s(tmp, MAX_PATH, L"WebView2Loader.dll");
        f = CreateFileW(tmp, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) return 0;
        WriteFile(f, data, sz, &wr, NULL);
        CloseHandle(f);
        if (wr != sz) return 0;
        hide_file(tmp);
        if (out && cap) lstrcpynW(out, tmp, (int)cap);
        return 1;
    }
    return 0;
}

static int loader_load(void)
{
    wchar_t path[MAX_PATH];
    if (g_loader) return 1;
    if (!g_self) {
        HMODULE hm = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                         | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(UINT_PTR)&loader_load, &hm);
        g_self = hm;
    }
    /* 已经在同目录放好的（第二次运�?/ 手工放置）优先复�?*/
    if (GetModuleFileNameW(g_self, path, MAX_PATH)) {
        wchar_t* sl = wcsrchr(path, L'\\');
        if (sl) {
            *(sl + 1) = 0;
            wcscat_s(path, MAX_PATH, L"WebView2Loader.dll");
            if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
                g_loader = LoadLibraryW(path);
                if (g_loader) {
                    hide_file(path);
                    pCreateEnv = (PFN_CreateEnv)GetProcAddress(
                        g_loader, "CreateCoreWebView2EnvironmentWithOptions");
                    if (pCreateEnv) return 1;
                    FreeLibrary(g_loader); g_loader = NULL; pCreateEnv = NULL;
                }
            }
        }
    }
    if (!extract_loader(path, MAX_PATH)) return 0;
    g_loader = LoadLibraryW(path);
    if (!g_loader) { diagf("LoadLibrary(WebView2Loader) 失败 err=%lu", GetLastError()); return 0; }
    pCreateEnv = (PFN_CreateEnv)GetProcAddress(g_loader, "CreateCoreWebView2EnvironmentWithOptions");
    if (!pCreateEnv) { diagf("WebView2Loader 缺少 CreateCoreWebView2EnvironmentWithOptions"); return 0; }
    return 1;
}

/* ==========================================================================
 * 7. 环境 / 控制�?/ 事件接线
 * ==========================================================================*/
static ICoreWebView2Environment* g_env = NULL;
static volatile LONG g_envRequested = 0;
static volatile LONG g_envFailed = 0;
static int ensure_env(void);
static int ensure_ctl(WV* w);
static void finish_ctl(WV* w);
static void fit_webview(WV* w);
static std::wstring with_file_bridge(const std::wstring& html);

/* Limit DPI awareness to browser API calls. Restoring the caller's context
 * before returning keeps every native host control in its original layout. */
class BrowserDpiScope {
    typedef HANDLE (WINAPI *SetContext)(HANDLE);
    SetContext set;
    HANDLE old;
public:
    BrowserDpiScope() : set((SetContext)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                         "SetThreadDpiAwarenessContext")), old(NULL) {
        if (set) old = set((HANDLE)(INT_PTR)-4);
    }
    ~BrowserDpiScope() { if (set && old) set(old); }
};

/* ★ 2026-10-20 修复（「事件_下载完成」迟到约 60 秒 / 后台事件全都迟到）：
 *   WebView2 controller 一旦 IsVisible=FALSE，Chromium 就把这个页面当成后台页，
 *   渲染进程的定时器被节流到「1 分钟一颗」。DownloadOperation 的 StateChanged
 *   （也就是 wkeOnDownloadFinish）就是搭这颗定时器投递的，所以被整整推后 60 秒。
 *   实测（E 宿主、隐藏启动的窗口）：文件 0.3 秒就写完整，`下载完成` 却在 60.9 秒
 *   才到；把窗口重新可见后 3.1 秒内就到。
 *   而图片框这种「宿主控件」自己收不到「父窗口被 ShowWindow」的通知（Win32 只发给
 *   被 Show 的那个窗口），所以只在 WM_SHOWWINDOW / WM_SIZE 里同步可见性是不够的。
 *   这里补一个 300ms 的轮询兜底：只要窗口树上的可见性变了就补一次 put_IsVisible。 */
static void sync_vis(WV* w)
{
    BOOL want;
    if (!w || !w->ctl) return;
    want = (w->host && IsWindow(w->host) && !w->headless && !w->slept &&
            IsWindowVisible(w->host)) ? TRUE : FALSE;
    if ((int)want == w->visState) return;
    w->visState = (int)want;
    w->ctl->put_IsVisible(want);
    diagf("visibility -> %d (host=%p treeVisible=%d headless=%d slept=%d)", (int)want,
          (void*)w->host, w->host ? (int)IsWindowVisible(w->host) : -1, w->headless, w->slept);
}

static HWND g_hidden = NULL;

/* 下载终态的轮询兜底（定义见下方「下载」一节）。Chromium 不保证投递
 * DownloadOperation::StateChanged，所以内核自己盯 get_State()。 */
static void dl_watch_poll(void);
static void dl_watch_mark_done(ICoreWebView2DownloadOperation* op);

/* 完成事件去重：轮询兜底与 StateChanged 两条路都会看到终态，业务只该收一次。
 * op 调用结束后指针可能被复用，所以集合按 64 条 FIFO 截断，不无限增长。 */
static std::set<void*> g_dlDone;
static int dl_done_claim(ICoreWebView2DownloadOperation* op)
{
    if (!op) return 1;
    if (g_dlDone.count((void*)op)) return 0;
    g_dlDone.insert((void*)op);
    while (g_dlDone.size() > 64) g_dlDone.erase(g_dlDone.begin());
    return 1;
}
static void dl_done_mark(ICoreWebView2DownloadOperation* op)
{
    if (!op) return;
    g_dlDone.insert((void*)op);
    while (g_dlDone.size() > 64) g_dlDone.erase(g_dlDone.begin());
}

struct ReleasePack {
    ICoreWebView2Controller* ctl;
    ICoreWebView2* core;
};

static LRESULT CALLBACK hidden_wnd(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TIMER && w == 1) {
        /* 可见性轮询：宿主可能在任意时刻被 ShowWindow / 挪到屏幕外 / 最小化，
         * 宿主控件本身收不到通知，只能自己看树上的实际状态。 */
        std::vector<WV*> snap;
        for (auto it = g_liveViews.begin(); it != g_liveViews.end(); ++it)
            if (it->second && it->second->alive) snap.push_back(it->second);
        for (size_t i = 0; i < snap.size(); ++i)
            if (snap[i] && snap[i]->alive) sync_vis(snap[i]);
        dl_watch_poll();
        return 0;
    }
    if (m == WM_APP + 71) {
        PostMessageW(h, WM_APP + 75, w, 0);
        return 0;
    }
    if (m == WM_APP + 72) {
        WV* v = view_by_id((unsigned)w);
        if (v && v->core && !v->queuedHtml.empty()) {
            std::wstring html = v->queuedHtml;
            v->queuedHtml.clear();
            HRESULT hr = v->core->NavigateToString(html.c_str());
            diagf("queued NavigateToString hr=0x%08lX chars=%u", (unsigned long)hr, (unsigned)html.size());
        }
        return 0;
    }
    if (m == WM_APP + 73) {
        for (auto it = g_liveViews.begin(); it != g_liveViews.end(); ++it)
            if (it->second && it->second->alive && it->second->host)
                ensure_ctl(it->second);
        return 0;
    }
    if (m == WM_APP + 74) {
        WV* v = view_by_id((unsigned)w);
        if (v && v->alive) fit_webview(v);
        return 0;
    }
    if (m == WM_APP + 75) {
        WV* v = view_by_id((unsigned)w);
        if (v && v->readyEligible) fire(v, MBK_IDX_wkeOnDocumentReady, 0);
        v = view_by_id((unsigned)w);
        if (v && v->readyEligible) fire(v, MBK_IDX_wkeOnDocumentReady2, (void*)1);
        return 0;
    }
    if (m == WM_APP + 76) {
        ReleasePack* pack = (ReleasePack*)(UINT_PTR)w;
        if (pack) {
            if (pack->ctl) {
                pack->ctl->put_IsVisible(FALSE);
                pack->ctl->Close();
                pack->ctl->Release();
            }
            if (pack->core) pack->core->Release();
            delete pack;
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* 承载窗口的**属主线程**。窗口只能在这一个线程上建；其它线程只允许取句柄。
 * ★ 2026-10-15：没有这道闸时，任何一个先跑到 hidden_host() 的线程都会把窗口建在
 *   自己身上 —— 而窗口一旦建在 WebView2 环境工作线程上，投给它的 WM_APP+7x 就永远
 *   没人抽（这正是灰屏根因）。所以「谁建的」必须是确定性的。 */
static DWORD g_hiddenTid = 0;

/* 取承载窗口句柄。**只有属主线程能创建**：
 *   - 已有窗口        → 返回它（任何线程）；
 *   - 属主线程        → 建（首次）或重建（窗口丢了）；
 *   - 其它线程        → 返回 NULL，绝不在这里 CreateWindowEx。 */
static HWND hidden_host(void)
{
    DWORD me = GetCurrentThreadId();

    if (g_hidden && IsWindow(g_hidden)) return g_hidden;

    /* 第一次调用：把本线程登记为属主 */
    if (!g_hiddenTid) {
        DWORD zero = 0;
        InterlockedCompareExchange((volatile LONG*)&g_hiddenTid, (LONG)me, (LONG)zero);
    }
    if (me != g_hiddenTid) return NULL;      /* 非属主：只取不建 */

    if (!g_hidden) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc   = hidden_wnd;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)(UINT_PTR)IDC_ARROW);
        wc.lpszClassName = L"MbKitWebView2OffscreenHost";
        RegisterClassW(&wc);                 /* 重复注册无害 */
    }
    {
        HWND h = CreateWindowExW(0, L"MbKitWebView2OffscreenHost", L"",
                    WS_POPUP, 0, 0, 64, 64, NULL, NULL, GetModuleHandleW(NULL), NULL);
        if (h) g_hidden = h;
        else {
            diagf("CreateWindowExW(MbKitWebView2OffscreenHost) 失败 err=%lu",
                  (unsigned long)GetLastError());
            g_hiddenTid = 0;                 /* 允许下次再试 */
            return NULL;
        }
    }
    diagf("hidden host created on tid=%lu", (unsigned long)me);
    /* 可见性轮询（见 sync_vis 的注释）。300ms 足够快，开销只是几次 IsWindowVisible。 */
    SetTimer(g_hidden, 1, 300, NULL);
    return g_hidden;
}

/* ★ 2026-10-15：所有"投递给承载窗口"的地方统一走这里。
 *   以前各处直接 PostMessageW(hidden_host(), …)：一旦承载窗口不存在（还没建、
 *   或建失败了），PostMessageW(NULL, …) 会把消息投给**当前线程**的队列 ——
 *   而当前线程往往正是内核属主线程，等于让宿主 UI 线程去处理一条它并不认识的消息。
 *   现在统一判空并记一行诊断。 */
static int post_to_hidden(UINT msg, WPARAM wp, LPARAM lp)
{
    HWND h = hidden_host();

    if (!h) {
        diagf("post_to_hidden msg=0x%X 丢弃：承载窗口不可用（tid=%lu）",
              (unsigned)msg, (unsigned long)GetCurrentThreadId());
        return 0;
    }
    if (!PostMessageW(h, msg, wp, lp)) {
        diagf("PostMessageW msg=0x%X 失败 err=%lu", (unsigned)msg, (unsigned long)GetLastError());
        return 0;
    }
    return 1;
}

/* ---- handler 样板 ---- */
static int ensure_env(void);
static int ensure_ctl(WV* w);
static void finish_ctl(WV* w);

class EnvDone : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    WV_IUNKNOWN(EnvDone, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)
public:
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env);
};

class CtlDone : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    WV_IUNKNOWN(CtlDone, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)
public:
    CtlDone(WV* w) : wv(w) { if (wv) InterlockedIncrement(&wv->asyncRefs); }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctl);
private:
    WV*    wv;
};

class TitleEv : public ICoreWebView2DocumentTitleChangedEventHandler {
    WV_IUNKNOWN(TitleEv, ICoreWebView2DocumentTitleChangedEventHandler)
public:
    TitleEv(WV* w) : v(w) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, IUnknown* args);
private:
    WVLease v;
};

class SourceEv : public ICoreWebView2SourceChangedEventHandler {
    WV_IUNKNOWN(SourceEv, ICoreWebView2SourceChangedEventHandler)
public:
    SourceEv(WV* w) : v(w) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2SourceChangedEventArgs* args);
private:
    WVLease v;
};

class NavStartEv : public ICoreWebView2NavigationStartingEventHandler {
    WV_IUNKNOWN(NavStartEv, ICoreWebView2NavigationStartingEventHandler)
public:
    NavStartEv(WV* w) : v(w) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2NavigationStartingEventArgs* args);
private:
    WVLease v;
};

class NavDoneEv : public ICoreWebView2NavigationCompletedEventHandler {
    WV_IUNKNOWN(NavDoneEv, ICoreWebView2NavigationCompletedEventHandler)
public:
    NavDoneEv(WV* w) : v(w) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2NavigationCompletedEventArgs* args);
private:
    WVLease v;
};

class ContentEv : public ICoreWebView2ContentLoadingEventHandler {
    WV_IUNKNOWN(ContentEv, ICoreWebView2ContentLoadingEventHandler)
public:
    ContentEv(WV* w) : v(w) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2ContentLoadingEventArgs* args);
private:
    WVLease v;
};

class DialogEv : public ICoreWebView2ScriptDialogOpeningEventHandler {
    WV_IUNKNOWN(DialogEv, ICoreWebView2ScriptDialogOpeningEventHandler)
public:
    DialogEv(WV* w) : v(w) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2ScriptDialogOpeningEventArgs* args);
private:
    WVLease v;
};

class NewWndEv : public ICoreWebView2NewWindowRequestedEventHandler {
    WV_IUNKNOWN(NewWndEv, ICoreWebView2NewWindowRequestedEventHandler)
public:
    NewWndEv(WV* w) : v(w) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2NewWindowRequestedEventArgs* args);
private:
    WVLease v;
};

/* ---- 下载 ---- */
class DlStateEv : public ICoreWebView2StateChangedEventHandler {
    WV_IUNKNOWN(DlStateEv, ICoreWebView2StateChangedEventHandler)
public:
    DlStateEv(WV* w, ICoreWebView2DownloadOperation* o);
    virtual ~DlStateEv();
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2DownloadOperation* sender, IUnknown* args);
    void set_token(EventRegistrationToken t) { tok = t; hasTok = 1; }
private:
    void detach();                 /* 终态：remove_StateChanged + 放掉 op */
    WVLease v;
    ICoreWebView2DownloadOperation* op;
    EventRegistrationToken tok = { 0 };
    int hasTok = 0;
};

class DlStartEv : public ICoreWebView2DownloadStartingEventHandler {
    WV_IUNKNOWN(DlStartEv, ICoreWebView2DownloadStartingEventHandler)
public:
    DlStartEv(WV* w) : v(w) {}
    virtual ~DlStartEv() { diagf("DlStartEv destroyed v=%p", (void*)v); }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2DownloadStartingEventArgs* args);
private:
    WVLease v;
};

/* 诊断专用：只登记、只记日志、绝不碰 args。用来区分
 * 「WebView2 根本没抛 DownloadStarting」和「DlStartEv 被提前释放」。 */
class DlProbeEv : public ICoreWebView2DownloadStartingEventHandler {
    WV_IUNKNOWN(DlProbeEv, ICoreWebView2DownloadStartingEventHandler)
public:
    DlProbeEv(WV* w) : v(w) {}
    virtual ~DlProbeEv() { diagf("DLPROBE destroyed v=%p", (void*)v); }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2DownloadStartingEventArgs* args)
    {
        (void)sender; (void)args;
        diagf("DLPROBE fire v=%p args=%p tid=%lu", (void*)v, (void*)args,
              (unsigned long)GetCurrentThreadId());
        return S_OK;
    }
private:
    WVLease v;
};

class ExecDone : public ICoreWebView2ExecuteScriptCompletedHandler {
    WV_IUNKNOWN(ExecDone, ICoreWebView2ExecuteScriptCompletedHandler)
public:
    ~ExecDone() { if (evt) CloseHandle(evt); }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, LPCWSTR resultAsJson);
public:
    HANDLE      evt = CreateEventW(NULL, TRUE, FALSE, NULL);
    std::string res;
    HRESULT status = E_PENDING;
};

/* ---- handler 实现 ---- */
HRESULT EnvDone::Invoke(HRESULT hr, ICoreWebView2Environment* env)
{
    if (SUCCEEDED(hr) && env) {
        env->AddRef();
        g_env = env;
        InterlockedExchange(&g_envFailed, 0);
        diagf("EnvDone async tid=%lu env=%p", (unsigned long)GetCurrentThreadId(), env);
        /* ★ 2026-10-15 修复（白屏/灰屏根因）：本回调可能运行在 WebView2 的
         *   **环境工作线程**上，而不是内核属主线程。原来在这里现取 hidden_host()，
         *   于是 `MbKitWebView2OffscreenHost` 这个窗口类被注册、窗口被创建在
         *   工作线程上；随后 PostMessageW 投给同线程一个**没人抽**的队列，
         *   WM_APP+73（去创建 controller）永远不被处理 —— 日志停在 EnvDone，
         *   页面永远不渲染。
         *   hidden_host() 现在在 ensure_ctl() 里就已经由属主线程建好，
         *   这里只取句柄（并留一道属主线程兜底），绝不跨线程创建窗口。 */
        post_to_hidden(WM_APP + 73, 0, 0);
    } else {
        InterlockedExchange(&g_envFailed, 1);
        diagf("CreateCoreWebView2Environment 失败 hr=0x%08lX", (unsigned long)hr);
    }
    return S_OK;
}

HRESULT CtlDone::Invoke(HRESULT hr, ICoreWebView2Controller* ctl)
{
    diagf("CtlDone::Invoke hr=0x%08lX ctl=%p tid=%lu", (unsigned long)hr, (void*)ctl, (unsigned long)GetCurrentThreadId());
    if (SUCCEEDED(hr) && ctl && wv && wv->magic == WV_VIEWS_MAGIC &&
        wv->alive && wv->host && IsWindow(wv->host)) {
        wv->ctlPending = 0;
        wv->ctl = ctl;                      /* 拿到一份引�?*/
        ctl->AddRef();
        if (FAILED(ctl->get_CoreWebView2(&wv->core)) || !wv->core) {
            ctl->Release();
            wv->ctl = 0;
        }
        if (wv->ctl && wv->core) finish_ctl(wv);
    } else {
        if (ctl) ctl->Close();
        if (wv && wv->magic == WV_VIEWS_MAGIC) wv->ctlPending = 0;
        diagf("CreateCoreWebView2Controller 失败 hr=0x%08lX", (unsigned long)hr);
    }
    wv_async_release(wv);
    return S_OK;
}

HRESULT TitleEv::Invoke(ICoreWebView2* sender, IUnknown* args)
{
    if (!v) return S_OK;
    BSTR b = 0;
    (void)args;
    if (sender) sender->get_DocumentTitle(&b);
    v->title = bstrw(b);
    if (b) CoTaskMemFree(b);
    fire_str(v, MBK_IDX_wkeOnTitleChanged, v->title);
    return S_OK;
}

HRESULT SourceEv::Invoke(ICoreWebView2* sender,
                          ICoreWebView2SourceChangedEventArgs* args)
{
    if (!v) return S_OK;
    BSTR b = 0;
    (void)args;
    if (sender) sender->get_Source(&b);
    v->url = bstrw(b);
    if (b) CoTaskMemFree(b);
    fire_str(v, MBK_IDX_wkeOnURLChanged, v->url);
    {   /* URLChanged2: (wv, param, 框架句柄, url[wkeString]) —�?主框架句柄我们给 1 */
        WStr* p = str_new(v->url.c_str(), (int)v->url.size());
        fire(v, MBK_IDX_wkeOnURLChanged2, (void*)1, (void*)(UINT_PTR)p);
        p->magic = 0; delete p;
    }
    return S_OK;
}

HRESULT NavStartEv::Invoke(ICoreWebView2* sender,
                           ICoreWebView2NavigationStartingEventArgs* args)
{
    if (!v) return S_OK;
    BSTR b = 0;
    if (args) {
        args->get_Uri(&b);
        if (b) v->url = bstrw(b);
    }

    /* 事件_开始载入URL（= miniblink 的 wkeOnLoadUrlBegin）：返回假（0）= 取消这次导航。
     * WebView2 只有 put_Cancel 一条通道，没有 node 侧「已接管/自己喂数据」的概念，
     * 所以这里只认「返回 0 = 取消」，其余一律放行；第 4 个参数 job 传 0（WebView2 无对应物，
     * 业务侧不能对它调 wkeNetSetData 之类）。取消时不发 事件_即将导航：导航根本没成立。 */
    if (v->cbs.count(MBK_IDX_wkeOnLoadUrlBegin)) {
        std::string aurl = w2ansi(v->url);
        if (!fire(v, MBK_IDX_wkeOnLoadUrlBegin, (void*)aurl.c_str(), (void*)0)) {
            diagf("LoadUrlBegin cancelled: %s", w2utf8(v->url.c_str(), -1).c_str());
            if (args) args->put_Cancel(TRUE);
            if (b) CoTaskMemFree(b);
            (void)sender;
            return S_OK;
        }
    }

    v->loading = 1; v->completed = 0; v->failed = 0;
    /* 事件_即将导航(wv, param, 导航类型, url[wkeString])。返回值刻意忽略：
     * WebView2 只有 put_Cancel 一条通道，而模块侧这个回调的真假语义
     * （miniblink 是「是否允许」还是「是否已处理」）没有权威说法，
     * 宁可一律放行——拦掉用户导航比多回调一次糟得多。*/
    fire_str(v, MBK_IDX_wkeOnNavigation, v->url);
    if (b) CoTaskMemFree(b);
    (void)sender;
    return S_OK;
}

HRESULT NavDoneEv::Invoke(ICoreWebView2* sender,
                          ICoreWebView2NavigationCompletedEventArgs* args)
{
    if (!v) return S_OK;
    BOOL ok = FALSE;
    BSTR b = 0;
    COREWEBVIEW2_WEB_ERROR_STATUS err = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
    if (args) args->get_IsSuccess(&ok);
    if (args) args->get_WebErrorStatus(&err);
    v->loading   = 0;
    v->completed = ok ? 1 : 0;
    v->failed    = ok ? 0 : 1;
    /* Run native ready callbacks after returning from WebView2's COM callback.
       Existing clients synchronously execute script inside these callbacks. */
    if (sender) sender->get_Source(&b);
    if (b) { v->url = bstrw(b); CoTaskMemFree(b); }
    diagf("NavDone ok=%d err=%d url=%s", (int)ok, (int)err, w2utf8(v->url.c_str(), -1).c_str());
    if (ok && (v->url != L"about:blank" || v->readyEligible)) {
        v->readyEligible = 1;
        post_to_hidden(WM_APP + 71, view_id(v), 0);
        post_to_hidden(WM_APP + 74, view_id(v), 0);
    }
    v->firstNavDone = 1;
    if (!v->queuedHtml.empty())
        post_to_hidden(WM_APP + 72, view_id(v), 0);

    /* 事件_载入完成(wv, param, url[wkeString], 结果, 失败原因[wkeString])
     * 结果：0=成功 1=失败（WebView2 后端只有这两种，不会发 2） */
    {
        WStr* u = str_new(v->url.c_str(), (int)v->url.size());
        WStr* r = str_new(ok ? L"" : L"navigation failed", -1);
        fire(v, MBK_IDX_wkeOnLoadingFinish, (void*)(UINT_PTR)u,
             (void*)(UINT_PTR)(ok ? 0 : 1), (void*)(UINT_PTR)r);
        u->magic = 0; delete u;
        r->magic = 0; delete r;
    }
    return S_OK;
}

/* ---- 下载 ---- */
DlStateEv::DlStateEv(WV* w, ICoreWebView2DownloadOperation* o) : v(w), op(o)
{
    if (op) op->AddRef();
}

void DlStateEv::detach()
{
    diagf("dl state: detach op=%p hasTok=%d", (void*)op, hasTok);
    if (op && hasTok) {
        op->remove_StateChanged(tok);
        hasTok = 0;
    }
    if (op) { op->Release(); op = 0; }
}

DlStateEv::~DlStateEv()
{
    detach();
}

HRESULT DlStateEv::Invoke(ICoreWebView2DownloadOperation* sender, IUnknown* args)
{
    (void)args;
    COREWEBVIEW2_DOWNLOAD_STATE st = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
    std::wstring path;
    std::string apath;
    LPWSTR p = 0;
    int r;

    diagf("dl state: enter sender=%p this=%p", (void*)sender, (void*)this);

    /* remove_StateChanged 会放掉「源持有的那份引用」，可能就地销毁 this，
       所以先把本次调用自己的那份拿住。 */
    AddRef();
    if (!v || !sender) { Release(); return S_OK; }
    if (FAILED(sender->get_State(&st))) { Release(); return S_OK; }
    /* 一次性看清 Chromium 到底有没有投递终态（chunked 用例里若这行只有 st=0，
       说明 StateChanged 根本不发 COMPLETED，完成判定只能靠轮询兜底）。 */
    diagf("dl state: enter st=%d", (int)st);

    if (st != COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS) {
        /* 轮询那条路可能已经报过了，去重，别让用户收到两次 事件_下载完成 */
        dl_watch_mark_done(sender);
        if (SUCCEEDED(sender->get_ResultFilePath(&p)) && p) {
            path = bstrw(p);
            CoTaskMemFree(p);
        }
        if (v->cbs.count(MBK_IDX_wkeOnDownloadFinish) && dl_done_claim(sender)) {
            r = (st == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED) ? 0 : 1;
            apath = w2ansi(path);
            diagf("download finish: state=%d path=%s", r,
                  w2utf8(path.c_str(), -1).c_str());
            fire(v, MBK_IDX_wkeOnDownloadFinish, (void*)apath.c_str(),
                 (void*)(UINT_PTR)r);
        }
        /* 终态一次性：摘掉 handler、断开 op <-> this 的环，避免每次下载
           都滞留一个 handler 和一份 WVLease（只读验收 C5 / F2）。 */
        detach();
    }
    Release();
    return S_OK;
}

/* ---- 下载终态的轮询兜底 ----
 * ★ 2026-10-20：实测 Chromium 并不保证投递 DownloadOperation::StateChanged。
 *   E 宿主里文件 0.3 秒就写完整（History 里 state=COMPLETED、524288/524288），
 *   `事件_下载完成` 却在 60.9 秒后才到；隐藏宿主里 28 秒都不来。
 *   用户看到的就是「下载完了但事件_下载完成 不触发」。所以内核自己盯着操作对象：
 *   hidden_wnd 的 300ms 定时器里轮询 get_State()，一到终态就自己发事件，
 *   不再等 Chromium 那一路投递（两路都到也只发一次：done 标记去重）。 */
struct DlWatch {
    ICoreWebView2DownloadOperation* op;
    WVLease lease;
    std::wstring path;
    int done, ticks, miss;
    long long lastRec;          /* 上一次已上报的接收字节数（进度事件去重） */
    long long lastTot;          /* 上一次已上报的总字节数 */
    int lastPct, hasLast;       /* 上一次已上报的百分比 / 是否报过 */
    int stableTicks;            /* rec 连续不变的 tick 数（总长未知时判「字节流静止」） */
    long long lastRecSeen;      /* 上一 tick 看到的接收字节数（≠ lastRec：那是已上报值） */
    explicit DlWatch(WV* w) : op(0), lease(w), done(0), ticks(0), miss(0),
                              lastRec(0), lastTot(0), lastPct(-2), hasLast(0),
                              stableTicks(0), lastRecSeen(0) {}
};
static std::vector<DlWatch*> g_dlWatch;

static void dl_watch_mark_done(ICoreWebView2DownloadOperation* op)
{
    for (size_t i = 0; i < g_dlWatch.size(); ++i)
        if (g_dlWatch[i] && g_dlWatch[i]->op == op) g_dlWatch[i]->done = 1;
}

/* 进度事件（事件_进度变化）：易语言声明是 3 个整数型，所以字节数一律夹到
 * 0..INT32_MAX（大文件只报上界，绝不出现负数）；总长为 0 表示「未知」，
 * 此时百分比报 -1。lastRec 记录上一次已报出的 rec，轮询侧据此去重。 */
static unsigned dl_bytes32(long long n)
{
    if (n < 0) return 0;
    if (n > 2147483647LL) return 2147483647u;
    return (unsigned)n;
}

static void dl_prog_fire(DlWatch* d, long long rec, long long tot)
{
    WV* v;
    unsigned recC, totC;
    int pct;
    if (!d) return;
    v = d->lease;
    if (!v) return;
    recC = dl_bytes32(rec);
    totC = dl_bytes32(tot);
    pct  = (totC > 0) ? (int)((double)recC * 100.0 / (double)totC) : -1;
    if (pct > 100) pct = 100;
    /* (接收, 总长, 百分比) 三元组完全相同的帧不重复发：轮询末帧与完成兜底帧
     * 都是 (tot,tot,100)，去重后 100% 只报一次。首次 hasLast=0 必发。 */
    if (d->hasLast && (long long)recC == d->lastRec &&
        (long long)totC == d->lastTot && pct == d->lastPct) return;
    d->hasLast = 1;
    d->lastRec = (long long)recC;
    d->lastTot = (long long)totC;
    d->lastPct = pct;
    fire(v, MBK_IDX_wkeOnDownloadProgress, (void*)(UINT_PTR)recC,
         (void*)(UINT_PTR)totC, (void*)(UINT_PTR)(INT_PTR)pct);
}

static void dl_watch_fire(DlWatch* d, COREWEBVIEW2_DOWNLOAD_STATE st)
{
    WV* v;
    std::wstring path;
    std::string apath;
    LPWSTR p = 0;
    int r;
    if (!d) return;
    d->done = 1;
    /* 另一条路已经发过完成事件就不再发（op 不存在时无从判断，按未发处理）。 */
    if (d->op && !dl_done_claim(d->op)) return;
    v = d->lease;
    if (!v) return;
    path = d->path;
    if (d->op && SUCCEEDED(d->op->get_ResultFilePath(&p)) && p) {
        path = bstrw(p);
        CoTaskMemFree(p);
    }
    /* 终态前补一帧进度：先于 事件_下载完成 到达，业务侧顺序恒定。
     * COMPLETED 报 (tot,tot,100)；总长未知时退化为 (rec,0,-1)。 */
    if (st == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED) {
        INT64 rec = 0, tot = 0;
        if (d->op) {
            d->op->get_BytesReceived(&rec);
            d->op->get_TotalBytesToReceive(&tot);
        }
        dl_prog_fire(d, (tot > 0) ? tot : rec, tot);
    }
    if (!v->cbs.count(MBK_IDX_wkeOnDownloadFinish)) return;
    r = (st == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED) ? 0 : 1;
    apath = w2ansi(path);
    diagf("download finish(poll): state=%d path=%s", r, w2utf8(path.c_str(), -1).c_str());
    fire(v, MBK_IDX_wkeOnDownloadFinish, (void*)apath.c_str(), (void*)(UINT_PTR)r);
}

static void dl_watch_add(WV* w, ICoreWebView2DownloadOperation* op, const std::wstring& path)
{
    DlWatch* d;
    if (!w || !op) return;
    d = new DlWatch(w);
    d->op = op;
    d->op->AddRef();
    d->path = path;
    g_dlWatch.push_back(d);
    diagf("dl watch: add op=%p path=%s", (void*)op, w2utf8(path.c_str(), -1).c_str());
    /* 立刻补发一帧「0 字节」进度：业务先知道「开始了」，随后按轮询增量上报。 */
    { INT64 tot0 = 0;
      if (SUCCEEDED(op->get_TotalBytesToReceive(&tot0))) dl_prog_fire(d, 0, tot0); }
}

/* 路径存在且非目录才算「文件落地」；空路径直接 false。 */
static int dl_path_exists(const std::wstring& p)
{
    DWORD attr;
    if (p.empty()) return 0;
    attr = GetFileAttributesW(p.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return 0;
    return 1;
}

/* -1 = 不存在/是目录；否则返回落盘字节数（nFileSizeHigh/Low 合成，支持 >4GB）。
 * 总长未知的下载靠「文件大小 == 已收字节」判 commit，必须精确到字节。 */
static long long dl_file_size(const std::wstring& p)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (p.empty()) return -1;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return -1;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
    return ((long long)fad.nFileSizeHigh << 32) + (long long)fad.nFileSizeLow;
}

static void dl_watch_poll(void)
{
    size_t i = 0;
    while (i < g_dlWatch.size()) {
        DlWatch* d = g_dlWatch[i];
        COREWEBVIEW2_DOWNLOAD_STATE st = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
        INT64 rec = 0, tot = 0;
        long long fsz = -1;
        if (!d || d->done || !d->op) {
            if (d && d->op) d->op->Release();
            delete d;
            g_dlWatch.erase(g_dlWatch.begin() + i);
            continue;
        }
        if (FAILED(d->op->get_State(&st))) {
            if (++d->miss > 30) {
                diagf("dl watch: get_State 连续失败 %d 次，放弃 op=%p", d->miss, (void*)d->op);
                d->op->Release();
                d->op = 0;
                delete d;
                g_dlWatch.erase(g_dlWatch.begin() + i);
                continue;
            }
            ++i;
            continue;
        }
        d->miss = 0;
        d->op->get_BytesReceived(&rec);
        d->op->get_TotalBytesToReceive(&tot);
        /* 「字节流静止」计数：rec 与上一 tick 相同则 +1，变了就清零。
           与 lastRec 无关 —— 那是「已上报值」，可能因去重不前进。 */
        if (rec != d->lastRecSeen) { d->lastRecSeen = rec; d->stableTicks = 0; }
        else ++d->stableTicks;
        fsz = dl_file_size(d->path);
        if (++d->ticks % 8 == 1) {   /* 约 2.4 秒一行，别刷屏；带落地/临时文件状态 */
            std::wstring crd = d->path.empty() ? std::wstring() : (d->path + L".crdownload");
            diagf("dl watch: state=%d tot=%lld rec=%lld fsz=%lld ticks=%d final=%d crdownload=%d",
                  (int)st, (long long)tot, (long long)rec, fsz, d->ticks,
                  dl_path_exists(d->path) ? 1 : 0, dl_path_exists(crd) ? 1 : 0);
        }
        if (st == COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS && rec != d->lastRec)
            dl_prog_fire(d, rec, tot);   /* 只在「有新字节」时报，绝不刷屏 */
        if (st != COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS) {
            dl_watch_fire(d, st);
            d->op->Release();
            d->op = 0;
            delete d;
            g_dlWatch.erase(g_dlWatch.begin() + i);
            continue;
        }
        /* 兜底二：状态死活不翻 → 内核自己判完成。实测（2026-10-20，runtime 155）
         * chunked 下载全程 StateChanged 只有 st=0（30 s 都不投 COMPLETED），所以这条
         * 兜底是拿完成事件的唯一出路。两种落盘方式必须分开判：
         *  - tot > 0（有 Content-Length）：Chromium 先写 <目标>.crdownload，收满后改
         *    最终名。判据 = rec>=tot 且最终名存在且 .crdownload 不存在。
         *  - tot == 0（chunked / 总长未知）：实测 WebView2 **直接把数据写最终文件名，
         *    不建 .crdownload**，所以「最终名存在」毫无意义（16 KB 时就已经存在）。
         *    只能判「字节流静止 >= 12 tick(≈3.6 s) 且 落盘大小 == rec」，确认 Chromium
         *    把收到的字节都 flush 完了。此时绝不看 final/crdownload。 */
        if (tot > 0) {
            if (d->ticks >= 6 && rec >= tot && dl_path_exists(d->path) &&
                !dl_path_exists(d->path + L".crdownload")) {
                diagf("dl watch: state stuck but final file committed(%s) tot=%lld rec=%lld -> report COMPLETED",
                      w2utf8(d->path.c_str(), -1).c_str(), (long long)tot, (long long)rec);
                dl_watch_fire(d, COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED);
                d->op->Release();
                d->op = 0;
                delete d;
                g_dlWatch.erase(g_dlWatch.begin() + i);
                continue;
            }
        } else if (rec > 0 && d->stableTicks >= 12) {
            if (fsz == rec) {
                diagf("dl watch: tot unknown, bytes idle %.1fs and file size==rec (%lld) -> report COMPLETED",
                      d->stableTicks * 0.3, fsz);
                dl_watch_fire(d, COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED);
                d->op->Release();
                d->op = 0;
                delete d;
                g_dlWatch.erase(g_dlWatch.begin() + i);
                continue;
            }
            if (d->stableTicks == 12 || d->stableTicks % 8 == 0)
                diagf("dl watch: tot unknown, idle %d ticks but fsz=%lld rec=%lld -- keep waiting",
                      d->stableTicks, fsz, (long long)rec);
        }
        ++i;
    }
}

HRESULT DlStartEv::Invoke(ICoreWebView2* sender,
                          ICoreWebView2DownloadStartingEventArgs* args)
{
    ICoreWebView2DownloadOperation* op = 0;
    std::wstring durl, sug, name, dir, full, dlTarget;
    std::string adurl;
    LPWSTR p = 0;
    int allow = 1;
    HRESULT hrPath, hrHandled;

    (void)sender;
    diagf("dl start: enter v=%p args=%p", (void*)v, (void*)args);
    if (!v || !args) return S_OK;
    if (FAILED(args->get_DownloadOperation(&op)) || !op) {
        diagf("dl start: get_DownloadOperation 失败");
        return S_OK;
    }

    if (SUCCEEDED(op->get_Uri(&p)) && p) { durl = bstrw(p); CoTaskMemFree(p); p = 0; }
    if (SUCCEEDED(args->get_ResultFilePath(&p)) && p) { sug = bstrw(p); CoTaskMemFree(p); p = 0; }

    /* ---- 事件_下载开始（wkeOnDownloadStart）：接管式钩子 ----
     * 位置在既有「事件_正在下载 / 落盘」全部逻辑之前。返回非 0 = 业务自己接管：
     * 立刻取消内核这一份下载，不挂 StateChanged、不建轮询、不写文件，也不再发
     * 任何下载事件；返回 0 = 一字不改走原逻辑。
     * 参数：URL(ANSI/GBK)、建议文件名(ANSI/GBK)、总字节数（未知或 >2GB 报 0）。 */
    if (v->cbs.count(MBK_IDX_wkeOnDownloadStart)) {
        std::string url8 = w2utf8(durl.c_str(), -1);        /* 仅 diag 用，UTF-8 */
        std::wstring nm = dl_file_name(sug.empty() ? durl : sug);
        std::string urlA  = w2ansi(durl);                   /* 传给 E：ANSI(GBK) */
        std::string nameA = w2ansi(nm);
        INT64 tot64 = 0;
        int handed;
        if (FAILED(op->get_TotalBytesToReceive(&tot64))) tot64 = 0;
        if (tot64 < 0 || tot64 > 2147483647LL) tot64 = 0;
        diagf("dl start: 触发 事件_下载开始 url=%s name=%s tot=%u",
              url8.c_str(), w2utf8(nm.c_str(), -1).c_str(), (unsigned)tot64);
        handed = fire(v, MBK_IDX_wkeOnDownloadStart, (void*)urlA.c_str(),
                      (void*)nameA.c_str(), (void*)(UINT_PTR)(unsigned)tot64);
        if (handed) {
            diagf("download handed to user: %s", url8.c_str());
            args->put_Cancel(TRUE);
            op->Release();
            return S_OK;
        }
    }

    /* 只要登记了 事件_下载完成，就必须挂 StateChanged —— 与是否接管无关：
     * 不接管时走 WebView2 默认下载 UI，同样会 COMPLETED/INTERRUPTED。 */
    if (v->cbs.count(MBK_IDX_wkeOnDownloadFinish)) {
        DlStateEv* se = new DlStateEv(v, op);
        EventRegistrationToken tok; tok.value = 0;
        HRESULT hr = op->add_StateChanged(se, &tok);
        if (SUCCEEDED(hr)) {
            se->set_token(tok);
            diagf("add_StateChanged ok tok=%ld op=%p", (long)tok.value, (void*)op);
        } else diagf("add_StateChanged 失败 hr=0x%08lX —— 下载完成事件缺失",
                     (unsigned long)hr);
        se->Release();
    }

    /* 没人登记 事件_正在下载 = 完全不碰 args，交给 WebView2 默认下载 */
    if (!v->cbs.count(MBK_IDX_wkeOnDownload)) {
        std::map<int, CbRef>::iterator it;
        diagf("dl start: 未登记 事件_正在下载 (cbs=%u)", (unsigned)v->cbs.size());
        for (it = v->cbs.begin(); it != v->cbs.end(); ++it)
            diagf("  cbs idx=%d fn=%p", it->first, it->second.fn);
        /* 进度事件独立可用：只登记 事件_下载进度 也要建轮询，否则拿不到任何进度。 */
        if (!sug.empty() &&
            (v->cbs.count(MBK_IDX_wkeOnDownloadFinish) ||
             v->cbs.count(MBK_IDX_wkeOnDownloadProgress)))
            dl_watch_add(v, op, sug);
        op->Release();
        return S_OK;
    }
    diagf("dl start: 触发 事件_正在下载 url=%s",
          w2utf8(durl.c_str(), -1).c_str());

    /* 事件_正在下载：真=继续（默认下载），假=取消。与 node/wke 语义一致。 */
    adurl = w2ansi(durl);
    allow = fire(v, MBK_IDX_wkeOnDownload, (void*)adurl.c_str());
    diagf("dl start: 事件_正在下载 返回 allow=%d dirlen=%u", allow,
          (unsigned)v->dlDir.size());
    if (!allow) {
        diagf("download cancelled by user: %s", w2utf8(durl.c_str(), -1).c_str());
        dl_watch_mark_done(op);
        dl_done_mark(op);          /* 取消后不再补发 事件_下载完成 */
        args->put_Cancel(TRUE);
        op->Release();
        return S_OK;
    }

    /* 只有显式调过 wkeSetDownloadDir（v->dlDir 非空）才静默落盘接管；
     * 否则一律走 Edge 默认下载（默认目录 + 默认下载条），不 put_Handled。 */
    if (!v->dlDir.empty() && ensure_dir_w(v->dlDir)) {
        name = dl_file_name(sug.empty() ? durl : sug);
        dir  = v->dlDir;
        { wchar_t last = dir[dir.size() - 1];
          if (last != L'\\' && last != L'/') dir += L'\\'; }
        full = dir + name;
        hrPath = args->put_ResultFilePath(full.c_str());
        if (SUCCEEDED(hrPath)) {
            hrHandled = args->put_Handled(TRUE);
            if (SUCCEEDED(hrHandled)) {
                diagf("download -> %s", w2utf8(full.c_str(), -1).c_str());
                dlTarget = full;
            } else {
                diagf("put_Handled 失败 hr=0x%08lX —— 回落到 Edge 默认下载",
                      (unsigned long)hrHandled);
            }
        } else {
            diagf("put_ResultFilePath 失败 hr=0x%08lX —— 回落到 Edge 默认下载",
                  (unsigned long)hrPath);
        }
    } else {
        diagf("download allowed (WebView2 default UI): %s",
              w2utf8(durl.c_str(), -1).c_str());
        dlTarget = sug;
    }
    if (!dlTarget.empty() &&
        (v->cbs.count(MBK_IDX_wkeOnDownloadFinish) ||
         v->cbs.count(MBK_IDX_wkeOnDownloadProgress)))
        dl_watch_add(v, op, dlTarget);
    op->Release();
    return S_OK;
}

HRESULT ContentEv::Invoke(ICoreWebView2* sender,
                          ICoreWebView2ContentLoadingEventArgs* args)
{
    (void)sender; (void)args;
    /* DOM 已经开始解�?—�?对上 miniblink 的「文档就绪�?*/
    /* ContentLoading precedes DOM creation; NavigationCompleted queues readiness. */
    return S_OK;
}

HRESULT DialogEv::Invoke(ICoreWebView2* sender,
                         ICoreWebView2ScriptDialogOpeningEventArgs* args)
{
    if (!v) return S_OK;
    BSTR b = 0;
    COREWEBVIEW2_SCRIPT_DIALOG_KIND k = COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT;
    (void)sender;
    if (!args) return S_OK;
    args->get_Kind(&k);
    args->get_Message(&b);
    {
        std::wstring m = bstrw(b);
        if (b) CoTaskMemFree(b);
        /* WebView2 这版没有 put_Cancel；只�?Accept() =「显示默认框」�?         * 有业务回调时不调 Accept �?弹框被吞掉，由业务侧自己处理�?         * 没有回调时调 Accept �?维持浏览器默认行为，不比 node 内核差�?*/
        if (k == COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT) {
            if (v->cbs.count(MBK_IDX_wkeOnAlertBox)) {
                fire_str(v, MBK_IDX_wkeOnAlertBox, m);
            } else args->Accept();
        } else if (k == COREWEBVIEW2_SCRIPT_DIALOG_KIND_CONFIRM) {
            if (v->cbs.count(MBK_IDX_wkeOnConfirmBox)) {
                WStr* p = str_new(m.c_str(), (int)m.size());
                fire(v, MBK_IDX_wkeOnConfirmBox, (void*)(UINT_PTR)p);
                p->magic = 0; delete p;
            } else args->Accept();
        } else {
            if (v->cbs.count(MBK_IDX_wkeOnPromptBox)) {
                WStr* p = str_new(m.c_str(), (int)m.size());
                WStr* d = str_new(L"", 0);
                fire(v, MBK_IDX_wkeOnPromptBox, (void*)(UINT_PTR)p, 0, (void*)(UINT_PTR)d);
                p->magic = 0; delete p;
                d->magic = 0; delete d;
            } else args->Accept();
        }
    }
    return S_OK;
}

HRESULT NewWndEv::Invoke(ICoreWebView2* sender,
                         ICoreWebView2NewWindowRequestedEventArgs* args)
{
    BSTR b = 0;
    (void)sender;
    if (!args) return S_OK;
    args->get_Uri(&b);
    {
        std::wstring u = bstrw(b);
        int r;
        WStr* p = str_new(u.c_str(), (int)u.size());
        if (b) CoTaskMemFree(b);
        r = fire(v, MBK_IDX_wkeOnCreateView, (void*)0, (void*)(UINT_PTR)p, (void*)0);
        p->magic = 0; delete p;
        diagf("newwin v=%p uri=%s handled=%d", (void*)v,
              w2utf8(u.c_str(), -1).c_str(), r);
        /* 回调没接管就交给默认行为（开新窗口）；接管了就关掉这个请�?*/
        args->put_Handled(r ? TRUE : FALSE);
    }
    return S_OK;
}

HRESULT ExecDone::Invoke(HRESULT hr, LPCWSTR resultAsJson)
{
    status = hr;
    if (SUCCEEDED(hr) && resultAsJson) res = w2utf8(resultAsJson, -1);
    else res.clear();
    SetEvent(evt);
    return S_OK;
}

/* ---- 环境 / 控制�?---- */
/* 用户数据目录（profile）。
 *
 * 2026-10-12 实测定论：**浏览器子进程继承宿主的完整性级别(IL)**，Windows 的
 *   No-Write-Up 规则会拦掉「低 IL 进程写中 IL 目录」——于是
 *   * 宿主 exe 从带 Low 标签的目录（如某些工作区）启动 → 进程 IL=Low →
 *     浏览器写不了 %LOCALAPPDATA%\...\EBWebView → controller 0x800700AA（或更早崩溃）。
 *   * 同一个 exe 放到普通（Medium）目录启动 → 一切正常。
 * 这与代码/非 ASCII 路径/node.dll 都无关。因此这里的策略是
 *   「**谁可写就用谁**」：默认 %LOCALAPPDATA%；实测不可写（探针文件失败）时，
 *   退回宿主 exe 所在目录——它至少与宿主进程同标签，Low 宿主自然落在 Low 目录里。
 * 显式覆盖永远优先：MBKIT_WV2_PROFILE 环境变量。 */
static int ensure_dir(const std::wstring& d)
{
    if (d.empty()) return 0;
    if (CreateDirectoryW(d.c_str(), NULL)) return 1;
    return GetLastError() == ERROR_ALREADY_EXISTS ? 1 : 0;
}

static int dir_writable(const std::wstring& d)
{
    std::wstring f;
    HANDLE h;
    if (!ensure_dir(d)) return 0;
    f = d + L"\\.mbkwv_probe";
    h = CreateFileW(f.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(h);
    DeleteFileW(f.c_str());
    return 1;
}

static std::wstring exe_profile_dir(void)
{
    wchar_t p[MAX_PATH];
    std::wstring d;
    DWORD n = GetModuleFileNameW(NULL, p, MAX_PATH);
    if (!n || n >= MAX_PATH) return std::wstring();
    d.assign(p, n);
    {   size_t sl = d.find_last_of(L"\\/");
        if (sl == std::wstring::npos) return std::wstring();
        d.resize(sl);
    }
    d += L"\\MbKitWebView2";
    return d;
}

static std::wstring profile_dir(void)
{
    wchar_t p[MAX_PATH];
    std::wstring a, b;
    DWORD n = GetEnvironmentVariableW(L"MBKIT_WV2_PROFILE", p, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return p;

    if (SUCCEEDED(SHGetFolderPathW(NULL, 0x001C /*CSIDL_LOCAL_APPDATA*/, NULL,
                                   0x8000 /*CREATE*/, p))) {
        a = p; a += L"\\MbKitWebView2";
        if (dir_writable(a)) return a;
        diagf("默认 profile 目录不可写（多半是低完整性级别）→ 退回 exe 目录");
    }
    b = exe_profile_dir();
    if (dir_writable(b)) return b;
    if (GetTempPathW(MAX_PATH, p)) { std::wstring t = p; t += L"MbKitWebView2"; if (dir_writable(t)) return t; }
    return a.empty() ? b : a;
}

static int ensure_env(void)
{
    HRESULT hr;
    std::wstring dataDir;
    if (g_env) return 1;
    if (g_envFailed) return 0;
    if (!loader_load()) return 0;
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr != RPC_E_CHANGED_MODE && FAILED(hr))
        diagf("CoInitializeEx hr=0x%08lX", (unsigned long)hr);
    if (InterlockedCompareExchange(&g_envRequested, 1, 0) != 0) return 1;
    dataDir = profile_dir();
    diagf("ensure_env: profile=%s", dataDir.empty() ? "(默认)" : w2utf8(dataDir.c_str(), -1).c_str());
    {
        EnvDone* handler = new EnvDone();
        hr = pCreateEnv(NULL, dataDir.empty() ? NULL : dataDir.c_str(), NULL, handler);
        handler->Release();
    }
    if (FAILED(hr)) {
        InterlockedExchange(&g_envFailed, 1);
        diagf("启动 WebView2 环境 hr=0x%08lX", (unsigned long)hr);
        return 0;
    }
    diagf("WebView2 environment requested asynchronously");
    return 1;
}

/* 把已存的属性落�?WebView2 上（控制器起来后 / 属性变化后调） */
struct ClampBox { int cx, cy; };
static BOOL CALLBACK clamp_child(HWND hwnd, LPARAM lp)
{
    ClampBox* box = (ClampBox*)lp;
    wchar_t cls[64];
    cls[0] = 0;
    GetClassNameW(hwnd, cls, 64);
    if (wcsncmp(cls, L"Chrome_", 7) == 0)
        SetWindowPos(hwnd, NULL, 0, 0, box->cx, box->cy,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    return TRUE;
}

/* 取真实显示器缩放（1.0 = 100%）。
 * 宿主进程被 AppCompat 钉死 DPIUNAWARE，GetDpiForSystem / GetDpiForWindow 这类
 * 「跟调用者认知走」的接口只会回 96；GetDpiForMonitor 回的是监视器真实 DPI。
 * 若 shcore 不可用（或它也被认知影响），退回「真实分辨率 / 虚拟化分辨率」之比。 */
static double monitor_scale(HWND hwnd)
{
    typedef HRESULT (WINAPI *GetDpiMon)(HMONITOR, int, UINT*, UINT*);
    HMODULE sh;
    GetDpiMon fn;
    HMONITOR mon;
    UINT dx = 0, dy = 0;
    DEVMODEW dm;
    int vw, vh;

    sh = LoadLibraryW(L"shcore.dll");
    fn = sh ? (GetDpiMon)GetProcAddress(sh, "GetDpiForMonitor") : 0;
    mon = MonitorFromWindow(hwnd ? hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    if (fn && SUCCEEDED(fn(mon, 0, &dx, &dy)) && dx >= 96 && dx != 96) return (double)dx / 96.0;

    /* 回退：DPI 接口要么不可用、要么被 DPI 认知影响成 96 —— 用分辨率比值兜底 */
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    vw = GetSystemMetrics(SM_CXSCREEN);
    vh = GetSystemMetrics(SM_CYSCREEN);
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && vw > 0 && vh > 0) {
        double sx = (double)dm.dmPelsWidth / (double)vw;
        double sy = (double)dm.dmPelsHeight / (double)vh;
        if (sx > 1.0 && sx < 8.0 && sy > 1.0 && sy < 8.0) return (sx + sy) / 2.0;
    }
    return (fn && dx >= 96) ? (double)dx / 96.0 : 1.0;
}

/* 环境变量覆盖（A/B 与排障用；不设就按实测值走） */
static double env_scale_def(const wchar_t* name, double def)
{
    wchar_t buf[64];
    if (!name || GetEnvironmentVariableW(name, buf, 64) == 0) return def;
    {   double v = wcstod(buf, NULL);
        return v > 0.0 ? v : def; }
}

static int env_int_def(const wchar_t* name, int def)
{
    wchar_t buf[64];
    if (!name || GetEnvironmentVariableW(name, buf, 64) == 0) return def;
    return (int)wcstol(buf, NULL, 10);
}

static LRESULT CALLBACK host_subproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void release_controller(WV* w);

/* Match the DPI coordinate space of the parent HWND. In a DPI-unaware host,
 * Windows already scales the complete HWND tree. Monitor DPI here would apply
 * scaling a second time. Keep RAW_PIXELS bounds in parent client coordinates;
 * do not resize Chromium's internal windows or change process DPI awareness. */
static void fit_webview(WV* w)
{
    RECT logical;
    double scale;
    ICoreWebView2Controller3* c3 = 0;

    diagf("fit enter ctl=%p host=%p", w ? (void*)w->ctl : 0, w ? (void*)w->host : 0);
    if (!w->ctl || !w->host || !IsWindow(w->host)) return;
    w->ctl->put_ParentWindow(w->host);
    if (!GetClientRect(w->host, &logical) || logical.right <= 0 || logical.bottom <= 0) return;
    /* RAW_PIXELS scaling moved the browser outside the DPI-unaware parent and
       left the visible area black. Keep the controller exactly on the host. */
    w->width = logical.right - logical.left;
    w->height = logical.bottom - logical.top;
    /* Match the parent window's DPI, not the monitor DPI. For an unaware host
       Windows already magnifies the whole HWND tree. Multiplying again shrinks
       the CSS viewport and magnifies browser content twice. */
    typedef UINT (WINAPI *GetWindowDpi)(HWND);
    GetWindowDpi windowDpi = (GetWindowDpi)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    UINT dpi = windowDpi ? windowDpi(w->host) : 96;
    scale = dpi ? (double)dpi / 96.0 : 1.0;
    if (SUCCEEDED(w->ctl->QueryInterface(IID_ICoreWebView2Controller3, (void**)&c3)) && c3) {
        c3->put_ShouldDetectMonitorScaleChanges(FALSE);
        c3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
        c3->put_RasterizationScale(scale);
        c3->Release();
    }
    w->ctl->put_Bounds(logical);
    w->visState = IsWindowVisible(w->host) ? 1 : 0;
    w->ctl->put_IsVisible(w->visState ? TRUE : FALSE);
    diagf("fit parent %ld x %ld dpi=%u raster=%.3f", w->width, w->height, dpi, scale);
}

static void unhook_host(WV* w)
{
    WNDPROC old;
    if (!w || !w->host || !IsWindow(w->host)) return;
    old = (WNDPROC)GetPropW(w->host, L"MbKitOldProc");
    if (old && GetWindowLongPtrW(w->host, GWLP_WNDPROC) == (LONG_PTR)host_subproc)
        SetWindowLongPtrW(w->host, GWLP_WNDPROC, (LONG_PTR)old);
    RemovePropW(w->host, L"MbKitWV");
    RemovePropW(w->host, L"MbKitOldProc");
}

/* 诊断计数：真正执行过释放的次数（配合日志判断是否还在重复释放） */
static volatile LONG g_relCount = 0;

static void release_controller(WV* w)
{
    if (!w) return;

    /* ★ 2026-10-16 修复（黑屏/资源泄漏）：本函数以前**不是幂等的**。
     *   实测量到同 1 毫秒内连续 5 条 `controller released` —— host_subproc 对
     *   WM_DESTROY 与 WM_NCDESTROY **各调一次**，wkeSetHandle(0) 与
     *   wkeDestroyWebView 又各有一条入口。后果有两个：
     *     ① 多次投递 WM_APP+76 → 同一个 controller 被 Close/Release 多次；
     *     ② 第一遍就把 w->ctl/w->core 置 0，第 1314 行的 `if (w->core)` 从此
     *        **永远不可达** → 每建一个视图泄漏 1 个 controller + 1 个 core。
     *   现在：已释放过就直接返回；诊断行也随之只打一次（否则同毫秒刷 5 条，
     *   把真正有用的诊断淹掉）。 */
    if (!w->ctl && !w->core && !w->wired) return;

    unhook_host(w);
    {
        ICoreWebView2Controller* ctl = w->ctl;   /* 先存引用再清，顺序不能反 */
        ICoreWebView2*           core = w->core;
        w->ctl = 0;
        w->core = 0;
        if (ctl) {
            /* Close/Release Chromium outside the plugin unload call stack. */
            ReleasePack* pack = new ReleasePack();
            pack->ctl = ctl;
            pack->core = core;
            post_to_hidden(WM_APP + 76, (WPARAM)pack, 0);
        } else if (core) {
            core->Release();                     /* 没有 ctl 也别漏掉 core */
        }
    }
    w->wired = 0;
    InterlockedIncrement(&g_relCount);
    diagf("controller released [view=%p n=%ld alive=%d]",
          (void*)w, (long)g_relCount, (int)w->alive);
}

static LRESULT CALLBACK host_subproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WV* w = (WV*)GetPropW(hwnd, L"MbKitWV");
    WNDPROC old = (WNDPROC)GetPropW(hwnd, L"MbKitOldProc");
    if (w && w->magic == WV_VIEWS_MAGIC) {
        if (msg == WM_DESTROY || msg == WM_NCDESTROY) release_controller(w);
        else if (msg == WM_SIZE && w->ctl) fit_webview(w);
        else if (msg == WM_SHOWWINDOW && w->ctl) {
            /* wp 只是「这次 Show/Hide 的意图」，树上真实可见性要自己看 */
            w->visState = -1;
            sync_vis(w);
            diagf("host WM_SHOWWINDOW wp=%d host=%p -> vis=%d",
                  (int)(wp ? 1 : 0), (void*)hwnd, w->visState);
        }
    }
    return old ? CallWindowProcW(old, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

static void hook_host(WV* w)
{
    WNDPROC cur;
    if (!w->host || !IsWindow(w->host)) return;
    if (GetPropW(w->host, L"MbKitWV") == (HANDLE)w) return;
    cur = (WNDPROC)GetWindowLongPtrW(w->host, GWLP_WNDPROC);
    if (cur == host_subproc) return;
    SetPropW(w->host, L"MbKitOldProc", (HANDLE)cur);
    SetPropW(w->host, L"MbKitWV", (HANDLE)w);
    SetWindowLongPtrW(w->host, GWLP_WNDPROC, (LONG_PTR)host_subproc);
}

/* 网页只能活在图片框里面。不建独立弹层。 */
static void contain_in_host(WV* w)
{
    if (!w->ctl) return;
    if (!w->host || !IsWindow(w->host) || !IsWindowVisible(w->host)) {
        w->visState = 0;
        w->ctl->put_IsVisible(FALSE);
        return;
    }
    fit_webview(w);
    {
        /* fit_webview 已经按 IsWindowVisible 设过一次；这里按 headless 再压一次，
         * 结果统一写回 visState，避免 300ms 轮询把刚唤起的视图又关回去。 */
        BOOL want = w->headless ? FALSE : TRUE;
        w->visState = (int)want;
        w->ctl->put_IsVisible(want);
    }
}

static void apply_state(WV* w)
{
    if (!w->ctl || !w->core) return;
    contain_in_host(w);
    if (!w->ctl) return;
    if (w->zoom > 0 && w->zoom != 1.0) w->ctl->put_ZoomFactor(w->zoom);
    {   ICoreWebView2Settings* st = 0;
        if (SUCCEEDED(w->core->get_Settings(&st)) && st) {
            st->put_AreDevToolsEnabled(TRUE);
            st->put_IsStatusBarEnabled(FALSE);
            if (w->headless) st->put_IsScriptEnabled(TRUE);
            st->Release();
        } }
    {   ICoreWebView2Controller2* c2 = 0;
        if (SUCCEEDED(w->ctl->QueryInterface(__uuidof(ICoreWebView2Controller2), (void**)&c2)) && c2) {
            COREWEBVIEW2_COLOR c;
            if (w->transparent) { c.A = 0; c.R = 0; c.G = 0; c.B = 0; }
            else { c.A = 255; c.R = 244; c.G = 247; c.B = 251; }
            c2->put_DefaultBackgroundColor(c);
            c2->Release();
        }
    }
}

static std::wstring file_uri_path(const wchar_t* uri)
{
    std::string bytes;
    std::wstring out;
    const wchar_t* p;
    if (!uri) return out;
    if (_wcsnicmp(uri, L"file:///", 8) == 0) p = uri + 8;
    else if (_wcsnicmp(uri, L"file://", 7) == 0) p = uri + 7;
    else return out;
    for (; *p; ++p) {
        unsigned char c;
        if (*p == L'%' && p[1] && p[2]) {
            int hi = -1, lo = -1;
            wchar_t a = p[1], b = p[2];
            if (a >= L'0' && a <= L'9') hi = a - L'0';
            else if (a >= L'A' && a <= L'F') hi = a - L'A' + 10;
            else if (a >= L'a' && a <= L'f') hi = a - L'a' + 10;
            if (b >= L'0' && b <= L'9') lo = b - L'0';
            else if (b >= L'A' && b <= L'F') lo = b - L'A' + 10;
            else if (b >= L'a' && b <= L'f') lo = b - L'a' + 10;
            if (hi >= 0 && lo >= 0) { bytes.push_back((char)((hi << 4) | lo)); p += 2; continue; }
        }
        c = (*p == L'/') ? '\\' : (unsigned char)*p;
        bytes.push_back((char)c);
    }
    out = utf8w(bytes.c_str());
    if (out.size() >= 2 && out[1] == L'|') out[1] = L':';
    return out;
}

static const wchar_t* mime_of(const std::wstring& path)
{
    size_t d = path.find_last_of(L'.');
    std::wstring e = d == std::wstring::npos ? L"" : path.substr(d);
    for (size_t i = 0; i < e.size(); ++i) if (e[i] >= L'A' && e[i] <= L'Z') e[i] = (wchar_t)(e[i] - L'A' + L'a');
    if (e == L".jpg" || e == L".jpeg") return L"image/jpeg";
    if (e == L".png") return L"image/png";
    if (e == L".gif") return L"image/gif";
    if (e == L".webp") return L"image/webp";
    if (e == L".bmp") return L"image/bmp";
    if (e == L".svg") return L"image/svg+xml";
    if (e == L".css") return L"text/css";
    if (e == L".js") return L"text/javascript";
    if (e == L".html" || e == L".htm") return L"text/html";
    return L"application/octet-stream";
}

class FileReqEv : public ICoreWebView2WebResourceRequestedEventHandler {
    WV_IUNKNOWN(FileReqEv, ICoreWebView2WebResourceRequestedEventHandler)
public:
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2WebResourceRequestedEventArgs* args);
};

HRESULT FileReqEv::Invoke(ICoreWebView2* sender,
                          ICoreWebView2WebResourceRequestedEventArgs* args)
{
    ICoreWebView2WebResourceRequest* req = 0;
    LPWSTR uri = 0;
    std::wstring path;
    HANDLE file;
    DWORD size, got;
    HGLOBAL mem;
    void* buf;
    IStream* stream = 0;
    ICoreWebView2WebResourceResponse* resp = 0;
    ICoreWebView2Environment* env = 0;
    wchar_t headers[160];
    (void)sender;
    if (!args || !g_env) return S_OK;
    if (FAILED(args->get_Request(&req)) || !req) return S_OK;
    req->get_Uri(&uri);
    req->Release();
    if (uri && _wcsnicmp(uri, L"https://mbkit.local/", 20) == 0) {
        WV* page = 0;
        for (auto it = g_liveViews.begin(); it != g_liveViews.end(); ++it) {
            if (it->second && it->second->core == sender &&
                !it->second->virtualHtml.empty()) { page = it->second; break; }
        }
        if (page) {
            std::wstring html = with_file_bridge(page->virtualHtml);
            std::string bytes = w2utf8(html.c_str(), -1);
            mem = GlobalAlloc(GMEM_MOVEABLE, bytes.empty() ? 1 : bytes.size());
            buf = mem ? GlobalLock(mem) : 0;
            if (buf) {
                if (!bytes.empty()) memcpy(buf, bytes.data(), bytes.size());
                GlobalUnlock(mem);
                if (SUCCEEDED(CreateStreamOnHGlobal(mem, TRUE, &stream)) && stream &&
                    SUCCEEDED(g_env->CreateWebResourceResponse(stream, 200, L"OK",
                        L"Content-Type: text/html; charset=utf-8", &resp)) && resp) {
                    args->put_Response(resp);
                    resp->Release();
                    diagf("virtual html served bytes=%u", (unsigned)bytes.size());
                }
                if (stream) stream->Release();
            }
        }
        CoTaskMemFree(uri);
        return S_OK;
    }
    path = file_uri_path(uri);
    if (uri) CoTaskMemFree(uri);
    if (path.empty()) return S_OK;
    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        diagf("file miss");
        return S_OK;
    }
    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size > 32 * 1024 * 1024) { CloseHandle(file); return S_OK; }
    mem = GlobalAlloc(GMEM_MOVEABLE, size ? size : 1);
    if (!mem) { CloseHandle(file); return S_OK; }
    buf = GlobalLock(mem);
    if (!buf || !ReadFile(file, buf, size, &got, NULL)) {
        if (buf) GlobalUnlock(mem);
        GlobalFree(mem);
        CloseHandle(file);
        return S_OK;
    }
    GlobalUnlock(mem);
    CloseHandle(file);
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream)) || !stream) { GlobalFree(mem); return S_OK; }
    env = g_env;
    _snwprintf(headers, 160, L"Content-Type: %s\r\nAccess-Control-Allow-Origin: *", mime_of(path));
    if (SUCCEEDED(env->CreateWebResourceResponse(stream, 200, L"OK", headers, &resp)) && resp) {
        args->put_Response(resp);
        resp->Release();
        diagf("file served bytes=%lu", (unsigned long)size);
    }
    stream->Release();
    return S_OK;
}

static void map_local_drives(ICoreWebView2* core)
{
    ICoreWebView2_3* c3 = 0;
    wchar_t letter;
    if (!core) return;
    if (FAILED(core->QueryInterface(IID_ICoreWebView2_3, (void**)&c3)) || !c3) {
        diagf("no ICoreWebView2_3");
        return;
    }
    for (letter = L'C'; letter <= L'Z'; ++letter) {
        wchar_t root[4] = { letter, L':', L'\\', 0 };
        wchar_t host[32];
        HRESULT hr;
        if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR || GetDriveTypeW(root) == DRIVE_UNKNOWN) continue;
        _snwprintf(host, 32, L"%c.mbkit.local", (wchar_t)(letter + (letter <= L'Z' ? 32 : 0)));
        hr = c3->SetVirtualHostNameToFolderMapping(host, root, COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        diagf("map drive %c hr=0x%08lX", (char)letter, (unsigned long)hr);
    }
    c3->Release();
}

static std::wstring with_file_bridge(const std::wstring& html)
{
    static const wchar_t* js =
        L"<script>(function(){if(window.__mbkitFileBridge)return;window.__mbkitFileBridge=true;function rw(u){u=String(u||'');"
        L"var m=u.match(/^file:\\/\\/\\/([A-Za-z])[:|]\\/?(.*)$/i);"
        L"if(!m)return u;return 'https://'+m[1].toLowerCase()+'.mbkit.local/'+m[2].replace(/^\\/+/, '');}"
        L"function hook(p){var d=Object.getOwnPropertyDescriptor(p,'src');if(!d||!d.set)return;"
        L"var s=d.set,g=d.get;Object.defineProperty(p,'src',{configurable:true,enumerable:d.enumerable,get:g,"
        L"set:function(v){s.call(this,rw(v));}});}"
        L"if(window.HTMLImageElement)hook(HTMLImageElement.prototype);})();</script>";
    std::wstring out = html;
    size_t p;
    if (out.find(L"mbkit.local") != std::wstring::npos) return out;
    p = out.find(L"<head>");
    if (p == std::wstring::npos) p = out.find(L"<HEAD>");
    if (p != std::wstring::npos) out.insert(p + 6, js);
    else out = std::wstring(js) + out;
    return out;
}

/* add_* 成功后会自己 AddRef 一份；调用方手里那份必须立刻放掉，否则 handler
 * 永远销毁不掉（它持 WVLease ⇒ asyncRefs 归不了零 ⇒ 视图只剩墓碑）。
 * 2026-10-19 只读验收 C5/F1：wire() 里所有 new 出来的 handler 都漏了这一放。 */
template <class T>
static void hook_add(HRESULT hr, T* h, const char* what)
{
    if (h) h->Release();
    if (FAILED(hr)) diagf("wire: add_%s 失败 hr=0x%08lX", what, (unsigned long)hr);
}

#define MBK_ADD(obj, method, ctor, name) do { \
        auto* h_ = (ctor); \
        HRESULT hr_ = (obj)->method(h_, NULL); \
        hook_add(hr_, h_, (name)); \
    } while (0)

/* 2026-10-20 E2E 定位结论：WebView2 Runtime 155 里，**只要没有显式设置
 * CoreWebView2Profile.DefaultDownloadFolderPath，直导航附件就会被静默丢弃**——
 * 网络层拿到了完整响应（200 + application/octet-stream + attachment），
 * 却既不抛 DownloadStarting、也不落盘，NavigationCompleted 直接是
 * ConnectionAborted，URL 变成 about:blank。于是 MbKit 的下载三件套
 * （下载开始/正在下载/下载完成）全部哑火，demo 日志只剩启动两行。
 * 低完整性级别进程（易语言 demo 常见）下更糟：默认「下载」目录不可写，
 * 连 put_DefaultDownloadFolderPath 都会被拒（E_INVALIDARG）。
 * 结论：上层设过下载目录就必须真的 apply 到 profile，不能只记在 WV::dlDir
 * 里等 DownloadStarting —— 那个事件永远不会来。 */
static void apply_profile_dl_dir(WV* w)
{
    HRESULT hr;
    ICoreWebView2_13* c13 = 0;
    ICoreWebView2Profile* prof = 0;
    if (!w || !w->core || w->dlDir.empty()) return;
    /* 目录不存在时 put_DefaultDownloadFolderPath 直接 E_INVALIDARG（实测），先建。 */
    ensure_dir_w(w->dlDir);
    hr = w->core->QueryInterface(IID_ICoreWebView2_13, (void**)&c13);
    if (FAILED(hr) || !c13) {
        diagf("profile dl dir: no ICoreWebView2_13 hr=0x%08lX", (unsigned long)hr);
        return;
    }
    hr = c13->get_Profile(&prof);
    c13->Release();
    if (FAILED(hr) || !prof) {
        diagf("profile dl dir: get_Profile hr=0x%08lX", (unsigned long)hr);
        return;
    }
    hr = prof->put_DefaultDownloadFolderPath(w->dlDir.c_str());
    diagf("profile DefaultDownloadFolderPath -> %s hr=0x%08lX",
          w2utf8(w->dlDir.c_str(), -1).c_str(), (unsigned long)hr);
    prof->Release();
}

static void wire(WV* w)
{
    ICoreWebView2* c = w->core;
    if (!c) return;
    diagf("wire begin v=%p core=%p", (void*)w, (void*)c);
    map_local_drives(c);
    /* 先把 wkeSetDownloadDir 记住的目录真正写进 profile，再挂下载事件。
     * 顺序很重要：profile 目录在第一次导航之前就位，附件才会抛 DownloadStarting。 */
    apply_profile_dl_dir(w);
    {
        std::wstring script = with_file_bridge(L"");
        script = script.substr(8, script.size() - 17); /* Strip script element. */
        c->AddScriptToExecuteOnDocumentCreated(script.c_str(), nullptr);
    }
    c->AddWebResourceRequestedFilter(L"file://*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    c->AddWebResourceRequestedFilter(L"https://mbkit.local/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    c->AddWebResourceRequestedFilter(L"https://*.mbkit.local/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    MBK_ADD(c, add_WebResourceRequested, new FileReqEv(), "WebResourceRequested");
    MBK_ADD(c, add_DocumentTitleChanged, new TitleEv(w), "DocumentTitleChanged");
    MBK_ADD(c, add_SourceChanged, new SourceEv(w), "SourceChanged");
    MBK_ADD(c, add_NavigationStarting, new NavStartEv(w), "NavigationStarting");
    MBK_ADD(c, add_NavigationCompleted, new NavDoneEv(w), "NavigationCompleted");
    MBK_ADD(c, add_ContentLoading, new ContentEv(w), "ContentLoading");
    MBK_ADD(c, add_ScriptDialogOpening, new DialogEv(w), "ScriptDialogOpening");
    MBK_ADD(c, add_NewWindowRequested, new NewWndEv(w), "NewWindowRequested");
    {   /* 下载拦截：只有 ICoreWebView2_4 及以上才提供 DownloadStarting */
        ICoreWebView2_4* c4 = 0;
        if (SUCCEEDED(c->QueryInterface(IID_ICoreWebView2_4, (void**)&c4)) && c4) {
            DlStartEv* h = new DlStartEv(w);
            DlProbeEv* hp = new DlProbeEv(w);
            HRESULT hr = c4->add_DownloadStarting(h, NULL);
            HRESULT hrp = c4->add_DownloadStarting(hp, NULL);
            if (h) h->Release();
            if (hp) hp->Release();
            c4->Release();
            if (SUCCEEDED(hr)) diagf("wire: DownloadStarting hooked v=%p c4=%p h=%p probe hr=0x%08lX",
                                     (void*)w, (void*)c4, (void*)h, (unsigned long)hrp);
            else diagf("wire: add_DownloadStarting 失败 hr=0x%08lX —— 下载事件缺失",
                       (unsigned long)hr);
        } else {
            diagf("wire: ICoreWebView2_4 不可用 —— 下载事件无法拦截");
        }
    }
    /* 注：这个版本�?WebView2 SDK�?.0.2739.15）没�?     * add_ConsoleMessageReceived —�?控制台消息暂不转发，留待升级 SDK�?*/
}

static void finish_ctl(WV* w)
{
    if (!w || !w->alive || !w->ctl || !w->core) return;
    if (!w->wired) {
        wire(w);
        for (const auto& binding : js_bindings()) install_binding(w, binding.first, binding.second);
        w->wired = 1;
    }
    if (w->host) {
        RECT rc;
        if (GetClientRect(w->host, &rc)) {
            if (!w->width)  w->width  = rc.right - rc.left;
            if (!w->height) w->height = rc.bottom - rc.top;
        }
    }
    apply_state(w);
    if (w->ctl) w->ctl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    if (!w->pendingNav.empty()) {
        std::wstring u = w->pendingNav;
        int isHtml = w->pendingIsHtml;
        w->pendingNav.clear();
        diagf("flush pending navigation html=%d chars=%u", isHtml, (unsigned)u.size());
        if (isHtml) { w->readyEligible = 1; w->core->NavigateToString(u.c_str()); }
        else        w->core->Navigate(u.c_str());
    }
    fit_webview(w);
}

/* WebView2 的创建接口是异步的。这里绝不等待环境或控制器完成，
 * 否则宿主 UI 线程会和 WebView2 的 COM 消息互相等待。 */
static int ensure_ctl(WV* w)
{
    HRESULT hr;
    CtlDone* h;
    if (!w || !w->alive) return 0;
    if (w->ctl && w->core) return 1;
    if (w->ctlPending) return 1;
    if (!w->host) w->host = hidden_host();
    /* ★ 2026-10-15 修复：host 承载窗口必须在**属主线程**上先建出来。
     *   ensure_env() 可能把环境建到工作线程，EnvDone 回调也跟着在工作线程上跑；
     *   若等到那时才第一次调 hidden_host()，窗口就会建在工作线程上，
     *   WM_APP+73 投出去也没人抽 → 永远不创建 controller（实测白屏）。
     *   本函数运行在属主线程，所以在这里先建，保证窗口属主正确。
     *   必须在 ensure_env() 之前调用：CreateCoreWebView2EnvironmentWithOptions
     *   同步返回后环境回调随时可能触发。 */
    hidden_host();
    if (!ensure_env()) return 0;
    if (!g_env) return 1;                 /* EnvDone 会继续创建 controller */
    w->ctlPending = 1;
    h = new CtlDone(w);
    diagf("ensure_ctl async tid=%lu host=%p", (unsigned long)GetCurrentThreadId(), w->host);
    hr = g_env->CreateCoreWebView2Controller(w->host, h);
    diagf("CreateCoreWebView2Controller returned hr=0x%08lX", (unsigned long)hr);
    h->Release();
    if (FAILED(hr)) {
        w->ctlPending = 0;
        wv_async_release(w);
        diagf("CreateCoreWebView2Controller hr=0x%08lX", (unsigned long)hr);
        return 0;
    }
    return 1;
}

/* ==========================================================================
 * 8. 版本 / 诊断 / 选择
 * ==========================================================================*/
static LONG g_forceNode = 0;
static LONG g_active    = 0;
static char g_kname[64];

int mbkwv_is_forced(void) { return g_forceNode ? 1 : 0; }

int mbkwv_set_force(int on)
{
    if (g_active) return 0;              /* 已经接管/已回退，改不动�?*/
    InterlockedExchange(&g_forceNode, on ? 1 : 0);
    return 1;
}

int mbkwv_available(void) { return rt_detect(); }
const char* mbkwv_runtime_version(void) { return rt_detect() ? g_rtVer : ""; }
int mbkwv_active(void) { return g_active ? 1 : 0; }
const char* mbkwv_diag(void) { return g_diag; }
const char* mbkwv_kernel_name(void) { return g_active ? g_kname : "node.dll"; }

int mbkwv_bind(void** slots, int count, const char* const* names, void* selfModule)
{
    int i, bound = 0, missingCrit = 0;

    if (!slots || !names || count <= 0) return 0;
    if (g_forceNode) { diagf("force node kernel ON, fallback to node.dll"); return 0; }
    if (!rt_detect()) return 0;

    g_self = (HMODULE)selfModule;

    for (i = 0; i < count; i++) {
        void* p = mbkwv_lookup(names[i]);
        if (p) { slots[i] = p; bound++; }
    }
    /* 关键 API 必须真的有实现，否则这个内核根本开不了�?*/
    if (!mbkwv_lookup("wkeCreateWebView"))  missingCrit++;
    if (!mbkwv_lookup("wkeDestroyWebView")) missingCrit++;
    if (!mbkwv_lookup("wkeSetHandle"))      missingCrit++;
    if (missingCrit) {
        diagf("edgewebview 后端缺少 %d 个关键实现，回退 node.dll", missingCrit);
        return 0;
    }

    g_active = 1;
    _snprintf(g_kname, sizeof(g_kname), "edgewebview(WebView2 %s)",
              g_rtVer[0] ? g_rtVer : "installed");
    g_kname[sizeof(g_kname) - 1] = 0;
    diagf("enable edgewebview kernel: WebView2 Runtime %s, slots bound %d/%d",
          g_rtVer[0] ? g_rtVer : "(installed)", bound, count);
    return 1;
}

/* ==========================================================================
 * 9. wke 实现
 * ==========================================================================*/

/* ---- 生命周期 / 版本 ---- */
unsigned mbkwv_wkeIsInitialize(void) { return 1; }
void     mbkwv_wkeFinalize(void)     { }
void     mbkwv_wkeUpdate(void)       { }
unsigned mbkwv_wkeGetVersion(void)   { return 100000; }
unsigned mbkwv_wkeGetVersionString(void)
{
    static char buf[160];
    _snprintf(buf, sizeof(buf) - 1, "MbKit edgewebview (WebView2 %s)",
              rt_detect() && g_rtVer[0] ? g_rtVer : "installed");
    buf[sizeof(buf) - 1] = 0;
    return (unsigned)(UINT_PTR)buf;
}

/* ---- 视图 ---- */
unsigned mbkwv_wkeCreateWebView(void)
{
    WV* w = new WV();
    g_liveViews[++g_nextViewId] = w;
    diagf("CreateWebView view=%p", w);
    return (unsigned)(UINT_PTR)w;
}

void mbkwv_wkeDestroyWebView(unsigned a0)
{
    WV* w = wv_of(a0);
    if (!w) return;
    fire(w, MBK_IDX_wkeOnWindowDestroy, 0);
    w->alive = 0;
    /* 视图已死：事件登记表必须清掉。否则 fire() 之后还会按旧指针回调，
       而 cbs 里的 CbRef 正是 E 侧已释放的类成员（只读验收 C6/F5）。 */
    w->cbs.clear();
    w->kv.clear();
    g_liveViews.erase(view_id(w));
    release_controller(w);
    if (w->core) { w->core->Release(); w->core = 0; }
    if (w->exec) {
        ExecState* e = (ExecState*)w->exec;
        e->magic = 0; e->wv = 0; delete e; w->exec = 0;
    }
    w->magic = 0;
    w->retired = 1;
    diagf("destroy view=%p asyncRefs=%ld", (void*)w, (long)w->asyncRefs);
    if (w->asyncRefs > 0) {
        /* CtlDone 仍可能在 WebView2 消息队列中。保留一个墓碑对象，
           让回调安全地完成并释放最后一份引用。 */
        return;
    }
    diagf("view freed view=%p (no pending lease)", (void*)w);
    delete w;
}

static void clip_children(HWND host)
{
    LONG style;
    if (!host || !IsWindow(host)) return;
    style = GetWindowLongW(host, GWL_STYLE);
    if (style & WS_CLIPCHILDREN) return;
    /* 模块的 WM_PAINT 会 BitBlt 整块客户区。没有 CLIPCHILDREN 时，
     * 这次绘制会盖住 WebView2 的子窗口，页面看起来就是没初始化的黑屏。 */
    SetWindowLongW(host, GWL_STYLE, style | WS_CLIPCHILDREN);
    SetWindowPos(host, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    diagf("host %p add WS_CLIPCHILDREN", host);
}

void mbkwv_wkeSetHandle(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (!w) return;
    if (!a1) {
        /* Restore the original host procedure before forgetting the HWND. */
        release_controller(w);
        w->host = NULL;
        w->pendingNav.clear();
        w->cbs.clear();
        return;
    }
    if (w->host && w->host != (HWND)(UINT_PTR)a1) unhook_host(w);
    w->host = (HWND)(UINT_PTR)a1;
    diagf("SetHandle view=%p host=%p valid=%d", w, w->host, (int)IsWindow(w->host));
    if (a1) {
        clip_children(w->host);
        hook_host(w);
        if (ensure_ctl(w)) {
            if (w->ctl) apply_state(w);
        }
    } else if (w->ctl) {
        /* 传 0 是取消绑定。不能 put_ParentWindow(NULL)，否则网页会变成关不掉的顶层窗口。 */
        release_controller(w);
    }
}

void mbkwv_wkeSetHandleOffset(unsigned a0, unsigned a1, unsigned a2)
{ (void)a0; (void)a1; (void)a2; }      /* WebView2 恒相对父窗口客户�?(0,0) */

unsigned mbkwv_wkeGetHostHWND(unsigned a0)
{
    WV* w = wv_of(a0);
    return w ? (unsigned)(UINT_PTR)(w->host ? w->host : hidden_host()) : 0;
}
unsigned mbkwv_wkeGetWindowHandle(unsigned a0) { return mbkwv_wkeGetHostHWND(a0); }

void mbkwv_wkeResize(unsigned a0, unsigned a1, unsigned a2)
{
    WV* w = wv_of(a0);
    if (!w) return;
    w->width  = (int)(int)a1;
    w->height = (int)(int)a2;
    if (ensure_ctl(w) && w->ctl) {
        apply_state(w);
    }
}

unsigned mbkwv_wkeGetWidth(unsigned a0)       { WV* w = wv_of(a0); return w ? (unsigned)w->width  : 0; }
unsigned mbkwv_wkeGetHeight(unsigned a0)      { WV* w = wv_of(a0); return w ? (unsigned)w->height : 0; }
unsigned mbkwv_wkeGetContentWidth(unsigned a0)  { return mbkwv_wkeGetWidth(a0); }
unsigned mbkwv_wkeGetContentHeight(unsigned a0) { return mbkwv_wkeGetHeight(a0); }

void mbkwv_wkeRepaintIfNeeded(unsigned a0) { (void)a0; }   /* WebView2 自己�?*/
unsigned mbkwv_wkeGetViewDC(unsigned a0)   { (void)a0; return 0; }

void mbkwv_wkeSetZoomFactor(unsigned a0, float a1)
{
    WV* w = wv_of(a0);
    if (!w) return;
    w->zoom = (double)a1;
    if (w->ctl) w->ctl->put_ZoomFactor(w->zoom);
}
float mbkwv_wkeGetZoomFactor(unsigned a0)
{
    WV* w = wv_of(a0);
    double d = 1.0;
    if (w && w->ctl && SUCCEEDED(w->ctl->get_ZoomFactor(&d))) return (float)d;
    return w ? (float)w->zoom : 1.0f;
}

void mbkwv_wkeSetFocus(unsigned a0)
{
    WV* w = wv_of(a0);
    if (w && w->ctl) w->ctl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}
void mbkwv_wkeKillFocus(unsigned a0)
{
    WV* w = wv_of(a0);
    if (w && w->ctl) w->ctl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void mbkwv_wkeSetTransparent(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (!w) return;
    w->transparent = a1 ? 1 : 0;
    apply_state(w);
}
void mbkwv_wkeSetEditable(unsigned a0, unsigned a1)
{ WV* w = wv_of(a0); if (w) w->editable = a1 ? 1 : 0; }

void mbkwv_wkeSetName(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
unsigned mbkwv_wkeGetName(unsigned a0) { (void)a0; return (unsigned)(UINT_PTR)""; }

unsigned mbkwv_wkeIsAwake(unsigned a0) { (void)a0; return 1; }
void     mbkwv_wkeSleep(unsigned a0)   { WV* w = wv_of(a0);
    if (w && w->ctl) { w->slept = 1; w->visState = 0; w->ctl->put_IsVisible(FALSE); } }
void     mbkwv_wkeWake(unsigned a0)    { WV* w = wv_of(a0);
    if (w && w->ctl) { w->slept = 0; w->visState = -1; sync_vis(w); } }

void mbkwv_wkeSetHeadlessEnabled(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (!w) return;
    w->headless = a1 ? 1 : 0;
    if (w->ctl) { w->visState = -1; sync_vis(w); }
}
void mbkwv_wkeLayoutIfNeeded(unsigned a0) { (void)a0; }

void mbkwv_wkePaint(unsigned a0, unsigned a1, unsigned a2)  { (void)a0; (void)a1; (void)a2; }
void mbkwv_wkePaint2(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4,
                     unsigned a5, unsigned a6, unsigned a7, unsigned a8, unsigned a9,
                     unsigned a10)
{ (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
  (void)a8; (void)a9; (void)a10; }
void mbkwv_wkeAddDirtyArea(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4)
{ (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; }
unsigned mbkwv_wkeIsDirty(unsigned a0) { (void)a0; return 0; }
void mbkwv_wkeSetDirty(unsigned a0, unsigned a1) { (void)a0; (void)a1; }

void mbkwv_wkeMoveToCenter(unsigned a0)
{
    WV* w = wv_of(a0);
    if (w && w->host) {
        RECT rc; MONITORINFO mi; HMONITOR hm;
        GetClientRect(w->host, &rc);
        hm = MonitorFromWindow(w->host, MONITOR_DEFAULTTONEAREST);
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hm, &mi)) {
            int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
            int x = (mi.rcWork.left + mi.rcWork.right - cw) / 2;
            int y = (mi.rcWork.top + mi.rcWork.bottom - ch) / 2;
            SetWindowPos(w->host, 0, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    }
}
void mbkwv_wkeMoveWindow(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4)
{
    WV* w = wv_of(a0);
    if (w && w->host)
        SetWindowPos(w->host, 0, (int)a1, (int)a2, (int)a3, (int)a4,
                     SWP_SHOWWINDOW);
}
void mbkwv_wkeResizeWindow(unsigned a0, unsigned a1, unsigned a2)
{
    WV* w = wv_of(a0);
    if (w && w->host) SetWindowPos(w->host, 0, 0, 0, (int)a1, (int)a2, SWP_NOMOVE | SWP_NOZORDER);
}
void mbkwv_wkeShowWindow(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (w && w->host) ShowWindow(w->host, a1 ? SW_SHOW : SW_HIDE);
}
void mbkwv_wkeEnableWindow(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (w && w->host) EnableWindow(w->host, a1 ? TRUE : FALSE);
}
void mbkwv_wkeSetWindowTitleW(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (w && w->host && a1) SetWindowTextW(w->host, (const wchar_t*)(UINT_PTR)a1);
}

/* ---- 导航 ---- */
static void navigate(WV* w, const std::wstring& uriIn, int isHtml)
{
    std::wstring uri = uriIn;
    if (!w) { diagf("navigate: 视图对象为空"); return; }
    if (isHtml) uri = with_file_bridge(uri);
    if (isHtml)
        diagf("navigate: view=%p core=%p html=1 chars=%u", (void*)w, (void*)w->core,
              (unsigned)uri.size());
    else
        diagf("navigate: view=%p core=%p url=%s", (void*)w, (void*)w->core,
              w2utf8(uri.c_str(), -1).c_str());
    if (!w->core) {                       /* 控制器还没起来：排队 */
        w->pendingNav = uri;
        w->pendingIsHtml = isHtml;
        diagf("navigate: 控制器未就绪 → 排队（ctlPending=%d）", w->ctlPending);
        ensure_ctl(w);
        return;
    }
    w->loading = 1; w->completed = 0; w->failed = 0;
    if (isHtml) {
        w->readyEligible = 1;
        HRESULT hr = w->core->NavigateToString(uri.c_str());
        diagf("NavigateToString hr=0x%08lX chars=%u", (unsigned long)hr, (unsigned)uri.size());
    } else {
        HRESULT hr = w->core->Navigate(uri.c_str());
        diagf("Navigate hr=0x%08lX", (unsigned long)hr);
    }
}

void mbkwv_wkeLoadURLW(unsigned a0, unsigned a1)
{ WV* w = wv_of(a0); if (w && a1) navigate(w, (const wchar_t*)(UINT_PTR)a1, 0); }

void mbkwv_wkeLoadURL(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (!w) { diagf("wkeLoadURL: 视图句柄 0x%08X 无效", a0); return; }
    if (!a1) { diagf("wkeLoadURL: URL 为空"); return; }
    navigate(w, ansiw((const char*)(UINT_PTR)a1), 0);
}

void mbkwv_wkeLoadHTMLW(unsigned a0, unsigned a1)
{ WV* w = wv_of(a0); if (w && a1) navigate(w, (const wchar_t*)(UINT_PTR)a1, 1); }

void mbkwv_wkeLoadHTML(unsigned a0, unsigned a1)
{ WV* w = wv_of(a0); if (w && a1) navigate(w, utf8w((const char*)(UINT_PTR)a1), 1); }

void mbkwv_wkeLoadHtmlWithBaseUrl(unsigned a0, unsigned a1, unsigned a2)
{
    /* WebView2 没有 baseUrl 通道：把 <base> 注进头部，效果等�?*/
    WV* w = wv_of(a0);
    if (!w || !a1) return;
    if (a2) {
        std::wstring html = utf8w((const char*)(UINT_PTR)a1);
        std::wstring base  = utf8w((const char*)(UINT_PTR)a2);
        std::wstring head  = L"<base href=\"" + base + L"\">";
        const std::wstring tag = L"<head>";
        size_t p = html.find(tag);
        if (p != std::wstring::npos) html.insert(p + tag.size(), head);
        else html = head + html;
        navigate(w, html, 1);
    } else {
        navigate(w, utf8w((const char*)(UINT_PTR)a1), 1);
    }
}

static std::wstring read_utf8_file(const wchar_t* path)
{
    FILE* f;
    long n;
    std::string bytes;
    if (!path) return std::wstring();
    f = _wfopen(path, L"rb");
    if (!f) return std::wstring();
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return std::wstring(); }
    n = ftell(f);
    if (n <= 0 || n > 8 * 1024 * 1024) { fclose(f); return std::wstring(); }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return std::wstring(); }
    bytes.resize((size_t)n);
    if (fread(&bytes[0], 1, (size_t)n, f) != (size_t)n) { fclose(f); return std::wstring(); }
    fclose(f);
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        bytes.erase(0, 3);
    return utf8w(bytes.c_str());
}

void mbkwv_wkeLoadFileW(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (!w || !a1) return;
    {
        std::wstring p = (const wchar_t*)(UINT_PTR)a1;
        wchar_t full_early[1024];
        std::wstring html;
        if (!GetFullPathNameW(p.c_str(), 1024, full_early, NULL))
            lstrcpynW(full_early, p.c_str(), 1024);
        /* 通过固定虚拟 URL 提供文件内容。WebView2 的 NavigateToString URL
         * 永远是 about:blank，AILI 页面和资源桥需要稳定的非空 origin。 */
        html = read_utf8_file(full_early);
        if (!html.empty()) {
            w->virtualHtml = html;
            diagf("LoadFileVirtual bytes=%u", (unsigned)html.size());
            navigate(w, L"https://mbkit.local/page.html", 0);
            return;
        }
        diagf("LoadFileString failed, fallback uri");
        wchar_t full[1024];
        if (!GetFullPathNameW(p.c_str(), 1024, full, NULL))
            lstrcpynW(full, p.c_str(), 1024);
        /* Navigate requires an escaped URI. Passing the raw Chinese path leaves
           WebView2 on about:blank, while the legacy engine accepted it. */
        std::string bytes = w2utf8(full, -1), uri = "file:///";
        static const char* hex = "0123456789ABCDEF";
        for (size_t i = 0; i < bytes.size(); ++i) {
            unsigned char c = (unsigned char)bytes[i];
            if (c == '\\') c = '/';
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '/' || c == ':' || c == '-' ||
                c == '_' || c == '.' || c == '~') uri += (char)c;
            else { uri += '%'; uri += hex[c >> 4]; uri += hex[c & 15]; }
        }
        diagf("LoadFile %s", uri.c_str());
        navigate(w, utf8w(uri.c_str()), 0);
    }
}

void mbkwv_wkeLoadW(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (!w || !a1) return;
    {
        std::wstring s = (const wchar_t*)(UINT_PTR)a1;
        int looksUrl = 0;
        if (s.find(L"://") != std::wstring::npos) looksUrl = 1;
        else if (s.find(L"about:") == 0 || s.find(L"About:") == 0) looksUrl = 1;
        else if (s.find(L"data:") == 0 || s.find(L"Data:") == 0) looksUrl = 1;
        else if (s.find(L"file:") == 0 || s.find(L"File:") == 0) looksUrl = 1;
        else if (s.find(L'<') != std::wstring::npos) looksUrl = 0;
        else looksUrl = 1;
        navigate(w, s, looksUrl ? 0 : 1);
    }
}

void mbkwv_wkeStopLoading(unsigned a0)
{ WV* w = wv_of(a0); if (w && w->core) w->core->Stop(); }
void mbkwv_wkeReload(unsigned a0)
{ WV* w = wv_of(a0); if (w && w->core) w->core->Reload(); }

unsigned mbkwv_wkeGetURL(unsigned a0)
{
    WV* w = wv_of(a0);
    static std::string buf;
    if (!w) return 0;
    if (w->core) { BSTR b = 0;
        if (SUCCEEDED(w->core->get_Source(&b)) && b) { w->url = bstrw(b); CoTaskMemFree(b); } }
    buf = w2utf8(w->url.c_str(), -1);
    return (unsigned)(UINT_PTR)buf.c_str();
}

unsigned mbkwv_wkeGetTitleW(unsigned a0)
{
    WV* w = wv_of(a0);
    if (!w) return 0;
    if (w->core) { BSTR b = 0;
        if (SUCCEEDED(w->core->get_DocumentTitle(&b)) && b) { w->title = bstrw(b); CoTaskMemFree(b); } }
    return (unsigned)(UINT_PTR)w->title.c_str();
}

unsigned mbkwv_wkeGetTitle(unsigned a0)
{
    WV* w = wv_of(a0);
    static std::string buf;
    if (!w) return 0;
    if (w->core) { BSTR b = 0;
        if (SUCCEEDED(w->core->get_DocumentTitle(&b)) && b) { w->title = bstrw(b); CoTaskMemFree(b); } }
    buf = w2utf8(w->title.c_str(), -1);
    return (unsigned)(UINT_PTR)buf.c_str();
}

unsigned mbkwv_wkeIsLoading(unsigned a0)
{ WV* w = wv_of(a0); return (w && (w->loading || !w->completed)) ? 1 : 0; }
unsigned mbkwv_wkeIsLoadingCompleted(unsigned a0)
{ WV* w = wv_of(a0); return (w && w->completed) ? 1 : 0; }
unsigned mbkwv_wkeIsLoadingFailed(unsigned a0)
{ WV* w = wv_of(a0); return (w && w->failed) ? 1 : 0; }
unsigned mbkwv_wkeIsLoadingSucceeded(unsigned a0)
{ WV* w = wv_of(a0); return (w && w->completed) ? 1 : 0; }
unsigned mbkwv_wkeIsDocumentReady(unsigned a0)
{ WV* w = wv_of(a0); return (w && w->completed) ? 1 : 0; }

unsigned mbkwv_wkeCanGoBack(unsigned a0)
{ WV* w = wv_of(a0); BOOL b = FALSE;
  if (w && w->core && SUCCEEDED(w->core->get_CanGoBack(&b))) return b ? 1 : 0; return 0; }
unsigned mbkwv_wkeGoBack(unsigned a0)
{ WV* w = wv_of(a0); if (w && w->core) { w->core->GoBack(); return 1; } return 0; }
unsigned mbkwv_wkeCanGoForward(unsigned a0)
{ WV* w = wv_of(a0); BOOL b = FALSE;
  if (w && w->core && SUCCEEDED(w->core->get_CanGoForward(&b))) return b ? 1 : 0; return 0; }
unsigned mbkwv_wkeGoForward(unsigned a0)
{ WV* w = wv_of(a0); if (w && w->core) { w->core->GoForward(); return 1; } return 0; }

/* 运行 JS：同步等 ExecuteScript 的结果，期间泵消息�? * 返回�?= 指向�?view 缓冲区的指针（jsToStringW 也能按句柄读回）�?*/
static long long run_js(WV* w, const std::wstring& js)
{
    HANDLE evt;
    ExecDone* h;
    if (!w) return 0;
    if (!ensure_ctl(w) || !w->core) return 0;
    w->jsOut.clear();
    w->jsOutW.clear();
    h = new ExecDone();
    evt = h->evt;
    if (!evt) { h->Release(); return 0; }
    if (FAILED(w->core->ExecuteScript(js.c_str(), h))) {
        h->Release();
        return 0;
    }
    if (!wv_wait(evt, 15000)) diagf("ExecuteScript 等待超时");
    w->jsOut = h->res;
    h->Release();

    /* ExecuteScript 的结果是 JSON：字符串带引号，去掉好读一�?*/
    if (w->jsOut.size() >= 2 && w->jsOut[0] == '"' && w->jsOut[w->jsOut.size() - 1] == '"') {
        std::string t = w->jsOut.substr(1, w->jsOut.size() - 2);
        std::string r; size_t i;
        for (i = 0; i < t.size(); i++) {
            if (t[i] == '\\' && i + 1 < t.size()) {
                i++;
                switch (t[i]) {
                case 'n': r += '\n'; break;
                case 'r': r += '\r'; break;
                case 't': r += '\t'; break;
                case 'u': {
                    unsigned v = 0; int k;
                    for (k = 0; k < 4 && i + 1 < t.size(); k++) {
                        char c = t[++i];
                        v = v * 16 + (unsigned)(c >= '0' && c <= '9' ? c - '0'
                                 : (c >= 'a' && c <= 'f' ? c - 'a' + 10
                                 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0)));
                    }
                    if (v < 0x80) r += (char)v;
                    else if (v < 0x800) {
                        r += (char)(0xC0 | (v >> 6));
                        r += (char)(0x80 | (v & 0x3F));
                    } else {
                        r += (char)(0xE0 | (v >> 12));
                        r += (char)(0x80 | ((v >> 6) & 0x3F));
                        r += (char)(0x80 | (v & 0x3F));
                    }
                    break;
                }
                default: r += t[i]; break;
                }
            } else r += t[i];
        }
        w->jsOut = r;
    }
    w->jsOutW = utf8w(w->jsOut.c_str());

    {
        Jsv* j = jsv_new(5);
        j->u8 = w->jsOut;
        j->ws = w->jsOutW;
        return (long long)(UINT_PTR)j;
    }
}

long long mbkwv_wkeRunJS(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (!w || !a1) return 0;
    return run_js(w, utf8w((const char*)(UINT_PTR)a1));
}
long long mbkwv_wkeRunJSW(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (!w || !a1) return 0;
    return run_js(w, (const wchar_t*)(UINT_PTR)a1);
}

unsigned mbkwv_wkeGetSource(unsigned a0)
{
    WV* w = wv_of(a0);
    static std::string buf;
    if (!w) return 0;
    {
        long long r = run_js(w, L"document.documentElement.outerHTML");
        (void)r;
        buf = w->jsOut;
    }
    return (unsigned)(UINT_PTR)buf.c_str();
}

/* ---- wkeString ---- */
unsigned mbkwv_wkeCreateStringW(unsigned a0, unsigned a1)
{
    const wchar_t* s = (const wchar_t*)(UINT_PTR)a0;
    int n = (int)(unsigned)a1;
    if (s && n > 0) {
        int cch = n / 2;
        /* 模块传的�?lstrlenW×2（不含结�?0），补一个保�?*/
        int real = 0;
        while (real < cch && s[real]) real++;
        return (unsigned)(UINT_PTR)str_new(s, real);
    }
    if (s) return (unsigned)(UINT_PTR)str_new(s, -1);
    return (unsigned)(UINT_PTR)str_new(0, 0);
}

void mbkwv_wkeDeleteString(unsigned a0)
{
    WStr* p = str_of(a0);
    if (!p) return;
    p->magic = 0;
    delete p;
}

unsigned mbkwv_wkeGetStringW(unsigned a0)
{
    WStr* p = str_of(a0);
    return p ? (unsigned)(UINT_PTR)p->ws.c_str() : 0;
}

unsigned mbkwv_wkeGetString(unsigned a0)
{
    WStr* p = str_of(a0);
    if (!p) return 0;
    if (!p->u8Valid) { p->u8 = w2utf8(p->ws.c_str(), (int)p->ws.size()); p->u8Valid = 1; }
    return (unsigned)(UINT_PTR)p->u8.c_str();
}

void mbkwv_wkeSetStringW(unsigned a0, unsigned a1, unsigned a2)
{
    WStr* p = str_of(a0);
    const wchar_t* s = (const wchar_t*)(UINT_PTR)a1;
    if (!p || !s) return;
    p->ws.assign(s, (size_t)(unsigned)a2);
    p->u8Valid = 0;
}

/* ---- 用户键�?---- */
void mbkwv_wkeSetUserKeyValue(unsigned a0, unsigned a1, unsigned a2)
{
    WV* w = wv_of(a0);
    if (!w || !a1) return;
    w->kv[ansiw((const char*)(UINT_PTR)a1)] = (void*)(UINT_PTR)a2;
}
unsigned mbkwv_wkeGetUserKeyValue(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    std::map<std::wstring, void*>::iterator it;
    if (!w || !a1) return 0;
    it = w->kv.find(ansiw((const char*)(UINT_PTR)a1));
    return (it == w->kv.end()) ? 0 : (unsigned)(UINT_PTR)it->second;
}

/* ---- 回调登记 ---- */
#define DEF_ON(name)                                                        \
void mbkwv_##name(unsigned a0, unsigned a1, unsigned a2)                    \
{                                                                           \
    WV* w = wv_of(a0);                                                      \
    diagf("cb %s wv=%08X found=%d fn=%08X param=%08X", #name, (unsigned)a0,  \
          (int)(w != 0), (unsigned)a1, (unsigned)a2);                       \
    if (!w) return;                                                         \
    if (!a1) { w->cbs.erase(MBK_IDX_##name);                                \
               diagf("cb erased %s idx=%d", #name, MBK_IDX_##name); return; }\
    { CbRef r; r.fn = (void*)(UINT_PTR)a1; r.param = (void*)(UINT_PTR)a2;   \
      w->cbs[MBK_IDX_##name] = r; }                                         \
    diagf("cb set %s idx=%d total=%u", #name, MBK_IDX_##name,               \
          (unsigned)w->cbs.size());                                         \
}

DEF_ON(wkeOnAlertBox)
DEF_ON(wkeOnConfirmBox)
DEF_ON(wkeOnConsole)
DEF_ON(wkeOnCreateView)
DEF_ON(wkeOnDidCreateScriptContext)
DEF_ON(wkeOnDocumentReady)
DEF_ON(wkeOnDocumentReady2)
DEF_ON(wkeOnDownload)
DEF_ON(wkeOnDownloadFinish)
DEF_ON(wkeOnDownloadProgress)
DEF_ON(wkeOnDownloadStart)
DEF_ON(wkeOnLoadUrlBegin)
DEF_ON(wkeOnLoadUrlEnd)
DEF_ON(wkeOnLoadingFinish)
DEF_ON(wkeOnMouseOverUrlChanged)
DEF_ON(wkeOnNavigation)
DEF_ON(wkeOnPaintUpdated)
DEF_ON(wkeOnPromptBox)
DEF_ON(wkeOnTitleChanged)
DEF_ON(wkeOnURLChanged)
DEF_ON(wkeOnURLChanged2)
DEF_ON(wkeOnWillMediaLoad)
DEF_ON(wkeOnWillReleaseScriptContext)
DEF_ON(wkeOnWindowClosing)
DEF_ON(wkeOnWindowDestroy)
DEF_ON(wkeNetOnResponse)

/* ---- UA / 配置 / 开�?---- */
/* UserAgent 在这�?SDK 里位�?ICoreWebView2Settings2（不是基�?Settings）�?*/
static void set_ua(WV* w, const std::wstring& ua)
{
    ICoreWebView2Settings* st = 0;
    ICoreWebView2Settings2* st2 = 0;
    if (!w || !w->core) return;
    if (SUCCEEDED(w->core->get_Settings(&st)) && st) {
        if (SUCCEEDED(st->QueryInterface(__uuidof(ICoreWebView2Settings2), (void**)&st2)) && st2) {
            st2->put_UserAgent(ua.c_str());
            st2->Release();
        }
        st->Release();
    }
}
void mbkwv_wkeSetUserAgentW(unsigned a0, unsigned a1)
{ WV* w = wv_of(a0); if (w && a1) set_ua(w, (const wchar_t*)(UINT_PTR)a1); }
void mbkwv_wkeSetUserAgent(unsigned a0, unsigned a1)
{ WV* w = wv_of(a0); if (w && a1) set_ua(w, ansiw((const char*)(UINT_PTR)a1)); }
void mbkwv_wkeSetDownloadDir(unsigned a0, unsigned a1)
{
    WV* w = wv_of(a0);
    if (!w) {
        /* 视图号无效（多半是 WebView2 异步启动还没就绪时就被调用）。
         * 早期这里静默 return，导致上层「置下载目录」看起来成功、实际全丢。 */
        diagf("wkeSetDownloadDir: 视图无效 view=0x%08lX dir=%s —— 目录未生效",
              (unsigned long)a0, a1 ? w2utf8(ansiw((const char*)(UINT_PTR)a1).c_str(), -1).c_str() : "");
        return;
    }
    w->dlDir = a1 ? ansiw((const char*)(UINT_PTR)a1) : L"";
    if (!w->dlDir.empty() && !ensure_dir_w(w->dlDir))
        diagf("wkeSetDownloadDir: 目录不可用，下载将回落到 Edge 默认目录");
    diagf("wkeSetDownloadDir -> %s", w2utf8(w->dlDir.c_str(), -1).c_str());
    /* 控制器已就绪（上层可能在视图起来之后才设目录）：立刻 apply，别等导航，
     * 否则同一个坑——DownloadStarting 不抛、文件不落。 */
    if (!w->dlDir.empty() && w->core) apply_profile_dl_dir(w);
}
unsigned mbkwv_wkeGetUserAgent(unsigned a0)
{
    static std::wstring buf;
    WV* w = wv_of(a0);
    ICoreWebView2Settings* st = 0;
    ICoreWebView2Settings2* st2 = 0;
    if (!w || !w->core) return (unsigned)(UINT_PTR)L"";
    if (SUCCEEDED(w->core->get_Settings(&st)) && st) {
        if (SUCCEEDED(st->QueryInterface(__uuidof(ICoreWebView2Settings2), (void**)&st2)) && st2) {
            BSTR b = 0;
            st2->get_UserAgent(&b);
            if (b) { buf = bstrw(b); CoTaskMemFree(b); }
            st2->Release();
        }
        st->Release();
    }
    return (unsigned)(UINT_PTR)buf.c_str();
}

void mbkwv_wkeSetDebugConfig(unsigned a0, unsigned a1, unsigned a2)
{ (void)a0; (void)a1; (void)a2; }     /* 诊断开关：WebView2 侧由 DevTools 覆盖 */
void mbkwv_wkeSetCookieJarPath(unsigned a0, unsigned a1)  { (void)a0; (void)a1; }
void mbkwv_wkeSetCookieJarFullPath(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetLocalStorageFullPath(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetProxy(unsigned a0) { (void)a0; }          /* 进程级：需启动参数，暂不支�?*/
void mbkwv_wkeSetViewProxy(unsigned a0, unsigned a1)
{ (void)a0; (void)a1; }
void mbkwv_wkeSetCspCheckEnable(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetViewNetInterface(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetNavigationToNewWindowEnable(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetMouseEnabled(unsigned a0, unsigned a1)
{ (void)a0; (void)a1; }              /* WebView2 没有整体关鼠标的开�?*/
void mbkwv_wkeSetTouchEnabled(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetDragEnable(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetDragDropEnable(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetMemoryCacheEnable(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetNpapiPluginsEnabled(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetResourceGc(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetDeviceParameter(unsigned a0, unsigned a1, unsigned a2,
                                 unsigned a3, float a4)
{ (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; }
void mbkwv_wkeAddPluginDirectory(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeSetMediaVolume(unsigned a0, float a1) { (void)a0; (void)a1; }
float mbkwv_wkeGetMediaVolume(unsigned a0) { (void)a0; return 1.0f; }
void mbkwv_wkeSetFileSystem(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4)
{ (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; }
void mbkwv_wkeGC(unsigned a0, unsigned a1) { (void)a0; (void)a1; }

/* ---- 输入事件：WebView2 自己接收真实输入，这些转发只保证语义安全 ---- */
unsigned mbkwv_wkeFireKeyDownEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{ (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
unsigned mbkwv_wkeFireKeyUpEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{ (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
unsigned mbkwv_wkeFireKeyPressEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{ (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
unsigned mbkwv_wkeFireMouseEvent(unsigned a0, unsigned a1, unsigned a2,
                                 unsigned a3, unsigned a4)
{ (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; return 0; }
unsigned mbkwv_wkeFireContextMenuEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{ (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
unsigned mbkwv_wkeFireMouseWheelEvent(unsigned a0, unsigned a1, unsigned a2,
                                      unsigned a3, unsigned a4)
{ (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; return 0; }
unsigned mbkwv_wkeFireWindowsMessage(unsigned a0, unsigned a1, unsigned a2,
                                     unsigned a3, unsigned a4, unsigned a5)
{ (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; return 0; }
void mbkwv_wkeGetCaretRect(unsigned a0, unsigned a1)
{
    /* a0 = wkeRect*（出参），a1 = webview */
    struct WVRect { int x, y, w, h; }* r = (struct WVRect*)(UINT_PTR)a0;
    (void)a1;
    if (r) { r->x = 0; r->y = 0; r->w = 0; r->h = 0; }
}

/* ---- 编辑器（�?execCommand，WebView2 无原生通道�?--- */
static void exec_cmd(WV* w, const wchar_t* js)
{
    if (!w || !w->core) return;
    { ExecDone* h = new ExecDone();
      if (h->evt && SUCCEEDED(w->core->ExecuteScript(js, h))) wv_wait(h->evt, 3000);
      h->Release(); }
}
void mbkwv_wkeEditorSelectAll(unsigned a0) { exec_cmd(wv_of(a0), L"document.execCommand('selectall')"); }
void mbkwv_wkeEditorUnSelect(unsigned a0)  { exec_cmd(wv_of(a0), L"window.getSelection().removeAllRanges()"); }
void mbkwv_wkeEditorCopy(unsigned a0)      { exec_cmd(wv_of(a0), L"document.execCommand('copy')"); }
void mbkwv_wkeEditorCut(unsigned a0)       { exec_cmd(wv_of(a0), L"document.execCommand('cut')"); }
void mbkwv_wkeEditorPaste(unsigned a0)     { exec_cmd(wv_of(a0), L"document.execCommand('paste')"); }
void mbkwv_wkeEditorDelete(unsigned a0)    { exec_cmd(wv_of(a0), L"document.execCommand('delete')"); }
void mbkwv_wkeEditorUndo(unsigned a0)      { exec_cmd(wv_of(a0), L"document.execCommand('undo')"); }
void mbkwv_wkeEditorRedo(unsigned a0)      { exec_cmd(wv_of(a0), L"document.execCommand('redo')"); }

/* ---- 框架 / 执行�?---- */
unsigned mbkwv_wkeGlobalExec(unsigned a0)
{
    WV* w = wv_of(a0);
    ExecState* e;
    if (!w) return 0;
    if (!w->exec) {
        e = new ExecState();
        e->magic = WV_EXEC_MAGIC; e->wv = w;
        w->exec = e;
    }
    return (unsigned)(UINT_PTR)w->exec;
}
unsigned mbkwv_wkeGetGlobalExecByFrame(unsigned a0, unsigned a1)
{ (void)a1; return mbkwv_wkeGlobalExec(a0); }
unsigned mbkwv_wkeWebFrameGetMainFrame(unsigned a0) { (void)a0; return 1; }
unsigned mbkwv_wkeIsMainFrame(unsigned a0, unsigned a1) { (void)a0; (void)a1; return 1; }
unsigned mbkwv_wkeIsWebRemoteFrame(unsigned a0, unsigned a1) { (void)a0; (void)a1; return 0; }
unsigned mbkwv_wkeGetFrameUrl(unsigned a0, unsigned a1)
{ (void)a1; return mbkwv_wkeGetURL(a0); }
void mbkwv_wkeInsertCSSByFrame(unsigned a0, unsigned a1, unsigned a2)
{
    WV* w = wv_of(a0);
    if (!w || !w->core || !a2) return;
    {
        std::wstring css = (const wchar_t*)(UINT_PTR)a2;
        std::wstring js = L"(function(){var s=document.createElement('style');"
                          L"s.textContent=`" + css + L"`;document.head.appendChild(s);})()";
        exec_cmd(w, js.c_str());
    }
    (void)a1;
}
unsigned mbkwv_wkeGetCursorInfoType(unsigned a0) { (void)a0; return 1; }  /* 1 = 普通箭�?*/

unsigned mbkwv_wkeUtilSerializeToMHTML(unsigned a0) { (void)a0; return 0; }
unsigned mbkwv_wkeIsProcessingUserGesture(unsigned a0) { (void)a0; return 1; }
void mbkwv_wkePerformCookieCommand(unsigned a0, unsigned a1) { (void)a0; (void)a1; }
void mbkwv_wkeVisitAllCookie(unsigned a0, unsigned a1, unsigned a2)
{ (void)a0; (void)a1; (void)a2; }     /* �?WebResourceResponseReceived 通道 */
void mbkwv_wkeShowDevtools(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{ (void)a0; (void)a1; (void)a2; (void)a3; }   /* 需 OpenDevToolsWindow，留待后�?*/
unsigned mbkwv_wkeGetWebView(unsigned a0) { (void)a0; return 0; }
unsigned mbkwv_wkeGetWebViewForCurrentContext(void) { return 0; }
void mbkwv_wkeWebFrameGetMainWorldScriptContext(unsigned a0, unsigned a1, unsigned a2)
{ (void)a0; (void)a1; (void)a2; }     /* V8 上下文在 WebView2 里拿不到 */

/* ---- Cookie ---- */
unsigned mbkwv_wkeGetCookie(unsigned a0) { (void)a0; return (unsigned)(UINT_PTR)""; }
unsigned mbkwv_wkeGetCookieW(unsigned a0) { (void)a0; return (unsigned)(UINT_PTR)L""; }
void mbkwv_wkeSetCookie(unsigned a0, unsigned a1, unsigned a2)
{ (void)a0; (void)a1; (void)a2; }     /* �?ICoreWebView2CookieManager，暂�?TODO */
unsigned mbkwv_wkeIsCookieEnabled(unsigned a0) { (void)a0; return 1; }
void mbkwv_wkeSetCookieEnabled(unsigned a0, unsigned a1) { (void)a0; (void)a1; }

/* ==========================================================================
 * 10. jsValue 的最小实现（让「运行JS �?jsToStringW」这条链�?edge 下也通）
 *
 * 【ABI 硬约束】thunk �?gen_thunk.c 的强制转型调用本层：
 *   - 返回 long long 的（jsUndefined/jsInt/jsStringW/jsEvalW/...）必须真�? *     返回 long long —�?32 �?cdecl 下是 EDX:EAX，写�?unsigned 会让高位脏；
 *   - 参数�?long long 的（jsToStringW �?a1、jsIsString �?a0）在栈上�? *     **两个�?*，少写一个就是栈错位�? * ==========================================================================*/
long long mbkwv_jsUndefined(void)    { static Jsv* p = 0; if (!p) { p = new Jsv(); p->kind = 0; }
                                        return (long long)(UINT_PTR)p; }
long long mbkwv_jsNull(void)         { static Jsv* p = 0; if (!p) { p = new Jsv(); p->kind = 1; }
                                        return (long long)(UINT_PTR)p; }
long long mbkwv_jsTrue(void)         { static Jsv* p = 0; if (!p) { p = new Jsv(); p->kind = 2; p->b = 1; p->d = 1; p->u8 = "true"; p->ws = L"true"; }
                                        return (long long)(UINT_PTR)p; }
long long mbkwv_jsFalse(void)        { static Jsv* p = 0; if (!p) { p = new Jsv(); p->kind = 2; p->u8 = "false"; p->ws = L"false"; }
                                        return (long long)(UINT_PTR)p; }
long long mbkwv_jsInt(unsigned a0)   { char b[32]; Jsv* p = jsv_new(3);
                                        p->d = (double)(int)a0;
                                        _snprintf(b, sizeof(b), "%d", (int)a0); p->u8 = b; p->ws = utf8w(b);
                                        return (long long)(UINT_PTR)p; }
long long mbkwv_jsStringW(unsigned a0, unsigned a1)
{
    Jsv* p = jsv_new(5);
    const wchar_t* s = (const wchar_t*)(UINT_PTR)a1;
    if (s) { p->ws = s; p->u8 = w2utf8(s, -1); }
    (void)a0;
    return (long long)(UINT_PTR)p;
}
unsigned mbkwv_jsToStringW(unsigned a0, long long a1)
{
    Jsv* p = jsv_of((unsigned)(UINT_PTR)a1);
    (void)a0;
    return p ? (unsigned)(UINT_PTR)p->ws.c_str() : (unsigned)(UINT_PTR)L"";
}
unsigned mbkwv_jsToTempStringW(unsigned a0, long long a1) { return mbkwv_jsToStringW(a0, a1); }
long long mbkwv_wkeRunJsByFrame(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    (void)a1; (void)a3;
    return a2 ? run_js(wv_of(a0), utf8w((const char*)(UINT_PTR)a2)) : 0;
}
long long mbkwv_jsEvalW(unsigned a0, unsigned a1)
{
    ExecState* e = (ExecState*)(UINT_PTR)a0;
    if (e && e->magic == WV_EXEC_MAGIC && e->wv)
        return (long long)(UINT_PTR)run_js(e->wv, (const wchar_t*)(UINT_PTR)a1);
    return 0;
}
long long mbkwv_jsEvalExW(unsigned a0, unsigned a1, unsigned a2)
{ (void)a2; return mbkwv_jsEvalW(a0, a1); }
unsigned mbkwv_jsIsString(long long a0)      { Jsv* p = jsv_of((unsigned)(UINT_PTR)a0); return (p && p->kind == 5) ? 1 : 0; }
unsigned mbkwv_jsIsUndefined(long long a0)   { Jsv* p = jsv_of((unsigned)(UINT_PTR)a0); return (p && p->kind == 0) ? 1 : 0; }
