#!/usr/bin/env bash
#
# Version consistency gate for KnishIO-Crypto-Core (kcore).
#
#   check-version.sh              -> every place this repo declares its version must agree
#   check-version.sh 0.2.0        -> ...and they must all equal that value (the release tag)
#
# Why this is a script and not inline YAML: a gate that only exists inside a workflow can only
# be exercised by triggering that workflow. The publish workflow runs on tag pushes only, so
# inline logic could not be tested without cutting a real release. As a script it runs locally,
# in CI on every push, and in the publish job, from one definition.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

expected="${1:-}"
# Tolerate a leading 'v' so callers can pass a raw tag ref without pre-stripping.
expected="${expected#v}"

declare -a names=() values=()

collect() {
  names+=("$1")
  values+=("$2")
}

# --- version sources for this repo ------------------------------------------------------
collect "CMakeLists.txt project(VERSION)" \
  "$(grep -A5 'project(' CMakeLists.txt | grep -oE 'VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' | head -1 | awk '{print $2}')"
# The public header is what bindings and consumers compile against.
collect "kcore.h KCORE_VERSION_STRING" \
  "$(grep -oE 'KCORE_VERSION_STRING[[:space:]]+"[0-9]+\.[0-9]+\.[0-9]+"' include/kcore.h | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
# The numeric macros are installed public API too, and nothing else ties them to the string.
collect "kcore.h KCORE_VERSION_MAJOR.MINOR.PATCH" \
  "$(awk '$1 == "#define" && $2 ~ /^KCORE_VERSION_(MAJOR|MINOR|PATCH)$/ { v[$2] = $3 }
          END { if (("KCORE_VERSION_MAJOR" in v) && ("KCORE_VERSION_MINOR" in v) && ("KCORE_VERSION_PATCH" in v))
                  print v["KCORE_VERSION_MAJOR"] "." v["KCORE_VERSION_MINOR"] "." v["KCORE_VERSION_PATCH"] }' include/kcore.h)"
# -----------------------------------------------------------------------------------------

mismatches=()

for i in "${!names[@]}"; do
  if [[ -z "${values[$i]}" ]]; then
    mismatches+=("${names[$i]}: could not parse a version — the source moved or the pattern is stale")
  fi
done

# Every declared source must agree with the first one.
reference="${values[0]}"
for i in "${!names[@]}"; do
  [[ -n "${values[$i]}" ]] || continue
  if [[ "${values[$i]}" != "$reference" ]]; then
    mismatches+=("${names[$i]} is '${values[$i]}' but ${names[0]} is '$reference'")
  fi
done

# And, when a version was supplied, they must all equal it.
if [[ -n "$expected" ]]; then
  for i in "${!names[@]}"; do
    [[ -n "${values[$i]}" ]] || continue
    if [[ "${values[$i]}" != "$expected" ]]; then
      mismatches+=("${names[$i]} is '${values[$i]}' but the expected version is '$expected'")
    fi
  done
fi

for i in "${!names[@]}"; do
  printf '  %-34s %s\n' "${names[$i]}" "${values[$i]:-<unparsed>}"
done
[[ -n "$expected" ]] && printf '  %-34s %s\n' "expected (tag)" "$expected"

if [[ ${#mismatches[@]} -gt 0 ]]; then
  echo "::error::version consistency check FAILED"
  printf '::error::%s\n' "${mismatches[@]}"
  exit 1
fi

echo "OK: version is $reference${expected:+ and matches the expected $expected}"

# Export for downstream workflow steps when running under Actions.
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  echo "version=$reference" >> "$GITHUB_OUTPUT"
fi
