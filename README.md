<div style="text-align:center">
  <img src="https://raw.githubusercontent.com/WishKnish/KnishIO-Technical-Whitepaper/master/KnishIO-Logo.png" alt="Knish.IO: Post-Blockchain Platform" />
</div>
<div style="text-align:center">info@wishknish.com | https://wishknish.com</div>

# Knish.IO Crypto Core (kcore)

kcore is one small C library that implements the KnishIO hot cryptographic paths for every SDK
through FFI: SHAKE256, WOTS+ chain walking, the WOTS+ address, and ML-KEM-1024 (FIPS 203) on
[mlkem-native](https://github.com/pq-code-package/mlkem-native) v1.2.0, vendored as the submodule
`external/mlkem-native`.

## API

`include/kcore.h`. Every function except `kcore_abi_version` returns `0` on success and `-1` on
invalid arguments; on `-1` no output buffer is touched.

| Function | Contract |
|---|---|
| `int kcore_abi_version(void)` | Returns `KCORE_ABI_VERSION` (1). Bindings refuse a library whose value differs from the one they were written for. |
| `kcore_shake256(in, inlen, out, outlen)` | SHAKE256(`in`) → `outlen` bytes, `outlen` 1..1048576. `in` may be `NULL` when `inlen` is 0. |
| `kcore_chains_hex(chunks, counts, n, ways)` | Advances `n` (1..64) WOTS+ chains in place. `chunks` holds `n`×128 hex characters (no NUL); chunk *i* becomes hex(SHAKE256(chunk, 64 bytes)) applied `counts[i]` (0..64) times. `ways` is 1 (scalar) or 4 (four-lane Keccak). |
| `kcore_wots_address(key_hex2048, address_hex64)` | WOTS+ address of a 2048-hex key: 64 hex characters, no NUL. |
| `kcore_mlkem1024_keypair(seed[64], pk[1568], sk[3168])` | Deterministic ML-KEM-1024 key generation. |
| `kcore_mlkem1024_encaps(pk, coins[32], ct[1568], ss[32])` | Deterministic encapsulation. |
| `kcore_mlkem1024_decaps(ct, sk, ss[32])` | Decapsulation (implicit rejection per FIPS 203). |

## Build and test

```bash
git submodule update --init
cmake -S . -B build -DKCORE_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure   # selftest, loadtest, exports (+ selftest_noavx2 on x86_64)
cmake --build build --target lint             # clang-tidy, when found
```

`./run-selftest.sh` runs the same build and tests. The installed package provides
`KnishIO::kcore` / `KnishIO::kcore-static` through `find_package(KnishIOCryptoCore)` and
`knishio-crypto-core.pc`; `packaging/check-release-package.sh <stage> <version>` checks a
relocated install through all three consumer paths.

| CMake option | Default | Meaning |
|---|---|---|
| `KCORE_BUILD_TESTS` | `OFF` | Selftest, loadtest, export check and the vector generator (needs Python 3). |
| `KCORE_PORTABLE` | `OFF` | mlkem-native as portable C, no assembly. MSVC always builds this way. |
| `KCORE_BUILD_LOADTEST` | `ON` | The dlopen/LoadLibrary loader test. The musl toolchains turn it off (static musl executables cannot `dlopen`). |

## Distribution targets (tier 1)

`scripts/build-dist.sh <target> <version> <outdir>` builds a package
(`knishio-crypto-core-<version>-<target>.tar.gz`, with a `SHAKE256SUMS` manifest) and, where the
target can run tests, a test bundle (`kcore-tests-<version>-<target>.tar.gz`) that
`scripts/run-dist-tests.sh` executes.

| Target | Toolchain | Floor / baseline |
|---|---|---|
| `linux-x64-gnu` | zig 0.16.0 `x86_64-linux-gnu.2.17` | glibc 2.17, baseline x86-64, runtime AVX2 |
| `linux-arm64-gnu` | zig 0.16.0 `aarch64-linux-gnu.2.17` | glibc 2.17, Cortex-A72 |
| `linux-x64-musl` | zig 0.16.0 `x86_64-linux-musl` | musl, baseline x86-64, runtime AVX2 |
| `linux-arm64-musl` | zig 0.16.0 `aarch64-linux-musl` | musl, Cortex-A72 |
| `darwin-universal` | Xcode clang, `arm64;x86_64` | macOS 11.0; arm64 uses the SHA3 Keccak backend, x86_64 runtime AVX2 |
| `windows-x64` | zig 0.16.0 `x86_64-windows-gnu` | baseline x86-64, runtime AVX2; imports KERNEL32 + the UCRT (`api-ms-win-crt-*`, in Windows 10+); `kcore.dll` + `kcore.dll.a` |
| `android-arm64-v8a` | NDK r27d, `android-31`, `-march=armv8-a+crc+crypto` | API 31, 16 KiB page-aligned `LOAD` segments, unversioned `libkcore.so` |
| `wasm32` | wasi-sdk-34, `wasm32-wasip1` reactor | WASI preview 1; exports the seven functions plus `malloc`/`free` |

The NDK and wasi-sdk downloads are pinned by sha256 in `scripts/build-dist.sh`.

## Exports and dispatch

- Shared libraries export exactly the seven API functions: `src/kcore.map` (ELF, symbol version
  `KCORE_1`), `src/kcore.exp` (Mach-O), `dllexport` (PE). `scripts/check-exports.sh` requires the
  export set to equal `scripts/exports.txt`.
- mlkem-native is compiled under the kcore-private namespace `kcmlk` (`kcmlk_*`, `kcmlk768_*`,
  `kcmlk1024_*`), not upstream's `mlkem` that the KnishIO C and C++ SDKs use, so `libkcore.a`
  defines no symbol that either SDK's static library defines (with `mlkem` they shared 84).
- `src/kcore_mlkem_config.h` configures mlkem-native for every target. On x86_64 it enables the
  AVX2 assembly while the C code stays baseline x86-64; `mlk_sys_check_capability` runs it only
  when CPUID reports AVX2 **and** the OS saves the YMM state (OSXSAVE + XGETBV). The define lives
  in the config file, not on the command line, so a universal macOS build enables it for the
  x86_64 slice only. The SHA3 capability keeps upstream's compile-time meaning: available
  whenever `__ARM_FEATURE_SHA3` is compiled in.
- `kcore_selftest_noavx2` (x86_64 only) forces the portable fallback, so both paths are tested on
  AVX2 hardware. The selftest pins ML-KEM-1024 output bytes with the fixture's `mlkem1024.keygen`
  vector (the public key every KnishIO SDK derives from the same seed), so each backend is checked
  against the other SDKs, not only against itself.
- CI also checks what each package links against (`scripts/check-needed.sh`) and, because the arm64
  CI runners have SHA3 in hardware, that the Linux arm64 and Android archives contain the scalar
  Keccak backend and no SHA3 (`v84a`) code (`scripts/check-arm64-baseline.sh`).

## Background

The design, the bake-off that chose it and the cross-build proof are in
`docs/kcore-ffi-distribution-2026-09-27.md` in the KnishIOClientSDK workspace.

## License

GPL-3.0; see `LICENSE`.
