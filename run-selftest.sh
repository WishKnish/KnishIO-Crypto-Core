#!/usr/bin/env bash
# Builds kcore with its tests and runs them (selftest, loadtest, export check; selftest_noavx2 on
# x86_64). The exit code is ctest's.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DKCORE_BUILD_TESTS=ON && cmake --build build -j &&
    ctest --test-dir build --output-on-failure
