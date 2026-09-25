/* ===========================================================================
 * mapkit_test2.c —— 渲染路径聚焦探针
 *
 * 要回答的问题：
 *   R1  wkeSetHandle 之后，窗口的 WndProc 是不是被内核换掉了？（前后各取一次）
 *   R2  注册 wkeOnPaintUpdated（内部 InvalidateRect）够不够让内核把页面画出来？
 *   R3  四种截图方式（屏幕 BitBlt / 窗口DC BitBlt / PrintWindow / wkePaint）各抓到什么？
 *
 * 用法：mapkit_test2.exe <MbKit.dll> <url> <前缀>
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

static FILE* g_log;
static HWND  g_hwnd;
static int   g_paintCb;

static void say(const char* fmt, ...)
{
    va_list ap; char buf[2048];
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf) - 1, fmt, ap); va_end(ap);
    printf("%s\n", buf);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

/* ---------- 映像导出表现场解析 ---------- */
static const unsigned char* g_img;
static unsigned int g_nnam;
static const unsigned int*   g_nams;
static const unsigned int*   g_funs;
static const unsigned short* g_ords;

static void exp_init(void* base)
{
    const unsigned char* hdr = (const unsigned char*)base;
    const unsigned char* nt = hdr + *(const unsigned int*)(hdr + 0x3C);
    const unsigned char* opt = nt + 0x18;
    const unsigned char* ed = hdr + *(const unsigned int*)(opt + 0x60);
    g_img = hdr;
    g_nnam = *(const unsigned int*)(ed + 0x18);
    g_funs = (const unsigned int*)(hdr + *(const unsigned int*)(ed + 0x1C));
    g_nams = (const unsigned int*)(hdr + *(const unsigned int*)(ed + 0x20));
    g_ords = (const unsigned short*)(hdr + *(const unsigned int*)(ed + 0x24));
}

static void* exp_find(const char* name)
{
    unsigned int k;
    for (k = 0; k < g_nnam; k++)
        if (strcmp((const char*)g_img + g_nams[k], name) == 0)
            return (void*)((unsigned char*)g_img + g_funs[g_ords[k]]);
    return NULL;
}

/* ---------- 截图 ---------- */
static void save_bmp24(unsigned char* px, int w, int h, int rowsz, const char* path,
                       const char* tag)
{
    unsigned char hdr[54];
    FILE* f = fopen(path, "wb");
    int i, nonwhite = 0, samples = (w * h + 6) / 7;
    if (!f) { say("   %s: 打不开 %s", tag, path); return; }
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
    fwrite(hdr, 1, 54, f);
    for (i = 0; i < h; i++) fwrite(px + (size_t)i * rowsz, 1, rowsz, f);
    fclose(f);
    for (i = 0; i < w * h; i += 7) {
        unsigned char b = px[i * 3], g2 = px[i * 3 + 1], r = px[i * 3 + 2];
        if (!(b == 255 && g2 == 255 && r == 255)) nonwhite++;
    }
    say("   %s: %s  %dx%d  非白采样 %d/%d", tag, path, w, h, nonwhite, samples);
}

static void grab_and_save(HDC src, int sx, int sy, int w, int h, const char* path,
                          const char* tag)
{
    BITMAPINFO bi;
    HDC mdc;
    HBITMAP hbm;
    unsigned char* px;
    int rowsz;

    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) { say("   %s: 尺寸异常 %dx%d", tag, w, h); return; }
    mdc = CreateCompatibleDC(src);
    hbm = CreateCompatibleBitmap(src, w, h);
    SelectObject(mdc, hbm);
    BitBlt(mdc, 0, 0, w, h, src, sx, sy, SRCCOPY);
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 24; bi.bmiHeader.biCompression = BI_RGB;
    rowsz = ((w * 3 + 3) / 4) * 4;
    px = (unsigned char*)malloc((size_t)rowsz * h);
    GetDIBits(mdc, hbm, 0, h, px, &bi, DIB_RGB_COLORS);
    save_bmp24(px, w, h, rowsz, path, tag);
    free(px); DeleteObject(hbm); DeleteDC(mdc);
}

