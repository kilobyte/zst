case $TOOL in
    gzip)	UNTOOL=gunzip ;;
    bzip2)	UNTOOL=bunzip2 ;;
    bzip3)	UNTOOL=bunzip3 ;;
    *)		UNTOOL=un$TOOL ;;
esac
echo UNTOOL = $UNTOOL

cp "$F" file
mkdir mybin # in case . is in PATH
ln -s "$Z" mybin/"$UNTOOL"
$TOOL file
rm -f file # zstd leaves it
mybin/$UNTOOL file$EXT
cmp -b "$F" file
