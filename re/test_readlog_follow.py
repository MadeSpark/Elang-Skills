#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""test_readlog_follow.py —— 验证 readlog --follow 的跨块场景：
半行追加、GBK 双字节字符被拆到两块、DELTA 包装行块首行。
"""
import subprocess
import sys
import time

PY = sys.executable
READLOG = r"C:\Users\MadeSpark\Desktop\测试\tools\readlog.py"
LOG = r"C:\Users\MadeSpark\Desktop\测试\re\rt_follow_test.log"

open(LOG, "wb").close()  # 空文件

p = subprocess.Popen([PY, READLOG, LOG, "--follow", "--business", "--timeout", "4"],
                     stdout=subprocess.PIPE)

def append(b):
    with open(LOG, "ab") as f:
        f.write(b)

time.sleep(0.5)
# 块 1：DELTA 包装行 + 独立行，其中包装行**整行**一次到位
append("[00:00:01.100][PANEL-DELTA] DELTA: [00:00:01] * 首行业务\r\n".encode("gbk"))
time.sleep(0.5)
# 块 2：一行业务被拆成 3 块（含 GBK 双字节「嵌」被拆开）
line = "[00:00:02] * 嵌套跨块行\r\n".encode("gbk")
append(line[:10])
time.sleep(0.3)
append(line[10:20])
time.sleep(0.3)
append(line[20:])
time.sleep(0.5)
# 块 3：非业务行 + 换行前的纯噪声
append("[00:00:03.500][PANEL-CHANGED] hwnd=0x1 oldlen=1 newlen=2\r\n".encode("gbk"))
append("被调试易程序运行完毕\r\n".encode("gbk"))

out, _ = p.communicate(timeout=30)
got = out.decode("utf-8", errors="replace").splitlines()
expect = ["[00:00:01] * 首行业务", "[00:00:02] * 嵌套跨块行"]
ok = got == expect
print("got     =", got)
print("expect  =", expect)
print("RESULT  =", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
