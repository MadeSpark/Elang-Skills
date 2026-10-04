/* ============================================================================
 * spike_host.c —— MbKit 辅助进程原型（第一段验证：离屏渲染跨进程管线）
 *
 * 干的事：
 *   1. 在**自己这个进程**里 LoadLibrary(node.dll) → wkeInitialize()
 *   2. 建一个屏幕外可见窗口（-4000,-4000）当"离屏渲染面"
 *   3. wkeCreateWebView + wkeSetHandle(本进程窗口) + wkeLoadW
 *   4. WM_PAINT → wkeRepaintIfNeeded → BitBlt(wkeGetViewDC) 直接写进**共享内存 DIB**
 *   5. 发布帧序号 + SetEvent 唤醒客户端；客户端只需把共享内存当普通 DIB 贴到自己窗口上
 *
 * 为什么可行（依据）：模块真实渲染路径就是
 *   wkeSetHandle → 子类化 → WM_PAINT → wkeRepaintIfNeeded → BitBlt(wkeGetViewDC)
 * （见 work_mb/repro/paintflow.c，实测 22.08% 非白像素 / 57488 个蓝色条像素）。
 * 内核只要"有一个窗口 + 有消息循环"就能出像素，**窗口在哪一进程并不重要**。
 *
 * 用法:
 *   spike_host.exe <node.dll> <url> <w> <h> <ctlName> <frameName> <evtName> <logPath>
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "mbk_ipc.h"

/* ------------------------------------------------------------------ 日志 */
static HANDLE g_log = INVALID_HANDLE_VALUE;

static void hlog(const char* fmt, ...)
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

/* ------------------------------------------------------- 内核函数（cdecl） */
static void          (__cdecl *wkeInitialize)(void);
static void*         (__cdecl *wkeCreateWebView)(void);
static void          (__cdecl *wkeSetHandle)(void*, HWND);
static void          (__cdecl *wkeResize)(void*, int, int);
static void          (__cdecl *wkeLoadW)(void*, const wchar_t*);
static int           (__cdecl *wkeIsLoadingCompleted)(void*);
static const wchar_t*(__cdecl *wkeGetTitleW)(void*);
static void          (__cdecl *wkeRepaintIfNeeded)(void*);
static HDC           (__cdecl *wkeGetViewDC)(void*);
static void          (__cdecl *wkeOnPaintUpdated)(void*, void*, void*);
static void          (__cdecl *wkeFinalize)(void);
/* 输入：粒度 API（wke 的鼠标 flags 数值 == Windows 的 MK_*） */
static int           (__cdecl *wkeFireWindowsMessage)(void*, HWND, UINT, WPARAM, LPARAM);
static int           (__cdecl *wkeFireMouseEvent)(void*, UINT, int, int, UINT);
static int           (__cdecl *wkeFireMouseWheelEvent)(void*, int, int, int, UINT);
static int           (__cdecl *wkeFireKeyDownEvent)(void*, UINT, UINT, int);
static int           (__cdecl *wkeFireKeyUpEvent)(void*, UINT, UINT, int);
static int           (__cdecl *wkeFireKeyPressEvent)(void*, UINT, UINT, int);
static void          (__cdecl *wkeSetFocus)(void*);
static void          (__cdecl *wkeKillFocus)(void*);

/* 键盘修饰：wke 的 WKE_EXTENDED=0x0100 / WKE_REPEAT=0x4000，与 KF_* 位一一对应 */
static UINT key_flags(LONG lp)
{
    UINT f = 0;
    if (HIWORD(lp) & KF_EXTENDED) f |= 0x0100;
    if (HIWORD(lp) & KF_REPEAT)   f |= 0x4000;
    return f;
}

