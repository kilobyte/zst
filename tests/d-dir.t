mkdir dir
cp -p $F dir/file
$TOOL dir/file
rm -f dir/file # zstd keeps it
$Z -rd dir
cmp -b $F dir/file
