cat </dev/null >file
$Z -F $TOOL file
$TOOL -d file$EXT
cmp -b /dev/null file