/* --------------------------------------------------------------- 全局状态 */
static MBK_CTL*  g_ctl;
static BYTE*     g_sharedBits;      /* 共享内存帧缓冲 */
static HDC       g_memdc;           /* BitBlt 目标 DC */
static HBITMAP   g_membmp;
static HGDIOBJ   g_memold;
static BYTE*     g_privBits;        /* CreateDIBSection 失败时的私有兜底 */
static void*     g_view;
static HWND      g_hwnd;
static HANDLE    g_evt;
static int       g_w, g_h;
static DWORD     g_lastRender;
static DWORD     g_lastTitle;
static DWORD     g_lastBeat;
static int       g_dbgIter;
static volatile LONG g_quit;

static void on_paint_updated(void* wv, void* param, void* hdc,
                             int x, int y, int cx, int cy)
{
    (void)wv; (void)hdc; (void)x; (void)y; (void)cx; (void)cy;
    InterlockedIncrement((volatile LONG*)&g_ctl->kernelInvalidates);
    if (param) InvalidateRect((HWND)param, NULL, FALSE);
}

static void publish(int x, int y, int w, int h)
{
    g_ctl->dirtyX = x; g_ctl->dirtyY = y; g_ctl->dirtyW = w; g_ctl->dirtyH = h;
    MemoryBarrier();
    InterlockedIncrement((volatile LONG*)&g_ctl->seqFrame);
    InterlockedIncrement((volatile LONG*)&g_ctl->frames);
    if (g_evt) SetEvent(g_evt);
}

/* 把 viewDC 的指定区域搬进共享内存帧缓冲，并发布 */
static void render_rect(int x, int y, int w, int h)
{
    HDC vdc;

    if (w <= 0 || h <= 0) return;
    wkeRepaintIfNeeded(g_view);
    vdc = wkeGetViewDC(g_view);
    if (!vdc) return;
    BitBlt(g_memdc, x, y, w, h, vdc, x, y, SRCCOPY);
    if (g_privBits) {                        /* 私有兜底：手工拷进共享内存 */
        int row;
        for (row = 0; row < h; row++) {
            memcpy(g_sharedBits + ((size_t)(y + row) * g_w + x) * 4,
                   g_privBits   + ((size_t)(y + row) * g_w + x) * 4,
                   (size_t)w * 4);
        }
    }
    GdiFlush();
    publish(x, y, w, h);
}

