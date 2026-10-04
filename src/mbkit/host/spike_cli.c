/* ============================================================================
 * spike_cli.c —— MbKit 辅助进程原型：**客户端侧**（模拟"插件"）
 *
 * 干的事：
 *   1. 建共享内存（控制块 + 帧缓冲）+ 事件
 *   2. 拉起辅助进程 spike_host.exe，把内核(node.dll)整套交给它
 *   3. 自己只做两件事：把共享帧贴到自己窗口上（SetDIBitsToDevice）+ 收事件重画
 *   4. 采样证明"页面真的画到了我的窗口上"（数非白像素 + 存 BMP）
 *
 * 关键验收：--restart 模式在三秒后**杀掉辅助进程**再重新拉起，
 *          若新进程又能重新初始化内核并出帧 ⇒ 「销毁辅助进程即释放 node 锁」成立。
 *
 * 用法:
 *   spike_cli.exe <node.dll> <url> <w> <h> <holdMs> <tag> [--restart] [--bmp 前缀]
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "mbk_ipc.h"

static HANDLE g_log = INVALID_HANDLE_VALUE;
static char   g_dir[MAX_PATH];

static void clog(const char* fmt, ...)
{
    char buf[1024];
    int n;
    va_list ap;
    DWORD wr;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 4, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[n++] = '\r';
    buf[n++] = '\n';
    if (g_log != INVALID_HANDLE_VALUE) {
        WriteFile(g_log, buf, (DWORD)n, &wr, NULL);
        FlushFileBuffers(g_log);
    }
    fwrite(buf, 1, (size_t)n, stdout);
    fflush(stdout);
}

static HANDLE  g_hctl, g_hframe, g_evt;
static MBK_CTL* g_ctl;
static BYTE*   g_bits;
static HWND    g_hwnd;
static int     g_w, g_h;
static LONG    g_seen;
static int     g_clientPaints;
static int     g_inputs;
static char    g_ctlName[128], g_frameName[128], g_evtName[128];
static char    g_hostLog[MAX_PATH];
static PROCESS_INFORMATION g_pi;
static int     g_hostAlive;

/* --------------------------------------------------------- 客户区贴图（唯一渲染代码） */
static void cli_paint(HWND h)
{
    RECT r;
    HDC dc;
    BITMAPINFO bi;
    int x, y, w, hh;

    if (!GetUpdateRect(h, &r, FALSE)) { ValidateRect(h, NULL); return; }
    x = r.left; y = r.top; w = r.right - r.left; hh = r.bottom - r.top;
    if (w <= 0 || hh <= 0) { ValidateRect(h, NULL); return; }

    mbk_make_bmi(&bi, g_w, g_h);
    dc = GetDC(h);
    SetDIBitsToDevice(dc, x, y, w, hh, x, y, 0, g_h, g_bits, &bi, DIB_RGB_COLORS);
    ReleaseDC(h, dc);
    ValidateRect(h, NULL);
    g_clientPaints++;
}

/* ------------------------------------------------- 输入转发（客户端 → 辅助进程） */
static void push_input(UINT m, WPARAM wp, LPARAM lp)
{
    LONG head = g_ctl->inHead;
    volatile MBK_INPUT* r;
    if (head - g_ctl->inTail >= MBK_IN_SIZE) return;      /* 环满则丢（不阻塞 UI） */
    r = &g_ctl->inq[head % MBK_IN_SIZE];
    r->msg = (LONG)m; r->wparam = (LONG)wp; r->lparam = (LONG)lp;
    MemoryBarrier();
    g_ctl->inHead = head + 1;                              /* 最后写 head */
    g_inputs++;
}

