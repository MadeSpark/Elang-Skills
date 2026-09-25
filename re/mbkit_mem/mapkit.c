/* ===========================================================================
 * mapkit.c —— 32 位 PE 手工映射器（内存加载 DLL，不落盘、不进加载器链表）
 *
 * 设计约束（为了能「编译成机器码片段塞进易语言 置入代码」）：
 *   1. 无 #include、不调 CRT、无全局/静态数据、无字符串常量（一切外部依赖都从
 *      参数传入的函数指针走）。否则 .text 里会出现重定位项，无法整体搬走。
 *   2. 入口函数只读 [ebp+8..] 的栈参数 —— 与调用约定无关（易语言侧会自己
 *      用 `mov esp,ebp; pop ebp; ret N` 收尾，cdecl/stdcall 都无所谓）。
 *   3. 用 -fno-tree-loop-distribute-patterns 阻止 gcc 把字节拷贝循环变成
 *      对 memcpy/memset 的调用（那会带来外部重定位）。
 *
 * 汇编产物的自检见 re/mbkit_mem/blob.py（objdump -r 必须为空）。
 * =========================================================================== */

typedef unsigned char  mk_u8;
typedef unsigned short mk_u16;
typedef unsigned int   mk_u32;
typedef signed   int   mk_i32;

/* ★ 必须带 __stdcall：Win32 API 是 callee-clean，若按 cdecl 调用，
 *   callee 弹参数 + caller 再 add esp ⇒ 栈会多弹一份，直接把调用方搞崩。
 *   本文件被调用时则无所谓约定（易语言侧用 `mov esp,ebp` 自己抹平）。 */
#define MKAPI_ __attribute__((stdcall))

#define MK_MEM_CR     0x3000u   /* MEM_COMMIT | MEM_RESERVE */
#define MK_PAGE_RWX   0x40u
#define MK_PAGE_RW    0x04u
#define MK_ATTACH     1u

#define MK_DIR_EXPORT 0
#define MK_DIR_IMPORT 1
#define MK_DIR_BASERELOC 5
#define MK_DIR_DELAYIMPORT 13
#define MK_DIR_TLS 9

/* 状态码（易语言侧据它判断失败在哪一步） */
#define MK_OK            0
#define MK_E_ARG         1
#define MK_E_DOS         2
#define MK_E_MACHINE     3
#define MK_E_ALLOC       4
#define MK_E_IMPORT      5
#define MK_E_IMPORTNAME  6
#define MK_E_IMPORTPROC  7
#define MK_E_ENTRY       8
#define MK_E_FWD         9      /* 映像里存在转发导出，本映射器不支持 */

typedef struct {
    void*  (MKAPI_ *VirtualAlloc)(void*, mk_u32, mk_u32, mk_u32);
    mk_i32 (MKAPI_ *VirtualFree)(void*, mk_u32, mk_u32);
    void*  (MKAPI_ *LoadLibraryA)(const char*);
    void*  (MKAPI_ *GetProcAddress)(void*, const char*);
    mk_u32 (MKAPI_ *TlsAlloc)(void);
    void*  (MKAPI_ *TlsGetValue)(mk_u32);
    mk_i32 (MKAPI_ *TlsSetValue)(mk_u32, void*);
    void*  (MKAPI_ *GetModuleHandleA)(const char*);
} MKAPI;

/* ---------------------------------------------------------------- 小工具 */
static void mk_zero(mk_u8* p, mk_u32 n) { while (n--) *p++ = 0; }

static void mk_copy(mk_u8* d, const mk_u8* s, mk_u32 n) { while (n--) *d++ = *s++; }

static mk_u32 mk_rd16(const mk_u8* p)
{
    return (mk_u32)p[0] | ((mk_u32)p[1] << 8);
}

static mk_u32 mk_rd32(const mk_u8* p)
{
    return (mk_u32)p[0] | ((mk_u32)p[1] << 8) | ((mk_u32)p[2] << 16) | ((mk_u32)p[3] << 24);
}

static void mk_wr32(mk_u8* p, mk_u32 v)
{
    p[0] = (mk_u8)(v & 0xFF);
    p[1] = (mk_u8)((v >> 8) & 0xFF);
    p[2] = (mk_u8)((v >> 16) & 0xFF);
    p[3] = (mk_u8)((v >> 24) & 0xFF);
}

/* ==========================================================================
 * 两条 API 之间的字节异或 —— 用来在「机器码片段」里定位自己
 * （不依赖任何数据段，纯位置无关）
 * ========================================================================== */

/* ==========================================================================
 * 主入口：mk_map
 *
 *   A       : 8 个 API 函数指针（由调用方填好）
 *   data    : 整个 DLL 文件的字节
 *   size    : 字节数
 *   pBase   : [out] 映射后的映像基址（失败写 0）
 *   pStatus : [out] 状态码
 *
 * 顺序严格照加载器：
 *   分配 → 拷头 → 拷节 → 重定位 → 解析导入 → TLS → 调 TLS 回调 → 调入口点
 * ========================================================================== */
