#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

if [[ -n "${CC:-}" ]]; then
    compiler="$CC"
elif command -v cc >/dev/null 2>&1; then
    compiler=cc
elif command -v gcc >/dev/null 2>&1; then
    compiler=gcc
elif command -v clang >/dev/null 2>&1; then
    compiler=clang
else
    echo "error: no C compiler was found." >&2
    echo "Ubuntu/Debian: sudo apt-get update && sudo apt-get install -y build-essential" >&2
    echo "Fedora: sudo dnf groupinstall -y 'Development Tools'" >&2
    echo "macOS: install Xcode Command Line Tools with: xcode-select --install" >&2
    exit 1
fi

CFLAGS="${CFLAGS:--O2 -std=gnu11 -Wall -Wextra}"
LDFLAGS="${LDFLAGS:-}"

read -r -a CFLAGS_ARRAY <<< "$CFLAGS"
read -r -a LDFLAGS_ARRAY <<< "$LDFLAGS"

"$compiler" "${CFLAGS_ARRAY[@]}" \
    bytebeat.c strict_expr.c quickjs.c cutils.c dtoa.c libregexp.c libunicode.c \
    -DCONFIG_VERSION='"bytebeat"' \
    -o bytebeat \
    "${LDFLAGS_ARRAY[@]}" -lm -lpthread -ldl

echo "Built ./bytebeat"