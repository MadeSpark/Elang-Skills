/* 实验用 DLL：故意做成「有导出 + 有 DllMain 副作用 + 有自己的静态数据」，
   用来检验手写 PE 映射器到底能做到哪一步。
   - cdecl 导出（裸名），匹配 易语言 .DLL命令 的朴素名查找与 MyGetProcAddress
   - stdcall 导出（@8 修饰），看名字修饰是否被正确处理
   - DllMain 写一个文件：证明映射器确实调了 DllMain
   - g_counter 每进程私有：区分「同一份映射」还是「两份」
*/
#include <windows.h>

static int g_counter = 0;

static void note(const char *s)
{
    HANDLE f = CreateFileA("dllmain_ran.txt", FILE_APPEND_DATA, FILE_SHARE_READ,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        SetFilePointer(f, 0, NULL, FILE_END);
        WriteFile(f, s, (DWORD)lstrlenA(s), &w, NULL);
        WriteFile(f, "\r\n", 2, &w, NULL);
        CloseHandle(f);
    }
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD dwReason, LPVOID lpReserved)
{
    (void)lpReserved;
    if (dwReason == DLL_PROCESS_ATTACH) {
        char buf[128];
        wsprintfA(buf, "DllMain ATTACH  base=%08X", (unsigned)(UINT_PTR)hInst);
        note(buf);
    } else if (dwReason == DLL_PROCESS_DETACH) {
        note("DllMain DETACH");
    }
    return TRUE;
}

/* cdecl，裸名导出 —— 这是我们真正关心的形态 */
__declspec(dllexport) int __cdecl Add2(int a, int b)
{
    g_counter++;
    return a + b;
}

__declspec(dllexport) int __cdecl BumpCounter(void)
{
    return ++g_counter;
}

__declspec(dllexport) int __cdecl GetCounter(void)
{
    return g_counter;
}

/* 用真实加载器 API 查自己的名字：手工映射下应当查不到 */
__declspec(dllexport) unsigned __cdecl SelfByName(void)
{
    return (unsigned)(UINT_PTR)GetModuleHandleA("expdll.dll");
}

/* 用加载器链表反查自己所在模块的基址：手工映射下应当返回 0 */
__declspec(dllexport) unsigned __cdecl SelfByAddress(void)
{
    HMODULE h = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(UINT_PTR)&Add2, &h))
        return (unsigned)(UINT_PTR)h;
    return 0;
}

/* 自己的函数地址低 16 位，用来在易语言侧核对「我算出来的基址对不对」 */
__declspec(dllexport) unsigned __cdecl AddrLow16(void)
{
    return (unsigned)(UINT_PTR)&Add2 & 0xFFFF;
}

/* stdcall 导出，看 @8 名字修饰 */
__declspec(dllexport) int __stdcall Add2S(int a, int b)
{
    return a + b + 1000;
}

/* ---- 对照实验用：返回值不依赖任何内存读写，方便区分「代码没拷进去」与「调用约定错」 ---- */
__declspec(dllexport) int __cdecl Const7(void)
{
    return 7;
}

__declspec(dllexport) int __cdecl Echo1(int x)
{
    return x;
}

__declspec(dllexport) int __cdecl Echo2(int a, int b)
{
    return a * 100 + b;
}
