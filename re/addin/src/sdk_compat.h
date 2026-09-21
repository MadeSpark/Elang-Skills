/*
 * sdk_compat.h —— 易语言官方 SDK 的 MinGW/g++ 兼容垫片
 * ---------------------------------------------------------------------------
 * 官方 SDK（D:\ides\e\sdk\cpp\elib）是给 VC6 + MFC 用的：
 *   1) mtypes.h 里 `#define WINAPI`（空）——SDK 内所有回调/导出全部按 __cdecl 约定；
 *   2) mtypes.h 与 <windows.h> 存在大量类型重定义（HWND、BOOL、LONG、WINAPI…），
 *      两者不能同时直接包含；
 *   3) 样例 HtmlView.cpp 依赖 stdafx.h/resource.h/HtmlView.h/hhctrl.h（MFC 系）。
 *
 * 本垫片只做最少的事：用 <windows.h> 提供基础 Win32 类型，然后把 WINAPI 恢复成
 * SDK 约定的“空”（=__cdecl），再补齐 windows.h 没有的 SDK 私有类型（INT/DATE/INT64…），
 * 最后按正确顺序引入 elib 里的三个头文件。
 *
 * ⚠️ 关键点：windows.h 中已经被解析过的原型（如 CreateThread 等）依旧保持其真实的
 *    __stdcall 链接约定——宏在此刻才被改写，不影响已展开的声明。
 */
#ifndef ELANG_ADDIN_SDK_COMPAT_H
#define ELANG_ADDIN_SDK_COMPAT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>

/* --- 恢复 SDK 的调用约定：WINAPI == 空 == __cdecl --------------------------- */
#ifdef WINAPI
#undef WINAPI
#endif
#define WINAPI

/* --- windows.h 在 Win32 下并不定义 NEAR/FAR 为空宏，补齐 --------------------- */
#ifndef FAR
#define FAR
#endif
#ifndef NEAR
#define NEAR
#endif

/* --- 补齐 SDK 私有类型（windows.h 未定义） -------------------------------- */
typedef int         INT;        /* 易语言 SDK 的 INT */
typedef float       FLOAT;      /* 易语言 SDK 的 FLOAT */
typedef double      DOUBLE;     /* 易语言 SDK 的 DOUBLE */
typedef double      DATE;       /* 易语言 SDK 的 DATE */
typedef long long   INT64;      /* 易语言 SDK 的 INT64 */
typedef DATE       *PDATE;      /* 易语言 SDK 的 PDATE */

/* mtypes.h 里 `#define _T(string) string`；lib2.h 依赖它 */
#ifndef _T
#define _T(x) x
#endif

/* --- 按正确顺序引入官方 SDK 头 ------------------------------------------- */
#include "lib2.h"                 /* 核心：LIB_INFO / 通知码 / MDATA_INF … */
#include "lang.h"                 /* __GBK_LANG_VER 等 */
#include "PublicIDEFunctions.h"   /* FN_* IDE 功能号（NES_RUN_FUNC 用） */

#endif /* ELANG_ADDIN_SDK_COMPAT_H */
