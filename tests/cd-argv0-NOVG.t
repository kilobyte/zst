case $TOOL in
    gzip)	TOOLCAT=gzcat ;;
    bzip2)	TOOLCAT=bzcat ;;
    bzip3)	TOOLCAT=bz3cat ;;
    *)		TOOLCAT=${TOOL}cat ;;
esac
echo TOOLCAT = $TOOLCAT

cp "$F" file
mkdir mybin # in case . is in PATH
ln -s "$Z" mybin/"$TOOLCAT"
$TOOL file
mybin/$TOOLCAT file$EXT >out
cmp -b "$F" out
