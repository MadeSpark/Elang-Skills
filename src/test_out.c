/*
 * test_out.c —— 控制台中文乱码的 A/B 对照测试（回归工具，纯进程内运行）
 *
 * 背景：Windows 控制台默认代码页是 936(GBK)。本程序内部文本是 UTF-8，
 * 若把 UTF-8 字节直接 printf/WriteFile 进控制台，中文会显示成
 * 「瀹屾垚: 77/77 涓敮鎸佸簱...」这类乱码（用户实测截图就是这个）。
 * 修法是 out_write()：stdout 挂在控制台上时转 UTF-16 走 WriteConsoleW。
 *
 * 本测试直接 #include elibdoc.c，复用里面**真实**的 out_write/op_printf，
 * 用 CreateConsoleScreenBuffer 造两块真正的控制台屏幕缓冲区对照：
 *   A 块：旧行为，UTF-8 字节 WriteFile 直写       → 应读回乱码「瀹屾垚」
 *   B 块：新行为，走 out_write（WriteConsoleW）  → 应读回「完成」
 * 最后 ReadConsoleOutputCharacterW 读回判定。
 *
 * 编译运行（32 位；必须在有控制台的终端里跑）：
 *   export PATH="/c/msys64/mingw32/bin:$PATH"
 *   gcc -O2 -static -finput-charset=UTF-8 -fexec-charset=UTF-8 \
 *       -o test_out.exe test_out.c -ladvapi32
 *   ./test_out.exe
 */
#include <wchar.h>
#define main elibdoc_main_unused          /* elibdoc.c 里的 main 改名，避免冲突 */
#include "elibdoc.c"
#undef main

static int read_buf(HANDLE h, wchar_t *out, int n)
{
    CONSOLE_SCREEN_BUFFER_INFO ci;
    COORD c0 = {0, 0};
    DWORD got = 0;
    out[0] = 0;
    if (!GetConsoleScreenBufferInfo(h, &ci)) return 0;
    if (ci.dwSize.X <= 0 || n <= 1) return 0;
    if (!ReadConsoleOutputCharacterW(h, out, (DWORD)(n - 1), c0, &got)) return 0;
    out[got < (DWORD)(n - 1) ? got : (DWORD)(n - 1)] = 0;
    return 1;
}

static const char *w2u(const wchar_t *w)
{
    static char buf[2048];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf) - 1, NULL, NULL);
    buf[n > 0 ? n - 1 : 0] = 0;
    return buf;
}

int main(void)
{
    const char *sample = "完成: 77/77 个支持库，可用命令 5951 条";
    HANDLE oldout = GetStdHandle(STD_OUTPUT_HANDLE);
    int bad = 0;

    if (!GetConsoleOutputCP()) { printf("SKIP: 本进程没有控制台\n"); return 2; }
    printf("控制台代码页 = %u\n", GetConsoleOutputCP());

    HANDLE hA = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    HANDLE hB = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    if (hA == INVALID_HANDLE_VALUE || hB == INVALID_HANDLE_VALUE) {
        printf("SKIP: CreateConsoleScreenBuffer 失败 (err=%lu)\n", GetLastError());
        return 2;
    }

    /* ---- A：旧行为，UTF-8 字节直接 WriteFile（控制台按 936 解码）---- */
    DWORD w = 0;
    WriteFile(hA, sample, (DWORD)strlen(sample), &w, NULL);

    /* ---- B：新行为，out_write 走 WriteConsoleW ---- */
    SetStdHandle(STD_OUTPUT_HANDLE, hB);
    op_printf("%s\n", sample);
    fflush(stdout);
    SetStdHandle(STD_OUTPUT_HANDLE, oldout);

    wchar_t a[512], b[512];
    if (!read_buf(hA, a, 512) || !read_buf(hB, b, 512)) {
        printf("SKIP: 读屏幕缓冲区失败 (err=%lu)\n", GetLastError());
        return 2;
    }

    printf("A 块（旧行为·裸写 UTF-8 字节）读回: %s\n", w2u(a));
    printf("B 块（新行为·out_write）    读回: %s\n", w2u(b));

    printf("---- 判定 ----\n");
    if (!wcsstr(b, L"完成") || !wcsstr(b, L"个支持库")) {
        printf("  [FAIL] WriteConsoleW 路径没有写出正确中文\n"); bad++;
    } else printf("  [OK] 控制台收到的是真 Unicode「完成 / 个支持库」\n");
    if (!wcsstr(a, L"瀹屾垚")) {
        printf("  [WARN] A 块没复现乱码（该控制台代码页可能不是 936），对照意义有限\n");
    } else printf("  [OK] 对照成立：旧行为在 936 控制台下确实产生「瀹屾垚」乱码\n");

    CloseHandle(hA);
    CloseHandle(hB);
    printf("结果: %s\n", bad ? "有失败" : "通过");
    return bad;
}
