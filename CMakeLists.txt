cmake_minimum_required(VERSION 3.20)

project(KnishIO-Crypto-Core
    VERSION 0.1.0
    DESCRIPTION "KnishIO shared crypto core: SHAKE256, WOTS+ chains, ML-KEM-1024"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_property(KCORE_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT KCORE_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(KCORE_BUILD_TESTS "Build the selftest, loadtest and export checks" OFF)
option(KCORE_PORTABLE "mlkem-native as portable C, no assembly" OFF)
# Toolchains whose executables cannot dlopen (static musl) set this OFF.
option(KCORE_BUILD_LOADTEST "Build the dlopen/LoadLibrary loader test" ON)

set(KCORE_MLKEM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/mlkem-native/mlkem)
if(NOT EXISTS ${KCORE_MLKEM_DIR}/mlkem_native.c)
    message(FATAL_ERROR "external/mlkem-native is missing: run git submodule update --init")
endif()

# --- backend selection -------------------------------------------------------------------------
# Native = mlkem-native's assembly backends (x86_64 AVX2 with runtime dispatch, AArch64 NEON and,
# where compiled in, SHA3). MSVC (including clang-cl) and any other architecture use portable C.
if(DEFINED CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "")
    set(KCORE_ARCHS ${CMAKE_OSX_ARCHITECTURES})
else()
    set(KCORE_ARCHS ${CMAKE_SYSTEM_PROCESSOR})
endif()

set(KCORE_NATIVE ON)
set(KCORE_HAS_X86_64 OFF)
foreach(arch IN LISTS KCORE_ARCHS)
    if(NOT arch MATCHES "^(x86_64|AMD64|amd64|aarch64|arm64|ARM64)$")
        set(KCORE_NATIVE OFF)
    endif()
    if(arch MATCHES "^(x86_64|AMD64|amd64)$")
        set(KCORE_HAS_X86_64 ON)
    endif()
endforeach()
if(KCORE_PORTABLE OR MSVC)
    set(KCORE_NATIVE OFF)
endif()

if(KCORE_NATIVE)
    message(STATUS "kcore backend: native")
    enable_language(ASM)
    string(APPEND CMAKE_ASM_FLAGS " ${CMAKE_C_FLAGS}")
    set(KCORE_ASM_SOURCES src/mlkem_multilevel_asm.S)
    set(KCORE_BACKEND_DEFS MLK_CONFIG_USE_NATIVE_BACKEND_ARITH MLK_CONFIG_USE_NATIVE_BACKEND_FIPS202)
else()
    message(STATUS "kcore backend: portable")
    set(KCORE_ASM_SOURCES "")
    set(KCORE_BACKEND_DEFS KCORE_PORTABLE)
endif()

set(KCORE_SOURCES src/kcore.c src/mlkem_multilevel.c ${KCORE_ASM_SOURCES})

function(kcore_configure target)
    target_compile_definitions(${target} PRIVATE
        MLK_CONFIG_MULTILEVEL_BUILD=1
        MLK_CONFIG_NO_SUPERCOP=1
        MLK_CONFIG_NO_RANDOMIZED_API=1
        [[MLK_CONFIG_FILE="kcore_mlkem_config.h"]]
        ${KCORE_BACKEND_DEFS})
    target_include_directories(${target}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${KCORE_MLKEM_DIR}/src
            ${KCORE_MLKEM_DIR}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    set_target_properties(${target} PROPERTIES
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON)
    # Skipped on Windows: the MinGW/zig link provides no __stack_chk_fail.
    if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android|Darwin)$")
        target_compile_options(${target} PRIVATE -fstack-protector-strong)
        target_compile_definitions(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)
    endif()
endfunction()

# --- libraries ---------------------------------------------------------------------------------
# The sources compile once per library: on Windows KCORE_API differs (dllexport vs nothing).
add_library(kcore SHARED ${KCORE_SOURCES})
add_library(KnishIO::kcore ALIAS kcore)
kcore_configure(kcore)
target_compile_definitions(kcore PRIVATE KCORE_BUILDING)
set_target_properties(kcore PROPERTIES
    OUTPUT_NAME kcore
    EXPORT_NAME kcore
    VERSION ${PROJECT_VERSION})
if(NOT ANDROID)
    # APKs require an unversioned libkcore.so.
    set_target_properties(kcore PROPERTIES SOVERSION 1)
endif()
if(APPLE)
    # CMake defaults the Mach-O compatibility version to SOVERSION (1.0.0), which would exceed the
    # current version (0.x) and make dyld refuse the library. Each release is compatible with
    # clients linked against it or any earlier release.
    set_target_properties(kcore PROPERTIES
        MACHO_COMPATIBILITY_VERSION ${PROJECT_VERSION}
        MACHO_CURRENT_VERSION ${PROJECT_VERSION})
endif()
if(WIN32)
    set_target_properties(kcore PROPERTIES PREFIX "" IMPORT_PREFIX "")
endif()
if(APPLE)
    target_link_options(kcore PRIVATE LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/src/kcore.exp)
    set_target_properties(kcore PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/kcore.exp)
elseif(NOT WIN32)
    target_link_options(kcore PRIVATE LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/kcore.map)
    set_target_properties(kcore PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/kcore.map)
endif()
if(ANDROID)
    target_link_options(kcore PRIVATE LINKER:-z,max-page-size=16384)
endif()

add_library(kcore-static STATIC ${KCORE_SOURCES})
add_library(KnishIO::kcore-static ALIAS kcore-static)
kcore_configure(kcore-static)
target_compile_definitions(kcore-static PUBLIC KCORE_STATIC)
if(WIN32)
    set_target_properties(kcore-static PROPERTIES OUTPUT_NAME kcore_static)
else()
    set_target_properties(kcore-static PROPERTIES OUTPUT_NAME kcore)
endif()
set_target_properties(kcore-static PROPERTIES EXPORT_NAME kcore-static)

# --- install and packaging ---------------------------------------------------------------------
install(TARGETS kcore kcore-static
    EXPORT KnishIOCryptoCoreTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/kcore.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT KnishIOCryptoCoreTargets
    FILE KnishIOCryptoCoreTargets.cmake
    NAMESPACE KnishIO::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/KnishIOCryptoCore)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/KnishIOCryptoCoreConfig.cmake.in
    KnishIOCryptoCoreConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/KnishIOCryptoCore)
write_basic_package_version_file(
    KnishIOCryptoCoreConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/KnishIOCryptoCoreConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/KnishIOCryptoCoreConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/KnishIOCryptoCore)

# prefix is derived from the .pc file's own location (${pcfiledir}), so the installed package
# works wherever it is extracted.
file(RELATIVE_PATH KCORE_PC_RELPREFIX "/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "/")
string(REGEX REPLACE "/$" "" KCORE_PC_RELPREFIX "${KCORE_PC_RELPREFIX}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/knishio-crypto-core.pc.in knishio-crypto-core.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/knishio-crypto-core.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# --- tests -------------------------------------------------------------------------------------
if(KCORE_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(KCORE_VECTORS ${CMAKE_BINARY_DIR}/vectors/differential.json)
    set(KCORE_FIXTURE ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cross-platform-test-vectors.json)
    add_custom_command(
        OUTPUT ${KCORE_VECTORS}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen_vectors.py
                ${KCORE_FIXTURE} ${CMAKE_BINARY_DIR}/vectors
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen_vectors.py ${KCORE_FIXTURE}
        COMMENT "Generating kcore selftest vectors"
        VERBATIM)
    add_custom_target(kcore_vectors ALL DEPENDS ${KCORE_VECTORS})

    add_executable(kcore_selftest tests/kcore_selftest.c)
    target_link_libraries(kcore_selftest PRIVATE kcore-static)
    target_include_directories(kcore_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    enable_testing()
    add_test(NAME selftest COMMAND kcore_selftest ${KCORE_VECTORS})
    set_tests_properties(selftest PROPERTIES PASS_REGULAR_EXPRESSION "SELFTEST ok")

    if(KCORE_NATIVE AND KCORE_HAS_X86_64)
        # Proof build: the same code with the AVX2 capability forced off (portable fallback).
        add_library(kcore-noavx2 STATIC ${KCORE_SOURCES})
        kcore_configure(kcore-noavx2)
        target_compile_definitions(kcore-noavx2 PRIVATE KCORE_FORCE_NO_AVX2 PUBLIC KCORE_STATIC)
        add_executable(kcore_selftest_noavx2 tests/kcore_selftest.c)
        target_link_libraries(kcore_selftest_noavx2 PRIVATE kcore-noavx2)
        target_include_directories(kcore_selftest_noavx2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        add_test(NAME selftest_noavx2 COMMAND kcore_selftest_noavx2 ${KCORE_VECTORS})
        set_tests_properties(selftest_noavx2 PROPERTIES PASS_REGULAR_EXPRESSION "SELFTEST ok")
    endif()

    if(KCORE_BUILD_LOADTEST)
        add_executable(kcore_loadtest tests/kcore_loadtest.c)
        target_link_libraries(kcore_loadtest PRIVATE ${CMAKE_DL_LIBS})
        add_dependencies(kcore_loadtest kcore)
        add_test(NAME loadtest COMMAND kcore_loadtest $<TARGET_FILE:kcore>)
        set_tests_properties(loadtest PROPERTIES PASS_REGULAR_EXPRESSION "LOADTEST ok")
    endif()

    if(NOT WIN32)
        if(APPLE)
            set(KCORE_EXPORT_KIND macho)
        else()
            set(KCORE_EXPORT_KIND elf)
        endif()
        add_test(NAME exports
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-exports.sh $<TARGET_FILE:kcore> ${KCORE_EXPORT_KIND})
        set_tests_properties(exports PROPERTIES PASS_REGULAR_EXPRESSION "EXPORTS ok")
    endif()
endif()

# --- lint --------------------------------------------------------------------------------------
# clang-tidy over the first-party sources (vendored mlkem-native excluded) against .clang-tidy.
# Read-only (no --fix). Run: cmake --build build --target lint
find_program(CLANG_TIDY_EXE NAMES clang-tidy HINTS /opt/homebrew/opt/llvm/bin)
if(CLANG_TIDY_EXE)
    set(KCORE_LINT_SOURCES src/kcore.c)
    if(KCORE_BUILD_TESTS)
        list(APPEND KCORE_LINT_SOURCES tests/kcore_selftest.c)
        if(KCORE_BUILD_LOADTEST)
            list(APPEND KCORE_LINT_SOURCES tests/kcore_loadtest.c)
        endif()
    endif()
    list(TRANSFORM KCORE_LINT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
    # macOS: Apple clang's implicit SDK sysroot differs from Homebrew LLVM clang-tidy's.
    set(KCORE_TIDY_EXTRA "")
    if(APPLE)
        execute_process(COMMAND xcrun --show-sdk-path
            OUTPUT_VARIABLE KCORE_MACOS_SDK OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(KCORE_TIDY_EXTRA --extra-arg=-isysroot${KCORE_MACOS_SDK})
    endif()
    add_custom_target(lint
        COMMAND ${CLANG_TIDY_EXE} ${KCORE_TIDY_EXTRA} -p ${CMAKE_BINARY_DIR} ${KCORE_LINT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running clang-tidy static analysis (lint gate)"
        VERBATIM)
else()
    message(STATUS "clang-tidy: NOT found; lint target unavailable")
endif()