static LRESULT CALLBACK host_wp(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_PAINT: {
        RECT r;
        if (g_view) {
            if (GetUpdateRect(h, &r, FALSE)) {
                InterlockedIncrement((volatile LONG*)&g_ctl->paints);
                render_rect(r.left, r.top, r.right - r.left, r.bottom - r.top);
            }
        }
        ValidateRect(h, NULL);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:                            /* 保底：内核不自发失效时也能持续出帧 */
        if (g_view) InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_SIZE: {
        int nw = LOWORD(lp), nh = HIWORD(lp);
        if (g_view && nw > 0 && nh > 0) {
            wkeResize(g_view, nw, nh);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

/* ------------------------------------------------------------ 输入环消费 */
/* ★ 实测坑（2026-10-02）：页面**尚未载入完成**时把窗口消息喂进内核，
 *   会让紧随其后的内核调用（此处是 wkeGetTitleW / wkeRepaintIfNeeded）**再也不返回**
 *   —— 现象是 tick 停在 0、frames 停在 1、页面永远不载入完成（载入完成检测本身也在这个循环里）。
 *   ⇒ 输入必须等 status>=MBK_ST_LOADED 才放行；就绪前的输入直接丢（不排队，避免上线后突然补放）。 */
static void pump_input(void)
{
    LONG logged = 0;
    int ready = (g_ctl->status >= MBK_ST_LOADED);

    while (g_ctl->inTail != g_ctl->inHead) {
        volatile MBK_INPUT* rv = &g_ctl->inq[g_ctl->inTail % MBK_IN_SIZE];
        LONG msg, wp, lp;
        msg = rv->msg; wp = rv->wparam; lp = rv->lparam;
        g_ctl->inTail++;

        if (!ready) {
            InterlockedIncrement((volatile LONG*)&g_ctl->inDropped);
            continue;
        }
        /* ★ 打点：日志先落盘再喂内核 —— 卡住时最后一条 [in>] 就是元凶消息 */
        if (logged < 8)
            hlog("[in>] msg=0x%04lX wp=0x%08lX lp=0x%08lX  (x=%d y=%d)",
                 (unsigned long)msg, (unsigned long)wp, (unsigned long)lp,
                 (short)LOWORD(lp), (short)HIWORD(lp));
        switch ((UINT)msg) {
        case WM_SETFOCUS:
            if (wkeSetFocus) wkeSetFocus(g_view);
            break;
        case WM_KILLFOCUS:
            if (wkeKillFocus) wkeKillFocus(g_view);
            break;
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
            /* wke 的 flags 数值与 Windows 的 MK_* 完全一致（WKE_LBUTTON=0x01 …），直接透传 */
            if (wkeFireMouseEvent)
                wkeFireMouseEvent(g_view, (UINT)msg,
                                  (short)LOWORD(lp), (short)HIWORD(lp),
                                  (UINT)wp & (MK_LBUTTON|MK_RBUTTON|MK_SHIFT|MK_CONTROL|MK_MBUTTON));
            break;
        case WM_MOUSEWHEEL: {
            POINT pt;                            /* 滚轮的 lParam 是**屏幕坐标** */
            pt.x = (short)LOWORD(lp);
            pt.y = (short)HIWORD(lp);
            ScreenToClient(g_hwnd, &pt);
            if (wkeFireMouseWheelEvent)
                wkeFireMouseWheelEvent(g_view, pt.x, pt.y, (short)HIWORD(wp),
                                       (UINT)LOWORD(wp) & (MK_LBUTTON|MK_RBUTTON|MK_SHIFT|MK_CONTROL|MK_MBUTTON));
            break;
        }
        case WM_KEYDOWN:
            if (wkeFireKeyDownEvent)
                wkeFireKeyDownEvent(g_view, (UINT)wp, key_flags(lp), FALSE);
            break;
        case WM_KEYUP:
            if (wkeFireKeyUpEvent)
                wkeFireKeyUpEvent(g_view, (UINT)wp, key_flags(lp), FALSE);
            break;
        case WM_CHAR:
            if (wkeFireKeyPressEvent)
                wkeFireKeyPressEvent(g_view, (UINT)wp, 0, FALSE);
            break;
        default:
            break;                               /* ★ 不用 wkeFireWindowsMessage：实测它会卡死（见文件头注释） */
        }
        if (logged < 8) hlog("[in<] msg=0x%04lX 返回", (unsigned long)msg);
        InterlockedIncrement((volatile LONG*)&g_ctl->inCount);
        logged++;
    }
}

/* ------------------------------------------------------------ 命令通道 v1 */
static void handle_cmd(void)
{
    LONG seq = g_ctl->cmdSeq;
    LONG op;
    if (seq == g_ctl->cmdAck) return;
    op = g_ctl->cmdOp;
    g_ctl->cmdResult = 0;

    switch (op) {
    case MBK_CMD_LOADURL: {
        wchar_t wu[1024];
        MultiByteToWideChar(CP_ACP, 0, g_ctl->payload, -1, wu, 1024);
        wkeLoadW(g_view, wu);
        hlog("[cmd] LOADURL %s", g_ctl->payload);
        break;
    }
    case MBK_CMD_RESIZE: {
        int nw = g_ctl->cmdA, nh = g_ctl->cmdB;
        wkeResize(g_view, nw, nh);
        InvalidateRect(g_hwnd, NULL, FALSE);
        hlog("[cmd] RESIZE %dx%d", nw, nh);
        break;
    }
    case MBK_CMD_QUIT:
        hlog("[cmd] QUIT");
        g_ctl->quit = 1;
        InterlockedExchange((volatile LONG*)&g_ctl->cmdAck, seq);
        PostQuitMessage(0);
        return;
    default:
        hlog("[cmd] unknown op=%ld", op);
        g_ctl->cmdResult = -1;
        break;
    }
    InterlockedExchange((volatile LONG*)&g_ctl->cmdAck, seq);
}

/* ------------------------------------------------------------------ main */
int main(int argc, char** argv)
{
    HANDLE hctl, hframe;
    WNDCLASSA wc;
    MSG msg;
    HMODULE hnode;
    BITMAPINFO bi;
    DWORD t0;
    const char* nodePath;
    const char* url;
    int i;

    if (argc < 9) {
        fprintf(stderr, "usage: spike_host.exe <node.dll> <url> <w> <h> <ctl> <frame> <evt> <log>\n");
        return 2;
    }
    nodePath = argv[1];
    url      = argv[2];
    g_w      = atoi(argv[3]);
    g_h      = atoi(argv[4]);

    g_log = CreateFileA(argv[8], GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log != INVALID_HANDLE_VALUE) SetFilePointer(g_log, 0, NULL, FILE_END);

    hlog("==== spike_host pid=%lu  node=%s  %dx%d  log=%s",
         (unsigned long)GetCurrentProcessId(), nodePath, g_w, g_h, argv[8]);

    /* ---- 共享内存 ---- */
    hctl = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, argv[5]);
    if (!hctl) { hlog("!! OpenFileMapping(ctl) 失败 gle=%lu", GetLastError()); return 3; }
    g_ctl = (MBK_CTL*)MapViewOfFile(hctl, FILE_MAP_ALL_ACCESS, 0, 0, MBK_CTL_BYTES);
    if (!g_ctl) { hlog("!! MapViewOfFile(ctl) 失败 gle=%lu", GetLastError()); return 3; }

    hframe = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, argv[6]);
    if (!hframe) { hlog("!! OpenFileMapping(frame) 失败 gle=%lu", GetLastError()); return 3; }
    g_sharedBits = (BYTE*)MapViewOfFile(hframe, FILE_MAP_ALL_ACCESS, 0, 0,
                                        (SIZE_T)g_w * g_h * 4);
    if (!g_sharedBits) { hlog("!! MapViewOfFile(frame) 失败 gle=%lu", GetLastError()); return 3; }

    g_evt = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[7]);
    if (!g_evt) { hlog("!! OpenEvent 失败 gle=%lu", GetLastError()); return 3; }

    if (g_ctl->magic != MBK_IPC_MAGIC) {
        hlog("!! 控制块 magic 不符：0x%08lX", (unsigned long)g_ctl->magic);
        return 4;
    }
    g_ctl->hostPid = (LONG)GetCurrentProcessId();
    g_ctl->frameW  = g_w;
    g_ctl->frameH  = g_h;
    g_ctl->frameStride = g_w * 4;
    strncpy(g_ctl->nodePath, nodePath, sizeof(g_ctl->nodePath) - 1);

    /* ---- 离屏窗口（可见但屏幕外：内核要求有窗口 + 消息循环） ---- */
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = host_wp;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "MbKitSpikeHostWnd";
    wc.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    if (!RegisterClassA(&wc)) { hlog("!! RegisterClass 失败 gle=%lu", GetLastError()); return 5; }
    g_hwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "MbKitSpikeHostWnd",
                             "mbkit-spike-offscreen", WS_POPUP | WS_VISIBLE,
                             -4000, -4000, g_w, g_h, NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) { hlog("!! CreateWindow 失败 gle=%lu", GetLastError()); return 5; }
    hlog("[1] 离屏窗口 hwnd=%p tid=%lu", (void*)g_hwnd, (unsigned long)GetCurrentThreadId());

    /* ---- 帧缓冲 DIB：优先直接坐在共享内存上（零拷贝） ---- */
    g_memdc = CreateCompatibleDC(NULL);
    mbk_make_bmi(&bi, g_w, g_h);
    {
        void* dibBits = NULL;
        g_membmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &dibBits, hframe, 0);
        if (g_membmp) {
            g_privBits = NULL;        /* dibBits 就是共享内存本身，无需再拷贝 */
            hlog("[2] DIB 直接坐在共享内存上（零拷贝）bits=%p", dibBits);
        } else {
            hlog("[2] CreateDIBSection(hSection) 失败 gle=%lu，退回私有位图 + 手工拷贝",
                 GetLastError());
            g_membmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &dibBits, NULL, 0);
            if (!g_membmp) { hlog("!! 私有 DIB 也失败 gle=%lu", GetLastError()); return 6; }
            g_privBits = (BYTE*)dibBits;
        }
    }
    g_memold = SelectObject(g_memdc, g_membmp);

    /* ---- 载入内核 ---- */
    hnode = LoadLibraryA(nodePath);
    if (!hnode) {
        hlog("!! LoadLibrary(%s) 失败 gle=%lu", nodePath, GetLastError());
        g_ctl->status = MBK_ST_FAILED;
        g_ctl->lastError = (LONG)GetLastError();
        return 7;
    }
