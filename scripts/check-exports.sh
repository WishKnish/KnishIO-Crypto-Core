#!/bin/sh
# check-exports.sh <file> <elf|macho|pe|wasm>
#
# Extracts the defined exported symbol names of a built kcore library and requires them to EQUAL
# scripts/exports.txt (both sorted). Prints "EXPORTS ok <file>" or
# "EXPORTS FAIL <file>: extra=<...> missing=<...>" and exits 1.
# Tools: $READELF (default readelf) for elf, $OBJDUMP (default objdump) for pe, nm/lipo for macho,
# node for wasm.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: check-exports.sh <file> <elf|macho|pe|wasm>" >&2
    exit 2
fi
file=$1
kind=$2
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
sort -u "$here/exports.txt" > "$tmp/want"

compare() { # $1 = label; stdin = names
    sort -u > "$tmp/got"
    if cmp -s "$tmp/want" "$tmp/got"; then
        return 0
    fi
    extra=$(comm -13 "$tmp/want" "$tmp/got" | tr '\n' ' ' | sed 's/ $//')
    missing=$(comm -23 "$tmp/want" "$tmp/got" | tr '\n' ' ' | sed 's/ $//')
    echo "EXPORTS FAIL $file$1: extra=$extra missing=$missing"
    return 1
}

case "$kind" in
elf)
    # Ndx ABS rows are version-definition symbols (GNU ld emits KCORE_1 itself), not functions.
    "${READELF:-readelf}" -W --dyn-syms "$file" > "$tmp/syms"
    awk '$7 != "UND" && $7 != "ABS" && ($5 == "GLOBAL" || $5 == "WEAK") && NF >= 8 { n = $8; sub(/@.*/, "", n); print n }' \
        "$tmp/syms" | compare "" || exit 1
    ;;
macho)
    archs=$(lipo -archs "$file")
    [ -n "$archs" ] || { echo "EXPORTS FAIL $file: no architectures"; exit 1; }
    for a in $archs; do
        nm -gU -arch "$a" "$file" | awk '{ n = $NF; sub(/^_/, "", n); print n }' | compare " [$a]" || exit 1
    done
    ;;
pe)
    "${OBJDUMP:-objdump}" -p "$file" > "$tmp/pe"
    awk '/\[Ordinal\/Name Pointer\] Table/ { f = 1; next } f && /^[ \t]*\[/ { print $NF; next } f { f = 0 }' \
        "$tmp/pe" | compare "" || exit 1
    ;;
wasm)
    node -e 'const fs = require("fs");
const m = new WebAssembly.Module(fs.readFileSync(process.argv[1]));
for (const e of WebAssembly.Module.exports(m)) if (e.name.startsWith("kcore_")) console.log(e.name);' "$file" \
        | compare "" || exit 1
    ;;
*)
    echo "check-exports.sh: unknown kind '$kind' (elf|macho|pe|wasm)" >&2
    exit 2
    ;;
esac
echo "EXPORTS ok $file"
