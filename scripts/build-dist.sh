#!/usr/bin/env bash
# build-dist.sh <target> <version> <outdir>
#
# Builds one kcore distribution package:
#   <outdir>/knishio-crypto-core-<version>-<target>.tar.gz   the release package (+ SHAKE256SUMS)
#   <outdir>/kcore-tests-<version>-<target>.tar.gz           the test bundle (not android/wasm32)
#
# Targets: linux-x64-gnu linux-arm64-gnu linux-x64-musl linux-arm64-musl windows-x64 (zig 0.16.0),
#          darwin-universal (Xcode clang), android-arm64-v8a (NDK r27d), wasm32 (wasi-sdk-34).
# The NDK comes from $ANDROID_NDK_ROOT and wasi-sdk from $WASI_SDK_PATH; when unset they are
# downloaded into .toolchains/ and checked against the sha256 values pinned below.
set -euo pipefail

# Pin provenance (checked 2026-09-29, not trust-on-first-download): the NDK zips match Google's
# SDK repository manifest (dl.google.com/android/repository/repository2-3.xml) on size and SHA-1,
# linux 663956036 / 22105e41..1121 (also the android/ndk wiki), darwin 839006233 / 2970926d..8c56;
# the wasi-sdk tarballs match the sha256 asset digests of GitHub release wasi-sdk-34.
NDK_VERSION=r27d
NDK_SHA256_linux=601246087a682d1944e1e16dd85bc6e49560fe8b6d61255be2829178c8ed15d9
NDK_SHA256_darwin=e69092f9d2bfa5d1199039980a14eb91c03cc971ab5c6968fc08a8e6b84e7bb7
WASI_VERSION=34
WASI_SHA256_x86_64_linux=b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4
WASI_SHA256_arm64_macos=9c59398106b417f8f14913380fdf0097a8cc0ff4af9eb3ce0065a859e88d49e9
ZIG_VERSION=0.16.0
EXPORTS=(kcore_abi_version kcore_chains_hex kcore_mlkem1024_decaps kcore_mlkem1024_encaps
         kcore_mlkem1024_keypair kcore_mlkem768_decaps kcore_mlkem768_encaps kcore_mlkem768_keypair
         kcore_shake256 kcore_wots_address)

fail() { echo "FAIL $*" >&2; exit 1; }

[ "$#" -eq 3 ] || { echo "usage: build-dist.sh <target> <version> <outdir>" >&2; exit 2; }
target=$1
version=$2
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p "$3"
outdir=$(cd "$3" && pwd)
case "$target" in
linux-x64-gnu | linux-arm64-gnu | linux-x64-musl | linux-arm64-musl | windows-x64 | darwin-universal | \
    android-arm64-v8a | wasm32) ;;
*) fail "unknown target '$target'" ;;
esac

P="$outdir/knishio-crypto-core-$version-$target"
T="$outdir/kcore-tests-$version-$target"
B="build/dist-$target"
rm -rf "$P" "$T" "$P.tar.gz" "$T.tar.gz" "$B"

sha256() { if command -v sha256sum > /dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | awk '{ print $1 }'; }

pack() { # <dir> -> <dir>.tar.gz, from <outdir>
    if [ "$(uname -s)" = Darwin ]; then
        # bsdtar would store xattrs (com.apple.provenance, quarantine) as LIBARCHIVE.xattr.* pax
        # headers and AppleDouble ._ files, which GNU tar warns about on extraction.
        COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs -czf "$1.tar.gz" -C "$outdir" "$(basename "$1")"
    else
        tar czf "$1.tar.gz" -C "$outdir" "$(basename "$1")"
    fi
}

fetch() { # <url> <file> <sha256>
    [ -f "$2" ] || { mkdir -p "$(dirname "$2")"; curl -fsSL --retry 3 -o "$2.part" "$1" && mv "$2.part" "$2"; }
    [ "$(sha256 "$2")" = "$3" ]
}

