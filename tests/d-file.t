cp -p $F file
$TOOL file
rm -f file # zstd keeps it
$Z -d file$EXT
cmp -b $F file
