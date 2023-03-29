mkdir foo
cd foo
ln -s /dev/null a
mkfifo b
$Z -r * 2>stderr || ret=$?
ls -al
cat stderr
test $ret -eq 2
grep -q 'is not a directory or a regular file -- ignored' stderr
