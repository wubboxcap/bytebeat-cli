#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

CC="${CC:-cc}"
CFLAGS="${CFLAGS:--O2 -std=gnu11 -Wall -Wextra}"
LDFLAGS="${LDFLAGS:-}"

read -r -a CFLAGS_ARRAY <<< "$CFLAGS"
read -r -a LDFLAGS_ARRAY <<< "$LDFLAGS"

"$CC" "${CFLAGS_ARRAY[@]}" \
    bytebeat.c strict_expr.c quickjs.c cutils.c dtoa.c libregexp.c libunicode.c \
    -DCONFIG_VERSION='"bytebeat"' \
    -o bytebeat \
    "${LDFLAGS_ARRAY[@]}" -lm -lpthread

echo "Built ./bytebeat"