! $Z -tF$TOOL </dev/null 2>stderr
cat stderr
grep ': stdin: not a compressed file$' stderr
