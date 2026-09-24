/* ============================================================================
 * poc_restart.c —— 验证「内核重启」是否可行（ASCII 输出 + 无缓冲，便于抓崩溃点）
 *
 * initialize -> create -> destroy -> finalize -> FreeLibrary -> LoadLibrary
 *
 * 用法：poc_restart.exe <node.dll 完整路径>
 * ==========================================================================*/
#include <windows.h>
#include <stdio.h>

typedef void  (__cdecl *fn_void)(void);
typedef int   (__cdecl *fn_int)(void);
typedef void* (__cdecl *fn_create)(void);
typedef void  (__cdecl *fn_destroy)(void*);

static void step(const char* s)
{
    printf("  %s\n", s);
    fflush(stdout);
}

int main(int argc, char** argv)
{
    const char* node = (argc > 1) ? argv[1] : "node.dll";
    int round;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== kernel restart feasibility ==\n");
    printf("node.dll = %s\n", node);

    for (round = 1; round <= 3; round++) {
        HMODULE h;
        fn_void    p_init, p_finalize;
        fn_int     p_isinit;
        fn_create  p_create;
        fn_destroy p_destroy;
        void*      v;

        printf("\n---------- round %d ----------\n", round);
        fflush(stdout);

        h = LoadLibraryA(node);
        if (!h) { printf("  [FAIL] LoadLibrary err=%lu\n", GetLastError()); fflush(stdout); return 2; }
        printf("  LoadLibrary -> h=0x%p\n", (void*)h); fflush(stdout);

        p_init     = (fn_void)   GetProcAddress(h, "wkeInitialize");
        p_isinit   = (fn_int)    GetProcAddress(h, "wkeIsInitialize");
        p_create   = (fn_create) GetProcAddress(h, "wkeCreateWebView");
        p_destroy  = (fn_destroy)GetProcAddress(h, "wkeDestroyWebView");
        p_finalize = (fn_void)   GetProcAddress(h, "wkeFinalize");
        if (!p_init || !p_isinit || !p_create || !p_destroy) {
            printf("  [FAIL] missing required export\n"); fflush(stdout); return 3;
        }
        printf("  exports: init=%p isinit=%p create=%p finalize=%p\n",
               (void*)p_init, (void*)p_isinit, (void*)p_create, (void*)p_finalize);
        fflush(stdout);

        printf("  wkeIsInitialize = %d\n", p_isinit()); fflush(stdout);
        if (p_isinit() == 0) { p_init(); step("called wkeInitialize"); }
        else                 { step("already initialized -> skip"); }

        v = p_create();
        printf("  wkeCreateWebView -> %p\n", v); fflush(stdout);
        if (!v) { step("[FAIL] create returned NULL"); return 4; }
        p_destroy(v);
        step("wkeDestroyWebView done");

        if (p_finalize) {
            p_finalize();
            printf("  wkeFinalize done; wkeIsInitialize now = %d\n", p_isinit());
            fflush(stdout);
        }

        step("calling FreeLibrary ...");
        if (!FreeLibrary(h)) {
            printf("  [WARN] FreeLibrary failed err=%lu\n", GetLastError());
            fflush(stdout);
        }
        step("FreeLibrary done");
    }

    printf("\n== ALL 3 ROUNDS OK: kernel CAN be restarted ==\n");
    fflush(stdout);
    return 0;
}
