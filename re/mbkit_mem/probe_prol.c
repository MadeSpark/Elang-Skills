/* probe_prol.c —— 判决实验：原样复现易语言那段「49 字节前导 + blob」的调用
 *
 * 背景：易语言里 `_映射映像` 一执行就 0xC0000005（池、kernel32、8 个 API 地址、
 *       内嵌宿主库 56320 B、blob 1952 B 全部已确认正常）。
 *
 * 本程序做的事和易语言完全一样：
 *   1) VirtualAlloc 拿 0x50000000（8192, RWX）
 *   2) 把 mapkit_text.bin 拷到 池+0x800
 *   3) 读入 src/mbkit/MbKit.dll
 *   4) 取 8 个 kernel32 API 地址
 *   5) 在可执行内存里摆上「易语言子程序帧 + 49 字节前导」，当 stdcall(12 参) 调用
 *   6) 打印 12 个实参、基址、状态码、异常地址
 *
 * 结论怎么用：
 *   - 原生这边**通过** ⇒ 前导与 blob 没问题，锅在易语言侧的实参槽 ⇒ 去易语言里把槽打出来
 *   - 原生这边**也崩** ⇒ 前导本身有错，直接改前导（零 IDE 成本地迭代）
 *
 * 编译（32 位）：
 *   export PATH="/c/msys64/mingw32/bin:$PATH"
 *   gcc -m32 -O0 -o probe_prol.exe probe_prol.c -lkernel32
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void* (WINAPI *PFN_VA)(void*, SIZE_T, DWORD, DWORD);
typedef int   (WINAPI *PFN_VF)(void*, SIZE_T, DWORD);
typedef void* (WINAPI *PFN_LLA)(const char*);
typedef void* (WINAPI *PFN_GPA)(void*, const char*);
typedef DWORD (WINAPI *PFN_TA)(void);
typedef void* (WINAPI *PFN_TG)(DWORD);
typedef int   (WINAPI *PFN_TS)(DWORD, void*);
typedef void* (WINAPI *PFN_GM)(const char*);

/* 易语言子程序帧 + 49 字节前导（与 memgen.mapper_code() 逐字节一致） */
static unsigned char PROL[] = {
    0x55, 0x8B, 0xEC,                                   /* push ebp; mov ebp,esp（易语言提供的帧） */
    0xFF,0x75,0x34, 0xFF,0x75,0x30, 0xFF,0x75,0x2C,      /* push arg12..arg1（逆序） */
    0xFF,0x75,0x28, 0xFF,0x75,0x24, 0xFF,0x75,0x20,
    0xFF,0x75,0x1C, 0xFF,0x75,0x18, 0xFF,0x75,0x14,
    0xFF,0x75,0x10, 0xFF,0x75,0x0C, 0xFF,0x75,0x08,
    0xB8, 0x00,0x08,0x00,0x50,                          /* mov eax, 0x50000800 */
    0xFF, 0xD0,                                         /* call eax */
    0x8B, 0xE5, 0x5D, 0xC2, 0x30, 0x00                  /* mov esp,ebp; pop ebp; ret 0x30 */
};

typedef void (__attribute__((stdcall)) *PFN12)(DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,
                                               DWORD,DWORD,DWORD,DWORD,DWORD,DWORD);
/* cdecl 版（blob 自己按 cdecl 取参：从 [esp+0x70] 起） */
typedef void (*PFN12C)(DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,
                       DWORD,DWORD,DWORD,DWORD,DWORD,DWORD);

/* 链接进来的 mk_map（已知可用路径，mapkit_test.c 用的就是它） */
extern void mk_map(void*, void*, void*, void*, void*, void*, void*, void*,
                   const unsigned char*, unsigned, unsigned*, unsigned*);

static unsigned char *read_file(const char *p, long *out)
{
    FILE *f = fopen(p, "rb");
    if (!f) { printf("[X] 打不开 %s\n", p); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n);
    if (fread(b, 1, n, f) != (size_t)n) { printf("[X] 读失败\n"); return NULL; }
    fclose(f); *out = n; return b;
}

