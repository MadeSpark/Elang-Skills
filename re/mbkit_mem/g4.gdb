set pagination off
set confirm off
break mapkit.c:272
run o
echo \n===== 断在 mapkit.c:272（入口点调用）=====\n
echo --- 变量 ---\n
p base
p entry
p imageBase
p delta
p data
p size
p nsec
p ndir
echo --- 即将执行的指令 ---\n
x/4i $pc
echo --- 单步到 call 之前 ---\n
stepi
stepi
stepi
stepi
stepi
printf "EAX=%08x  EBX=%08x  EDI=%08x  ESI=%08x\n", $eax, $ebx, $edi, $esi
x/3i $pc
echo \n===== DONE =====\n
quit
