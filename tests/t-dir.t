mkdir dir
cp -p $F dir/file
$TOOL dir/file
rm -f dir/file # zstd keeps it
$Z -rt dir
