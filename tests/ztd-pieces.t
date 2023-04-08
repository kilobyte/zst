printf foo|$Z -F$TOOL >f
printf bar|$Z -F$TOOL >>f
printf baz|$Z -F$TOOL >>f
if which >/dev/null 2>/dev/null hd;then hd f;fi
$Z -t <f
printf foobarbaz >exp
$Z -cd <f|cmp -b exp -
