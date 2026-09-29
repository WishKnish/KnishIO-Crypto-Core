#!/bin/sh
# check-glibc-floor.sh <max> <files...>
#
# Passes when the highest GLIBC_x.y symbol version referenced by any of the ELF files is <= <max>
# (numeric comparison). Uses $READELF (default readelf).
# Prints "GLIBC_FLOOR ok max=<v>" or "GLIBC_FLOOR FAIL max=<v> > <max>" and exits 1.
set -eu

if [ "$#" -lt 2 ]; then
    echo "usage: check-glibc-floor.sh <max> <files...>" >&2
    exit 2
fi
limit=$1
shift
max=$("${READELF:-readelf}" -W --dyn-syms "$@" |
    awk '{ s = $0; while (match(s, /GLIBC_[0-9]+\.[0-9]+/)) { v = substr(s, RSTART + 6, RLENGTH - 6); split(v, p, ".");
             k = p[1] * 1000 + p[2]; if (k > best) { best = k; bv = v } s = substr(s, RSTART + RLENGTH) } }
         END { print bv }')
if [ -z "$max" ]; then
    echo "GLIBC_FLOOR FAIL max=none > $limit (no GLIBC_ versions found in $*)"
    exit 1
fi
if awk -v m="$max" -v l="$limit" 'BEGIN { split(m, a, "."); split(l, b, "."); exit !(a[1] * 1000 + a[2] <= b[1] * 1000 + b[2]) }'; then
    echo "GLIBC_FLOOR ok max=$max"
else
    echo "GLIBC_FLOOR FAIL max=$max > $limit"
    exit 1
fi
