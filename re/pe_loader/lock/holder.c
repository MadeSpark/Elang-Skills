/* 只做一件事：LoadLibrary 指定 DLL 然后挂住，用来测「文件被加载时能不能替换/删除」。
   用法: holder.exe <dll 全路径> [秒数] */
#include <windows.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    HMODULE h;
    int secs = 15;
    if (argc < 2) { printf("用法: holder.exe <dll路径> [秒]\n"); return 2; }
    if (argc >= 3) secs = atoi(argv[2]);
    h = LoadLibraryA(argv[1]);
    printf("LoadLibrary(\"%s\") = %p  err=%lu\n", argv[1], (void*)h, GetLastError());
    fflush(stdout);
    Sleep(secs * 1000);
    return 0;
}
