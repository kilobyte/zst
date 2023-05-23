cp -p $F file
touch file$EXT
$Z -F $TOOL -f file
ls -l file*
$Z -df file$EXT
cmp -b $F file
