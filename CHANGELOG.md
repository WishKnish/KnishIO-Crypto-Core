# Changelog

All notable changes to KnishIO Crypto Core (kcore) are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
kcore has **no package registry**: a release is a git tag (`CMakeLists.txt`
`project(... VERSION ...)` is the version of record) and a GitHub Release carrying the
per-target packages and the source tarball.
Conventions for tags, commits, and these entries: `docs/SDK-RELEASE-CONVENTIONS.md`
in the KnishIOClientSDK monorepo.

## [Unreleased]

### Added

- Seven-function C ABI (`kcore_abi_version`, `kcore_shake256`, `kcore_chains_hex`,
  `kcore_wots_address`, `kcore_mlkem1024_keypair`, `kcore_mlkem1024_encaps`,
  `kcore_mlkem1024_decaps`) on mlkem-native v1.2.0, with the `KCORE_1` ELF symbol version and
  `KCORE_ABI_VERSION` 1 for bindings to check.
- Shared libraries export only the seven API functions (ELF version script, Mach-O exported
  symbols list, PE `dllexport`); no mlkem-native symbol leaks. mlkem-native is built under the
  private `kcmlk` namespace, so the static library shares no defined symbol with the KnishIO
  C/C++ SDKs' own `mlkem`-prefixed build.
- Runtime AVX2 dispatch (CPUID plus the OSXSAVE/XGETBV OS-support check) on every x86_64
  target, with the C code compiled for baseline x86-64.
- The SHA3 (v8.4-A) Keccak backends on Apple arm64.
- A Cortex-A72-safe Linux arm64 baseline (`-mcpu=cortex_a72`).
- A glibc 2.17 floor for the Linux gnu packages.
- Eight distribution targets: linux-x64-gnu, linux-arm64-gnu, linux-x64-musl,
  linux-arm64-musl, darwin-universal, windows-x64, android-arm64-v8a (16 KiB pages) and
  wasm32 (WASI reactor).
- Selftest over the cross-platform test vectors plus seeded differential cases, including the
  byte-frozen cross-SDK ML-KEM-1024 keygen vector; a dlopen/LoadLibrary loader test; and a WASM
  check.
- Release packages are gated on their DT_NEEDED set and, for Linux arm64 and Android, on the
  absence of SHA3 (ARMv8.4) Keccak code.
- CI on real runners (Linux x64/arm64, macOS arm64/x86_64, Windows x64) and a tag-triggered
  GitHub Release workflow.

[Unreleased]: https://github.com/WishKnish/KnishIO-Crypto-Core/commits/main
