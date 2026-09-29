/*
 * mlkem-native configuration for kcore, used on every target as
 * -DMLK_CONFIG_FILE="kcore_mlkem_config.h": the upstream defaults plus
 *
 * 1. Runtime AVX2 dispatch on x86_64. MLK_SYS_X86_64_AVX2 is defined HERE rather than on the
 *    command line because a CMake definition cannot be limited to the x86_64 slice of a universal
 *    macOS build; the C code is still compiled WITHOUT -mavx2, and the AVX2 assembly runs only when
 *    kcore_x86_avx2_usable() (CPUID + OSXSAVE/XGETBV) says so. common.h includes this file before
 *    sys.h, and mlkem_multilevel.c keeps MLK_CONFIG_MONOBUILD_KEEP_SHARED_HEADERS defined across
 *    both parameter-set passes, so the define survives into the ML-KEM-1024 pass.
 * 2. The SHA3 capability keeps the upstream compile-time semantics: 1 whenever __ARM_FEATURE_SHA3
 *    is compiled in (Apple arm64), so the v84a Keccak backends stay selectable.
 *
 * No outer include guard: mlkem_multilevel.c includes mlkem_native.c once per parameter set, and
 * each pass must see the upstream defaults again. Only the function is guarded.
 */
#include "mlkem_native_config.h"

#if (defined(__x86_64__) || defined(_M_X64)) && !defined(KCORE_PORTABLE) && !(defined(_MSC_VER) && !defined(__clang__))
#define MLK_SYS_X86_64_AVX2
#endif

#define MLK_CONFIG_CUSTOM_CAPABILITY_FUNC

#if !defined(__ASSEMBLER__) && !defined(KCORE_MLK_CAP_DEFINED)
#define KCORE_MLK_CAP_DEFINED
#include <stdint.h>
#include "src/sys.h"
#include "kcore_cpu_x86.h"

static MLK_INLINE int mlk_sys_check_capability(mlk_sys_cap cap)
{
    if (cap == MLK_SYS_CAP_AVX2) {
#if defined(KCORE_FORCE_NO_AVX2)
        return 0; /* proof build: always take the portable fallback */
#else
        return kcore_x86_avx2_usable();
#endif
    }
    if (cap == MLK_SYS_CAP_SHA3) {
#if defined(__ARM_FEATURE_SHA3)
        return 1;
#else
        return 0;
#endif
    }
    return 0;
}
#endif
