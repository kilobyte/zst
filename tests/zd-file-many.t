HD="0 1 2 3 4 5 6 7 8 9 0 A B C D E F"
for a in $HD; do
  for b in $HD; do
    for c in $HD; do
      echo meow >$a$b$c
    done
  done
done

$Z -F$TOOL *
$Z -d *$EXT
! grep -lv '^meow$' *
