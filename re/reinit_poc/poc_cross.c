/* ============================================================================
 * poc_cross.c —— 定位「跨线程崩」的确切原因
 *
 * 场景：T1 里 wkeInitialize + create + destroy；随后在 T2 里再 create。
 * 模式：
 *   force=1 : T2 里**无条件**再调一次 wkeInitialize()，然后 create
 *   force=0 : T2 里直接 create（对照）
 *
 * 用法：poc_cross.exe <node.dll 完整路径> <force 0|1>
 * ==========================================================================*/
#include <windows.h>
#include <stdio.h>

typedef void  (__cdecl *fn_void)(void);
typedef int   (__cdecl *fn_int)(void);
typedef void* (__cdecl *fn_create)(void);
typedef void  (__cdecl *fn_destroy)(void*);

static HMODULE    g_h;
static fn_void    g_init;
static fn_int     g_isinit;
static fn_create  g_create;
static fn_destroy g_destroy;
static int        g_force;

static DWORD WINAPI worker(LPVOID param)
{
    void* v;
    (void)param;
    printf("[T2 tid=%lu] ---- worker start (force=%d) ----\n", GetCurrentThreadId(), g_force); fflush(stdout);

    printf("[T2] wkeIsInitialize = %d\n", g_isinit()); fflush(stdout);

    if (g_force) {
        printf("[T2] calling wkeInitialize() UNCONDITIONALLY ...\n"); fflush(stdout);
        g_init();
        printf("[T2] wkeInitialize returned; wkeIsInitialize = %d\n", g_isinit()); fflush(stdout);
    }

    printf("[T2] calling wkeCreateWebView ...\n"); fflush(stdout);
    v = g_create();
    printf("[T2] wkeCreateWebView -> %p\n", v); fflush(stdout);

    if (v) { g_destroy(v); printf("[T2] destroy done\n"); fflush(stdout); }
    printf("[T2] ---- worker end (OK) ----\n"); fflush(stdout);
    return 0;
}

int main(int argc, char** argv)
{
    const char* node = (argc > 1) ? argv[1] : "node.dll";
    HANDLE th;
    void*  v1;
    DWORD  wr;

    setvbuf(stdout, NULL, _IONBF, 0);
    g_force = (argc > 2) ? atoi(argv[2]) : 1;

    printf("== cross-thread diagnosis (force=%d) ==\n", g_force);
    printf("[T1 tid=%lu] LoadLibrary ...\n", GetCurrentThreadId()); fflush(stdout);

    g_h = LoadLibraryA(node);
    if (!g_h) { printf("[FAIL] LoadLibrary err=%lu\n", GetLastError()); return 2; }

    g_init    = (fn_void)   GetProcAddress(g_h, "wkeInitialize");
    g_isinit  = (fn_int)    GetProcAddress(g_h, "wkeIsInitialize");
    g_create  = (fn_create) GetProcAddress(g_h, "wkeCreateWebView");
    g_destroy = (fn_destroy)GetProcAddress(g_h, "wkeDestroyWebView");
    if (!g_init || !g_isinit || !g_create || !g_destroy) { printf("[FAIL] exports\n"); return 3; }

    printf("[T1] wkeIsInitialize = %d\n", g_isinit()); fflush(stdout);
    if (g_isinit() == 0) { g_init(); printf("[T1] called wkeInitialize\n"); fflush(stdout); }

    printf("[T1] calling wkeCreateWebView ...\n"); fflush(stdout);
    v1 = g_create();
    printf("[T1] wkeCreateWebView -> %p\n", v1); fflush(stdout);
    if (!v1) { printf("[FAIL] T1 create NULL\n"); return 4; }
    g_destroy(v1);
    printf("[T1] destroy done. now start T2.\n\n"); fflush(stdout);

    th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!th) { printf("[FAIL] CreateThread\n"); return 5; }
    wr = WaitForSingleObject(th, 20000);
    printf("\n[T1] WaitForSingleObject -> %lu (0=WAIT_OBJECT_0)\n", wr); fflush(stdout);
    if (wr == WAIT_TIMEOUT) { printf("[FAIL] T2 卡死超时\n"); return 6; }
    CloseHandle(th);

    printf("== T2 完成且进程未崩：跨线程可用 ==\n"); fflush(stdout);
    return 0;
}
