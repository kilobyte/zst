mkdir foo
$Z foo 2>stderr || ret=$?
cat stderr
test $ret -eq 2
grep -q 'is not a regular file -- ignored' stderr
