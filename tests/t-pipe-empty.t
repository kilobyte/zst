! $Z -tF$TOOL </dev/null 2>stderr
cat stderr
grep ': stdin: unexpected end of file$' stderr
