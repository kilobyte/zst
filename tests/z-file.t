cp -p $F file
$Z -F $TOOL file
$TOOL -d file$EXT
cmp -b $F file
