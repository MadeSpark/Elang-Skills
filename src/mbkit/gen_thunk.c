/* 本文件由 gen_fwd.py 自动生成 —— 请勿手改。
 * 数据来源：易语言模块「miniblink」的 .DLL命令 声明表（e2t 产物）。
 * 重新生成：python gen_fwd.py <清单.json>
 */
/* 编组 thunk —— 本文件由 gen_fwd.py 自动生成，请勿手改。
 * 每个导出 = 调用线程侧收参打包(tramp 指针+参数包) + K 侧签名化执行体。
 * 详见 mbk_marshal.h 与 mbkit_fwd.c 的 mbk_marshal_exec。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "gen_fwd.h"
#include "mbk_marshal.h"

/* 小数型按位型搬运（4/8 字节，不经浮点寄存器；memcpy 免严格别名问题） */
static long long mbk_f32bits(float f)  { unsigned u; memcpy(&u, &f, 4); return (long long)u; }
static float     mbk_bitsf32(long long v) { unsigned u = (unsigned)(unsigned long long)v; float f; memcpy(&f, &u, 4); return f; }
static long long mbk_f64bits(double d) { long long r; memcpy(&r, &d, 8); return r; }
static double    mbk_bitsf64(long long v){ double d; memcpy(&d, &v, 8); return d; }

