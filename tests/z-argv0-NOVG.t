cp "$F" file
mkdir mybin # in case . is in PATH
ln -s "$Z" mybin/"$TOOL"
mybin/$TOOL file
$TOOL -t file$EXT
