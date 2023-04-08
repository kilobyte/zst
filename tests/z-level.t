# Need enough data for compression level to matter.
dd if=/dev/urandom bs=65536 count=1|od >file
$Z -F$TOOL -z1 <file >1$EXT
$Z -F$TOOL -z9 <file >9$EXT
find . -name '*'$EXT -printf '%s\t%P\n'|sort -nr|sort -ck2
