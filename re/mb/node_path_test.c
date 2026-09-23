/* ============================================================================
 * node_path_test.c —— 验证 MbKit 的「自定义内核位置」能力（不依赖易语言 IDE）
 *
 * 用法：
 *     node_path_test.exe <MbKit.dll 完整路径> [自定义内核目录或 node.dll 完整文件名]
 *
 * 它按易语言模块 `miniblink` 改造后的**实际顺序**走一遍：
 *   ① LoadLibraryA(<完整路径>)     —— 等同模块里的「内嵌资源释放到磁盘 + 预载」
 *   ② LoadLibraryA("MbKit.dll")    —— 等同 .DLL命令 的**裸名**库名，应复用同一份
 *   ③ MbKit_SetNodePath(<目录>)    —— 等同公开函数「WKE设置node路径」
 *   ④ MbKit_Init(NULL)             —— 等同「WKE初始化」
 *   ⑤ 打印 KernelPath / Diag       —— 自证内核真的是从指定位置进来的
 *
 * 输出全部 ASCII + UTF-8（重定向场景），避免控制台编码干扰。
 * 环境变量 MBKIT_SILENT=1 可抑制失败时的提示框（无人值守用）。
 * ==========================================================================*/
#include <windows.h>
#include <stdio.h>

typedef int          (__stdcall *FN_SETPATH)(const char*);
typedef const char*  (__stdcall *FN_CSTR)(void);
typedef int          (__stdcall *FN_INT)(void);
typedef int          (__stdcall *FN_INIT)(const char*);

int main(int argc, char** argv)
{
    HMODULE m1, m2;
    FN_SETPATH SetPath;
    FN_CSTR    CfgPath, KPath, Diag, FwdErr;
    FN_INT     Loaded, Ready;
    FN_INIT    Init;
    int        rc;
    char       exeDir[MAX_PATH];

    if (argc < 2) {
        printf("usage: %s <MbKit.dll full path> [custom kernel dir | node.dll full path]\n", argv[0]);
        return 2;
    }

    GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    {
        char* p = strrchr(exeDir, '\\');
        if (p) *p = 0;
    }
    printf("[00] host exe          = %s\n", exeDir);

    /* ① 完整路径预载 */
    m1 = LoadLibraryA(argv[1]);
    printf("[01] LoadLibraryA(full path)      = %p\n", (void*)m1);
    if (!m1) {
        printf("     GetLastError = %lu   <-- 预载失败，后面不用测了\n", (unsigned long)GetLastError());
        return 3;
    }

    /* ② 裸名：等同 .DLL命令 里写的 "MbKit.dll" */
    m2 = LoadLibraryA("MbKit.dll");
    printf("[02] LoadLibraryA(\"MbKit.dll\")     = %p   same-as-[01] = %s\n",
           (void*)m2, (m1 == m2) ? "YES" : "NO");
    if (m1 != m2) printf("     <-- 警告：裸名没有复用已加载的那一份\n");

    SetPath = (FN_SETPATH)GetProcAddress(m1, "MbKit_SetNodePath");
    CfgPath = (FN_CSTR)   GetProcAddress(m1, "MbKit_ConfigNodePath");
    KPath   = (FN_CSTR)   GetProcAddress(m1, "MbKit_KernelPath");
    Diag    = (FN_CSTR)   GetProcAddress(m1, "MbKit_Diag");
    FwdErr  = (FN_CSTR)   GetProcAddress(m1, "MbKit_FwdError");
    Loaded  = (FN_INT)    GetProcAddress(m1, "MbKit_KernelLoaded");
    Ready   = (FN_INT)    GetProcAddress(m1, "MbKit_IsReady");
    Init    = (FN_INIT)   GetProcAddress(m1, "MbKit_Init");
    if (!SetPath || !CfgPath || !KPath || !Diag || !Loaded || !Ready || !Init) {
        printf("     GetProcAddress failed (missing new exports?)\n");
        return 4;
    }

    /* ③ 自定义内核位置 */
    if (argc >= 3 && argv[2][0]) {
        printf("[03] SetNodePath(\"%s\") = %d   (1=accepted, 0=refused)\n",
               argv[2], SetPath(argv[2]));
    } else {
        printf("[03] SetNodePath           = <not set>  -> default = host exe dir\n");
    }
    printf("[04] ConfigNodePath        = [%s]\n", CfgPath());
    printf("[05] KernelLoaded (before) = %d\n", Loaded());

    /* ④ 真正把内核拉起来 */
    rc = Init(NULL);
    printf("[06] Init(NULL)            = %d   (0=MBKIT_OK, -2=MBKIT_E_NODLL)\n", rc);
    printf("[07] KernelLoaded (after)  = %d\n", Loaded());
    printf("[08] IsReady               = %d\n", Ready());
    printf("[09] KernelPath            = [%s]\n", KPath());
    printf("[10] FwdError              = [%s]\n", FwdErr ? FwdErr() : "");
    printf("[11] Diag:\n%s\n", Diag());
    return (rc == 0) ? 0 : 1;
}
