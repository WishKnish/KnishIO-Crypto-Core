#!/bin/sh
# run-dist-tests.sh <tests-dir> [<shared-lib>] [--expect-avx2]
#
# Runs an extracted kcore-tests-<version>-<target> bundle (POSIX sh: Alpine, CentOS 7, macOS, Git
# Bash). Every test binary is invoked as `$KCORE_RUN_PREFIX <binary> ...`; KCORE_RUN_PREFIX is empty
# by default and set to "arch -x86_64" to run the x86_64 slice of a universal macOS binary.
#   1. kcore_selftest <tests-dir>/differential.json            -> "SELFTEST ok <n>"
#   2. kcore_selftest_noavx2 (when present), same check
#   3. with <shared-lib>: kcore_loadtest <shared-lib>           -> "LOADTEST ok"
#      (compiled from <tests-dir>/kcore_loadtest.c with cc when the bundle has no loadtest binary)
#   4. echoes the "cpu x86_64 avx2=" line; with --expect-avx2, avx2=0 is a ::warning::, not a failure
# Exits 1 on any failure.
set -u

if [ "$#" -lt 1 ]; then
    echo "usage: run-dist-tests.sh <tests-dir> [<shared-lib>] [--expect-avx2]" >&2
    exit 2
fi
dir=$1
shift
lib=""
expect_avx2=0
for a in "$@"; do
    case "$a" in
    --expect-avx2) expect_avx2=1 ;;
    *) lib=$a ;;
    esac
done
prefix=${KCORE_RUN_PREFIX:-}
vec="$dir/differential.json"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
status=0

find_bin() { # $1 = base name; prints the path of <dir>/<name> or <dir>/<name>.exe
    if [ -f "$dir/$1" ]; then
        echo "$dir/$1"
    elif [ -f "$dir/$1.exe" ]; then
        echo "$dir/$1.exe"
    fi
}

cpu_line() { # $1 = stderr file
    line=$(grep '^cpu ' "$1" | sed -n 1p)
    [ -n "$line" ] || return 0
    echo "$line"
    if [ "$expect_avx2" = 1 ] && [ "$line" = "cpu x86_64 avx2=0" ]; then
        echo "::warning::AVX2 not detected on this runner; the portable path ran"
    fi
}

run_selftest() { # $1 = binary
    # shellcheck disable=SC2086  # the prefix is deliberately word-split
    $prefix "$1" "$vec" > "$tmp/out" 2> "$tmp/err"
    rc=$?
    cat "$tmp/out"
    cpu_line "$tmp/err"
    if [ "$rc" -ne 0 ] || ! grep -q '^SELFTEST ok ' "$tmp/out"; then
        cat "$tmp/err" >&2
        echo "FAIL $(basename "$1") exit=$rc"
        status=1
    fi
}

st=$(find_bin kcore_selftest)
if [ -z "$st" ]; then
    echo "FAIL no kcore_selftest in $dir"
    exit 1
fi
run_selftest "$st"
na=$(find_bin kcore_selftest_noavx2)
[ -z "$na" ] || run_selftest "$na"

if [ -n "$lib" ]; then
    lt=$(find_bin kcore_loadtest)
    if [ -z "$lt" ] && command -v cc > /dev/null 2>&1; then
        ldl=""
        if [ "$(uname -s)" = Linux ] && ldd --version 2>&1 | grep -qiE 'glibc|gnu libc'; then
            ldl="-ldl"
        fi
        # shellcheck disable=SC2086
        if cc -O2 -o "$tmp/lt" "$dir/kcore_loadtest.c" $ldl; then
            lt="$tmp/lt"
        else
            echo "FAIL compiling kcore_loadtest.c"
            status=1
        fi
    fi
    if [ -n "$lt" ]; then
        # shellcheck disable=SC2086
        $prefix "$lt" "$lib" > "$tmp/out" 2>&1
        rc=$?
        cat "$tmp/out"
        if [ "$rc" -ne 0 ] || ! grep -q '^LOADTEST ok' "$tmp/out"; then
            echo "FAIL $(basename "$lt") exit=$rc"
            status=1
        fi
    elif [ "$status" = 0 ]; then
        echo "LOADTEST skipped (no binary, no cc)"
    fi
fi

exit "$status"
