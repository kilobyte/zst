echo meow >meow.txt
$Z -cdf meow.txt >stdout
cmp -b meow.txt stdout
$TOOL meow.txt
$Z -cdf meow.txt$EXT >stdout
echo meow >meow.txt
cmp -b meow.txt stdout
