dd if=/dev/zero bs=65536 count=16 status=none >in
cp in file
$Z -F $TOOL file
$TOOL -d file$EXT
cmp -b in file
