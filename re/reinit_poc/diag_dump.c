/* diag_dump.c —— 打印 MbKit_Diag 自检（验收报告附页） */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int          (__stdcall *pInit)(const char*);
typedef const char*  (__stdcall *pStr)(void);

int main(void)
{
    HMODULE h = LoadLibraryA("MbKit.dll");
    pInit   init;
    pStr    diag, ver, err;
    if (!h) { printf("!! LoadLibrary(MbKit.dll) err=%lu\n", GetLastError()); return 1; }
    init = (pInit)GetProcAddress(h, "MbKit_Init");
    diag = (pStr)GetProcAddress(h, "MbKit_Diag");
    ver  = (pStr)GetProcAddress(h, "MbKit_Version");
    err  = (pStr)GetProcAddress(h, "MbKit_FwdError");
    if (!init || !diag) { printf("!! 缺导出\n"); return 1; }
    printf("Init rc=%d\n", init(NULL));
    printf("Version: %s\n", ver ? ver() : "(?)");
    printf("FwdError: %s\n", err ? err() : "(?)");
    printf("%s\n", diag());
    return 0;
}
