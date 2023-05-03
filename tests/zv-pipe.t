echo foo|$Z -vF$TOOL >/dev/null 2>stderr
cat stderr
grep -q '^stdin: 4 → [0-9][0-9] ([0-9]\{3,4\}%)$' stderr
$Z </dev/null -vF$TOOL >/dev/null 2>stderr
cat stderr
grep -q '^stdin: 0 → [0-9]\+ (header)$' stderr
