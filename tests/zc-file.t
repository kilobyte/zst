cp -p $F file
$Z -cF $TOOL file >stdout
$TOOL -cd stdout >file
cmp -b $F file
