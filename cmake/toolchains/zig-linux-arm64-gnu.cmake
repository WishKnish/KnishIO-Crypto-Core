# kcore cross toolchain: zig 0.16.0, target aarch64-linux-gnu.2.17, -mcpu=cortex_a72.
# Used by scripts/build-dist.sh linux-arm64-gnu.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER zig cc -target aarch64-linux-gnu.2.17 -mcpu=cortex_a72)
set(CMAKE_ASM_COMPILER zig cc -target aarch64-linux-gnu.2.17 -mcpu=cortex_a72)
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig-ar)
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig-ranlib)

# Strip at link time; the dynamic export table stays.
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-s")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-s")