#define SYM(n) do { *(FARPROC*)&n = GetProcAddress(hnode, #n); \
                    if (!n) { hlog("!! 缺少导出 %s", #n); g_ctl->status = MBK_ST_FAILED; return 8; } } while (0)
    SYM(wkeInitialize);
    SYM(wkeCreateWebView);
    SYM(wkeSetHandle);
    SYM(wkeResize);
    SYM(wkeLoadW);
    SYM(wkeIsLoadingCompleted);
    SYM(wkeGetTitleW);
    SYM(wkeRepaintIfNeeded);
    SYM(wkeGetViewDC);
    wkeOnPaintUpdated = (void (__cdecl*)(void*,void*,void*))GetProcAddress(hnode, "wkeOnPaintUpdated");
    wkeFinalize       = (void (__cdecl*)(void))GetProcAddress(hnode, "wkeFinalize");
    /* 输入类：缺了也不致命（老内核可能没有），但会记一笔 */
    wkeFireWindowsMessage = (int (__cdecl*)(void*,HWND,UINT,WPARAM,LPARAM))
        GetProcAddress(hnode, "wkeFireWindowsMessage");
    wkeFireMouseEvent      = (int (__cdecl*)(void*,UINT,int,int,UINT))GetProcAddress(hnode, "wkeFireMouseEvent");
    wkeFireMouseWheelEvent = (int (__cdecl*)(void*,int,int,int,UINT))GetProcAddress(hnode, "wkeFireMouseWheelEvent");
    wkeFireKeyDownEvent    = (int (__cdecl*)(void*,UINT,UINT,int))GetProcAddress(hnode, "wkeFireKeyDownEvent");
    wkeFireKeyUpEvent      = (int (__cdecl*)(void*,UINT,UINT,int))GetProcAddress(hnode, "wkeFireKeyUpEvent");
    wkeFireKeyPressEvent   = (int (__cdecl*)(void*,UINT,UINT,int))GetProcAddress(hnode, "wkeFireKeyPressEvent");
    wkeSetFocus   = (void (__cdecl*)(void*))GetProcAddress(hnode, "wkeSetFocus");
    wkeKillFocus  = (void (__cdecl*)(void*))GetProcAddress(hnode, "wkeKillFocus");
    hlog("[3] 输入导出: Mouse=%p Wheel=%p KeyDown=%p KeyPress=%p SetFocus=%p",
         (void*)wkeFireMouseEvent, (void*)wkeFireMouseWheelEvent,
         (void*)wkeFireKeyDownEvent, (void*)wkeFireKeyPressEvent, (void*)wkeSetFocus);
#undef SYM
    hlog("[3] node.dll 已载入 base=%p，导出解析完成", (void*)hnode);

    wkeInitialize();
    g_view = wkeCreateWebView();
    hlog("[4] wkeInitialize 完成  view=%p", g_view);
    if (!g_view) { g_ctl->status = MBK_ST_FAILED; g_ctl->lastError = -1; return 9; }

    wkeSetHandle(g_view, g_hwnd);
    wkeResize(g_view, g_w, g_h);
    if (wkeOnPaintUpdated) wkeOnPaintUpdated(g_view, (void*)on_paint_updated, (void*)g_hwnd);
    hlog("[5] 绑定窗口 + resize + 注册 paint 回调完成");

    { /* 载入页面（ANSI → wide） */
        wchar_t wu[1024];
        MultiByteToWideChar(CP_ACP, 0, url, -1, wu, 1024);
        wkeLoadW(g_view, wu);
        hlog("[6] wkeLoadW(%s)", url);
    }

    SetTimer(g_hwnd, 1, 30, NULL);
    g_ctl->status = MBK_ST_READY;

    t0 = GetTickCount();
    g_ctl->quit = 0;
    while (!g_quit) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_quit = 1; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (g_ctl->quit) break;
        handle_cmd();
        pump_input();
        if (g_dbgIter == 0) hlog("[dbg] it0: pump_input 返回");

        /* 标题回传：页面的 JS 改 document.title ⇒ 客户端日志里能看到（输入通路的端到端判据） */
        if (GetTickCount() - g_lastTitle >= 200) {
            const wchar_t* t;
            g_lastTitle = GetTickCount();
            t = wkeGetTitleW ? wkeGetTitleW(g_view) : NULL;
            if (t) lstrcpynW(g_ctl->title, t, 128);
        }
        if (g_dbgIter == 0) hlog("[dbg] it0: titlePoll 返回");

        /* ★ 渲染 tick：不等内核通知，主动周期性 wkeRepaintIfNeeded + 取帧。
         * 这是辅助进程该干的事 —— 它就是个"帧源"，把 view 内容持续推给客户端。 */
        if (GetTickCount() - g_lastRender >= 15) {
            g_lastRender = GetTickCount();
            InterlockedIncrement((volatile LONG*)&g_ctl->renderTick);
            render_rect(0, 0, g_w, g_h);
        }
        if (g_dbgIter == 0) hlog("[dbg] it0: renderTick 返回（tick=%ld）", g_ctl->renderTick);

        /* 心跳：卡住时最后一条心跳就是"卡在哪一轮"（排查内核调用不返回用） */
        if (GetTickCount() - g_lastBeat >= 2000) {
            g_lastBeat = GetTickCount();
            hlog("[beat] status=%ld tick=%ld frames=%ld in=%ld drop=%ld title=%ls",
                 g_ctl->status, g_ctl->renderTick, g_ctl->frames,
                 g_ctl->inCount, g_ctl->inDropped, g_ctl->title);
        }

        /* 载入完成检测（首帧发布后置 LOADED） */
        if (g_ctl->status == MBK_ST_READY) {
            if (wkeIsLoadingCompleted(g_view)) {
                const wchar_t* t = wkeGetTitleW ? wkeGetTitleW(g_view) : NULL;
                if (t) { lstrcpynW(g_ctl->title, t, 128); }
                InvalidateRect(g_hwnd, NULL, FALSE);
                g_ctl->status = MBK_ST_LOADED;
                hlog("[7] 载入完成 title=%ls 耗时=%lums paints=%ld frames=%ld",
                     g_ctl->title, (unsigned long)(GetTickCount() - t0),
                     g_ctl->paints, g_ctl->frames);
            } else if (GetTickCount() - t0 > 20000) {
                hlog("[7] 20s 仍未载入完成，仍继续出帧（诊断）");
                g_ctl->status = MBK_ST_FAILED;
                g_ctl->lastError = -2;
            }
        }
        if (g_dbgIter < 2) { hlog("[dbg] it%d: 循环尾", g_dbgIter); g_dbgIter++; }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 5, QS_ALLINPUT);
    }

    hlog("---- 退出：paints=%ld frames=%ld status=%ld ----",
         g_ctl->paints, g_ctl->frames, g_ctl->status);
    if (wkeFinalize) wkeFinalize();
    for (i = 0; i < 3; i++) { MSG m2; while (PeekMessageA(&m2,NULL,0,0,PM_REMOVE)) DispatchMessageA(&m2); Sleep(0); }
    return 0;
}
