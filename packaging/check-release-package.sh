#!/usr/bin/env bash
# Consumer build gate for a KnishIO Crypto Core release package (an installed staging tree).
#
# usage: packaging/check-release-package.sh <stage-dir> <version>
#
# Checks that a copy of the package moved to a new directory works through every consumer
# path: the CMake package config and the pkg-config file carry no build-host paths, the
# installed header compiles on its own, and consumer/main.c builds and prints the expected
# SHAKE256 digest with plain -I/-L flags, with pkg-config, and with find_package (shared and
# static). Prints `PASS <check>` or `FAIL <check>: <detail>` per check and exits 1 if any
# check failed. It never writes into <stage-dir>; all work happens in a temporary directory.
set -uo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <stage-dir> <version>" >&2
    exit 2
fi
VERSION=$2
if ! STAGE=$(cd "$1" 2>/dev/null && pwd -P); then
    echo "FAIL stage: $1 is not a directory"
    exit 1
fi
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
CONSUMER="$HERE/consumer"
CC=${CC:-cc}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

# consumer/main.c prints SHAKE256("knishio-consumer-smoke") as 32 bytes of lowercase hex.
E=$(python3 -c "import hashlib;print(hashlib.shake_256(b'knishio-consumer-smoke').hexdigest(32))")

FAILED=0
pass() { echo "PASS $*"; }
fail() {
    echo "FAIL $1: $2"
    FAILED=1
}
# Indented excerpt of a log file, for a FAIL detail.
excerpt() { sed -n '1,25p' "$1" | sed 's/^/    /'; }
# Runs a consumer binary; its stdout must equal E.
run_expect() {
    local check=$1 bin=$2 out
    if ! out=$("$bin" 2>"$W/run.err"); then
        fail "$check" "$(basename "$bin") exited non-zero"
        excerpt "$W/run.err"
        return 1
    fi
    if [ "$out" != "$E" ]; then
        fail "$check" "$(basename "$bin") printed '$out', expected '$E'"
        return 1
    fi
    return 0
}

# 1. no-host-paths: the CMake package config and the pkg-config file must not name any
#    absolute path of the machine that built the package.
if python3 - "$STAGE" "$1" >"$W/host-paths.log" <<'PY'; then
import os
import re
import sys

stage, stage_arg = sys.argv[1], os.path.abspath(sys.argv[2])
pattern = re.compile(r'(^|[\s";=])/(usr|opt|Users|home|private|var|tmp|Volumes)/')
literals = {stage, stage_arg}
roots = [os.path.join(stage, 'lib', 'cmake'), os.path.join(stage, 'lib', 'pkgconfig')]
missing = [os.path.relpath(r, stage) for r in roots if not os.path.isdir(r)]
if missing:
    print('missing ' + ', '.join(missing))
    sys.exit(1)
hits, scanned = [], 0
for root in roots:
    for base, dirs, files in os.walk(root):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(base, name)
            scanned += 1
            with open(path, encoding='utf-8', errors='replace') as fh:
                for number, line in enumerate(fh, 1):
                    if pattern.search(line) or any(lit in line for lit in literals):
                        hits.append(f'{os.path.relpath(path, stage)}:{number}: {line.strip()}')
if hits:
    print(f'{len(hits)} line(s) with an absolute host path')
    print('\n'.join(hits))
    sys.exit(1)
print(f'{scanned} files')
PY
    pass "no-host-paths ($(sed -n 1p "$W/host-paths.log"))"
else
    fail no-host-paths "$(sed -n 1p "$W/host-paths.log")"
    sed -n '2,40p' "$W/host-paths.log" | sed 's/^/    /'
fi

# 2. relocate: every later check runs against a copy at a different path.
mkdir -p "$W/relocated"
if cp -R "$STAGE" "$W/relocated/pkg" 2>"$W/relocate.err"; then
    R=$(cd "$W/relocated/pkg" && pwd -P)
    pass "relocate ($R)"
else
    fail relocate "cp -R $STAGE failed"
    excerpt "$W/relocate.err"
    exit 1
