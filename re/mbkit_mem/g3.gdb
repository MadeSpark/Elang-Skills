set pagination off
set confirm off
run o
echo \n===== FAULT =====\n
printf "EIP=%08x EAX=%08x\n", $eip, $eax
bt
echo \n===== mk_map LOCALS (-O1) =====\n
frame 2
info args
info locals
echo \n===== DONE =====\n
quit
