/* probe_dllhandle.c —— 实证「FreeLibrary 之后 LoadLibrary 句柄为什么不变」
 * 判别两种机制：
 *   A. 引用计数未归零 ⇒ 根本没卸载（FreeLibrary 后 GetModuleHandleA 仍非 0）
 *   B. 卸了，但无 ASLR（DllCharacteristics 无 DYNAMICBASE）+ 同进程内存布局一致
 *      ⇒ 重新映射落在同一个重定位基址 ⇒ 句柄数值相同
 * 同时验证：不同路径的同内容 DLL = 不同模块 = 不同句柄。
 * 工作目录必须是 内嵌浏览框测试\（有 A.dll / 3884.dll）。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int main(void)
{
    HMODULE h1, ha, h2, hb;
    char buf[64];

    setvbuf(stdout, NULL, _IONBF, 0);

    /* 易语言 DLL 的支持库（krnln.fne 等）在 IDE lib 目录 —— 提供搜索路径 */
    SetDllDirectoryA("D:\\ides\\e\\lib");
    {
        char full[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, full);
        lstrcatA(full, "\\A.dll");
        h1 = LoadLibraryExA(full, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    printf("1) LoadLibraryA(A.dll)          = 0x%08X err=%lu\n",
           (unsigned)(UINT_PTR)h1, h1 ? 0 : GetLastError());
    ha = GetModuleHandleA("A.dll");
    printf("2) GetModuleHandleA(A.dll)      = 0x%08X  (应等于 1)\n", (unsigned)(UINT_PTR)ha);

    if (!FreeLibrary(h1))
        printf("3) FreeLibrary FAILED err=%lu\n", GetLastError());
    else
        printf("3) FreeLibrary -> OK (返回真 ≠ 已卸载，只是引用计数-1)\n");

    ha = GetModuleHandleA("A.dll");
    printf("4) Free 后 GetModuleHandleA     = 0x%08X  %s\n", (unsigned)(UINT_PTR)ha,
           ha ? "★ 仍非0 ⇒ 引用计数未归零，模块根本没卸载（机制A）"
              : "=0 ⇒ 确实已卸载出加载器链表");

    h2 = LoadLibraryA("A.dll");
    printf("5) 重新 LoadLibraryA(A.dll)     = 0x%08X  %s\n", (unsigned)(UINT_PTR)h2,
           h2 == h1 ? "与第1次相同" : "与第1次不同");
    FreeLibrary(h2);
    /* 再来一轮，看「卸干净后再载」是否仍复现同值 */
    h2 = LoadLibraryA("A.dll");
    printf("6) 第三次 LoadLibraryA(A.dll)   = 0x%08X\n", (unsigned)(UINT_PTR)h2);

    hb = LoadLibraryA("3884.dll");   /* 同内容、不同路径 */
    printf("7) LoadLibraryA(3884.dll)       = 0x%08X  %s\n", (unsigned)(UINT_PTR)hb,
           (hb && h2 && hb != h2) ? "不同句柄 ⇒ 路径不同=独立模块" : "相同?!");
    FreeLibrary(hb);
    FreeLibrary(h2);

    /* 顺带打印 A.dll 的 ImageBase，对照日志句柄 */
    if (h2) { }
    wsprintfA(buf, "done");
    printf("%s\n", buf);
    return 0;
}
