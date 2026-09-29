# kcore cross toolchain: zig 0.16.0, target x86_64-linux-gnu.2.17, -mcpu=x86_64.
# Used by scripts/build-dist.sh linux-x64-gnu.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER zig cc -target x86_64-linux-gnu.2.17 -mcpu=x86_64)
set(CMAKE_ASM_COMPILER zig cc -target x86_64-linux-gnu.2.17 -mcpu=x86_64)
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig-ar)
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig-ranlib)

# Strip at link time; the dynamic export table stays.
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-s")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-s")
