touch a
touch b.gz
touch c.bz2
touch d.xz
touch e.zst
$Z -F$TOOL * 2>stderr || ret=$?
cat stderr
test $ret -eq 2
test `grep unchanged stderr|wc -l` -eq 4