ndk_root() {
    if [ -n "${ANDROID_NDK_ROOT:-}" ]; then
        echo "$ANDROID_NDK_ROOT"
        return
    fi
    local os sum zip dir=.toolchains/android-ndk-$NDK_VERSION
    case "$(uname -s)" in
    Linux) os=linux sum=$NDK_SHA256_linux ;;
    Darwin) os=darwin sum=$NDK_SHA256_darwin ;;
    *) fail "no pinned NDK for $(uname -s); set ANDROID_NDK_ROOT" ;;
    esac
    if [ ! -f "$dir/build/cmake/android.toolchain.cmake" ]; then
        zip=.toolchains/android-ndk-$NDK_VERSION-$os.zip
        fetch "https://dl.google.com/android/repository/android-ndk-$NDK_VERSION-$os.zip" "$zip" "$sum" ||
            fail "NDK sha256"
        rm -rf "$dir"
        unzip -q "$zip" -d .toolchains
    fi
    echo "$PWD/$dir"
}

wasi_root() {
    if [ -n "${WASI_SDK_PATH:-}" ]; then
        echo "$WASI_SDK_PATH"
        return
    fi
    local host sum name
    case "$(uname -m)-$(uname -s)" in
    x86_64-Linux) host=x86_64-linux sum=$WASI_SHA256_x86_64_linux ;;
    arm64-Darwin) host=arm64-macos sum=$WASI_SHA256_arm64_macos ;;
    *) fail "no pinned wasi-sdk for $(uname -m)-$(uname -s); set WASI_SDK_PATH" ;;
    esac
    name=wasi-sdk-$WASI_VERSION.0-$host
    if [ ! -x ".toolchains/$name/bin/clang" ]; then
        fetch "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-$WASI_VERSION/$name.tar.gz" \
            ".toolchains/$name.tar.gz" "$sum" || fail "wasi-sdk sha256"
        rm -rf ".toolchains/$name"
        tar -xzf ".toolchains/$name.tar.gz" -C .toolchains
    fi
    echo "$PWD/.toolchains/$name"
}

tests_bundle=0
case "$target" in
linux-* | windows-x64)
    [ "$(zig version 2> /dev/null)" = "$ZIG_VERSION" ] || fail "zig $ZIG_VERSION required"
    cmake -S . -B "$B" -DCMAKE_TOOLCHAIN_FILE="cmake/toolchains/zig-$target.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DKCORE_BUILD_TESTS=ON
    cmake --build "$B" -j
    cmake --install "$B" --prefix "$P"
    tests_bundle=1
    ;;
darwin-universal)
    cmake -S . -B "$B" -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
        -DCMAKE_C_FLAGS="-Xarch_arm64 -mcpu=apple-m1" -DCMAKE_BUILD_TYPE=Release -DKCORE_BUILD_TESTS=ON
    cmake --build "$B" -j
    cmake --install "$B" --prefix "$P"
    tests_bundle=1
    ;;
android-arm64-v8a)
    ndk=$(ndk_root)
    cmake -S . -B "$B" -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-31 -DCMAKE_C_FLAGS="-march=armv8-a+crc+crypto" \
        -DCMAKE_BUILD_TYPE=Release -DKCORE_BUILD_TESTS=OFF
    cmake --build "$B" -j
    cmake --install "$B" --prefix "$P" --strip
    ;;
wasm32)
    wasi=$(wasi_root)
    exports=()
    for e in "${EXPORTS[@]}" malloc free; do exports+=("-Wl,--export=$e"); done
    mkdir -p "$P/include"
    "$wasi/bin/clang" --target=wasm32-wasip1 -O3 -mexec-model=reactor \
        -Iinclude -Isrc -Iexternal/mlkem-native/mlkem/src -Iexternal/mlkem-native/mlkem \
        -DMLK_CONFIG_MULTILEVEL_BUILD=1 -DMLK_CONFIG_NO_SUPERCOP=1 -DMLK_CONFIG_NO_RANDOMIZED_API=1 \
        '-DMLK_CONFIG_FILE="kcore_mlkem_config.h"' \
        src/kcore.c src/mlkem_multilevel.c -o "$P/kcore.wasm" "${exports[@]}"
    cp include/kcore.h "$P/include/"
    ;;
esac

cp LICENSE README.md "$P/"
python3 .github/scripts/shake256sums.py "$P"
pack "$P"

tests_out=none
if [ "$tests_bundle" = 1 ]; then
    mkdir -p "$T"
    for f in kcore_selftest kcore_selftest_noavx2 kcore_loadtest; do
        for x in "" .exe; do
            [ -f "$B/$f$x" ] && cp "$B/$f$x" "$T/"
        done
    done
    cp "$B/vectors/differential.json" tests/kcore_loadtest.c "$T/"
    pack "$T"
    tests_out="$T.tar.gz"
fi
echo "DIST $target package=$P.tar.gz tests=$tests_out"
