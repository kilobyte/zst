dd if=/dev/urandom bs=65536 count=16 status=none|od >in
cp in file
$Z -F $TOOL file
$TOOL -d file$EXT
cmp -b in file
