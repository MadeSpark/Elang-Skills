/* selftest.h —— 隐藏自检入口 */
#ifndef QAQ_GUI_SELFTEST_H
#define QAQ_GUI_SELFTEST_H

#include <wchar.h>

/* srcArg / outArg 可为 NULL（使用默认路径） */
int SelfTest(const wchar_t *cliArg, const wchar_t *logPath,
             const wchar_t *srcArg, const wchar_t *outArg);

#endif
