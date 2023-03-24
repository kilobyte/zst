printf foo|$Z -F$TOOL >f
printf bar|$Z -F$TOOL >>f
printf baz|$Z -F$TOOL >>f
hd f
$Z -t <f
printf foobarbaz >exp
$Z -cd <f|cmp -b exp -
