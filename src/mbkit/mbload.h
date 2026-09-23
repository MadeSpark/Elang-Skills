/* ============================================================================
 * mbload.h —— MbKit.dll 动态装载器（模拟易语言 `.DLL命令` 的用法）
 * ----------------------------------------------------------------------------
 * 业务侧不静态链接 MbKit，而是运行时 LoadLibrary + GetProcAddress。
 * 原因：① 与易语言模块的实际加载方式一致；② MbKit 已自 PIN，
 *       FreeLibrary 不会把它卸掉，业务侧可以随便重载。
 * ==========================================================================*/
#ifndef MBLOAD_H
#define MBLOAD_H

#include <windows.h>
#include <string.h>
#include "mbkit_api.h"

typedef struct {
    HMODULE h;

    int         (__stdcall *Init)(const char*);
    int         (__stdcall *IsReady)(void);
    const char* (__stdcall *Version)(void);
    const char* (__stdcall *LastError)(void);
    const char* (__stdcall *NodePath)(void);

    void*       (__stdcall *Create)(void*, int, int, int, int, MbEventCb, void*);
    int         (__stdcall *Destroy)(void*);
    int         (__stdcall *DetachAll)(void);
    int         (__stdcall *LiveCount)(void);

    int         (__stdcall *LoadUrl)(void*, const wchar_t*);
    int         (__stdcall *LoadHtml)(void*, const wchar_t*);
    long long   (__stdcall *RunJs)(void*, const wchar_t*);
    int         (__stdcall *Resize)(void*, int, int);
    int         (__stdcall *SetZoom)(void*, float);
    int         (__stdcall *SetHeadless)(void*, int);
    int         (__stdcall *IsLoaded)(void*);
    int         (__stdcall *GetTitle)(void*, wchar_t*, int);
    int         (__stdcall *ForcePaint)(void*);
    int         (__stdcall *Pump)(int);

    /* ANSI(GBK) 变体 */
    int         (__stdcall *LoadUrlA)(void*, const char*);
    int         (__stdcall *LoadHtmlA)(void*, const char*);
    int         (__stdcall *RunJsA)(void*, const char*);
    int         (__stdcall *GetTitleA)(void*, char*, int);

    unsigned    (__stdcall *NodeModule)(void);
    unsigned    (__stdcall *SelfModule)(void);
    int         (__stdcall *KernelInitCount)(void);
    int         (__stdcall *Pinned)(void);
    int         (__stdcall *SuppressedCount)(void);
    int         (__stdcall *AliveModule)(unsigned, unsigned);
} MBKIT;

#define MBK_GET(m, n)                                                        \
    do {                                                                     \
        k->m = (__typeof__(k->m))GetProcAddress(k->h, n);                    \
        if (!k->m) return 0;                                                 \
    } while (0)

/* 可选导出：缺了不算加载失败（便于新旧版本共存） */
#define MBK_GET_OPT(m, n)                                                    \
    do { k->m = (__typeof__(k->m))GetProcAddress(k->h, n); } while (0)

static int mbk_load(MBKIT* k, const char* dllPath)
{
    memset(k, 0, sizeof(*k));
    k->h = LoadLibraryA(dllPath && *dllPath ? dllPath : "MbKit.dll");
    if (!k->h) return 0;

    MBK_GET(Init,           "MbKit_Init");
    MBK_GET(IsReady,        "MbKit_IsReady");
    MBK_GET(Version,        "MbKit_Version");
    MBK_GET(LastError,      "MbKit_LastError");
    MBK_GET(NodePath,       "MbKit_NodePath");
    MBK_GET(Create,         "MbKit_Create");
    MBK_GET(Destroy,        "MbKit_Destroy");
    MBK_GET(DetachAll,      "MbKit_DetachAll");
    MBK_GET(LiveCount,      "MbKit_LiveCount");
    MBK_GET(LoadUrl,        "MbKit_LoadUrl");
    MBK_GET(LoadHtml,       "MbKit_LoadHtml");
    MBK_GET(RunJs,          "MbKit_RunJs");
    MBK_GET(Resize,         "MbKit_Resize");
    MBK_GET(SetZoom,        "MbKit_SetZoom");
    MBK_GET(SetHeadless,    "MbKit_SetHeadless");
    MBK_GET(IsLoaded,       "MbKit_IsLoaded");
    MBK_GET(GetTitle,       "MbKit_GetTitle");
    MBK_GET(ForcePaint,     "MbKit_ForcePaint");
    MBK_GET(Pump,           "MbKit_Pump");
    MBK_GET_OPT(LoadUrlA,       "MbKit_LoadUrlA");
    MBK_GET_OPT(LoadHtmlA,      "MbKit_LoadHtmlA");
    MBK_GET_OPT(RunJsA,         "MbKit_RunJsA");
    MBK_GET_OPT(GetTitleA,      "MbKit_GetTitleA");
    MBK_GET(NodeModule,     "MbKit_NodeModule");
    MBK_GET(SelfModule,     "MbKit_SelfModule");
    MBK_GET(KernelInitCount,"MbKit_KernelInitCount");
    MBK_GET(Pinned,         "MbKit_Pinned");
    MBK_GET(SuppressedCount,"MbKit_SuppressedCount");
    MBK_GET(AliveModule,    "MbKit_AliveModule");
    return 1;
}
#undef MBK_GET

#endif /* MBLOAD_H */
