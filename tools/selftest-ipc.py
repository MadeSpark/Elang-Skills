#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""宿主支持库双向 IPC 的自测（不需要 e.exe、不需要 .fne）。

本脚本扮演 **fne 那一侧**（命名管道客户端），把 elaunch.PipeBridge 当被测服务端，
在同一个进程里跑完整的握手 / 决策 / 心跳 / 重连流程：

    python tools/selftest-ipc.py            # 全绿退出码 0
    python tools/selftest-ipc.py -v         # 打印每一帧

覆盖点：
  1. hello → ack（令牌正确 / 令牌错误两条路径）
  2. 9 种按钮组合的 decide 决策（单按钮 / 有 IDOK / 无 IDOK 有 IDCANCEL /
     有 YES 不猜 / 无按钮不动作 / pol=observe 一律不动）
  3. ping → pong
  4. 坏帧（非 JSON / 非对象）不打断连接
  5. 断线重连（客户端断开→重连→再握手→再决策）
  6. 审计文件真的在写、close() 能在 2 秒内返回
  7. result 帧统计（how=fallback 计入 fallbacks）
  8. policy 帧（set_policy("observe") / set_policy("dismiss", hide="on")）

真机验收（e.exe + 真 fne）看 docs/验收报告-宿主事件总线与双向IPC.md。
"""
import argparse
import ctypes
import json
import os
import sys
import time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import elaunch  # noqa: E402

IDOK, IDCANCEL, IDYES, IDNO = 1, 2, 6, 7
TOKEN = "0123456789abcdef0123456789abcdef"
PIPE = "\\\\.\\pipe\\elang_addin_selftest.%d" % os.getpid()

_fails = []
_passes = [0]


def ck(cond, msg):
    if cond:
        _passes[0] += 1
        return True
    _fails.append(msg)
    print("  [FAIL] %s" % msg)
    return False


def gb(s):
    """文本 → GBK 字节的十六进制小写串（协议规定的文本编码）。"""
    return (s or "").encode("gbk", "replace").hex()


def ungb(h):
    return bytes.fromhex(h or "").decode("gbk", "replace")


class Client(object):
    """命名管道客户端 = 假 fne。"""

    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3

    def __init__(self, path):
        self.k = ctypes.WinDLL("kernel32", use_last_error=True)
        self.k.CreateFileW.restype = ctypes.c_void_p
        self.k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD,
                                       wintypes.DWORD, wintypes.LPVOID,
                                       wintypes.DWORD, wintypes.DWORD,
                                       wintypes.HANDLE]
        self.k.WriteFile.argtypes = [ctypes.c_void_p, ctypes.c_char_p, wintypes.DWORD,
                                     ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
        self.k.ReadFile.argtypes = [ctypes.c_void_p, ctypes.c_char_p, wintypes.DWORD,
                                    ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
        self.path = path
        self.h = None
        self.buf = b""
        self.err = 0

    def connect(self, timeout=3.0):
        t0 = time.time()
        while time.time() - t0 < timeout:
            h = self.k.CreateFileW(self.path, self.GENERIC_READ | self.GENERIC_WRITE,
                                   0, None, self.OPEN_EXISTING, 0, None)
            if h not in (None, 0, 0xFFFFFFFFFFFFFFFF):
                self.h = h
                return True
            self.err = ctypes.get_last_error()
            time.sleep(0.03)
        return False

    def send_raw(self, raw):
        n = wintypes.DWORD(0)
        ok = self.k.WriteFile(self.h, raw, len(raw), ctypes.byref(n), None)
        if not ok:
            raise OSError(ctypes.get_last_error(), "WriteFile 失败")
        return True

    def send(self, obj):
        return self.send_raw((json.dumps(obj, ensure_ascii=True,
                                         separators=(",", ":")) + "\n").encode("ascii"))

    def recv(self, timeout=3.0):
        """读一行（带缓冲）。管道是阻塞读，但服务端一定会在毫秒级回帧；
        真卡死由 main() 里的看门狗（60s）兜底。"""
        buf = ctypes.create_string_buffer(4096)
        n = wintypes.DWORD(0)
        while True:
            nl = self.buf.find(b"\n")
            if nl >= 0:
                line, self.buf = self.buf[:nl], self.buf[nl + 1:]
                try:
                    return json.loads(line.decode("ascii"))
                except ValueError:
                    return {"t": "<bad>", "raw": line.decode("ascii", "replace")}
            ok = self.k.ReadFile(self.h, buf, 4096, ctypes.byref(n), None)
            if not ok or n.value == 0:
                return None
            self.buf += buf.raw[:n.value]

    def close(self):
        if self.h and self.h not in (0xFFFFFFFFFFFFFFFF,):
            self.k.CloseHandle(ctypes.c_void_p(self.h))
        self.h = None


def hello(c, token=TOKEN, pid=None):
    c.send({"t": "hello", "token": gb(token), "pid": pid or os.getpid(), "fw": gb("v1.3.9")})


def dialog(c, did, btns, pol="dismiss", title="错误", msg="测试弹窗"):
    c.send({"t": "dialog", "id": did, "hwnd": 0x10000 + did, "cls": gb("#32770"),
            "title": gb(title), "msg": gb(msg),
            "btns": [{"i": i, "txt": gb("按钮%d" % i)} for i in btns],
            "edits": [], "pol": pol, "ts": int(time.time())})


def expect_decide(c, want_act, want_btn=None, timeout=3.0):
    fr = c.recv(timeout)
    if not ck(fr is not None, "等 decide 超时"):
        return
    ck(fr.get("t") == "decide", "回帧不是 decide：%r" % (fr,))
    ck(fr.get("act") == want_act, "act 应为 %r，实际 %r" % (want_act, fr.get("act")))
    if want_btn is not None:
        ck(fr.get("btn") == want_btn, "btn 应为 %r，实际 %r" % (want_btn, fr.get("btn")))


def case_handshake(bridge, c):
    print("[1] 握手")
    ck(c.connect(), "客户端连不上管道 %s" % bridge.pipe_path)
    hello(c, token="deadbeef" * 4)
    fr = c.recv(2.0)
    ck(fr is not None, "错误令牌没回 ack")
    if fr:
        ck(fr.get("t") == "ack" and fr.get("ok") == 0, "错误令牌应回 ok=0，实际 %r" % (fr,))
        ck(ungb(fr.get("err")) == "token mismatch", "err 文案应为 token mismatch")
    ck(bridge.handshake_fail >= 1, "handshake_fail 未计数")
    c.close()

    c.connect()
    hello(c)
    fr = c.recv(2.0)
    ck(fr is not None and fr.get("t") == "ack" and fr.get("ok") == 1, "正确令牌未握手成功：%r" % (fr,))
    ck(bridge.wait_ready(2.0), "wait_ready 返回 False")
    ck(bridge.connected, "connected 应为 True")
    ck(bridge.peer_pid == os.getpid(), "peer_pid 不对：%r" % (bridge.peer_pid,))
    ck(bridge.peer_fw == "v1.3.9", "peer_fw 不对：%r" % (bridge.peer_fw,))


def case_decisions(c):
    print("[2] 决策规则")
    # 单按钮 → 点它
    dialog(c, 900, [IDOK])
    expect_decide(c, "click", IDOK)
    # 多按钮含 IDOK → 点 IDOK
    dialog(c, 901, [IDYES, IDOK, IDCANCEL])
    expect_decide(c, "click", IDOK)
    # 多按钮无 IDOK、有 IDCANCEL 且无 IDYES → 点 IDCANCEL
    dialog(c, 902, [IDNO, IDCANCEL])
    expect_decide(c, "click", IDCANCEL)
    # 多按钮无 IDOK 且有 IDYES → 不猜
    dialog(c, 903, [IDYES, IDNO])
    expect_decide(c, "none")
    # 多按钮无 IDOK、有 IDCANCEL 但也有 IDYES → 不猜
    dialog(c, 904, [IDYES, IDCANCEL])
    expect_decide(c, "none")
    # 无按钮 → 不动作
    dialog(c, 905, [])
    expect_decide(c, "none")
    # 双 IDOK 语义异常（重复 id）仍应点 IDOK
    dialog(c, 906, [IDCANCEL, IDOK])
    expect_decide(c, "click", IDOK)
    # 只有 IDYES → 不猜
    dialog(c, 907, [IDYES])
    expect_decide(c, "click", IDYES)      # 单按钮规则优先：只有一个按钮就点它
    # observe 模式 → 一律 none（只记录）
    dialog(c, 908, [IDOK], pol="observe")
    expect_decide(c, "none")


def case_ping_bad(c, bridge):
    print("[3] 心跳 / 坏帧")
    ts = int(time.time() * 1000)
    c.send({"t": "ping", "ts": ts})
    fr = c.recv(2.0)
    ck(fr is not None and fr.get("t") == "pong" and fr.get("ts") == ts, "pong 不对：%r" % (fr,))
    base_bad = bridge.bad_frames
    c.send_raw(b"this is not json\n")
    c.send_raw(b"[1,2,3]\n")             # 合法 JSON 但不是对象
    time.sleep(0.2)
    ck(bridge.bad_frames >= base_bad + 2, "坏帧计数没涨（%d → %d）" % (base_bad, bridge.bad_frames))
    dialog(c, 909, [IDOK])
    expect_decide(c, "click", IDOK)
    ck(bridge.bad_frames >= base_bad + 2, "坏帧后连接状态异常")


def case_result_event(c, bridge):
    print("[4] result / event 统计")
    before_ev = dict(bridge.events_in)
    before_fb = bridge.fallbacks
    c.send({"t": "event", "k": "DLG-ERROR", "cls": gb("#32770"),
            "title": gb("错误"), "msg": gb("无法找到启动子程序")})
    c.send({"t": "result", "id": 900, "how": "fallback", "act": "click", "ms": 1501})
    time.sleep(0.25)
    ck(bridge.events_in.get("DLG-ERROR", 0) == before_ev.get("DLG-ERROR", 0) + 1,
       "events_in[DLG-ERROR] 未计数：%r" % (bridge.events_in,))
    ck(bridge.fallbacks == before_fb + 1, "fallbacks 未计数")


def case_policy(c, bridge):
    print("[7] policy 帧（Agent 运行期改宿主策略）")
    before = bridge.frames_out
    ck(bridge.set_policy("observe"), "set_policy(observe) 返回 False")
    fr = c.recv(2.0)
    ck(fr is not None and fr.get("t") == "policy", "没收到 policy 帧：%r" % (fr,))
    if fr and fr.get("t") == "policy":
        ck(fr.get("dlg") == "observe", "policy.dlg 应为 observe，实际 %r" % (fr.get("dlg"),))
        ck("hide" not in fr, "observe 不应带 hide 字段：%r" % (fr,))
    ck(bridge.frames_out > before, "frames_out 没随 policy 增长")
    ck(bridge.set_policy("dismiss", hide="on"), "set_policy(dismiss,hide=on) 返回 False")
    fr = c.recv(2.0)
    ck(fr is not None and fr.get("dlg") == "dismiss" and fr.get("hide") == "on",
       "带 hide 的 policy 帧不对：%r" % (fr,))


def case_reconnect(bridge, c, audit):
    print("[5] 断线重连")
    c.close()
    time.sleep(0.3)
    ck(not bridge.connected, "客户端断开后 connected 仍为 True")
    ok = c.connect(3.0)
    ck(ok, "重连失败")
    if not ok:
        return
    hello(c)
    fr = c.recv(2.0)
    ck(fr is not None and fr.get("ok") == 1, "重连后握手失败：%r" % (fr,))
    dialog(c, 950, [IDOK])
    expect_decide(c, "click", IDOK)
    if audit:
        ck(os.path.exists(audit) and os.path.getsize(audit) > 0, "审计文件没写：%s" % audit)
        lines = open(audit, "r", encoding="utf-8").read().splitlines()
        ck(len(lines) >= 8, "审计行数太少：%d" % len(lines))
        ck(any('"dir": "in"' in ln or '"dir":"in"' in ln for ln in lines), "审计里没有 in 方向")
        ck(any('"dir": "out"' in ln or '"dir":"out"' in ln for ln in lines), "审计里没有 out 方向")


def main():
    ap = argparse.ArgumentParser(description="宿主 IPC 桥自测（假 fne 客户端）")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--keep-audit", action="store_true", help="保留审计文件（默认删）")
    args = ap.parse_args()

    if sys.platform != "win32":
        print("仅支持 Windows（命名管道）")
        return 2

    audit = os.path.join(os.environ.get("TEMP") or ".", "selftest_ipc_%d.jsonl" % os.getpid())
    try:
        os.remove(audit)
    except OSError:
        pass

    # 看门狗：任何一步卡住（等不到回帧）就在 60s 硬退出，避免自测把 CI 挂住
    import threading
    threading.Timer(60.0, lambda: os._exit(3)).start()

    br = elaunch.PipeBridge(PIPE, TOKEN, audit, 1500).start()
    if br.error:
        print("管道启动失败：%s" % br.error)
        return 2
    print("pipe = %s" % PIPE)
    c = Client(PIPE)
    t0 = time.time()
    try:
        case_handshake(br, c)
        case_decisions(c)
        case_ping_bad(c, br)
        case_result_event(c, br)
        case_policy(c, br)
        case_reconnect(br, c, None if args.keep_audit else audit)
    finally:
        c.close()
        t1 = time.time()
        br.close()
        dt = time.time() - t1
        print("[6] close() 用时 %.3fs" % dt)
        ck(dt < 2.0, "close() 用了 %.2fs（应 < 2s）" % dt)

    st = br.stats() if hasattr(br, "stats") else {}
    if args.verbose:
        print(json.dumps(st, ensure_ascii=False, indent=1))
    print("")
    print("PASS %d / FAIL %d   (%.2fs)" % (_passes[0], len(_fails), time.time() - t0))
    if _fails:
        for m in _fails[:20]:
            print("  - %s" % m)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
