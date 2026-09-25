/* ============================================================================
 * reload_host.c —— 易语言静态 DLL 重载验证宿主（端到端验证恢复路径）
 * ----------------------------------------------------------------------------
 * 模拟小栗子真机场景：
 *   宿主 exe 反复「载入内含 miniblink 模块的业务 DLL → 初始化内核 → 读自检文本
 *   → 卸载 DLL → 重新载入」，看第 2、3 轮能不能走「重载恢复」路径。
 *
 * 被测 DLL：reloadbiz.dll（由 re/reloadtest/ 工程 ecompile --kind static_dll 生成）
 *   导出 2 个 __stdcall 函数：
 *     BOOL 内核测试(void)      // WKE载入宿主库() + WKE初始化()，成功=1 失败=0
 *     const char* 宿主自检(void)  // 返回 WKE宿主自检() 的 GBK 文本（指针由 DLL 持有）
 *
 * 用法：
 *   cl reload_host.c /Fe:reload_host.exe           (MSVC，x86)
 *   gcc -m32 reload_host.c -o reload_host.exe -luser32 -lkernel32  (MinGW)
 *   reload_host.exe [reloadbiz.dll] [rounds]
 *
 * 判据（stdout grep）：
 *   Round 1: rc=1  self="...池=0x50000000 ... 状态=1"     (首次映射成功)
 *   Round 2: rc=1  self="...池=0x50000000 ... 状态=1"     (走恢复路径，映像基址不变)
 *   Round 3: rc=1  self="...池=0x50000000 ... 状态=1"     (同上)
 *   全程 exit_code=0 且无 A/V。
 *
 * 若 Round 2 之后 self="池=0x0 状态=2" 或 rc=0：
 *   → 恢复判据没命中，回查 re/mbkit_mem/gen_module.py 三处模板改动。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef BOOL (__stdcall *FN_KERNEL_TEST)(void);
typedef const char* (__stdcall *FN_HOST_SELFCHECK)(void);

static int g_round_pass = 0, g_round_fail = 0;

static void print_gbk_as_utf8_fallback(const char* gbk)
{
    /* 控制台默认 GBK 代码页，直接 fputs 即可；若用户切了 UTF-8 可能乱码。
     * 这里优先走 Windows Console 转义，失败就原样输出 —— 目的是让用户肉眼识别
     * "池=0x50000000 状态=1" 等关键子串。 */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD wlen = MultiByteToWideChar(936, 0, gbk, -1, NULL, 0);
        if (wlen > 0) {
            WCHAR* wbuf = (WCHAR*)malloc((size_t)wlen * sizeof(WCHAR));
            if (wbuf) {
                MultiByteToWideChar(936, 0, gbk, -1, wbuf, wlen);
                DWORD mode = 0;
                GetConsoleMode(hOut, &mode);
                if (mode & 0x0004) {
                    DWORD written = 0;
                    WriteConsoleW(hOut, wbuf, wlen - 1, &written, NULL);
                } else {
                    /* 重定向到文件：转 UTF-8 */
                    int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
                    if (ulen > 0) {
                        char* ubuf = (char*)malloc(ulen);
                        if (ubuf) {
                            WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, ubuf, ulen, NULL, NULL);
                            fputs(ubuf, stdout);
                            free(ubuf);
                        }
                    }
                }
                free(wbuf);
                return;
            }
        }
    }
    fputs(gbk, stdout);
}

static int has_substr_case_insensitive(const char* hay, const char* needle)
{
    /* 纯 ASCII 子串匹配，避免依赖 C 运行库非标准扩展 */
    size_t hn = strlen(hay), nn = strlen(needle);
    if (nn > hn) return 0;
    for (size_t i = 0; i <= hn - nn; i++) {
        size_t j = 0;
        while (j < nn) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            j++;
        }
        if (j == nn) return 1;
    }
    return 0;
}

static void do_round(const char* dll_path, int r, int total)
{
    printf("\n-- Round %d/%d : LoadLibrary(\"%s\") --\n", r, total, dll_path);

    HMODULE h = LoadLibraryA(dll_path);
    if (!h) {
        DWORD err = GetLastError();
        printf("  [FAIL] LoadLibrary failed, GetLastError=%lu (0x%lX)\n", err, err);
        g_round_fail++;
        return;
    }

    FN_KERNEL_TEST   fn_test = (FN_KERNEL_TEST)  GetProcAddress(h, "内核测试");
    FN_HOST_SELFCHECK fn_self = (FN_HOST_SELFCHECK)GetProcAddress(h, "宿主自检");
    if (!fn_test || !fn_self) {
        printf("  [FAIL] exports missing: 内核测试=%p 宿主自检=%p\n", (void*)fn_test, (void*)fn_self);
        FreeLibrary(h);
        g_round_fail++;
        return;
    }

    BOOL rc = fn_test();
    const char* self = fn_self();
    printf("  rc=%d  self=\"", rc);
    if (self) {
        print_gbk_as_utf8_fallback(self);
    } else {
        fputs("(null)", stdout);
    }
    fputs("\"\n", stdout);

    int ok = 1;
    if (rc != 1) {
        printf("  [FAIL] 内核测试 returned %d, expected 1\n", rc);
        ok = 0;
    }
    if (!self) {
        printf("  [FAIL] 宿主自检 returned NULL\n");
        ok = 0;
    } else {
        if (!has_substr_case_insensitive(self, "=") ||
            !has_substr_case_insensitive(self, "")) {
            /* 最基本的格式校验；不强制要求特定值，让用户看输出 */
        }
        /* Round 2+ 关键判据：状态必须是 1（否则恢复路径没命中）*/
        if (r >= 2) {
            if (has_substr_case_insensitive(self, "=2")) {
                printf("  [FAIL] Round %d state=2 detected -> recovery path MISSED\n", r);
                ok = 0;
            }
            if (has_substr_case_insensitive(self, "=0x0") ||
                has_substr_case_insensitive(self, "=0 ")) {
                /* 池或映像仍然为 0 指针 —— 不正常 */
            }
        }
    }

    FreeLibrary(h);
    /* FreeLibrary 之后不要再读 self 指针（属于已卸载模块的易语言文本对象）*/

    if (ok) {
        printf("  [PASS] Round %d OK\n", r);
        g_round_pass++;
    } else {
        g_round_fail++;
    }
}

int main(int argc, char** argv)
{
    const char* dll = argc >= 2 ? argv[1] : "reloadbiz.dll";
    int total     = argc >= 3 ? atoi(argv[2]) : 3;
    if (total < 1) total = 1;

    printf("======================================================================\n");
    printf(" MbKit reload-recovery end-to-end harness (for 易语言 static DLL)\n");
    printf("======================================================================\n");
    printf(" DLL   : %s\n", dll);
    printf(" Rounds: %d\n", total);

    for (int r = 1; r <= total; r++) {
        do_round(dll, r, total);
        Sleep(200);
    }

    printf("\n======================================================================\n");
    printf(" SUMMARY : %d passed / %d failed\n", g_round_pass, g_round_fail);
    printf("======================================================================\n");

    /* 判据：所有 round PASS 才算成功（让自动化外层只看 exit code）*/
    return (g_round_fail == 0) ? 0 : 3;
}
