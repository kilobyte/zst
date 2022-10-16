cp -p $F file
$TOOL -k file
$Z -f file
$Z -df file$EXT
cmp -b $F file
