set pagination off
set confirm off
run o
echo \n===== BACKTRACE =====\n
bt
echo \n===== REGS =====\n
info registers eax ebx ecx edx esi edi ebp esp eip
echo \n===== FAULT INSN CONTEXT =====\n
x/12i $eip-24
echo \n===== STACK =====\n
x/24wx $esp
echo \n===== mk_map FRAME ARGS =====\n
info args
info locals
echo \n===== DONE =====\n
quit