static void tr_JsBindGetter(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl JsBindGetter(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_JsBindGetter; c.tramp = tr_JsBindGetter; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_JsBindSetter(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl JsBindSetter(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_JsBindSetter; c.tramp = tr_JsBindSetter; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_jsArg(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1) = (long long (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

long long __cdecl jsArg(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsArg; c.tramp = tr_jsArg; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsArgCount(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl jsArgCount(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsArgCount; c.tramp = tr_jsArgCount; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsArgType(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1) = (unsigned (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

unsigned __cdecl jsArgType(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsArgType; c.tramp = tr_jsArgType; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsArrayBuffer(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1, unsigned a2) = (long long (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

long long __cdecl jsArrayBuffer(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsArrayBuffer; c.tramp = tr_jsArrayBuffer; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsBindFunction(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl jsBindFunction(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsBindFunction; c.tramp = tr_jsBindFunction; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_jsBoolean(MBKCALL* c)
{
    long long (*fn)(unsigned a0) = (long long (__cdecl *)(unsigned a0))c->real;
    c->ret.l = fn((unsigned)c->a[0]);
}

long long __cdecl jsBoolean(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsBoolean; c.tramp = tr_jsBoolean; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsCall(MBKCALL* c)
{
    long long (*fn)(unsigned a0, long long a1, long long a2, long long a3, unsigned a4) = (long long (__cdecl *)(unsigned a0, long long a1, long long a2, long long a3, unsigned a4))c->real;
    c->ret.l = fn((unsigned)c->a[0], c->a[1], c->a[2], c->a[3], (unsigned)c->a[4]);
}

long long __cdecl jsCall(unsigned a0, long long a1, long long a2, long long a3, unsigned a4)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsCall; c.tramp = tr_jsCall; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    c.a[2] = a2;
    c.a[3] = a3;
    c.a[4] = (long long)(unsigned)a4;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsCallGlobal(MBKCALL* c)
{
    long long (*fn)(unsigned a0, long long a1, long long a2, unsigned a3) = (long long (__cdecl *)(unsigned a0, long long a1, long long a2, unsigned a3))c->real;
    c->ret.l = fn((unsigned)c->a[0], c->a[1], c->a[2], (unsigned)c->a[3]);
}

long long __cdecl jsCallGlobal(unsigned a0, long long a1, long long a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsCallGlobal; c.tramp = tr_jsCallGlobal; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    c.a[2] = a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsDeleteObjectProp(MBKCALL* c)
{
    void (*fn)(unsigned a0, long long a1, unsigned a2) = (void (__cdecl *)(unsigned a0, long long a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], c->a[1], (unsigned)c->a[2]);
}

void __cdecl jsDeleteObjectProp(unsigned a0, long long a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsDeleteObjectProp; c.tramp = tr_jsDeleteObjectProp; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_jsDouble(MBKCALL* c)
{
    long long (*fn)(double a0) = (long long (__cdecl *)(double a0))c->real;
    c->ret.l = fn(mbk_bitsf64(c->a[0]));
}

long long __cdecl jsDouble(double a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsDouble; c.tramp = tr_jsDouble; c.kind = 2;
    c.a[0] = mbk_f64bits(a0);
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsEmptyArray(MBKCALL* c)
{
    long long (*fn)(unsigned a0) = (long long (__cdecl *)(unsigned a0))c->real;
    c->ret.l = fn((unsigned)c->a[0]);
}

long long __cdecl jsEmptyArray(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsEmptyArray; c.tramp = tr_jsEmptyArray; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsEmptyObject(MBKCALL* c)
{
    long long (*fn)(unsigned a0) = (long long (__cdecl *)(unsigned a0))c->real;
    c->ret.l = fn((unsigned)c->a[0]);
}

long long __cdecl jsEmptyObject(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsEmptyObject; c.tramp = tr_jsEmptyObject; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsEvalExW(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1, unsigned a2) = (long long (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

long long __cdecl jsEvalExW(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsEvalExW; c.tramp = tr_jsEvalExW; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsEvalW(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1) = (long long (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

long long __cdecl jsEvalW(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsEvalW; c.tramp = tr_jsEvalW; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsFalse(MBKCALL* c)
{
    long long (*fn)(void) = (long long (__cdecl *)(void))c->real;
    c->ret.l = fn();
}

long long __cdecl jsFalse(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsFalse; c.tramp = tr_jsFalse; c.kind = 2;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsFloat(MBKCALL* c)
{
    long long (*fn)(float a0) = (long long (__cdecl *)(float a0))c->real;
    c->ret.l = fn(mbk_bitsf32(c->a[0]));
}

long long __cdecl jsFloat(float a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsFloat; c.tramp = tr_jsFloat; c.kind = 2;
    c.a[0] = mbk_f32bits(a0);
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsFunction(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1) = (long long (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

long long __cdecl jsFunction(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsFunction; c.tramp = tr_jsFunction; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsGC(MBKCALL* c)
{
    void (*fn)(void) = (void (__cdecl *)(void))c->real;
    fn();
}

void __cdecl jsGC(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGC; c.tramp = tr_jsGC; c.kind = 0;
    mbk_marshal_exec(&c);
}

static void tr_jsGet(MBKCALL* c)
{
    long long (*fn)(unsigned a0, long long a1, unsigned a2) = (long long (__cdecl *)(unsigned a0, long long a1, unsigned a2))c->real;
    c->ret.l = fn((unsigned)c->a[0], c->a[1], (unsigned)c->a[2]);
}

long long __cdecl jsGet(unsigned a0, long long a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGet; c.tramp = tr_jsGet; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsGetArrayBuffer(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, long long a1) = (unsigned (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], c->a[1]);
}

unsigned __cdecl jsGetArrayBuffer(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGetArrayBuffer; c.tramp = tr_jsGetArrayBuffer; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsGetAt(MBKCALL* c)
{
    long long (*fn)(unsigned a0, long long a1, unsigned a2) = (long long (__cdecl *)(unsigned a0, long long a1, unsigned a2))c->real;
    c->ret.l = fn((unsigned)c->a[0], c->a[1], (unsigned)c->a[2]);
}

long long __cdecl jsGetAt(unsigned a0, long long a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGetAt; c.tramp = tr_jsGetAt; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsGetData(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, long long a1) = (unsigned (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], c->a[1]);
}

unsigned __cdecl jsGetData(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGetData; c.tramp = tr_jsGetData; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsGetGlobal(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1) = (long long (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

long long __cdecl jsGetGlobal(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGetGlobal; c.tramp = tr_jsGetGlobal; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsGetKeys(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, long long a1) = (unsigned (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], c->a[1]);
}

unsigned __cdecl jsGetKeys(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGetKeys; c.tramp = tr_jsGetKeys; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsGetLastErrorIfException(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl jsGetLastErrorIfException(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGetLastErrorIfException; c.tramp = tr_jsGetLastErrorIfException; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsGetLength(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, long long a1) = (unsigned (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], c->a[1]);
}

unsigned __cdecl jsGetLength(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGetLength; c.tramp = tr_jsGetLength; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsGetWebView(MBKCALL* c)
{
    long long (*fn)(unsigned a0) = (long long (__cdecl *)(unsigned a0))c->real;
    c->ret.l = fn((unsigned)c->a[0]);
}

long long __cdecl jsGetWebView(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGetWebView; c.tramp = tr_jsGetWebView; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsGlobalObject(MBKCALL* c)
{
    long long (*fn)(unsigned a0) = (long long (__cdecl *)(unsigned a0))c->real;
    c->ret.l = fn((unsigned)c->a[0]);
}

long long __cdecl jsGlobalObject(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsGlobalObject; c.tramp = tr_jsGlobalObject; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsInt(MBKCALL* c)
{
    long long (*fn)(unsigned a0) = (long long (__cdecl *)(unsigned a0))c->real;
    c->ret.l = fn((unsigned)c->a[0]);
}

long long __cdecl jsInt(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsInt; c.tramp = tr_jsInt; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsIsArray(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsIsArray(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsArray; c.tramp = tr_jsIsArray; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsBoolean(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsIsBoolean(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsBoolean; c.tramp = tr_jsIsBoolean; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsFalse(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsIsFalse(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsFalse; c.tramp = tr_jsIsFalse; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsFunction(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsIsFunction(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsFunction; c.tramp = tr_jsIsFunction; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsJsValueValid(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, long long a1) = (unsigned (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], c->a[1]);
}

unsigned __cdecl jsIsJsValueValid(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsJsValueValid; c.tramp = tr_jsIsJsValueValid; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsNull(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsIsNull(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsNull; c.tramp = tr_jsIsNull; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsNumber(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsIsNumber(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsNumber; c.tramp = tr_jsIsNumber; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsObject(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsIsObject(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsObject; c.tramp = tr_jsIsObject; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsString(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsIsString(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsString; c.tramp = tr_jsIsString; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsTrue(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsIsTrue(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsTrue; c.tramp = tr_jsIsTrue; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsUndefined(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsIsUndefined(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsUndefined; c.tramp = tr_jsIsUndefined; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsIsValidExecState(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl jsIsValidExecState(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsIsValidExecState; c.tramp = tr_jsIsValidExecState; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsNull(MBKCALL* c)
{
    long long (*fn)(void) = (long long (__cdecl *)(void))c->real;
    c->ret.l = fn();
}

long long __cdecl jsNull(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsNull; c.tramp = tr_jsNull; c.kind = 2;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsObject(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1) = (long long (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

long long __cdecl jsObject(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsObject; c.tramp = tr_jsObject; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsSet(MBKCALL* c)
{
    void (*fn)(unsigned a0, long long a1, unsigned a2, long long a3) = (void (__cdecl *)(unsigned a0, long long a1, unsigned a2, long long a3))c->real;
    fn((unsigned)c->a[0], c->a[1], (unsigned)c->a[2], c->a[3]);
}

void __cdecl jsSet(unsigned a0, long long a1, unsigned a2, long long a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsSet; c.tramp = tr_jsSet; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = a3;
    mbk_marshal_exec(&c);
}

static void tr_jsSetAt(MBKCALL* c)
{
    void (*fn)(unsigned a0, long long a1, unsigned a2, long long a3) = (void (__cdecl *)(unsigned a0, long long a1, unsigned a2, long long a3))c->real;
    fn((unsigned)c->a[0], c->a[1], (unsigned)c->a[2], c->a[3]);
}

void __cdecl jsSetAt(unsigned a0, long long a1, unsigned a2, long long a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsSetAt; c.tramp = tr_jsSetAt; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = a3;
    mbk_marshal_exec(&c);
}

static void tr_jsSetGlobal(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1, long long a2) = (long long (__cdecl *)(unsigned a0, unsigned a1, long long a2))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1], c->a[2]);
}

long long __cdecl jsSetGlobal(unsigned a0, unsigned a1, long long a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsSetGlobal; c.tramp = tr_jsSetGlobal; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = a2;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsSetLength(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, long long a1, unsigned a2) = (unsigned (__cdecl *)(unsigned a0, long long a1, unsigned a2))c->real;
    c->ret.u = fn((unsigned)c->a[0], c->a[1], (unsigned)c->a[2]);
}

unsigned __cdecl jsSetLength(unsigned a0, long long a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsSetLength; c.tramp = tr_jsSetLength; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsStringW(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1) = (long long (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

long long __cdecl jsStringW(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsStringW; c.tramp = tr_jsStringW; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsThrowException(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1) = (long long (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

long long __cdecl jsThrowException(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsThrowException; c.tramp = tr_jsThrowException; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsToBoolean(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, long long a1) = (unsigned (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], c->a[1]);
}

unsigned __cdecl jsToBoolean(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsToBoolean; c.tramp = tr_jsToBoolean; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_jsToDouble(MBKCALL* c)
{
    double (*fn)(unsigned a0, long long a1) = (double (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.d = fn((unsigned)c->a[0], c->a[1]);
}

double __cdecl jsToDouble(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsToDouble; c.tramp = tr_jsToDouble; c.kind = 4;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return c.ret.d;
}

static void tr_jsToFloat(MBKCALL* c)
{
    float (*fn)(unsigned a0, long long a1) = (float (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.f = fn((unsigned)c->a[0], c->a[1]);
}

float __cdecl jsToFloat(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsToFloat; c.tramp = tr_jsToFloat; c.kind = 3;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return c.ret.f;
}

static void tr_jsToInt(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, long long a1) = (unsigned (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], c->a[1]);
}

unsigned __cdecl jsToInt(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsToInt; c.tramp = tr_jsToInt; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsToStringW(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, long long a1) = (unsigned (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], c->a[1]);
}

unsigned __cdecl jsToStringW(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsToStringW; c.tramp = tr_jsToStringW; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsToTempStringW(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, long long a1) = (unsigned (__cdecl *)(unsigned a0, long long a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], c->a[1]);
}

unsigned __cdecl jsToTempStringW(unsigned a0, long long a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsToTempStringW; c.tramp = tr_jsToTempStringW; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsTrue(MBKCALL* c)
{
    long long (*fn)(void) = (long long (__cdecl *)(void))c->real;
    c->ret.l = fn();
}

long long __cdecl jsTrue(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsTrue; c.tramp = tr_jsTrue; c.kind = 2;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_jsTypeOf(MBKCALL* c)
{
    unsigned (*fn)(long long a0) = (unsigned (__cdecl *)(long long a0))c->real;
    c->ret.u = fn(c->a[0]);
}

unsigned __cdecl jsTypeOf(long long a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsTypeOf; c.tramp = tr_jsTypeOf; c.kind = 1;
    c.a[0] = a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_jsUndefined(MBKCALL* c)
{
    long long (*fn)(void) = (long long (__cdecl *)(void))c->real;
    c->ret.l = fn();
}

long long __cdecl jsUndefined(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_jsUndefined; c.tramp = tr_jsUndefined; c.kind = 2;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_wkeAddDirtyArea(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3], (unsigned)c->a[4]);
}

void __cdecl wkeAddDirtyArea(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeAddDirtyArea; c.tramp = tr_wkeAddDirtyArea; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    c.a[4] = (long long)(unsigned)a4;
    mbk_marshal_exec(&c);
}

static void tr_wkeAddPluginDirectory(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeAddPluginDirectory(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeAddPluginDirectory; c.tramp = tr_wkeAddPluginDirectory; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeCanGoBack(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeCanGoBack(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeCanGoBack; c.tramp = tr_wkeCanGoBack; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeCanGoForward(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeCanGoForward(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeCanGoForward; c.tramp = tr_wkeCanGoForward; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeCreateStringW(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1) = (unsigned (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

unsigned __cdecl wkeCreateStringW(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeCreateStringW; c.tramp = tr_wkeCreateStringW; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeCreateWebView(MBKCALL* c)
{
    unsigned (*fn)(void) = (unsigned (__cdecl *)(void))c->real;
    c->ret.u = fn();
}

unsigned __cdecl wkeCreateWebView(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeCreateWebView; c.tramp = tr_wkeCreateWebView; c.kind = 1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeCreateWebWindow(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3], (unsigned)c->a[4], (unsigned)c->a[5]);
}

unsigned __cdecl wkeCreateWebWindow(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeCreateWebWindow; c.tramp = tr_wkeCreateWebWindow; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    c.a[4] = (long long)(unsigned)a4;
    c.a[5] = (long long)(unsigned)a5;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeDeleteString(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeDeleteString(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeDeleteString; c.tramp = tr_wkeDeleteString; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeDestroyWebView(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeDestroyWebView(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeDestroyWebView; c.tramp = tr_wkeDestroyWebView; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeDestroyWebWindow(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeDestroyWebWindow(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeDestroyWebWindow; c.tramp = tr_wkeDestroyWebWindow; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeEditorCopy(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeEditorCopy(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeEditorCopy; c.tramp = tr_wkeEditorCopy; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeEditorCut(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeEditorCut(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeEditorCut; c.tramp = tr_wkeEditorCut; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeEditorDelete(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeEditorDelete(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeEditorDelete; c.tramp = tr_wkeEditorDelete; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeEditorPaste(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeEditorPaste(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeEditorPaste; c.tramp = tr_wkeEditorPaste; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeEditorRedo(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeEditorRedo(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeEditorRedo; c.tramp = tr_wkeEditorRedo; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeEditorSelectAll(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeEditorSelectAll(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeEditorSelectAll; c.tramp = tr_wkeEditorSelectAll; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeEditorUnSelect(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeEditorUnSelect(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeEditorUnSelect; c.tramp = tr_wkeEditorUnSelect; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeEditorUndo(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeEditorUndo(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeEditorUndo; c.tramp = tr_wkeEditorUndo; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeEnableWindow(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeEnableWindow(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeEnableWindow; c.tramp = tr_wkeEnableWindow; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeFinalize(MBKCALL* c)
{
    void (*fn)(void) = (void (__cdecl *)(void))c->real;
    fn();
}

void __cdecl wkeFinalize(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeFinalize; c.tramp = tr_wkeFinalize; c.kind = 0;
    mbk_marshal_exec(&c);
}

static void tr_wkeFireContextMenuEvent(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

unsigned __cdecl wkeFireContextMenuEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeFireContextMenuEvent; c.tramp = tr_wkeFireContextMenuEvent; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeFireKeyDownEvent(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

unsigned __cdecl wkeFireKeyDownEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeFireKeyDownEvent; c.tramp = tr_wkeFireKeyDownEvent; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeFireKeyPressEvent(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

unsigned __cdecl wkeFireKeyPressEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeFireKeyPressEvent; c.tramp = tr_wkeFireKeyPressEvent; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeFireKeyUpEvent(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

unsigned __cdecl wkeFireKeyUpEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeFireKeyUpEvent; c.tramp = tr_wkeFireKeyUpEvent; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeFireMouseEvent(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3], (unsigned)c->a[4]);
}

unsigned __cdecl wkeFireMouseEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeFireMouseEvent; c.tramp = tr_wkeFireMouseEvent; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    c.a[4] = (long long)(unsigned)a4;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeFireMouseWheelEvent(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3], (unsigned)c->a[4]);
}

unsigned __cdecl wkeFireMouseWheelEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeFireMouseWheelEvent; c.tramp = tr_wkeFireMouseWheelEvent; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    c.a[4] = (long long)(unsigned)a4;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeFireWindowsMessage(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3], (unsigned)c->a[4], (unsigned)c->a[5]);
}

unsigned __cdecl wkeFireWindowsMessage(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeFireWindowsMessage; c.tramp = tr_wkeFireWindowsMessage; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    c.a[4] = (long long)(unsigned)a4;
    c.a[5] = (long long)(unsigned)a5;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeGC(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeGC(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGC; c.tramp = tr_wkeGC; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeGetBlinkMainThreadIsolate(MBKCALL* c)
{
    unsigned (*fn)(void) = (unsigned (__cdecl *)(void))c->real;
    c->ret.u = fn();
}

unsigned __cdecl wkeGetBlinkMainThreadIsolate(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetBlinkMainThreadIsolate; c.tramp = tr_wkeGetBlinkMainThreadIsolate; c.kind = 1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetCaretRect(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeGetCaretRect(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetCaretRect; c.tramp = tr_wkeGetCaretRect; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeGetContentHeight(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetContentHeight(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetContentHeight; c.tramp = tr_wkeGetContentHeight; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetContentWidth(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetContentWidth(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetContentWidth; c.tramp = tr_wkeGetContentWidth; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetCookie(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetCookie(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetCookie; c.tramp = tr_wkeGetCookie; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetCookieW(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetCookieW(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetCookieW; c.tramp = tr_wkeGetCookieW; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetCursorInfoType(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetCursorInfoType(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetCursorInfoType; c.tramp = tr_wkeGetCursorInfoType; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetFrameUrl(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1) = (unsigned (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

unsigned __cdecl wkeGetFrameUrl(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetFrameUrl; c.tramp = tr_wkeGetFrameUrl; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetGlobalExecByFrame(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1) = (unsigned (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

unsigned __cdecl wkeGetGlobalExecByFrame(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetGlobalExecByFrame; c.tramp = tr_wkeGetGlobalExecByFrame; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetHeight(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetHeight(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetHeight; c.tramp = tr_wkeGetHeight; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetHostHWND(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetHostHWND(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetHostHWND; c.tramp = tr_wkeGetHostHWND; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetMediaVolume(MBKCALL* c)
{
    float (*fn)(unsigned a0) = (float (__cdecl *)(unsigned a0))c->real;
    c->ret.f = fn((unsigned)c->a[0]);
}

float __cdecl wkeGetMediaVolume(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetMediaVolume; c.tramp = tr_wkeGetMediaVolume; c.kind = 3;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.f;
}

static void tr_wkeGetName(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetName(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetName; c.tramp = tr_wkeGetName; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetSource(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetSource(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetSource; c.tramp = tr_wkeGetSource; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetString(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetString(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetString; c.tramp = tr_wkeGetString; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetStringW(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetStringW(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetStringW; c.tramp = tr_wkeGetStringW; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetTitle(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetTitle(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetTitle; c.tramp = tr_wkeGetTitle; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetTitleW(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetTitleW(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetTitleW; c.tramp = tr_wkeGetTitleW; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetURL(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetURL(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetURL; c.tramp = tr_wkeGetURL; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetUserAgent(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetUserAgent(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetUserAgent; c.tramp = tr_wkeGetUserAgent; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetUserKeyValue(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1) = (unsigned (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

unsigned __cdecl wkeGetUserKeyValue(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetUserKeyValue; c.tramp = tr_wkeGetUserKeyValue; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetVersion(MBKCALL* c)
{
    unsigned (*fn)(void) = (unsigned (__cdecl *)(void))c->real;
    c->ret.u = fn();
}

unsigned __cdecl wkeGetVersion(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetVersion; c.tramp = tr_wkeGetVersion; c.kind = 1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetVersionString(MBKCALL* c)
{
    unsigned (*fn)(void) = (unsigned (__cdecl *)(void))c->real;
    c->ret.u = fn();
}

unsigned __cdecl wkeGetVersionString(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetVersionString; c.tramp = tr_wkeGetVersionString; c.kind = 1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetViewDC(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetViewDC(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetViewDC; c.tramp = tr_wkeGetViewDC; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetWebView(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetWebView(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetWebView; c.tramp = tr_wkeGetWebView; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetWebViewForCurrentContext(MBKCALL* c)
{
    unsigned (*fn)(void) = (unsigned (__cdecl *)(void))c->real;
    c->ret.u = fn();
}

unsigned __cdecl wkeGetWebViewForCurrentContext(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetWebViewForCurrentContext; c.tramp = tr_wkeGetWebViewForCurrentContext; c.kind = 1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetWidth(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetWidth(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetWidth; c.tramp = tr_wkeGetWidth; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetWindowHandle(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGetWindowHandle(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetWindowHandle; c.tramp = tr_wkeGetWindowHandle; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGetZoomFactor(MBKCALL* c)
{
    float (*fn)(unsigned a0) = (float (__cdecl *)(unsigned a0))c->real;
    c->ret.f = fn((unsigned)c->a[0]);
}

float __cdecl wkeGetZoomFactor(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGetZoomFactor; c.tramp = tr_wkeGetZoomFactor; c.kind = 3;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.f;
}

static void tr_wkeGlobalExec(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGlobalExec(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGlobalExec; c.tramp = tr_wkeGlobalExec; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeGoBack(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGoBack(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGoBack; c.tramp = tr_wkeGoBack; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeGoForward(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeGoForward(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeGoForward; c.tramp = tr_wkeGoForward; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeInsertCSSByFrame(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeInsertCSSByFrame(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeInsertCSSByFrame; c.tramp = tr_wkeInsertCSSByFrame; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeIsAwake(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeIsAwake(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsAwake; c.tramp = tr_wkeIsAwake; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsCookieEnabled(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeIsCookieEnabled(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsCookieEnabled; c.tramp = tr_wkeIsCookieEnabled; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsDirty(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeIsDirty(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsDirty; c.tramp = tr_wkeIsDirty; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsDocumentReady(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeIsDocumentReady(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsDocumentReady; c.tramp = tr_wkeIsDocumentReady; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsInitialize(MBKCALL* c)
{
    unsigned (*fn)(void) = (unsigned (__cdecl *)(void))c->real;
    c->ret.u = fn();
}

unsigned __cdecl wkeIsInitialize(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsInitialize; c.tramp = tr_wkeIsInitialize; c.kind = 1;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsLoading(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeIsLoading(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsLoading; c.tramp = tr_wkeIsLoading; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsLoadingCompleted(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeIsLoadingCompleted(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsLoadingCompleted; c.tramp = tr_wkeIsLoadingCompleted; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsLoadingFailed(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeIsLoadingFailed(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsLoadingFailed; c.tramp = tr_wkeIsLoadingFailed; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsLoadingSucceeded(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeIsLoadingSucceeded(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsLoadingSucceeded; c.tramp = tr_wkeIsLoadingSucceeded; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsMainFrame(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1) = (unsigned (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

unsigned __cdecl wkeIsMainFrame(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsMainFrame; c.tramp = tr_wkeIsMainFrame; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsProcessingUserGesture(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeIsProcessingUserGesture(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsProcessingUserGesture; c.tramp = tr_wkeIsProcessingUserGesture; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeIsWebRemoteFrame(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1) = (unsigned (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

unsigned __cdecl wkeIsWebRemoteFrame(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeIsWebRemoteFrame; c.tramp = tr_wkeIsWebRemoteFrame; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return (unsigned char)c.ret.u;
}

static void tr_wkeJsBindFunction(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

void __cdecl wkeJsBindFunction(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeJsBindFunction; c.tramp = tr_wkeJsBindFunction; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
}

static void tr_wkeJsBindGetter(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeJsBindGetter(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeJsBindGetter; c.tramp = tr_wkeJsBindGetter; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeJsBindSetter(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeJsBindSetter(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeJsBindSetter; c.tramp = tr_wkeJsBindSetter; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeKillFocus(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeKillFocus(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeKillFocus; c.tramp = tr_wkeKillFocus; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeLayoutIfNeeded(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeLayoutIfNeeded(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeLayoutIfNeeded; c.tramp = tr_wkeLayoutIfNeeded; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeLoadFileW(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeLoadFileW(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeLoadFileW; c.tramp = tr_wkeLoadFileW; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeLoadHTML(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeLoadHTML(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeLoadHTML; c.tramp = tr_wkeLoadHTML; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeLoadHTMLW(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeLoadHTMLW(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeLoadHTMLW; c.tramp = tr_wkeLoadHTMLW; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeLoadHtmlWithBaseUrl(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeLoadHtmlWithBaseUrl(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeLoadHtmlWithBaseUrl; c.tramp = tr_wkeLoadHtmlWithBaseUrl; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeLoadURL(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeLoadURL(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeLoadURL; c.tramp = tr_wkeLoadURL; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeLoadURLW(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeLoadURLW(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeLoadURLW; c.tramp = tr_wkeLoadURLW; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeLoadW(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeLoadW(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeLoadW; c.tramp = tr_wkeLoadW; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeMoveToCenter(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeMoveToCenter(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeMoveToCenter; c.tramp = tr_wkeMoveToCenter; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeMoveWindow(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3], (unsigned)c->a[4]);
}

void __cdecl wkeMoveWindow(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeMoveWindow; c.tramp = tr_wkeMoveWindow; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    c.a[4] = (long long)(unsigned)a4;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetAddHTTPHeaderFieldToUrlRequest(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeNetAddHTTPHeaderFieldToUrlRequest(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetAddHTTPHeaderFieldToUrlRequest; c.tramp = tr_wkeNetAddHTTPHeaderFieldToUrlRequest; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetCancelRequest(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeNetCancelRequest(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetCancelRequest; c.tramp = tr_wkeNetCancelRequest; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeNetCancelWebUrlRequest(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeNetCancelWebUrlRequest(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetCancelWebUrlRequest; c.tramp = tr_wkeNetCancelWebUrlRequest; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeNetChangeRequestUrl(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeNetChangeRequestUrl(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetChangeRequestUrl; c.tramp = tr_wkeNetChangeRequestUrl; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetContinueJob(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeNetContinueJob(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetContinueJob; c.tramp = tr_wkeNetContinueJob; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetCreateWebUrlRequest(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

unsigned __cdecl wkeNetCreateWebUrlRequest(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetCreateWebUrlRequest; c.tramp = tr_wkeNetCreateWebUrlRequest; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeNetGetExpectedContentLength(MBKCALL* c)
{
    long long (*fn)(unsigned a0) = (long long (__cdecl *)(unsigned a0))c->real;
    c->ret.l = fn((unsigned)c->a[0]);
}

long long __cdecl wkeNetGetExpectedContentLength(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetGetExpectedContentLength; c.tramp = tr_wkeNetGetExpectedContentLength; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_wkeNetGetFavicon(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

unsigned __cdecl wkeNetGetFavicon(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetGetFavicon; c.tramp = tr_wkeNetGetFavicon; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeNetGetHTTPHeaderField(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1) = (unsigned (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

unsigned __cdecl wkeNetGetHTTPHeaderField(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetGetHTTPHeaderField; c.tramp = tr_wkeNetGetHTTPHeaderField; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeNetGetHttpStatusCode(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeNetGetHttpStatusCode(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetGetHttpStatusCode; c.tramp = tr_wkeNetGetHttpStatusCode; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeNetGetMIMEType(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeNetGetMIMEType(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetGetMIMEType; c.tramp = tr_wkeNetGetMIMEType; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetGetPostBody(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeNetGetPostBody(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetGetPostBody; c.tramp = tr_wkeNetGetPostBody; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeNetGetRequestMethod(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeNetGetRequestMethod(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetGetRequestMethod; c.tramp = tr_wkeNetGetRequestMethod; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeNetGetResponseUrl(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeNetGetResponseUrl(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetGetResponseUrl; c.tramp = tr_wkeNetGetResponseUrl; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeNetGetUrlByJob(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeNetGetUrlByJob(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetGetUrlByJob; c.tramp = tr_wkeNetGetUrlByJob; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeNetHoldJobToAsynCommit(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeNetHoldJobToAsynCommit(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetHoldJobToAsynCommit; c.tramp = tr_wkeNetHoldJobToAsynCommit; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetHookRequest(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeNetHookRequest(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetHookRequest; c.tramp = tr_wkeNetHookRequest; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetOnResponse(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeNetOnResponse(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetOnResponse; c.tramp = tr_wkeNetOnResponse; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetSetData(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeNetSetData(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetSetData; c.tramp = tr_wkeNetSetData; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetSetHTTPHeaderField(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

void __cdecl wkeNetSetHTTPHeaderField(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetSetHTTPHeaderField; c.tramp = tr_wkeNetSetHTTPHeaderField; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetSetMIMEType(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeNetSetMIMEType(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetSetMIMEType; c.tramp = tr_wkeNetSetMIMEType; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetSetURL(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeNetSetURL(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetSetURL; c.tramp = tr_wkeNetSetURL; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeNetStartUrlRequest(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (unsigned (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    c->ret.u = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

unsigned __cdecl wkeNetStartUrlRequest(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeNetStartUrlRequest; c.tramp = tr_wkeNetStartUrlRequest; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkePaint(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkePaint(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkePaint; c.tramp = tr_wkePaint; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkePaint2(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5, unsigned a6, unsigned a7, unsigned a8, unsigned a9, unsigned a10) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5, unsigned a6, unsigned a7, unsigned a8, unsigned a9, unsigned a10))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3], (unsigned)c->a[4], (unsigned)c->a[5], (unsigned)c->a[6], (unsigned)c->a[7], (unsigned)c->a[8], (unsigned)c->a[9], (unsigned)c->a[10]);
}

void __cdecl wkePaint2(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5, unsigned a6, unsigned a7, unsigned a8, unsigned a9, unsigned a10)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkePaint2; c.tramp = tr_wkePaint2; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    c.a[4] = (long long)(unsigned)a4;
    c.a[5] = (long long)(unsigned)a5;
    c.a[6] = (long long)(unsigned)a6;
    c.a[7] = (long long)(unsigned)a7;
    c.a[8] = (long long)(unsigned)a8;
    c.a[9] = (long long)(unsigned)a9;
    c.a[10] = (long long)(unsigned)a10;
    mbk_marshal_exec(&c);
}

static void tr_wkePerformCookieCommand(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkePerformCookieCommand(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkePerformCookieCommand; c.tramp = tr_wkePerformCookieCommand; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkePostURL(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

void __cdecl wkePostURL(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkePostURL; c.tramp = tr_wkePostURL; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
}

static void tr_wkePostURLW(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

void __cdecl wkePostURLW(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkePostURLW; c.tramp = tr_wkePostURLW; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
}

static void tr_wkeReload(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeReload(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeReload; c.tramp = tr_wkeReload; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeRepaintIfNeeded(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeRepaintIfNeeded(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeRepaintIfNeeded; c.tramp = tr_wkeRepaintIfNeeded; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeResize(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeResize(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeResize; c.tramp = tr_wkeResize; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeResizeWindow(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeResizeWindow(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeResizeWindow; c.tramp = tr_wkeResizeWindow; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeRunJS(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1) = (long long (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

long long __cdecl wkeRunJS(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeRunJS; c.tramp = tr_wkeRunJS; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_wkeRunJSW(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1) = (long long (__cdecl *)(unsigned a0, unsigned a1))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

long long __cdecl wkeRunJSW(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeRunJSW; c.tramp = tr_wkeRunJSW; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_wkeRunJsByFrame(MBKCALL* c)
{
    long long (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (long long (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    c->ret.l = fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

long long __cdecl wkeRunJsByFrame(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeRunJsByFrame; c.tramp = tr_wkeRunJsByFrame; c.kind = 2;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
    return c.ret.l;
}

static void tr_wkeSetCookie(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeSetCookie(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetCookie; c.tramp = tr_wkeSetCookie; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetCookieEnabled(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetCookieEnabled(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetCookieEnabled; c.tramp = tr_wkeSetCookieEnabled; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetCookieJarFullPath(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetCookieJarFullPath(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetCookieJarFullPath; c.tramp = tr_wkeSetCookieJarFullPath; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetCookieJarPath(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetCookieJarPath(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetCookieJarPath; c.tramp = tr_wkeSetCookieJarPath; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetCspCheckEnable(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetCspCheckEnable(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetCspCheckEnable; c.tramp = tr_wkeSetCspCheckEnable; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetDebugConfig(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeSetDebugConfig(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetDebugConfig; c.tramp = tr_wkeSetDebugConfig; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetDeviceParameter(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, float a4) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, float a4))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3], mbk_bitsf32(c->a[4]));
}

void __cdecl wkeSetDeviceParameter(unsigned a0, unsigned a1, unsigned a2, unsigned a3, float a4)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetDeviceParameter; c.tramp = tr_wkeSetDeviceParameter; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    c.a[4] = mbk_f32bits(a4);
    mbk_marshal_exec(&c);
}

static void tr_wkeSetDirty(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetDirty(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetDirty; c.tramp = tr_wkeSetDirty; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetDragDropEnable(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetDragDropEnable(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetDragDropEnable; c.tramp = tr_wkeSetDragDropEnable; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetDragEnable(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetDragEnable(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetDragEnable; c.tramp = tr_wkeSetDragEnable; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetDragFiles(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3], (unsigned)c->a[4]);
}

void __cdecl wkeSetDragFiles(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetDragFiles; c.tramp = tr_wkeSetDragFiles; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    c.a[4] = (long long)(unsigned)a4;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetEditable(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetEditable(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetEditable; c.tramp = tr_wkeSetEditable; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetFileSystem(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3], (unsigned)c->a[4]);
}

void __cdecl wkeSetFileSystem(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetFileSystem; c.tramp = tr_wkeSetFileSystem; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    c.a[4] = (long long)(unsigned)a4;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetFocus(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeSetFocus(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetFocus; c.tramp = tr_wkeSetFocus; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetHandle(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetHandle(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetHandle; c.tramp = tr_wkeSetHandle; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetHandleOffset(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeSetHandleOffset(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetHandleOffset; c.tramp = tr_wkeSetHandleOffset; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetHeadlessEnabled(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetHeadlessEnabled(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetHeadlessEnabled; c.tramp = tr_wkeSetHeadlessEnabled; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetLocalStorageFullPath(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetLocalStorageFullPath(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetLocalStorageFullPath; c.tramp = tr_wkeSetLocalStorageFullPath; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetMediaVolume(MBKCALL* c)
{
    void (*fn)(unsigned a0, float a1) = (void (__cdecl *)(unsigned a0, float a1))c->real;
    fn((unsigned)c->a[0], mbk_bitsf32(c->a[1]));
}

void __cdecl wkeSetMediaVolume(unsigned a0, float a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetMediaVolume; c.tramp = tr_wkeSetMediaVolume; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = mbk_f32bits(a1);
    mbk_marshal_exec(&c);
}

static void tr_wkeSetMemoryCacheEnable(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetMemoryCacheEnable(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetMemoryCacheEnable; c.tramp = tr_wkeSetMemoryCacheEnable; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetMouseEnabled(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetMouseEnabled(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetMouseEnabled; c.tramp = tr_wkeSetMouseEnabled; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetName(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetName(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetName; c.tramp = tr_wkeSetName; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetNavigationToNewWindowEnable(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetNavigationToNewWindowEnable(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetNavigationToNewWindowEnable; c.tramp = tr_wkeSetNavigationToNewWindowEnable; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetNpapiPluginsEnabled(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetNpapiPluginsEnabled(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetNpapiPluginsEnabled; c.tramp = tr_wkeSetNpapiPluginsEnabled; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetProxy(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeSetProxy(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetProxy; c.tramp = tr_wkeSetProxy; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetResourceGc(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetResourceGc(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetResourceGc; c.tramp = tr_wkeSetResourceGc; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetStringW(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeSetStringW(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetStringW; c.tramp = tr_wkeSetStringW; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetTouchEnabled(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetTouchEnabled(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetTouchEnabled; c.tramp = tr_wkeSetTouchEnabled; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetTransparent(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetTransparent(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetTransparent; c.tramp = tr_wkeSetTransparent; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetUserAgent(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetUserAgent(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetUserAgent; c.tramp = tr_wkeSetUserAgent; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetUserAgentW(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetUserAgentW(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetUserAgentW; c.tramp = tr_wkeSetUserAgentW; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetUserKeyValue(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeSetUserKeyValue(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetUserKeyValue; c.tramp = tr_wkeSetUserKeyValue; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetViewNetInterface(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetViewNetInterface(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetViewNetInterface; c.tramp = tr_wkeSetViewNetInterface; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetViewProxy(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetViewProxy(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetViewProxy; c.tramp = tr_wkeSetViewProxy; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetWindowTitleW(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeSetWindowTitleW(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetWindowTitleW; c.tramp = tr_wkeSetWindowTitleW; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSetZoomFactor(MBKCALL* c)
{
    void (*fn)(unsigned a0, float a1) = (void (__cdecl *)(unsigned a0, float a1))c->real;
    fn((unsigned)c->a[0], mbk_bitsf32(c->a[1]));
}

void __cdecl wkeSetZoomFactor(unsigned a0, float a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSetZoomFactor; c.tramp = tr_wkeSetZoomFactor; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = mbk_f32bits(a1);
    mbk_marshal_exec(&c);
}

static void tr_wkeShowDevtools(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2, unsigned a3) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2, unsigned a3))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2], (unsigned)c->a[3]);
}

void __cdecl wkeShowDevtools(unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeShowDevtools; c.tramp = tr_wkeShowDevtools; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    c.a[3] = (long long)(unsigned)a3;
    mbk_marshal_exec(&c);
}

static void tr_wkeShowWindow(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1) = (void (__cdecl *)(unsigned a0, unsigned a1))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1]);
}

void __cdecl wkeShowWindow(unsigned a0, unsigned a1)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeShowWindow; c.tramp = tr_wkeShowWindow; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    mbk_marshal_exec(&c);
}

static void tr_wkeSleep(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeSleep(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeSleep; c.tramp = tr_wkeSleep; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeStopLoading(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeStopLoading(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeStopLoading; c.tramp = tr_wkeStopLoading; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeUpdate(MBKCALL* c)
{
    void (*fn)(void) = (void (__cdecl *)(void))c->real;
    fn();
}

void __cdecl wkeUpdate(void)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeUpdate; c.tramp = tr_wkeUpdate; c.kind = 0;
    mbk_marshal_exec(&c);
}

static void tr_wkeUtilSerializeToMHTML(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeUtilSerializeToMHTML(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeUtilSerializeToMHTML; c.tramp = tr_wkeUtilSerializeToMHTML; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeVisitAllCookie(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeVisitAllCookie(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeVisitAllCookie; c.tramp = tr_wkeVisitAllCookie; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeWake(MBKCALL* c)
{
    void (*fn)(unsigned a0) = (void (__cdecl *)(unsigned a0))c->real;
    fn((unsigned)c->a[0]);
}

void __cdecl wkeWake(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeWake; c.tramp = tr_wkeWake; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
}

static void tr_wkeWebFrameGetMainFrame(MBKCALL* c)
{
    unsigned (*fn)(unsigned a0) = (unsigned (__cdecl *)(unsigned a0))c->real;
    c->ret.u = fn((unsigned)c->a[0]);
}

unsigned __cdecl wkeWebFrameGetMainFrame(unsigned a0)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeWebFrameGetMainFrame; c.tramp = tr_wkeWebFrameGetMainFrame; c.kind = 1;
    c.a[0] = (long long)(unsigned)a0;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

static void tr_wkeWebFrameGetMainWorldScriptContext(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

void __cdecl wkeWebFrameGetMainWorldScriptContext(unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx = MBK_IDX_wkeWebFrameGetMainWorldScriptContext; c.tramp = tr_wkeWebFrameGetMainWorldScriptContext; c.kind = 0;
    c.a[0] = (long long)(unsigned)a0;
    c.a[1] = (long long)(unsigned)a1;
    c.a[2] = (long long)(unsigned)a2;
    mbk_marshal_exec(&c);
}

static void tr_wkeOnAlertBox(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnConfirmBox(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnConsole(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnCreateView(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnDidCreateScriptContext(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnDocumentReady(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnDocumentReady2(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnDownload(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnLoadUrlBegin(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnLoadUrlEnd(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnLoadingFinish(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnMouseOverUrlChanged(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnNavigation(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnPaintUpdated(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnPromptBox(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnTitleChanged(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnURLChanged(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnURLChanged2(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnWillMediaLoad(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnWillReleaseScriptContext(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnWindowClosing(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

static void tr_wkeOnWindowDestroy(MBKCALL* c)
{
    void (*fn)(unsigned a0, unsigned a1, unsigned a2) = (void (__cdecl *)(unsigned a0, unsigned a1, unsigned a2))c->real;
    fn((unsigned)c->a[0], (unsigned)c->a[1], (unsigned)c->a[2]);
}

/* ---- 语义特例（2026-09-25）：初始化不再绑定调用线程 ---- */
void __cdecl wkeInitialize(void)
{
    extern int __stdcall MbKit_EnsureKernelInit(void);
    MbKit_EnsureKernelInit();   /* 内核落常驻线程 K，调用线程从此无关 */
}

void __cdecl wkeInitializeEx(unsigned a0)
{
    extern int __stdcall MbKit_EnsureKernelInit(void);
    (void)a0;                    /* 配置参数暂被忽略（见文件头说明） */
    MbKit_EnsureKernelInit();
}

/* K 侧执行体分发表（下标 = MBK_IDX_*；NULL = 不经编组的特例） */
const mbk_tramp_fn mbk_tramps[MBK_ALL_COUNT] = {
    [0] = tr_JsBindGetter,
    [1] = tr_JsBindSetter,
    [2] = tr_jsArg,
    [3] = tr_jsArgCount,
    [4] = tr_jsArgType,
    [5] = tr_jsArrayBuffer,
    [6] = tr_jsBindFunction,
    [7] = tr_jsBoolean,
    [8] = tr_jsCall,
    [9] = tr_jsCallGlobal,
    [10] = tr_jsDeleteObjectProp,
    [11] = tr_jsDouble,
    [12] = tr_jsEmptyArray,
    [13] = tr_jsEmptyObject,
    [14] = tr_jsEvalExW,
    [15] = tr_jsEvalW,
    [16] = tr_jsFalse,
    [17] = tr_jsFloat,
    [18] = tr_jsFunction,
    [19] = tr_jsGC,
    [20] = tr_jsGet,
    [21] = tr_jsGetArrayBuffer,
    [22] = tr_jsGetAt,
    [23] = tr_jsGetData,
    [24] = tr_jsGetGlobal,
    [25] = tr_jsGetKeys,
    [26] = tr_jsGetLastErrorIfException,
    [27] = tr_jsGetLength,
    [28] = tr_jsGetWebView,
    [29] = tr_jsGlobalObject,
    [30] = tr_jsInt,
    [31] = tr_jsIsArray,
    [32] = tr_jsIsBoolean,
    [33] = tr_jsIsFalse,
    [34] = tr_jsIsFunction,
    [35] = tr_jsIsJsValueValid,
    [36] = tr_jsIsNull,
    [37] = tr_jsIsNumber,
    [38] = tr_jsIsObject,
    [39] = tr_jsIsString,
    [40] = tr_jsIsTrue,
    [41] = tr_jsIsUndefined,
    [42] = tr_jsIsValidExecState,
    [43] = tr_jsNull,
    [44] = tr_jsObject,
    [45] = tr_jsSet,
    [46] = tr_jsSetAt,
    [47] = tr_jsSetGlobal,
    [48] = tr_jsSetLength,
    [49] = tr_jsStringW,
    [50] = tr_jsThrowException,
    [51] = tr_jsToBoolean,
    [52] = tr_jsToDouble,
    [53] = tr_jsToFloat,
    [54] = tr_jsToInt,
    [55] = tr_jsToStringW,
    [56] = tr_jsToTempStringW,
    [57] = tr_jsTrue,
    [58] = tr_jsTypeOf,
    [59] = tr_jsUndefined,
    [60] = tr_wkeAddDirtyArea,
    [61] = tr_wkeAddPluginDirectory,
    [62] = tr_wkeCanGoBack,
    [63] = tr_wkeCanGoForward,
    [64] = tr_wkeCreateStringW,
    [65] = tr_wkeCreateWebView,
    [66] = tr_wkeCreateWebWindow,
    [67] = tr_wkeDeleteString,
    [68] = tr_wkeDestroyWebView,
    [69] = tr_wkeDestroyWebWindow,
    [70] = tr_wkeEditorCopy,
    [71] = tr_wkeEditorCut,
    [72] = tr_wkeEditorDelete,
    [73] = tr_wkeEditorPaste,
    [74] = tr_wkeEditorRedo,
    [75] = tr_wkeEditorSelectAll,
    [76] = tr_wkeEditorUnSelect,
    [77] = tr_wkeEditorUndo,
    [78] = tr_wkeEnableWindow,
    [79] = tr_wkeFinalize,
    [80] = tr_wkeFireContextMenuEvent,
    [81] = tr_wkeFireKeyDownEvent,
    [82] = tr_wkeFireKeyPressEvent,
    [83] = tr_wkeFireKeyUpEvent,
    [84] = tr_wkeFireMouseEvent,
    [85] = tr_wkeFireMouseWheelEvent,
    [86] = tr_wkeFireWindowsMessage,
    [87] = tr_wkeGC,
    [88] = tr_wkeGetBlinkMainThreadIsolate,
    [89] = tr_wkeGetCaretRect,
    [90] = tr_wkeGetContentHeight,
    [91] = tr_wkeGetContentWidth,
    [92] = tr_wkeGetCookie,
    [93] = tr_wkeGetCookieW,
    [94] = tr_wkeGetCursorInfoType,
    [95] = tr_wkeGetFrameUrl,
    [96] = tr_wkeGetGlobalExecByFrame,
    [97] = tr_wkeGetHeight,
    [98] = tr_wkeGetHostHWND,
    [99] = tr_wkeGetMediaVolume,
    [100] = tr_wkeGetName,
    [101] = tr_wkeGetSource,
    [102] = tr_wkeGetString,
    [103] = tr_wkeGetStringW,
    [104] = tr_wkeGetTitle,
    [105] = tr_wkeGetTitleW,
    [106] = tr_wkeGetURL,
    [107] = tr_wkeGetUserAgent,
    [108] = tr_wkeGetUserKeyValue,
    [109] = tr_wkeGetVersion,
    [110] = tr_wkeGetVersionString,
    [111] = tr_wkeGetViewDC,
    [112] = tr_wkeGetWebView,
    [113] = tr_wkeGetWebViewForCurrentContext,
    [114] = tr_wkeGetWidth,
    [115] = tr_wkeGetWindowHandle,
    [116] = tr_wkeGetZoomFactor,
    [117] = tr_wkeGlobalExec,
    [118] = tr_wkeGoBack,
    [119] = tr_wkeGoForward,
    [122] = tr_wkeInsertCSSByFrame,
    [123] = tr_wkeIsAwake,
    [124] = tr_wkeIsCookieEnabled,
    [125] = tr_wkeIsDirty,
    [126] = tr_wkeIsDocumentReady,
    [127] = tr_wkeIsInitialize,
    [128] = tr_wkeIsLoading,
    [129] = tr_wkeIsLoadingCompleted,
    [130] = tr_wkeIsLoadingFailed,
    [131] = tr_wkeIsLoadingSucceeded,
    [132] = tr_wkeIsMainFrame,
    [133] = tr_wkeIsProcessingUserGesture,
    [134] = tr_wkeIsWebRemoteFrame,
    [135] = tr_wkeJsBindFunction,
    [136] = tr_wkeJsBindGetter,
    [137] = tr_wkeJsBindSetter,
    [138] = tr_wkeKillFocus,
    [139] = tr_wkeLayoutIfNeeded,
    [140] = tr_wkeLoadFileW,
    [141] = tr_wkeLoadHTML,
    [142] = tr_wkeLoadHTMLW,
    [143] = tr_wkeLoadHtmlWithBaseUrl,
    [144] = tr_wkeLoadURL,
    [145] = tr_wkeLoadURLW,
    [146] = tr_wkeLoadW,
    [147] = tr_wkeMoveToCenter,
    [148] = tr_wkeMoveWindow,
    [149] = tr_wkeNetAddHTTPHeaderFieldToUrlRequest,
    [150] = tr_wkeNetCancelRequest,
    [151] = tr_wkeNetCancelWebUrlRequest,
    [152] = tr_wkeNetChangeRequestUrl,
    [153] = tr_wkeNetContinueJob,
    [154] = tr_wkeNetCreateWebUrlRequest,
    [155] = tr_wkeNetGetExpectedContentLength,
    [156] = tr_wkeNetGetFavicon,
    [157] = tr_wkeNetGetHTTPHeaderField,
    [158] = tr_wkeNetGetHttpStatusCode,
    [159] = tr_wkeNetGetMIMEType,
    [160] = tr_wkeNetGetPostBody,
    [161] = tr_wkeNetGetRequestMethod,
    [162] = tr_wkeNetGetResponseUrl,
    [163] = tr_wkeNetGetUrlByJob,
    [164] = tr_wkeNetHoldJobToAsynCommit,
    [165] = tr_wkeNetHookRequest,
    [166] = tr_wkeNetOnResponse,
    [167] = tr_wkeNetSetData,
    [168] = tr_wkeNetSetHTTPHeaderField,
    [169] = tr_wkeNetSetMIMEType,
    [170] = tr_wkeNetSetURL,
    [171] = tr_wkeNetStartUrlRequest,
    [172] = tr_wkeOnAlertBox,
    [173] = tr_wkeOnConfirmBox,
    [174] = tr_wkeOnConsole,
    [175] = tr_wkeOnCreateView,
    [176] = tr_wkeOnDidCreateScriptContext,
    [177] = tr_wkeOnDocumentReady,
    [178] = tr_wkeOnDocumentReady2,
    [179] = tr_wkeOnDownload,
    [180] = tr_wkeOnLoadUrlBegin,
    [181] = tr_wkeOnLoadUrlEnd,
    [182] = tr_wkeOnLoadingFinish,
    [183] = tr_wkeOnMouseOverUrlChanged,
    [184] = tr_wkeOnNavigation,
    [185] = tr_wkeOnPaintUpdated,
    [186] = tr_wkeOnPromptBox,
    [187] = tr_wkeOnTitleChanged,
    [188] = tr_wkeOnURLChanged,
    [189] = tr_wkeOnURLChanged2,
    [190] = tr_wkeOnWillMediaLoad,
    [191] = tr_wkeOnWillReleaseScriptContext,
    [192] = tr_wkeOnWindowClosing,
    [193] = tr_wkeOnWindowDestroy,
    [194] = tr_wkePaint,
    [195] = tr_wkePaint2,
    [196] = tr_wkePerformCookieCommand,
    [197] = tr_wkePostURL,
    [198] = tr_wkePostURLW,
    [199] = tr_wkeReload,
    [200] = tr_wkeRepaintIfNeeded,
    [201] = tr_wkeResize,
    [202] = tr_wkeResizeWindow,
    [203] = tr_wkeRunJS,
    [204] = tr_wkeRunJSW,
    [205] = tr_wkeRunJsByFrame,
    [206] = tr_wkeSetCookie,
    [207] = tr_wkeSetCookieEnabled,
    [208] = tr_wkeSetCookieJarFullPath,
    [209] = tr_wkeSetCookieJarPath,
    [210] = tr_wkeSetCspCheckEnable,
    [211] = tr_wkeSetDebugConfig,
    [212] = tr_wkeSetDeviceParameter,
    [213] = tr_wkeSetDirty,
    [214] = tr_wkeSetDragDropEnable,
    [215] = tr_wkeSetDragEnable,
    [216] = tr_wkeSetDragFiles,
    [217] = tr_wkeSetEditable,
    [218] = tr_wkeSetFileSystem,
    [219] = tr_wkeSetFocus,
    [220] = tr_wkeSetHandle,
    [221] = tr_wkeSetHandleOffset,
    [222] = tr_wkeSetHeadlessEnabled,
    [223] = tr_wkeSetLocalStorageFullPath,
    [224] = tr_wkeSetMediaVolume,
    [225] = tr_wkeSetMemoryCacheEnable,
    [226] = tr_wkeSetMouseEnabled,
    [227] = tr_wkeSetName,
    [228] = tr_wkeSetNavigationToNewWindowEnable,
    [229] = tr_wkeSetNpapiPluginsEnabled,
    [230] = tr_wkeSetProxy,
    [231] = tr_wkeSetResourceGc,
    [232] = tr_wkeSetStringW,
    [233] = tr_wkeSetTouchEnabled,
    [234] = tr_wkeSetTransparent,
    [235] = tr_wkeSetUserAgent,
    [236] = tr_wkeSetUserAgentW,
    [237] = tr_wkeSetUserKeyValue,
    [238] = tr_wkeSetViewNetInterface,
    [239] = tr_wkeSetViewProxy,
    [240] = tr_wkeSetWindowTitleW,
    [241] = tr_wkeSetZoomFactor,
    [242] = tr_wkeShowDevtools,
    [243] = tr_wkeShowWindow,
    [244] = tr_wkeSleep,
    [245] = tr_wkeStopLoading,
    [246] = tr_wkeUpdate,
    [247] = tr_wkeUtilSerializeToMHTML,
    [248] = tr_wkeVisitAllCookie,
    [249] = tr_wkeWake,
    [250] = tr_wkeWebFrameGetMainFrame,
    [251] = tr_wkeWebFrameGetMainWorldScriptContext,
};

