#!/bin/sh
# Corpus test: tokenize every .vhd/.vhdl file in hdl-modules at a pinned
# commit and check the invariants in tests/corpus_run.c. The corpus is
# fetched, not vendored. Usage: sh tests/corpus.sh [corpus_run binary]
set -e
REPO=https://github.com/hdl-modules/hdl-modules
PIN=8142d3a1d878651811427e609293946e699c5cb7
DIR=build/corpus/hdl-modules
RUN=${1:-build/corpus_run}

if [ "$(git -C "$DIR" rev-parse HEAD 2>/dev/null)" != "$PIN" ]; then
	rm -rf "$DIR"
	mkdir -p "$DIR"
	git -C "$DIR" init -q
	# Keep bytes exactly as committed (CRLF etc.) on every platform.
	git -C "$DIR" config core.autocrlf false
	git -C "$DIR" remote add origin "$REPO"
	git -C "$DIR" fetch -q --depth 1 origin "$PIN"
	git -C "$DIR" checkout -q FETCH_HEAD
fi
test "$(git -C "$DIR" rev-parse HEAD)" = "$PIN"
find "$DIR" -type f \( -name '*.vhd' -o -name '*.vhdl' \) | LC_ALL=C sort \
	| "$RUN"
