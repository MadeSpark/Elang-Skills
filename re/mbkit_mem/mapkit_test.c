/* ===========================================================================
 * mapkit_test.c —— 原生验证台：手工映射 MbKit.dll，再走 wke* 链路真实加载网页
 *
 * 验证链（与易语言侧将要生成的 demo 完全同构）：
 *   读文件 → mk_map（手工映射） → 现场解析导出表填槽
 *   → MbKit_Init → wkeCreateWebView → wkeSetHandle(主窗口句柄)
 *   → wkeResize → wkeShowWindow → wkeLoadURLW → 消息循环(wkeWake) → 取标题 → 截图
 *
 * 编译（32 位）：见 build_test.sh
 * =========================================================================== */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char  mk_u8;
typedef unsigned int   mk_u32;
typedef signed   int   mk_i32;
#define MKAPI_ __attribute__((stdcall))

typedef struct {
    void*  (MKAPI_ *VirtualAlloc)(void*, mk_u32, mk_u32, mk_u32);
    mk_i32 (MKAPI_ *VirtualFree)(void*, mk_u32, mk_u32);
    void*  (MKAPI_ *LoadLibraryA)(const char*);
    void*  (MKAPI_ *GetProcAddress)(void*, const char*);
    mk_u32 (MKAPI_ *TlsAlloc)(void);
    void*  (MKAPI_ *TlsGetValue)(mk_u32);
    mk_i32 (MKAPI_ *TlsSetValue)(mk_u32, void*);
    void*  (MKAPI_ *GetModuleHandleA)(const char*);
} MKAPI;

extern void mk_map(const MKAPI* A, const mk_u8* data, mk_u32 size,
                   mk_u32* pBase, mk_u32* pStatus);

static const char* ST[10] = {
    "OK", "参数非法", "DOS/PE 头异常", "不是 i386", "VirtualAlloc 失败",
    "LoadLibrary 导入库失败", "导入名异常", "GetProcAddress 导入函数失败",
    "入口点返回 0", "出现转发导出(不支持)"
};

static FILE* g_log;
static HWND  g_hwnd;
static int   g_evNo;

