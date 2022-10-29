dd if=/dev/urandom bs=65536 count=16 status=none >in
cp in file
$TOOL file
rm -f file # zstd keeps it
$Z -d file$EXT
cmp -b in file
