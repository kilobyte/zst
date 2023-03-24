mkdir foo
cd foo
touch a
touch b.gz
touch c.bz2
touch d.xz
touch e.zst
cd ..
$Z -F$TOOL -r foo 2>stderr || ret=$?
cat stderr
test $ret -eq 2
test `grep unchanged stderr|wc -l` -eq 4