static LRESULT CALLBACK cli_wp(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_LBUTTONDOWN:  SetFocus(h); push_input(m, wp, lp); return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:  case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:  case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_KEYDOWN:      case WM_KEYUP:
    case WM_SYSKEYDOWN:   case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SETFOCUS:     case WM_KILLFOCUS:
        push_input(m, wp, lp);
        return 0;
    case WM_PAINT:      cli_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE:      DestroyWindow(h); return 0;
    case WM_DESTROY:    PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

/* ------------------------------------------------------------------ 采样 */
/* 数一块 32bpp 自顶向下 BGRA 缓冲里"非白"的像素数 */
static int count_nonwhite(const BYTE* bits, int w, int h)
{
    int x, y, n = 0;
    for (y = 0; y < h; y++) {
        const BYTE* p = bits + (size_t)y * w * 4;
        for (x = 0; x < w; x++, p += 4) {
            if (!(p[0] > 240 && p[1] > 240 && p[2] > 240)) n++;
        }
    }
    return n;
}

/* 抓窗口**屏幕内容**（证明客户端真的画上去了，而不是只有共享内存里有像素） */
static int grab_window(HWND h, const char* bmpPath, int* nonwhite)
{
    RECT cr;
    HDC wdc, mdc;
    HBITMAP bmp;
    HGDIOBJ old;
    BITMAPINFO bi;
    BYTE* bits;
    int w, hh, n = 0, ok = 0;
    DWORD wr;

    GetClientRect(h, &cr);
    w = cr.right; hh = cr.bottom;
    if (w <= 0 || hh <= 0) return 0;
    mbk_make_bmi(&bi, w, hh);
    bits = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * hh * 4);
    if (!bits) return 0;

    wdc = GetDC(h);
    mdc = CreateCompatibleDC(wdc);
    bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, NULL, NULL, 0);
    old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, hh, wdc, 0, 0, SRCCOPY);
    GdiFlush();
    if (GetDIBits(mdc, bmp, 0, hh, bits, &bi, DIB_RGB_COLORS)) {
        n = count_nonwhite(bits, w, hh);
        ok = 1;
        if (bmpPath) {  /* 存成 24bpp BMP，供人眼看 */
            BITMAPFILEHEADER fh;
            BITMAPINFOHEADER ih;
            DWORD stride = ((w * 3 + 3) / 4) * 4, imgsz = stride * hh, total;
            BYTE* img = (BYTE*)HeapAlloc(GetProcessHeap(), 0, imgsz);
            int y;
            for (y = 0; y < hh; y++) {            /* BGRA 自顶向下 → BGR 自底向上 */
                BYTE* dst = img + (size_t)(hh - 1 - y) * stride;
                const BYTE* src = bits + (size_t)y * w * 4;
                int x2;
                for (x2 = 0; x2 < w; x2++) {
                    dst[x2 * 3 + 0] = src[x2 * 4 + 0];
                    dst[x2 * 3 + 1] = src[x2 * 4 + 1];
                    dst[x2 * 3 + 2] = src[x2 * 4 + 2];
                }
            }
            memset(&fh, 0, sizeof(fh)); memset(&ih, 0, sizeof(ih));
            total = sizeof(fh) + sizeof(ih) + imgsz;
            fh.bfType = 0x4D42; fh.bfSize = total;
            fh.bfOffBits = sizeof(fh) + sizeof(ih);
            ih.biSize = sizeof(ih); ih.biWidth = w; ih.biHeight = hh;
            ih.biPlanes = 1; ih.biBitCount = 24; ih.biCompression = BI_RGB;
            ih.biSizeImage = imgsz;
            {
                HANDLE f = CreateFileA(bmpPath, GENERIC_WRITE, 0, NULL,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (f != INVALID_HANDLE_VALUE) {
                    WriteFile(f, &fh, sizeof(fh), &wr, NULL);
                    WriteFile(f, &ih, sizeof(ih), &wr, NULL);
                    WriteFile(f, img, imgsz, &wr, NULL);
                    CloseHandle(f);
                }
            }
            HeapFree(GetProcessHeap(), 0, img);
        }
    }
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(h, wdc);
    HeapFree(GetProcessHeap(), 0, bits);
    if (nonwhite) *nonwhite = n;
    return ok;
}

static void probe(const char* when, const char* bmpPrefix)
{
    int shared = count_nonwhite(g_bits, g_w, g_h);
    int win = -1;
    unsigned hash = 2166136261u;
    size_t n = (size_t)g_w * g_h * 4, k;
    char bmpPath[MAX_PATH];
    char titleA[160] = "";
    for (k = 0; k < n; k++) { hash ^= g_bits[k]; hash *= 16777619u; }
    WideCharToMultiByte(CP_ACP, 0, g_ctl->title, -1, titleA, sizeof(titleA), NULL, NULL);
    bmpPath[0] = 0;
    if (bmpPrefix && bmpPrefix[0]) {
        sprintf(bmpPath, "%s_%s.bmp", bmpPrefix, when);
    }
    grab_window(g_hwnd, bmpPath[0] ? bmpPath : NULL, &win);
    clog("[probe %-14s] 共享帧非白=%d/%d (%.2f%%) hash=%08X  窗口非白=%d  客户端paint=%d  "
         "辅助进程paints=%ld frames=%ld 内核失效=%ld tick=%ld 输入转发=%ld 本地转发=%d "
         "status=%ld title=\"%s\"%s%s",
         when, shared, g_w * g_h, shared * 100.0 / (g_w * g_h), hash, win, g_clientPaints,
         g_ctl->paints, g_ctl->frames, g_ctl->kernelInvalidates, g_ctl->renderTick,
         g_ctl->inCount, g_inputs, g_ctl->status, titleA,
         bmpPath[0] ? "  BMP=" : "", bmpPath[0] ? bmpPath : "");
}