static void say(const char* fmt, ...)
{
    va_list ap;
    char buf[2048];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

/* ---- 现场解析映射后映像的导出表（等价 GetProcAddress 的查找算法） ---- */
static const unsigned char* g_img;
static unsigned int g_nnam;
static const unsigned int*   g_nams;
static const unsigned int*   g_funs;
static const unsigned short* g_ords;

static int exp_init(void* base)
{
    const unsigned char* hdr = (const unsigned char*)base;
    unsigned int e_lfanew = *(const unsigned int*)(hdr + 0x3C);
    const unsigned char* nt = hdr + e_lfanew;
    const unsigned char* opt = nt + 0x18;
    unsigned int ed_rva = *(const unsigned int*)(opt + 0x60);
    const unsigned char* ed = hdr + ed_rva;

    g_img  = hdr;
    g_nnam = *(const unsigned int*)(ed + 0x18);
    g_funs = (const unsigned int*)(hdr + *(const unsigned int*)(ed + 0x1C));
    g_nams = (const unsigned int*)(hdr + *(const unsigned int*)(ed + 0x20));
    g_ords = (const unsigned short*)(hdr + *(const unsigned int*)(ed + 0x24));
    return (int)g_nnam;
}

static void* exp_find(const char* name)
{
    unsigned int k;
    for (k = 0; k < g_nnam; k++) {
        const char* nm = (const char*)(g_img + g_nams[k]);
        if (strcmp(nm, name) == 0)
            return (void*)((unsigned char*)g_img + g_funs[g_ords[k]]);
    }
    return NULL;
}

/* ---- 抓窗口客户区为 BMP ---- */
static int save_bmp(HWND hwnd, const char* path)
{
    HDC hdc = GetDC(hwnd);
    RECT rc;
    int w, h, i, rowsz;
    HDC mdc;
    HBITMAP hbm;
    BITMAPINFO bi;
    unsigned char* px;
    FILE* f;
    unsigned char hdr[54];

    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) { ReleaseDC(hwnd, hdc); return 0; }

    mdc = CreateCompatibleDC(hdc);
    hbm = CreateCompatibleBitmap(hdc, w, h);
    SelectObject(mdc, hbm);
    /* ★ 必须用 PrintWindow：BitBlt 取的是「屏幕该区域」的内容，窗口被遮挡时
     *   抓到的是别的程序。PrintWindow + PW_RENDERFULLCONTENT(2) 让窗口自己渲染。 */
    if (!PrintWindow(hwnd, mdc, 2))
        BitBlt(mdc, 0, 0, w, h, hdc, 0, 0, SRCCOPY);

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    rowsz = ((w * 3 + 3) / 4) * 4;
    px = (unsigned char*)malloc((size_t)rowsz * h);
    if (!px) { DeleteObject(hbm); DeleteDC(mdc); ReleaseDC(hwnd, hdc); return 0; }
    GetDIBits(mdc, hbm, 0, h, px, &bi, DIB_RGB_COLORS);

    memset(hdr, 0, 54);
    hdr[0] = 'B'; hdr[1] = 'M';
    *(unsigned int*)(hdr + 2) = 54 + (unsigned int)rowsz * h;
    *(unsigned int*)(hdr + 10) = 54;
    *(unsigned int*)(hdr + 14) = 40;
    *(int*)(hdr + 18) = w;
    *(int*)(hdr + 22) = -h;
    *(unsigned short*)(hdr + 26) = 1;
    *(unsigned short*)(hdr + 28) = 24;
    *(unsigned int*)(hdr + 34) = (unsigned int)rowsz * h;

    f = fopen(path, "wb");
    if (f) {
        fwrite(hdr, 1, 54, f);
        for (i = 0; i < h; i++) fwrite(px + (size_t)i * rowsz, 1, rowsz, f);
        fclose(f);
    }
    free(px);
    DeleteObject(hbm);
    DeleteDC(mdc);
    ReleaseDC(hwnd, hdc);
    return f ? 1 : 0;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

/* ---- 需要的导出（顺序即下面的 p[] 下标） ---- */
static const char* NEED[] = {
    "MbKit_Init", "MbKit_IsReady", "MbKit_KernelPath", "MbKit_Diag", "MbKit_LastError",
    "wkeInitialize", "wkeIsInitialize", "wkeGetVersionString", "wkeCreateWebView",
    "wkeSetHandle", "wkeResize", "wkeShowWindow", "wkeLoadURLW", "wkeWake",
    "wkeIsLoadingCompleted", "wkeIsLoadingFailed", "wkeGetTitleW", "wkeGetURL",
    "wkeSetHandleOffset", "wkeDestroyWebView", "wkeFinalize"
};
#define N_NEED ((int)(sizeof(NEED) / sizeof(NEED[0])))
static void* P[N_NEED];

int main(int argc, char** argv)
{
    const char* dllpath = (argc > 1) ? argv[1] : "MbKit.dll";
    const wchar_t* urlw;
    char urlu8[1024];
    const char* datpath = (argc > 3) ? argv[3] : "shot.bmp";
    FILE* f;
    long fsz;
    unsigned char* buf;
    MKAPI A;
    mk_u32 base = 0, st = 99;
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    void* view = NULL;
    MSG msg;
    WNDCLASSA wc;
    int i, j;

    g_log = fopen("maptest.log", "wb");
    if (argc > 2) { MultiByteToWideChar(CP_ACP, 0, argv[2], -1, (wchar_t*)urlu8, 512); }
    else { MultiByteToWideChar(CP_ACP, 0, "https://www.madespark.cc/", -1, (wchar_t*)urlu8, 512); }
    urlw = (const wchar_t*)urlu8;

    f = fopen(dllpath, "rb");
    if (!f) { say("[X] 打不开 %s", dllpath); return 2; }
    fseek(f, 0, SEEK_END); fsz = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (unsigned char*)malloc((size_t)fsz);
    if (!buf || fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) { say("[X] 读失败"); return 2; }
    fclose(f);
    say("[i] %s : %ld 字节", dllpath, fsz);

    A.VirtualAlloc     = (void*  (MKAPI_ *)(void*, mk_u32, mk_u32, mk_u32))GetProcAddress(k32, "VirtualAlloc");
    A.VirtualFree      = (mk_i32 (MKAPI_ *)(void*, mk_u32, mk_u32))GetProcAddress(k32, "VirtualFree");
    A.LoadLibraryA     = (void*  (MKAPI_ *)(const char*))GetProcAddress(k32, "LoadLibraryA");
    A.GetProcAddress   = (void*  (MKAPI_ *)(void*, const char*))GetProcAddress(k32, "GetProcAddress");
    A.TlsAlloc         = (mk_u32 (MKAPI_ *)())GetProcAddress(k32, "TlsAlloc");
    A.TlsGetValue      = (void*  (MKAPI_ *)(mk_u32))GetProcAddress(k32, "TlsGetValue");
    A.TlsSetValue      = (mk_i32 (MKAPI_ *)(mk_u32, void*))GetProcAddress(k32, "TlsSetValue");
    A.GetModuleHandleA = (void*  (MKAPI_ *)(const char*))GetProcAddress(k32, "GetModuleHandleA");

    /* ================= 1. 手工映射 ================= */
    mk_map(&A, buf, (mk_u32)fsz, &base, &st);
    say("[1] mk_map -> base=0x%08X status=%u (%s)", base, st, ST[st < 10 ? st : 9]);
    if (!base) return 3;
    say("[1] 映像区间 0x%08X..0x%08X，导出 %d 个具名", base, base + 0x1B000, exp_init((void*)base));

    for (i = 0; i < N_NEED; i++) {
        P[i] = exp_find(NEED[i]);
        if (!P[i]) say("    [X] 找不到导出 %s", NEED[i]);
    }

    /* ================= 2. 初始化内核（node.dll 由转发层懒加载） ================= */
    say("[2] MbKit_Init(NULL) = %d", ((int (MKAPI_ *)(const char*))P[0])(NULL));
    say("[2] MbKit_IsReady()  = %d", ((int (MKAPI_ *)())P[1])());
    say("[2] MbKit_KernelPath = %s", ((const char* (MKAPI_ *)())P[2])());
    say("[2] exe 的 GetLastError = %lu", GetLastError());
    {
        const char* dg = ((const char* (MKAPI_ *)())P[3])();
        char tmp[4096];
        snprintf(tmp, sizeof(tmp) - 1, "%s", dg ? dg : "(null)");
        tmp[sizeof(tmp) - 1] = 0;
        say("[2] MbKit_Diag():\n%s", tmp);
    }
    say("[2] wkeIsInitialize()  = %d", ((int (MKAPI_ *)())P[6])());
    say("[2] wkeVersionString() = %s", ((const char* (MKAPI_ *)())P[7])());

    /* ================= 3. 主窗口 + 绑定句柄 + 载网页 ================= */
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "MbKitMemDemo";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    g_hwnd = CreateWindowA("MbKitMemDemo", "MbKit 内存加载演示 - madespark.cc",
                           WS_OVERLAPPEDWINDOW, 60, 60, 1000, 760,
                           NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) { say("[X] CreateWindow 失败 err=%lu", GetLastError()); return 4; }
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    BringWindowToTop(g_hwnd);
    UpdateWindow(g_hwnd);
    say("[3] 主窗口句柄 = 0x%08X", (unsigned int)(UINT_PTR)g_hwnd);

    view = ((void* (MKAPI_ *)())P[8])();
    say("[3] wkeCreateWebView() = 0x%08X", (unsigned int)(UINT_PTR)view);
    if (!view) { say("[X] 建视图失败"); return 5; }

    ((int (MKAPI_ *)(void*, void*))P[9])(view, g_hwnd);        /* wkeSetHandle */
    if (P[18]) ((int (MKAPI_ *)(void*, int, int))P[18])(view, 0, 0);
    ((int (MKAPI_ *)(void*, int, int))P[10])(view, 980, 700);  /* wkeResize */
    ((int (MKAPI_ *)(void*, int))P[11])(view, 1);              /* wkeShowWindow */
    ((int (MKAPI_ *)(void*, const wchar_t*))P[12])(view, urlw);
    say("[3] 已绑句柄并请求载入：%s", argv[2] ? argv[2] : "https://www.madespark.cc/");

    for (i = 0; i < 600; i++) {                                /* 最多 60 秒 */
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        ((int (MKAPI_ *)(void*))P[13])(view);                  /* wkeWake */
        if (((int (MKAPI_ *)())P[14])() || ((int (MKAPI_ *)())P[15])()) break;
        Sleep(100);
    }
    say("[3] 轮询结束（第 %d 轮）：Completed=%d Failed=%d",
        i, ((int (MKAPI_ *)())P[14])(), ((int (MKAPI_ *)())P[15])());

    for (j = 0; j < 60; j++) {                                 /* 再喂 6 秒让它渲染完 */
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        ((int (MKAPI_ *)(void*))P[13])(view);
        Sleep(100);
    }

    {
        const wchar_t* t = ((const wchar_t* (MKAPI_ *)(void*))P[16])(view);   /* wkeGetTitleW */
        char tb[512];
        MultiByteToWideChar(0, 0, "", 0, NULL, 0);
        if (t) { WideCharToMultiByte(CP_ACP, 0, t, -1, tb, sizeof(tb), NULL, NULL); say("[3] 网页标题 = %s", tb); }
        else say("[3] 标题为空");
    }
    {
        const char* u = ((const char* (MKAPI_ *)(void*))P[17])(view);         /* wkeGetURL */
        say("[3] 当前 URL = %s", u ? u : "(null)");
    }
    if (save_bmp(g_hwnd, datpath)) say("[3] 已截图 -> %s", datpath);
    else say("[3] 截图失败");

    say("[done]");
    if (g_log) fclose(g_log);
    (void)g_evNo;
    return 0;
}