fi
PCP="$R/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# 3. headers: the installed kcore.h compiles alone with the package's pkg-config cflags.
if PC_CFLAGS=$(PKG_CONFIG_PATH="$PCP" pkg-config --cflags knishio-crypto-core 2>"$W/pc-cflags.err"); then
    if [ ! -f "$R/include/kcore.h" ]; then
        fail headers "include/kcore.h is not in the package"
    else
        printf '#include <kcore.h>\n' >"$W/header.c"
        # shellcheck disable=SC2086  # pkg-config output is a flag list
        if "$CC" -std=c17 $PC_CFLAGS -fsyntax-only "$W/header.c" >"$W/header.log" 2>&1; then
            pass "headers (1 header)"
        else
            fail headers "kcore.h does not compile alone"
            excerpt "$W/header.log"
        fi
    fi
else
    fail headers "pkg-config --cflags knishio-crypto-core failed"
    excerpt "$W/pc-cflags.err"
fi

# 4. direct: plain -I/-L against the package.
if "$CC" -I"$R/include" "$CONSUMER/main.c" -o "$W/consumer-direct" \
        -L"$R/lib" -lkcore -Wl,-rpath,"$R/lib" >"$W/direct.log" 2>&1; then
    run_expect direct "$W/consumer-direct" && pass direct
else
    fail direct "compile/link with -I$R/include -L$R/lib -lkcore failed"
    excerpt "$W/direct.log"
fi

# 5. pkg-config: the relocated knishio-crypto-core.pc resolves to the copy and builds the consumer.
if ! PKG_CONFIG_PATH="$PCP" pkg-config --exists --print-errors knishio-crypto-core >"$W/pc.err" 2>&1; then
    fail pkg-config "pkg-config --exists knishio-crypto-core failed"
    excerpt "$W/pc.err"
else
    PC_PREFIX=$(PKG_CONFIG_PATH="$PCP" pkg-config --variable=prefix knishio-crypto-core)
    PC_REAL=$(cd "$PC_PREFIX" 2>/dev/null && pwd -P)
    if [ "$PC_REAL" != "$R" ]; then
        fail pkg-config "prefix '$PC_PREFIX' resolves to '${PC_REAL:-nothing}', not the relocated package $R"
    else
        PC_LIBDIR=$(PKG_CONFIG_PATH="$PCP" pkg-config --variable=libdir knishio-crypto-core)
        PC_FLAGS=$(PKG_CONFIG_PATH="$PCP" pkg-config --cflags --libs knishio-crypto-core)
        # shellcheck disable=SC2086
        if "$CC" "$CONSUMER/main.c" -o "$W/consumer-pkgconfig" $PC_FLAGS \
                -Wl,-rpath,"$PC_LIBDIR" >"$W/pkgconfig.log" 2>&1; then
            run_expect pkg-config "$W/consumer-pkgconfig" && pass pkg-config
        else
            fail pkg-config "compile/link with pkg-config --cflags --libs knishio-crypto-core failed"
            excerpt "$W/pkgconfig.log"
        fi
    fi
fi

# 6. find_package: the relocated CMake package config builds a shared and a static consumer.
if ! cmake -S "$CONSUMER" -B "$W/cmake" -DCMAKE_PREFIX_PATH="$R" -DEXPECTED_VERSION="$VERSION" \
        -DCMAKE_BUILD_TYPE=Release >"$W/cmake-configure.log" 2>&1; then
    fail find_package "cmake configure of packaging/consumer failed"
    grep -E 'CMake Error|Could NOT|not found|Unknown|FATAL' "$W/cmake-configure.log" | sed -n '1,20p' | sed 's/^/    /'
else
    PKG_DIR=$(sed -n 's/^KnishIOCryptoCore_DIR:PATH=//p' "$W/cmake/CMakeCache.txt")
    PKG_DIR_REAL=$(cd "$PKG_DIR" 2>/dev/null && pwd -P)
    case "$PKG_DIR_REAL/" in
        "$R"/*)
            if cmake --build "$W/cmake" >"$W/cmake-build.log" 2>&1; then
                run_expect find_package "$W/cmake/consumer" \
                    && run_expect find_package "$W/cmake/consumer_static" \
                    && pass "find_package (consumer, consumer_static)"
            else
                fail find_package "cmake --build of packaging/consumer failed"
                grep -E 'error|Error|undefined' "$W/cmake-build.log" | sed -n '1,20p' | sed 's/^/    /'
            fi
            ;;
        *)
            fail find_package "KnishIOCryptoCore_DIR '$PKG_DIR' is not inside the relocated package $R"
            ;;
    esac
fi

exit "$FAILED"