/* ------------------------------------------------------------- 辅助进程管理 */
static int spawn_host(const char* node, const char* url)
{
    char cmd[4096];
    STARTUPINFOA si;

    sprintf(cmd, "\"%s\\spike_host.exe\" \"%s\" \"%s\" %d %d \"%s\" \"%s\" \"%s\" \"%s\"",
            g_dir, node, url, g_w, g_h, g_ctlName, g_frameName, g_evtName, g_hostLog);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, g_dir, &si, &g_pi)) {
        clog("!! CreateProcess 失败 gle=%lu  cmd=%s", GetLastError(), cmd);
        return 0;
    }
    g_hostAlive = 1;
    clog("[spawn] 辅助进程 pid=%lu  cmd=%s", (unsigned long)g_pi.dwProcessId, cmd);
    return 1;
}

static void kill_host(const char* why)
{
    if (!g_hostAlive) return;
    TerminateProcess(g_pi.hProcess, 0);
    WaitForSingleObject(g_pi.hProcess, 3000);
    CloseHandle(g_pi.hThread);
    CloseHandle(g_pi.hProcess);
    g_hostAlive = 0;
    clog("[kill ] 辅助进程已销毁（%s）—— node.dll 随进程消亡，锁死的句柄就此释放", why);
}

static void ctl_reset(void)
{
    g_ctl->status    = MBK_ST_BOOT;
    g_ctl->lastError = 0;
    g_ctl->hostPid   = 0;
    g_ctl->seqFrame  = 0;
    g_ctl->seqAck    = 0;
    g_ctl->dirtyX = g_ctl->dirtyY = g_ctl->dirtyW = g_ctl->dirtyH = 0;
    g_ctl->paints = 0;
    g_ctl->frames = 0;
    g_ctl->quit   = 0;
    g_ctl->cmdSeq = 0;
    g_ctl->cmdAck = 0;
    g_ctl->cmdOp  = MBK_CMD_NONE;
    g_ctl->title[0] = 0;
    g_ctl->errText[0] = 0;
    g_seen = 0;
    ResetEvent(g_evt);
}

