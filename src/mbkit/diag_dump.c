/* ============================================================================
 * diag_dump.c —— 验收 C 的观测器：走一遍导出边界编组，然后把 MbKit_Diag 打印出来
 *
 * 期望看到（2026-09-25 导出边界编组落地后）：
 *   内核属主 : 线程 <K 的 tid>（K 模式（编组，重载安全））
 *   编组调用 : 共 N 次（跨线程 >0，失败 0）
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int         (__stdcall *p_ensure)(void);
typedef const char* (__stdcall *p_diag)(void);
typedef void*       (__cdecl  *p_create)(void);
typedef void        (__cdecl  *p_destroy)(void*);
typedef const char* (__stdcall *p_ver)(void);

int main(void)
{
    HMODULE  h = LoadLibraryA("MbKit.dll");
    p_ensure ensure;
    p_diag   diag;
    p_create create;
    p_destroy destroy;
    p_ver    ver;

    if (!h) { printf("LoadLibrary(MbKit.dll) failed\n"); return 1; }

    ensure  = (p_ensure)GetProcAddress(h, "MbKit_EnsureKernelInit");
    diag    = (p_diag)GetProcAddress(h, "MbKit_Diag");
    create  = (p_create)GetProcAddress(h, "wkeCreateWebView");
    destroy = (p_destroy)GetProcAddress(h, "wkeDestroyWebView");
    ver     = (p_ver)GetProcAddress(h, "MbKit_Version");

    /* ① 内核落常驻线程 K */
    if (ensure) ensure();

    /* ② 从主线程走两个编组 thunk（建/销 view）—— 主线程 != K，cross 计数 +2 */
    {
        void* wv = create ? create() : NULL;
        if (wv && destroy) destroy(wv);
    }

    printf("version : %s\n", ver ? ver() : "(?)");
    printf("%s\n", diag ? diag() : "(no MbKit_Diag)");
    return 0;
}
