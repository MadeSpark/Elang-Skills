# -*- coding: utf-8 -*-
"""生成易语言复现工程文本。
用法: python gen.py <newthread|samethread> <multiinit 0|1> [finalize 0|1]
  newthread : 每轮新建线程（复刻用户场景）
  samethread: 全程同一线程
  multiinit : 1 = 绑定前显式调用 WKE初始化 (真)（wkeInitializeEx，实测会崩，仅作对照）
  finalize  : 1 = 每轮销毁后调用 WKE卸载 ()（wkeFinalize，验证能否真正复位内核）
"""
import io
import os
import sys

B = r'C:\Users\MadeSpark\Desktop\测试\re\userdemo_v137'
mode = sys.argv[1] if len(sys.argv) > 1 else 'newthread'
multi = sys.argv[2] if len(sys.argv) > 2 else '0'
fin = sys.argv[3] if len(sys.argv) > 3 else '0'

if mode == 'newthread':
    run_line = '线程_启动 (&载入一轮)'
    delay = '        延时 (8000)\r\n'
    title = '每轮新线程'
    start = '    载入 (窗口1, , 假)\r\n    线程_启动 (&自动测试)\r\n    返回 (0)'
else:
    run_line = '载入一轮 ()'
    delay = ''
    title = '同一线程'
    start = '    载入 (窗口1, , 假)\r\n    自动测试 ()\r\n    返回 (0)'

ASM = '''.版本 2

.程序集 程序集1
.子程序 _启动子程序, 整数型
@@START@@
    

.子程序 自动测试
    .局部变量 轮次, 整数型

    写到文件 (取运行目录 () ＋ “\\reinit_log.txt”, { })
    写日志 (“========== @@TITLE@@ / @@MULTI@@ / @@FIN@@ ==========”)
    .计次循环首 (4, 轮次)
        写日志 (“===== 第 ” ＋ 到文本 (轮次) ＋ “ 轮 =====”)
        @@RUN@@
@@DELAY@@        写日志 (“----- 第 ” ＋ 到文本 (轮次) ＋ “ 轮结束 -----”)
    .计次循环尾 ()
    写日志 (“========== 4 轮全部完成，未崩溃 ==========”)
    窗口1.销毁 ()
    

.子程序 载入一轮
    载入 (测试窗口, , 真)
    

.子程序 写日志
    .参数 参数_文本, 文本型
    .局部变量 fh, 整数型

    fh ＝ 打开文件 (取运行目录 () ＋ “\\reinit_log.txt”, 3)
    .如果真 (fh ＝ 0)
        返回 ()
    .如果真结束
    移到文件尾 (fh)
    写出文本 (fh, 参数_文本 ＋ #换行符)
    关闭文件 (fh)
    
'''

multi_line = ('    WKE初始化 (真)\r\n'
              '    写日志 (“  [创建完毕] WKE初始化(真) 已调用”)\r\n') if multi == '1' else ''
fin_line = ('    WKE卸载 ()\r\n'
            '    写日志 (“  [将被销毁] WKE卸载() 已调用”)\r\n') if fin == '1' else ''

WIN = '''.版本 2

.程序集 窗口程序集_测试窗口
.程序集变量 mb, MB浏览框

.子程序 _测试窗口_尺寸被改变
    图片框1.宽度 ＝ 测试窗口.宽度
    图片框1.高度 ＝ 测试窗口.高度
    

.子程序 _测试窗口_创建完毕
    .局部变量 绑定结果, 逻辑型

    WKE设置node路径 (“C:\\Users\\MadeSpark\\Desktop\\测试\\main\\corn\\node.dll”)
    写日志 (“  [创建完毕] node = ” ＋ WKE取node路径 ())
@@MULTI@@    绑定结果 ＝ mb.绑定 (图片框1.取窗口句柄 ())
    写日志 (“  [创建完毕] 句柄 = ” ＋ 到文本 (图片框1.取窗口句柄 ()) ＋ “，绑定返回 = ” ＋ 到文本 (绑定结果) ＋ “，浏览器指针 = ” ＋ 到文本 (mb.取浏览器指针 ()))
    mb.载入 (“https://www.madespark.cc/”)
    mb.事件_开始载入URL (&子程序1)
    测试窗口.标题 ＝ “miniblink_test”
    写日志 (“  [创建完毕] 已发起载入”)
    线程_启动 (&自动关闭)
    

.子程序 子程序1, 逻辑型
    .参数 wkeWebView, 整数型
    .参数 用户数据, 整数型
    .参数 文本_URL, 文本型
    .参数 job, 整数型

    写日志 (“  [事件] 开始载入URL: ” ＋ 文本_URL)
    返回 (真)
    

.子程序 自动关闭
    延时 (5000)
    写日志 (“  [自动关闭] 销毁测试窗口”)
    测试窗口.销毁 ()
    

.子程序 _测试窗口_将被销毁
    写日志 (“  [将被销毁] mb.销毁() 之前”)
    mb.销毁 ()
    写日志 (“  [将被销毁] mb.销毁() 已返回”)
@@FIN@@
.子程序 写日志
    .参数 参数_文本, 文本型
    .局部变量 fh, 整数型

    fh ＝ 打开文件 (取运行目录 () ＋ “\\reinit_log.txt”, 3)
    .如果真 (fh ＝ 0)
        返回 ()
    .如果真结束
    移到文件尾 (fh)
    写出文本 (fh, 参数_文本 ＋ #换行符)
    关闭文件 (fh)
    
'''


def w(path, text):
    text = text.replace('\r\n', '\n').replace('\n', '\r\n')
    io.open(path, 'w', encoding='utf-8-sig', newline='').write(text)


a = (ASM.replace('@@START@@', start).replace('@@TITLE@@', title)
        .replace('@@MULTI@@', '多线程初始化' if multi == '1' else '默认初始化')
        .replace('@@FIN@@', '销毁后卸载内核' if fin == '1' else '不卸载内核')
        .replace('@@RUN@@', run_line)
        .replace('@@DELAY@@', delay))
w(os.path.join(B, 'class', '程序集1.static.e.txt'), a)
w(os.path.join(B, 'class', '窗口程序集_测试窗口.form.e.txt'),
  WIN.replace('@@MULTI@@', multi_line).replace('@@FIN@@', fin_line))
print('mode=%s multiinit=%s finalize=%s 已生成' % (mode, multi, fin))
