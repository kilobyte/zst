HD="0 1 2 3 4 5 6 7 8 9 A B C D E F"
for a in $HD; do
  for b in $HD; do
    for c in $HD; do
      x=$a$b$c
      mkdir $x
      echo meow >$x/foo
      echo bark >$x/bar
    done
  done
done

$Z -rF$TOOL *
$Z -dr *
grep -lr '^meow$' .|wc -l|tee meow
grep -lr '^bark$' .|wc -l|tee bark
diff -u meow - <<END
4096
END
diff -u bark - <<END
4096
END
