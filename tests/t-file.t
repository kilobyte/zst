cp -p $F file
$TOOL file
rm -f file # zstd keeps it
$Z -t file$EXT
