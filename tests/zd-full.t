! $Z -zF$TOOL </dev/null >/dev/full 2>stderr
cat stderr
grep -q ': stdout: No space left on device' stderr
echo meow|$TOOL >file$EXT
! $Z -d <file$EXT >/dev/full 2>stderr
cat stderr
grep -q ': stdout: No space left on device' stderr
! $Z -cd file$EXT >/dev/full 2>stderr
cat stderr
grep -q ': stdout: No space left on device' stderr