static void cap_screen(const char* path)
{
    HDC hdc = GetDC(NULL);
    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    grab_and_save(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                  path, "屏幕 BitBlt(整窗)");
    ReleaseDC(NULL, hdc);
}

static void cap_client(const char* path)
{
    HDC hdc = GetDC(g_hwnd);
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    grab_and_save(hdc, 0, 0, rc.right, rc.bottom, path, "窗口DC BitBlt(客户区)");
    ReleaseDC(g_hwnd, hdc);
}

static void cap_printwindow(const char* path)
{
    HDC sdc = GetDC(g_hwnd);
    HDC mdc = CreateCompatibleDC(sdc);
    RECT rc;
    HBITMAP hbm;
    BITMAPINFO bi;
    unsigned char* px;
    int w, h, rowsz;
    int ok;

    GetClientRect(g_hwnd, &rc);
    w = rc.right; h = rc.bottom;
    if (w <= 0 || h <= 0) { DeleteDC(mdc); ReleaseDC(g_hwnd, sdc); return; }
    hbm = CreateCompatibleBitmap(sdc, w, h);
    SelectObject(mdc, hbm);
    ok = PrintWindow(g_hwnd, mdc, 2);
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 24; bi.bmiHeader.biCompression = BI_RGB;
    rowsz = ((w * 3 + 3) / 4) * 4;
    px = (unsigned char*)malloc((size_t)rowsz * h);
    GetDIBits(mdc, hbm, 0, h, px, &bi, DIB_RGB_COLORS);
    save_bmp24(px, w, h, rowsz, path, ok ? "PrintWindow(成功)" : "PrintWindow(失败)");
    free(px); DeleteObject(hbm); DeleteDC(mdc); ReleaseDC(g_hwnd, sdc);
}

/* wkePaint 到内存（32 位 BGRA 或 BGRX） */
static void cap_wkepaint(void* view, void* wkePaint, void* wkeGetW, void* wkeGetH,
                         const char* path)
{
    int w = ((int (MKAPI_ *)(void*))wkeGetW)(view);
    int h = ((int (MKAPI_ *)(void*))wkeGetH)(view);
    unsigned char* buf;
    FILE* f;
    unsigned char hdr[54];
    int i, rowsz, nonwhite = 0;

    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) { say("   wkePaint: 尺寸异常 %dx%d", w, h); return; }
    buf = (unsigned char*)calloc((size_t)w * h * 4, 1);
    if (!buf) return;
    ((int (MKAPI_ *)(void*, void*, int))wkePaint)(view, buf, 0);

    rowsz = ((w * 3 + 3) / 4) * 4;
    f = fopen(path, "wb");
    if (!f) { free(buf); return; }
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
    fwrite(hdr, 1, 54, f);
    {
        unsigned char* row = (unsigned char*)malloc(rowsz);
        for (i = 0; i < h; i++) {
            int x;
            memset(row, 0, rowsz);
            for (x = 0; x < w; x++) {
                unsigned char* p = buf + ((size_t)i * w + x) * 4;
                row[x * 3] = p[0]; row[x * 3 + 1] = p[1]; row[x * 3 + 2] = p[2];
                if (!(p[0] == 255 && p[1] == 255 && p[2] == 255)) nonwhite++;
            }
            fwrite(row, 1, rowsz, f);
        }
        free(row);
    }
    fclose(f);
    free(buf);
    say("   wkePaint: %s  %dx%d  非白像素 %d/%d", path, w, h, nonwhite, w * h);
}

/* ---------- 重绘回调 ----------
 * 模式 inv ：照模块做法，只 InvalidateRect（等 WM_PAINT）
 * 模式 blit：回调里直接把内核给的 hdc 贴到窗口 DC（不经 WM_PAINT）
 */
static int g_mode;              /* 0=inv 1=blit */
static int g_blitOk;

