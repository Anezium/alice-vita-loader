#!/usr/bin/env bash
# Build and run the host-side futex test (Linux, gcc). No Vita SDK needed.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../../source"
OUT="${1:-$(mktemp -d)}"
gcc -O2 -g -Wall -pthread -I"$HERE/shim" -I"$SRC" \
    "$HERE/futex_host_test.c" "$SRC/reimpl/futex.c" -o "$OUT/futex_host_test"
"$OUT/futex_host_test"
