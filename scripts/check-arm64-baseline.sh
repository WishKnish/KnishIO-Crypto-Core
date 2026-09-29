#!/bin/sh
# check-arm64-baseline.sh <libkcore.a>
#
# The Linux arm64 and Android packages must run on Cortex-A72-class cores (no ARMv8.4 SHA3). The
# CI arm runners have SHA3 in hardware, so running the selftest there cannot catch a build that
# let SHA3 code in. This checks the archive instead: the scalar Keccak backend must be compiled in
# (> 0 defined x1_scalar symbols) and the SHA3 backends must be absent (0 v84a symbols).
# Uses $NM (default nm). Prints "ARM64_BASELINE ok x1_scalar=<n> v84a=0" or
# "ARM64_BASELINE FAIL ..." and exits 1.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: check-arm64-baseline.sh <libkcore.a>" >&2
    exit 2
fi
syms=$("${NM:-nm}" "$1" 2> /dev/null | awk 'NF >= 3 && $2 ~ /^[TtDdRr]$/ { print $3 }')
scalar=$(printf '%s\n' "$syms" | grep -c x1_scalar || true)
sha3=$(printf '%s\n' "$syms" | grep -c v84a || true)
if [ "$scalar" -gt 0 ] && [ "$sha3" -eq 0 ]; then
    echo "ARM64_BASELINE ok x1_scalar=$scalar v84a=0"
else
    echo "ARM64_BASELINE FAIL $1: x1_scalar=$scalar (need > 0) v84a=$sha3 (need 0)"
    exit 1
fi