int main(int argc, char** argv)
{
    const char* node; const char* url;
    int holdMs, restart = 0, i, tag = 0;
    char tagName[MAX_PATH];
    DWORD t0;
    MSG msg;
    int probed_kill = 0, probed_after = 0, lastProbeSec = 0;
    WNDCLASSA wc;
    RECT wr;
    char title[128];
    char bmpPrefix[MAX_PATH] = "";

    if (argc < 7) {
        fprintf(stderr,
            "usage: spike_cli.exe <node.dll> <url> <w> <h> <holdMs> <tag> [--restart] [--bmp prefix]\n");
        return 2;
    }
    node = argv[1]; url = argv[2];
    g_w = atoi(argv[3]); g_h = atoi(argv[4]);
    holdMs = atoi(argv[5]);
    strncpy(tagName, argv[6], sizeof(tagName) - 1); tagName[sizeof(tagName)-1] = 0;
    tag = atoi(tagName);
    for (i = 7; i < argc; i++) {
        if (!strcmp(argv[i], "--restart")) restart = 1;
        else if (!strcmp(argv[i], "--bmp") && i + 1 < argc) { strncpy(bmpPrefix, argv[++i], MAX_PATH - 1); }
    }

    GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    { char* p = strrchr(g_dir, '\\'); if (p) *p = 0; }

    sprintf(g_ctlName,   "MBKSPK_CTL_%s_%lu",   tagName, (unsigned long)GetCurrentProcessId());
    sprintf(g_frameName, "MBKSPK_FRM_%s_%lu",   tagName, (unsigned long)GetCurrentProcessId());
    sprintf(g_evtName,   "MBKSPK_EVT_%s_%lu",   tagName, (unsigned long)GetCurrentProcessId());
    sprintf(g_hostLog,   "%s\\spike_host_%s.log", g_dir, tagName);

    { char lp[MAX_PATH]; sprintf(lp, "%s\\spike_cli_%s.log", g_dir, tagName);
      g_log = CreateFileA(lp, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (g_log != INVALID_HANDLE_VALUE) SetFilePointer(g_log, 0, NULL, FILE_END); }

    clog("==== spike_cli tag=%s pid=%lu  node=%s  %dx%d  hold=%dms restart=%d",
         tagName, (unsigned long)GetCurrentProcessId(), node, g_w, g_h, holdMs, restart);

    /* ---- 共享内存 + 事件 ---- */
    g_hctl = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                MBK_CTL_BYTES, g_ctlName);
    g_ctl = (MBK_CTL*)MapViewOfFile(g_hctl, FILE_MAP_ALL_ACCESS, 0, 0, MBK_CTL_BYTES);
    g_hframe = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                  (DWORD)((SIZE_T)g_w * g_h * 4), g_frameName);
    g_bits = (BYTE*)MapViewOfFile(g_hframe, FILE_MAP_ALL_ACCESS, 0, 0,
                                  (SIZE_T)g_w * g_h * 4);
    g_evt = CreateEventA(NULL, FALSE, FALSE, g_evtName);
    if (!g_ctl || !g_bits || !g_evt) { clog("!! 共享内存/事件创建失败 gle=%lu", GetLastError()); return 3; }
    memset(g_ctl, 0, MBK_CTL_BYTES);
    memset(g_bits, 0xFF, (SIZE_T)g_w * g_h * 4);     /* 先铺白，便于判断"有没有画上" */
    g_ctl->magic = MBK_IPC_MAGIC;
    g_ctl->version = MBK_IPC_VERSION;
    g_ctl->frameW = g_w; g_ctl->frameH = g_h; g_ctl->frameStride = g_w * 4;
    g_ctl->clientPid = (LONG)GetCurrentProcessId();
    ctl_reset();
    clog("[init ] ctl=%s frame=%s evt=%s", g_ctlName, g_frameName, g_evtName);

    /* ---- 客户端窗口（客户区正好 w×h） ---- */
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = cli_wp;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "MbKitSpikeCliWnd";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClassA(&wc);
    sprintf(title, "MbKit 辅助进程原型 [%s]", tagName);
    wr.left = 0; wr.top = 0; wr.right = g_w; wr.bottom = g_h;
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExA(0, "MbKitSpikeCliWnd", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             60 + 60 * tag, 60,
                             wr.right - wr.left, wr.bottom - wr.top,
                             NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) { clog("!! CreateWindow 失败 gle=%lu", GetLastError()); return 4; }
    g_ctl->clientWnd = (LONG)(LONG_PTR)g_hwnd;
    {
        POINT pt; pt.x = 0; pt.y = 0; ClientToScreen(g_hwnd, &pt);
        clog("[init ] 窗口 hwnd=%p 客户区=%dx%d 客户区屏幕原点=(%d,%d) tid=%lu",
             (void*)g_hwnd, g_w, g_h, (int)pt.x, (int)pt.y, (unsigned long)GetCurrentThreadId());
    }

    if (!spawn_host(node, url)) return 5;

    t0 = GetTickCount();
    while (1) {
        DWORD el = GetTickCount() - t0;

        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { el = (DWORD)holdMs + 1; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (el > (DWORD)holdMs) break;

        /* 帧到达 → 让对应区域失效重画 */
        if (g_ctl->seqFrame != g_seen) {
            g_seen = g_ctl->seqFrame;
            InvalidateRect(g_hwnd, NULL, FALSE);
        }

        /* 每秒采一次（看 hash 变不变 = 动态内容到底有没有推过来） */
        if (el > 1000 && (int)(el / 1000) != lastProbeSec) {
            char nm[32];
            lastProbeSec = (int)(el / 1000);
            sprintf(nm, "t%ds", lastProbeSec);
            probe(nm, NULL);
        }
        if (restart && el > 3500 && !probed_kill) {
            probed_kill = 1;
            probe("before-kill", bmpPrefix);
            kill_host("模拟插件重载");
        }
        if (restart && el > 4000 && !probed_after && !g_hostAlive) {
            clog("[reload] 重新拉起辅助进程（新一轮内核初始化）");
            ctl_reset();
            memset(g_bits, 0xFF, (SIZE_T)g_w * g_h * 4);
            probed_after = 1;
            if (!spawn_host(node, url)) break;
        }
        if (restart && el > 7000 && probed_after) {
            static int done = 0;
            if (!done) { done = 1; probe("after-reload", bmpPrefix); }
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }

    probe("final", bmpPrefix);
    clog("[exit ] 客户端 paint 次数=%d  辅助进程 frames=%ld status=%ld title=%ls",
         g_clientPaints, g_ctl->frames, g_ctl->status, g_ctl->title);
    kill_host("客户端退出");
    clog("==== spike_cli 结束 ====");
    return 0;
}
