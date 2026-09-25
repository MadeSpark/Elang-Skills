set pagination off
set confirm off
run o
echo \n===== FAULT =====\n
printf "EIP=%08x\n", $eip
echo \n===== BACKTRACE =====\n
bt
echo \n===== FRAME 1 (main) =====\n
frame 1
info args
echo \n===== FRAME 2 (mk_map) =====\n
frame 2
info args
echo \n===== mk_map LOCALS =====\n
info locals
echo \n===== KEY VALUES =====\n
p base
p entry
p imageBase
p sizeOfImage
p sizeOfHeaders
p nsec
p opt
p nt
p ndir
p st
p delta
p data
p size
p A
echo \n===== Aaa =====\n
p Aaa
echo \n===== DONE =====\n
quit
