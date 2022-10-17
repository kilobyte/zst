cp -p $F file
$TOOL file
rm -f file # zstd keeps it
truncate -s 1024 file$EXT
! $Z -d file$EXT
