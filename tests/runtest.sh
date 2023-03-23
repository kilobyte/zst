#!/bin/sh
set -e

if [ -z "$SRC" ] || [ -z "$BIN" ] || [ -z "$TOOL" ]; then
	echo >&2 "Required ENV vars not set."
	exit 1
fi

if [ -n "$USE_VALGRIND" ] && which >/dev/null 2>/dev/null valgrind; then
	VG="valgrind --error-exitcode=43 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all "
fi
TESTDIR="$BIN/tests/test-$1-$TOOL"
export Z="$VG$BIN/zst" F="$SRC/zst.c"

rm -rf "$TESTDIR"
mkdir "$TESTDIR"
cd "$TESTDIR"
sh -e "$SRC/tests/$1.t"
rm -rf "$TESTDIR"
