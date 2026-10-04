/* ============================================================================
 * mbk_ipc.h —— MbKit 辅助进程架构：宿主进程(客户端桩) <-> 辅助进程 的共享内存契约
 *
 * 设计要点：
 *   · 控制块与帧缓冲**分成两个 file mapping**，避免 CreateDIBSection 的 offset
 *     必须 64KB 对齐的坑（帧缓冲从偏移 0 开始）。
 *   · 控制块里所有跨进程字段都是 volatile LONG，用 Interlocked* 访问。
 *   · 帧缓冲是 32bpp 自顶向下 BGRA（biHeight 为负），双方 DIB 头必须完全一致。
 *   · 一个"客户端会话"= 一套 (ctl, frame, evt) 命名对象 + 一个辅助进程。
 *     命名里带客户端序号，多个插件各拿一套 => 天然隔离，不会互相打架。
 * ==========================================================================*/
#ifndef MBK_IPC_H
#define MBK_IPC_H

#include <windows.h>

#define MBK_IPC_MAGIC    0x314B424D   /* 'MBK1' */
#define MBK_IPC_VERSION  2

/* 控制块大小（固定 4KB，够用且便于对齐） */
#define MBK_CTL_BYTES    4096

/* 辅助进程状态机 */
#define MBK_ST_BOOT      0   /* 进程已起，还没建窗口 */
#define MBK_ST_READY     1   /* 离屏窗口 + 视图已建，可以下命令 */
#define MBK_ST_LOADED    2   /* 页面载入完成（首帧已发布） */
#define MBK_ST_FAILED    3   /* 初始化失败，看 errText / lastError */

/* 命令码（v1 只用 LoadHtml / LoadUrl / Resize / Quit，够跑通闭环） */
#define MBK_CMD_NONE     0
#define MBK_CMD_LOADHTML 1
#define MBK_CMD_LOADURL  2
#define MBK_CMD_RESIZE   3
#define MBK_CMD_RUNJS    4
#define MBK_CMD_QUIT     5

/* 输入事件环（客户端 → 辅助进程）：原样透传窗口消息，辅助进程交给
 * wkeFireWindowsMessage(view, NULL, msg, wparam, lparam) 自己翻译。
 * 单生产者(客户端 UI 线程) / 单消费者(辅助进程主线程)，靠"最后写 head"保证可见性。 */
#define MBK_IN_SIZE 64

#pragma pack(push, 4)
typedef struct {
    volatile LONG msg;
    volatile LONG wparam;
    volatile LONG lparam;
} MBK_INPUT;

typedef struct {
    volatile LONG magic;
    volatile LONG version;
    volatile LONG status;
    volatile LONG lastError;
    volatile LONG hostPid;
    volatile LONG clientPid;

    /* ---- 帧 ---- */
    volatile LONG frameW;         /* 每帧尺寸（应等于窗口客户区） */
    volatile LONG frameH;
    volatile LONG frameStride;    /* = frameW * 4 */
    volatile LONG seqFrame;       /* 辅助进程每发布一帧 ++ */
    volatile LONG seqAck;         /* 客户端每取走一帧 ++ */
    volatile LONG dirtyX, dirtyY, dirtyW, dirtyH;
    volatile LONG paints;         /* 辅助进程 WM_PAINT 次数（诊断） */
    volatile LONG frames;         /* 辅助进程 BitBlt 次数（诊断） */
    volatile LONG kernelInvalidates; /* 内核自己要求重绘的次数（诊断：动态内容是否驱动重绘） */
    volatile LONG renderTick;     /* 辅助进程渲染循环 tick 数（诊断） */
    volatile LONG clientWnd;      /* 客户端绑定窗口（诊断用，辅助进程不碰它） */
    volatile LONG quit;           /* 客户端置 1 = 要求辅助进程自行退出 */

    /* ---- 命令通道（简单请求/应答，v1 一条一条来） ---- */
    volatile LONG cmdSeq;         /* 客户端 ++ 表示投递了命令 */
    volatile LONG cmdAck;         /* 辅助进程完成后 ++ */
    volatile LONG cmdOp;
    volatile LONG cmdA, cmdB, cmdC, cmdD;
    volatile LONG cmdResult;
    /* 字符串载荷：客户端写、辅助进程读（LoadHtml / RunJs 用） */
    volatile LONG payloadLen;
    char           payload[1024];

    /* ---- 输入环 ---- */
    volatile LONG inHead;         /* 客户端写；写完 head 才可见 */
    volatile LONG inTail;         /* 辅助进程写 */
    volatile LONG inCount;        /* 辅助进程已转发次数（诊断） */
    volatile LONG inDropped;      /* 就绪前被丢弃的输入数（诊断） */
    MBK_INPUT     inq[MBK_IN_SIZE];

    /* ---- 诊断 / 文本 ---- */
    wchar_t title[128];
    wchar_t errText[128];
    char    nodePath[260];
} MBK_CTL;
#pragma pack(pop)

/* 编译期护栏：控制块必须装得进固定大小的 mapping */
typedef char mbk_ctl_fits[ (sizeof(MBK_CTL) <= MBK_CTL_BYTES) ? 1 : -1 ];

/* 32bpp 自顶向下 BI_RGB 的公共 DIB 头（双方必须一致，否则位图错位） */
static __inline void mbk_make_bmi(BITMAPINFO* bi, int w, int h)
{
    ZeroMemory(bi, sizeof(*bi));
    bi->bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi->bmiHeader.biWidth       = w;
    bi->bmiHeader.biHeight      = -h;          /* 负 = 自顶向下 */
    bi->bmiHeader.biPlanes      = 1;
    bi->bmiHeader.biBitCount    = 32;
    bi->bmiHeader.biCompression = BI_RGB;
}

#endif /* MBK_IPC_H */
