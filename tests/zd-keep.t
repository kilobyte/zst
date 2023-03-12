touch file
$Z -k -F$TOOL file
test -f file
rm file
$Z -kd file$EXT
test -f file$EXT