static LONG WINAPI veh(EXCEPTION_POINTERS *ep)
{
    CONTEXT *c = ep->ContextRecord;
    printf("[7] ★ 异常：code = 0x%08X\n", ep->ExceptionRecord->ExceptionCode);
    printf("    出错指令 EIP = 0x%08X   出错时访问的地址 = 0x%08X\n",
           (unsigned)c->Eip,
           (unsigned)ep->ExceptionRecord->ExceptionInformation[1]);
    printf("    EAX=%08X ECX=%08X EDX=%08X EBX=%08X\n",
           (unsigned)c->Eax, (unsigned)c->Ecx, (unsigned)c->Edx, (unsigned)c->Ebx);
    printf("    ESI=%08X EDI=%08X EBP=%08X ESP=%08X\n",
           (unsigned)c->Esi, (unsigned)c->Edi, (unsigned)c->Ebp, (unsigned)c->Esp);
    fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char **argv)
{
    /* 关键：禁止 WER 弹「程序已停止工作」对话框。
     * 否则 0xC0000005 之后 Windows 会挂起等用户点按，批量实验全部卡死。 */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    AddVectoredExceptionHandler(1, veh);
    const char *mode = (argc > 1) ? argv[1] : "prol";
    /* 可选：argv[2] = blob 路径（默认 mapkit_text.bin）
     *       argv[3] = MbKit.dll 路径（默认两个内置相对路径） */
    const char *blobPath = (argc > 2) ? argv[2] : "mapkit_text.bin";
    const char *dllPath  = (argc > 3) ? argv[3] : NULL;
    long nblob = 0, ndll = 0;
    unsigned char *blob = read_file(blobPath, &nblob);
    if (!blob) return 2;
    unsigned char *dll = NULL;
    if (dllPath) dll = read_file(dllPath, &ndll);
    if (!dll) dll  = read_file("../../src/mbkit/MbKit.dll", &ndll);
    if (!dll) dll  = read_file("src_mbkit_MbKit.dll", &ndll);
    if (!dll) return 2;
    printf("[1] blob = %ld B ；MbKit.dll = %ld B\n", nblob, ndll);

    /* 池：必须是最低 64KB 对齐（0x50000000 天然满足） */
    void *pool = VirtualAlloc((void*)0x50000000, 8192, MEM_COMMIT | MEM_RESERVE,
                              PAGE_EXECUTE_READWRITE);
    printf("[2] 池 = 0x%08X（想要 0x50000000）\n", (unsigned)pool);
    if ((unsigned)pool != 0x50000000) { printf("[X] 首选基址没拿到\n"); return 3; }

    memcpy((unsigned char*)pool + 0x800, blob, nblob);
    printf("[3] blob 已拷到 0x%08X，前 8 字节 = %02X %02X %02X %02X %02X %02X %02X %02X\n",
           (unsigned)pool + 0x800,
           ((unsigned char*)pool)[0x800], ((unsigned char*)pool)[0x801],
           ((unsigned char*)pool)[0x802], ((unsigned char*)pool)[0x803],
           ((unsigned char*)pool)[0x804], ((unsigned char*)pool)[0x805],
           ((unsigned char*)pool)[0x806], ((unsigned char*)pool)[0x807]);

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    DWORD aVA = (DWORD)GetProcAddress(k32, "VirtualAlloc");
    DWORD aVF = (DWORD)GetProcAddress(k32, "VirtualFree");
    DWORD aLLA = (DWORD)GetProcAddress(k32, "LoadLibraryA");
    DWORD aGPA = (DWORD)GetProcAddress(k32, "GetProcAddress");
    DWORD aTA = (DWORD)GetProcAddress(k32, "TlsAlloc");
    DWORD aTG = (DWORD)GetProcAddress(k32, "TlsGetValue");
    DWORD aTS = (DWORD)GetProcAddress(k32, "TlsSetValue");
    DWORD aGM = (DWORD)GetProcAddress(k32, "GetModuleHandleA");
    printf("[4] 8 个 API = %08X %08X %08X %08X %08X %08X %08X %08X\n",
           aVA, aVF, aLLA, aGPA, aTA, aTG, aTS, aGM);

    /* 模式 1 = prol ：走「易语言子程序帧 + 49 字节前导」
       模式 2 = raw  ：把 blob 当普通 cdecl 函数**直调**（完全绕开前导）
       模式 3 = o     ：调用链接进来的 mk_map 符号（mapkit_test.c 用的那条已知可用路径） */
    if (mode[0] == 'r') {
        PFN12C raw = (PFN12C)((char*)pool + 0x800);
        printf("[5] 模式 raw：直调 blob @ 0x%08X\n", (unsigned)raw);
        printf("[6] 12 个实参：data=0x%08X size=%lu 出参=0x%08X / 0x%08X\n",
               (unsigned)dll, (unsigned long)ndll,
               (unsigned)pool + 0x1800, (unsigned)pool + 0x1804);
        fflush(stdout);
        raw(aVA, aVF, aLLA, aGPA, aTA, aTG, aTS, aGM,
            (DWORD)dll, (DWORD)ndll,
            (DWORD)pool + 0x1800, (DWORD)pool + 0x1804);
        printf("[7] 返回正常\n");
    } else if (mode[0] == 'o') {
        printf("[5] 模式 o：调用链接进来的 mk_map 符号 @ 0x%08X\n", (unsigned)mk_map);
        fflush(stdout);
        mk_map((void*)aVA, (void*)aVF, (void*)aLLA, (void*)aGPA,
               (void*)aTA, (void*)aTG, (void*)aTS, (void*)aGM,
               dll, (unsigned)ndll,
               (unsigned*)((char*)pool + 0x1800), (unsigned*)((char*)pool + 0x1804));
        printf("[7] 返回正常\n");
    } else {
        void *code = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
        memcpy(code, PROL, sizeof(PROL));
        PFN12 fn = (PFN12)code;
        printf("[5] 模式 prol：前导 = %u 字节 @ 0x%08X，stdcall(12 参)\n",
               (unsigned)sizeof(PROL), (unsigned)code);
        printf("[6] 12 个实参：data=0x%08X size=%lu 出参=0x%08X / 0x%08X\n",
               (unsigned)dll, (unsigned long)ndll,
               (unsigned)pool + 0x1800, (unsigned)pool + 0x1804);
        fflush(stdout);
        fn(aVA, aVF, aLLA, aGPA, aTA, aTG, aTS, aGM,
           (DWORD)dll, (DWORD)ndll,
           (DWORD)pool + 0x1800, (DWORD)pool + 0x1804);
        printf("[7] 返回正常\n");
    }
    /* ★ 判据行：必须只输出**真实取回的值**，且不掺任何硬编码常量文本。
     *   （踩过的坑：这里原来还打印一行「对照：base=0x6C2C0000」的说明，
     *     结果任何「没崩但也没成功」的运行都会被外部 grep 误判成通过。） */
    {
        DWORD rb = *(DWORD*)((char*)pool + 0x1800);
        DWORD rs = *(DWORD*)((char*)pool + 0x1804);
        printf("[8] RESULT base=0x%08X status=%u %s\n",
               (unsigned)rb, (unsigned)rs,
               (rb != 0 && rs == 0) ? "PASS" : "FAIL");
    }
    return 0;
}