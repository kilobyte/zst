[ `whoami` != root ] || exit 42
cp -p $F file
$TOOL file
rm -f file
chmod u-r file$EXT
! $Z -d file$EXT 2>stderr
cat stderr
grep -q 'Permission denied' stderr
