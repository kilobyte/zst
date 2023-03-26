dd if=/dev/urandom bs=65536 count=1 status=none|od >file
$Z -F$TOOL file
dd if=/dev/zero of=file$EXT bs=4096 seek=1 count=1 conv=notrunc status=none
$Z -d file$EXT 2>stderr || ret=$?
cat stderr
test $ret -eq 1
grep -q corrupt stderr
