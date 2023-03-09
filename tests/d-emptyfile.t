cat </dev/null >file
$TOOL file
rm -f file # zstd keeps it
$Z -d file$EXT
cmp -b /dev/null file
