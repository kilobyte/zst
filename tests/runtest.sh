#!/bin/sh
set -e

if [ -z "$SRC" ] || [ -z "$BIN" ]; then
	echo >&2 "Required ENV vars not set."
	exit 1
fi

if [ -n "$USE_VALGRIND" ] && [ "${1%NOVG}" = "$1" ] && which >/dev/null 2>/dev/null valgrind; then
	VG="valgrind --error-exitcode=43 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all "
fi
TESTDIR="$BIN/tests/test-$1-$TOOL"
export Z="$VG$BIN/zst" F="$SRC/zst.c"

rm -rf "$TESTDIR"
mkdir "$TESTDIR"
cd "$TESTDIR"
if [ -n "$TOOL" ]; then T=t; else T=T; fi
sh -e "$SRC/tests/$1.$T"
rm -rf "$TESTDIR"
