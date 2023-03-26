mkdir foo
cd foo
touch a
touch b
gzip b
cd ..
$Z -dr foo 2>stderr || ret=$?
cat stderr
test $ret -eq 2
grep -q 'unknown suffix -- ignored' stderr
