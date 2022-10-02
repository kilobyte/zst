cp -p $F file
$TOOL file
rm -f file # zstd keeps it
$Z -cd file$EXT >stdout
cmp -b $F stdout