static void on_paint(void* wv, void* param, void* hdc, int x, int y, int cx, int cy)
{
    RECT rc;
    (void)wv;
    g_paintCb++;
    if (!param) return;
    rc.left = x; rc.top = y; rc.right = x + cx; rc.bottom = y + cy;
    if (g_paintCb <= 3)
        say("   [cb] 第%d次重绘 脏区 (%d,%d,%d,%d) hdc=0x%08X -> %s",
            g_paintCb, x, y, cx, cy, (unsigned int)(UINT_PTR)hdc, g_mode ? "直贴" : "InvalidateRect");
    if (g_mode) {
        HDC wdc = GetDC((HWND)param);
        if (wdc && hdc) {
            if (BitBlt(wdc, x, y, cx, cy, (HDC)hdc, x, y, SRCCOPY)) g_blitOk++;
            ReleaseDC((HWND)param, wdc);
        }
    } else {
        InvalidateRect((HWND)param, &rc, 1);
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

static const char* NEED[] = {
    "MbKit_Init", "MbKit_IsReady", "MbKit_KernelPath", "MbKit_Diag", "MbKit_LastError",
    "wkeInitialize", "wkeIsInitialize", "wkeGetVersionString", "wkeCreateWebView",
    "wkeSetHandle", "wkeResize", "wkeShowWindow", "wkeLoadURLW", "wkeWake",
    "wkeIsLoadingCompleted", "wkeIsLoadingFailed", "wkeGetTitleW", "wkeGetURL",
    "wkeSetHandleOffset", "wkeDestroyWebView", "wkeFinalize",
    "wkeFireWindowsMessage", "wkeSetFocus", "wkeKillFocus", "wkeOnPaintUpdated",
    "wkeRepaintIfNeeded", "wkeGetViewDC", "wkePaint", "wkeGetWidth", "wkeGetHeight"
};
#define N_NEED ((int)(sizeof(NEED) / sizeof(NEED[0])))
static void* P[N_NEED];

int main(int argc, char** argv)
{
    const char* dllpath = (argc > 1) ? argv[1] : "MbKit.dll";
    const char* prefix = (argc > 3) ? argv[3] : "z";
    char url[512], path[512], tb[512];
    const char* mode = (argc > 4) ? argv[4] : "inv";
    wchar_t urlw[512];
    FILE* f;
    long fsz;
    unsigned char* buf;
    MKAPI A;
    mk_u32 base = 0, st = 99;
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    void* view;
    MSG msg;
    WNDCLASSA wc;
    int i, k;
    LONG procBefore, procAfter, procNow;

    g_log = fopen("probe.log", "wb");
    g_mode = (strcmp(mode, "blit") == 0);
    say("[i] 重绘模式 = %s", g_mode ? "blit（回调直贴窗口）" : "inv（InvalidateRect）");
    strncpy(url, (argc > 2) ? argv[2] : "https://www.madespark.cc/", sizeof(url) - 1);
    url[sizeof(url) - 1] = 0;
    MultiByteToWideChar(CP_ACP, 0, url, -1, urlw, 512);

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

    mk_map(&A, buf, (mk_u32)fsz, &base, &st);
    say("[1] mk_map -> base=0x%08X status=%u", base, st);
    if (!base) return 3;
    exp_init((void*)base);
    say("[1] 具名导出 %u", g_nnam);
    for (i = 0; i < N_NEED; i++) {
        P[i] = exp_find(NEED[i]);
        if (!P[i]) say("    [X] 缺导出 %s", NEED[i]);
    }

    say("[2] MbKit_Init = %d, IsReady = %d", ((int (MKAPI_ *)(const char*))P[0])(NULL),
        ((int (MKAPI_ *)())P[1])());
    say("[2] 内核 = %s", ((const char* (MKAPI_ *)())P[2])());
    say("[2] wke 版本 = %s", ((const char* (MKAPI_ *)())P[7])());

    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "MbKitMemProbe";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    g_hwnd = CreateWindowA("MbKitMemProbe", "MbKit 内存加载探针 - madespark.cc",
                           WS_OVERLAPPEDWINDOW, 40, 40, 1000, 760, NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) { say("[X] CreateWindow 失败"); return 4; }
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    BringWindowToTop(g_hwnd);
    UpdateWindow(g_hwnd);
    say("[3] 窗口 = 0x%08X", (unsigned int)(UINT_PTR)g_hwnd);

    procBefore = GetWindowLongW(g_hwnd, GWL_WNDPROC);
    say("[R1] wkeSetHandle 之前 WndProc = 0x%08X（我们自己的 = 0x%08X）",
        (unsigned int)procBefore, (unsigned int)(UINT_PTR)WndProc);

    view = ((void* (MKAPI_ *)())P[8])();
    say("[3] view = 0x%08X", (unsigned int)(UINT_PTR)view);
    if (!view) return 5;
    ((int (MKAPI_ *)(void*, void*))P[9])(view, g_hwnd);
    procAfter = GetWindowLongW(g_hwnd, GWL_WNDPROC);
    say("[R1] wkeSetHandle 之后 WndProc = 0x%08X  %s", (unsigned int)procAfter,
        procAfter == procBefore ? "（没变：内核没有子类化）" : "（变了：内核自己子类化了）");

    if (P[18]) ((int (MKAPI_ *)(void*, int, int))P[18])(view, 0, 0);
    ((int (MKAPI_ *)(void*, int, int))P[10])(view, 980, 700);
    ((int (MKAPI_ *)(void*, int))P[11])(view, 1);
    ((int (MKAPI_ *)(void*, void*, void*))P[24])(view, (void*)on_paint, (void*)g_hwnd);
    say("[3] 已注册 wkeOnPaintUpdated + wkeResize(980,700) + wkeShowWindow(1)");

    ((int (MKAPI_ *)(void*, const wchar_t*))P[12])(view, urlw);
    say("[3] wkeLoadURLW(%s)", url);

    for (i = 0; i < 150; i++) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        ((int (MKAPI_ *)(void*))P[13])(view);
        if (((int (MKAPI_ *)())P[14])() || ((int (MKAPI_ *)())P[15])()) { say("[3] 载入完成于第 %d 轮", i); break; }
        Sleep(100);
    }
    say("[3] 轮询结束（第 %d 轮）：Completed=%d Failed=%d", i,
        ((int (MKAPI_ *)())P[14])(), ((int (MKAPI_ *)())P[15])());
    {
        const wchar_t* t = ((const wchar_t* (MKAPI_ *)(void*))P[16])(view);
        if (t) { WideCharToMultiByte(CP_ACP, 0, t, -1, tb, sizeof(tb), NULL, NULL); say("[3] 标题 = %s", tb); }
        say("[3] URL = %s", ((const char* (MKAPI_ *)(void*))P[17])(view));
    }
    say("[R2] 重绘回调触发 %d 次，直贴成功 %d 次", g_paintCb, g_blitOk);

    InvalidateRect(g_hwnd, NULL, TRUE);
    UpdateWindow(g_hwnd);
    for (k = 0; k < 30; k++) {
        MSG m2;
        while (PeekMessageA(&m2, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m2); DispatchMessageA(&m2); }
        ((int (MKAPI_ *)(void*))P[13])(view);
        Sleep(100);
    }

    procNow = GetWindowLongW(g_hwnd, GWL_WNDPROC);
    say("[R1] 现在 WndProc = 0x%08X", (unsigned int)procNow);

    snprintf(path, sizeof(path), "%s_screen.bmp", prefix); cap_screen(path);
    snprintf(path, sizeof(path), "%s_client.bmp", prefix); cap_client(path);
    snprintf(path, sizeof(path), "%s_print.bmp", prefix);  cap_printwindow(path);
    snprintf(path, sizeof(path), "%s_wkepaint.bmp", prefix);
    cap_wkepaint(view, P[27], P[28], P[29], path);

    say("[done]");
    if (g_log) fclose(g_log);
    return 0;
}
