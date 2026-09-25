set pagination off
set confirm off
break mapkit.c:272
run o
echo \n===== 寄存器 =====\n
info registers eax ebx ecx edx esi edi esp ebp
echo \n===== 栈 [esp .. esp+0x60] =====\n
x/24wx $esp
echo \n===== 栈 [esp+0x60 .. esp+0xC0]（此处应有真实参数）=====\n
x/24wx $esp+0x60
echo \n===== base 的地址 =====\n
p/x &base
p/x &entry
p/x &delta
p/x &imageBase
p/x &Aaa
echo \n===== DONE =====\n
quit
