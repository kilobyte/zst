#!/bin/sh
set -e

if [ -z "$SRC" ] || [ -z "$BIN" ] || [ -z "$TOOL" ]; then
	echo >&2 "Required ENV vars not set."
	exit 1
fi

TESTDIR="$BIN/tests/test-$1-$TOOL"
export Z="$BIN/zst" F="$SRC/zst.c"

rm -rf "$TESTDIR"
mkdir "$TESTDIR"
cd "$TESTDIR"
sh -e "$SRC/tests/$1.t"
rm -rf "$TESTDIR"