void mk_map(void* pVA, void* pVF, void* pLLA, void* pGPA,
            void* pTA, void* pTG, void* pTS, void* pGM,
            const mk_u8* data, mk_u32 size,
            mk_u32* pBase, mk_u32* pStatus)
{
    MKAPI Aaa;
    const MKAPI* A = &Aaa;
    Aaa.VirtualAlloc = (void*  (MKAPI_ *)(void*, mk_u32, mk_u32, mk_u32))pVA;
    Aaa.VirtualFree  = (mk_i32 (MKAPI_ *)(void*, mk_u32, mk_u32))pVF;
    Aaa.LoadLibraryA = (void*  (MKAPI_ *)(const char*))pLLA;
    Aaa.GetProcAddress = (void* (MKAPI_ *)(void*, const char*))pGPA;
    Aaa.TlsAlloc     = (mk_u32 (MKAPI_ *)(void))pTA;
    Aaa.TlsGetValue  = (void*  (MKAPI_ *)(mk_u32))pTG;
    Aaa.TlsSetValue  = (mk_i32 (MKAPI_ *)(mk_u32, void*))pTS;
    Aaa.GetModuleHandleA = (void* (MKAPI_ *)(const char*))pGM;

    mk_u32 nt, opt, nsec, szopt, secTab, imageBase, sizeOfImage, sizeOfHeaders;
    mk_u32 entry, ndir, dd, sec, delta, i, k, rawLen, total;
    mk_u32 base = 0, st = MK_OK;
    mk_u8* img;
    mk_u8* tls;

    if (pBase)   *pBase = 0;
    if (pStatus) *pStatus = MK_E_ARG;
    if (!A || !data || size < 0x100 || !pBase) return;

    /* ---- 1. 头 ---------------------------------------------------------- */
    nt = mk_rd32(data + 0x3C);
    if (nt < 0x40 || nt + 0x78 > size) { st = MK_E_DOS; goto done; }
    if (mk_rd32(data + nt) != 0x00004550u) { st = MK_E_DOS; goto done; }
    if (mk_rd16(data + nt + 4) != 0x014Cu) { st = MK_E_MACHINE; goto done; }
    nsec = mk_rd16(data + nt + 6);
    szopt = mk_rd16(data + nt + 0x14);
    opt = nt + 0x18;
    if (opt + 0x60 > size) { st = MK_E_DOS; goto done; }

    imageBase = mk_rd32(data + opt + 0x1C);
    sizeOfImage = mk_rd32(data + opt + 0x38);
    sizeOfHeaders = mk_rd32(data + opt + 0x3C);
    entry = mk_rd32(data + opt + 0x10);
    ndir = mk_rd32(data + opt + 0x5C);
    dd = opt + 0x60;
    secTab = opt + szopt;
    if (!sizeOfImage || !sizeOfHeaders || secTab + nsec * 40u > size) { st = MK_E_DOS; goto done; }

    /* ---- 2. 分配（优先占住首选基址，这样 delta = 0，可跳过重定位） ------ */
    base = (mk_u32)A->VirtualAlloc((void*)imageBase, sizeOfImage, MK_MEM_CR, MK_PAGE_RWX);
    if (!base) {
        base = (mk_u32)A->VirtualAlloc(0, sizeOfImage, MK_MEM_CR, MK_PAGE_RWX);
        if (!base) { st = MK_E_ALLOC; goto done; }
    }
    delta = base - imageBase;

    /* ---- 3. 拷头 + 拷节 ------------------------------------------------- */
    mk_copy((mk_u8*)base, data, sizeOfHeaders);

    for (sec = 0; sec < nsec; sec++) {
        const mk_u8* sh = data + secTab + sec * 40u;
        mk_u32 vsize = mk_rd32(sh + 0x08);
        mk_u32 vaddr = mk_rd32(sh + 0x0C);
        mk_u32 rsize = mk_rd32(sh + 0x10);
        mk_u32 raddr = mk_rd32(sh + 0x14);
        mk_u32 nb;

        if (!vsize) continue;
        nb = (rsize < vsize) ? rsize : vsize;
        if (nb) {
            if (raddr + nb > size) { st = MK_E_DOS; goto done; }
            mk_copy((mk_u8*)(base + vaddr), data + raddr, nb);
        }
        if (vsize > nb) mk_zero((mk_u8*)(base + vaddr + nb), vsize - nb);
    }

    /* ---- 4. 重定位 ------------------------------------------------------ */
    if (delta && ndir > MK_DIR_BASERELOC && mk_rd32(data + dd + MK_DIR_BASERELOC * 8u)) {
        mk_u8* rel = (mk_u8*)(base + mk_rd32(data + dd + MK_DIR_BASERELOC * 8u));
        mk_u32 rsize = mk_rd32(data + dd + MK_DIR_BASERELOC * 8u + 4);
        mk_u32 p = 0;
        while (p + 8 <= rsize) {
            mk_u32 pvaddr = mk_rd32(rel + p);
            mk_u32 bsize = mk_rd32(rel + p + 4);
            mk_u32 q = p + 8;
            mk_u32 end;
            if (bsize < 8 || p + bsize > rsize) break;
            end = p + bsize;
            while (q + 2 <= end) {
                mk_u16 e = (mk_u16)mk_rd16(rel + q);
                mk_u32 type = e >> 12;
                mk_u32 off = e & 0x0FFFu;
                if (type == 3) {          /* IMAGE_REL_BASED_HIGHLOW */
                    mk_u8* t = (mk_u8*)(base + pvaddr + off);
                    mk_wr32(t, mk_rd32(t) + delta);
                } else if (type != 0) {   /* 其余类型 x86 上不出现 */
                    st = MK_E_FWD; goto done;
                }
                q += 2;
            }
            p = end;
        }
    }

    /* ---- 5. 导入表 ------------------------------------------------------ */
    if (ndir > MK_DIR_IMPORT && mk_rd32(data + dd + MK_DIR_IMPORT * 8u)) {
        mk_u8* imp = (mk_u8*)(base + mk_rd32(data + dd + MK_DIR_IMPORT * 8u));
        for (;;) {
            mk_u32 oft = mk_rd32(imp + 0x00);
            mk_u32 nameRva = mk_rd32(imp + 0x0C);
            mk_u32 ft = mk_rd32(imp + 0x10);
            void* h;
            mk_u32 n;

            if (!oft && !nameRva && !ft) break;
            if (!nameRva) { st = MK_E_IMPORTNAME; goto done; }
            h = A->LoadLibraryA((const char*)(base + nameRva));
            if (!h) { st = MK_E_IMPORT; goto done; }

            for (n = 0;; n++) {
                mk_u8* src = (mk_u8*)(base + (oft ? oft : ft) + n * 4u);
                mk_u32 v = mk_rd32(src);
                void* proc;
                if (!v) break;
                if (v & 0x80000000u) {
                    proc = A->GetProcAddress(h, (const char*)(v & 0xFFFFu));
                } else {
                    proc = A->GetProcAddress(h, (const char*)(base + v + 2u));
                }
                if (!proc) { st = MK_E_IMPORTPROC; goto done; }
                mk_wr32((mk_u8*)(base + ft + n * 4u), (mk_u32)proc);
            }
            imp += 20;
        }
    }

    /* ---- 6. TLS --------------------------------------------------------- */
    if (ndir > MK_DIR_TLS && mk_rd32(data + dd + MK_DIR_TLS * 8u)) {
        mk_u32 idx;
        tls = (mk_u8*)(base + mk_rd32(data + dd + MK_DIR_TLS * 8u));
        idx = A->TlsAlloc();
        if (idx != 0xFFFFFFFFu) {
            mk_u32 aIdx = mk_rd32(tls + 0x08);
            mk_u32 aRaw = mk_rd32(tls + 0x00);
            mk_u32 aEnd = mk_rd32(tls + 0x04);
            mk_u32 aCb = mk_rd32(tls + 0x0C);
            mk_u32 zfill = mk_rd32(tls + 0x10);

            /* 若某字段没被重定位过（值仍落在首选基址区间），补上 delta */
            if (aIdx >= imageBase && aIdx < imageBase + sizeOfImage) aIdx += delta;
            if (aRaw >= imageBase && aRaw < imageBase + sizeOfImage) aRaw += delta;
            if (aEnd >= imageBase && aEnd < imageBase + sizeOfImage) aEnd += delta;
            if (aCb >  imageBase && aCb < imageBase + sizeOfImage) aCb += delta;

            if (aIdx) mk_wr32((mk_u8*)aIdx, idx);

            rawLen = (aEnd > aRaw) ? (aEnd - aRaw) : 0;
            total = rawLen + zfill;
            if (total) {
                mk_u8* buf = (mk_u8*)A->VirtualAlloc(0, total, MK_MEM_CR, MK_PAGE_RW);
                if (buf) {
                    if (rawLen) mk_copy(buf, (const mk_u8*)aRaw, rawLen);
                    if (zfill) mk_zero(buf + rawLen, zfill);
                    A->TlsSetValue(idx, buf);
                }
            }
            if (aCb) {
                mk_u32* cb = (mk_u32*)aCb;
                for (k = 0; k < 64 && cb[k]; k++)
                    ((void (*)(void*, mk_u32, void*))cb[k])((void*)base, MK_ATTACH, 0);
            }
        }
    }

    /* ---- 7. 入口点（mingw 的 _DllMainCRTStartup 会顺手做完 CRT 初始化） -- */
    if (entry) {
        mk_i32 rc = ((mk_i32 (*)(void*, mk_u32, void*))(base + entry))(
                        (void*)base, MK_ATTACH, 0);
        if (!rc) { st = MK_E_ENTRY; goto done; }
    }

    img = 0; (void)img;
    st = MK_OK;

done:
    if (st != MK_OK) {
        if (base && A->VirtualFree) A->VirtualFree((void*)base, 0, 0x8000u /*MEM_RELEASE*/);
        base = 0;
    }
    *pBase = base;
    if (pStatus) *pStatus = st;
}
