cp -p $F file.zst
! $Z -d file.zst 2>stderr
cat stderr
grep -q 'not a compressed file' stderr
