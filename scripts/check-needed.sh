#!/bin/sh
# check-needed.sh <shared-lib> <allowed-ERE>
#
# Every DT_NEEDED entry of the ELF shared library must match <allowed-ERE> (anchor it yourself,
# e.g. '^(libc\.so|libdl\.so|libm\.so)$'), and there must be at least one. Proves what a package
# links against. Uses $READELF (default readelf).
# Prints "NEEDED ok <file>: <libs>" or "NEEDED FAIL <file>: ..." and exits 1.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: check-needed.sh <shared-lib> <allowed-ERE>" >&2
    exit 2
fi
libs=$("${READELF:-readelf}" -d "$1" | awk '/\(NEEDED\)/ { n = $NF; gsub(/[][]/, "", n); print n }')
if [ -z "$libs" ]; then
    echo "NEEDED FAIL $1: no DT_NEEDED entries"
    exit 1
fi
bad=$(printf '%s\n' "$libs" | grep -Ev -- "$2" || true)
list=$(printf '%s\n' "$libs" | tr '\n' ' ' | sed 's/ $//')
if [ -n "$bad" ]; then
    echo "NEEDED FAIL $1: $(printf '%s\n' "$bad" | tr '\n' ' ' | sed 's/ $//') not allowed by '$2' (all: $list)"
    exit 1
fi
echo "NEEDED ok $1: $list"
