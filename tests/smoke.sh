#!/bin/sh
# Smoke test: the binary builds and runs.
set -e
./vhdiagram >/dev/null
echo "smoke: ok"
