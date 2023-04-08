[ `whoami` != root ] || exit 42
cp -p $F file
chmod u-r file
! $Z file 2>stderr
cat stderr
grep -q 'Permission denied' stderr
