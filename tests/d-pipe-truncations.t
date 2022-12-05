echo meow|$TOOL >file$EXT
LEN=`stat -c %s file$EXT`
echo LEN=$LEN
i=0
while [ $i -lt $LEN ]; do
    echo len=$i
    ! dd status=none if=file$EXT bs=1 count=$i|$Z -d >/dev/null 2>stderr
    cat stderr
    grep -q ': stdin: \(not a compressed file\|unexpected end of file\)$' stderr
    i=$(( i+1 ))
done
