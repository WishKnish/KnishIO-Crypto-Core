/*
 * KnishIO assembly unit for mlkem-native (FIPS 203), the companion of
 * mlkem_multilevel.c: same namespace, same multilevel build, and the
 * level-independent assembly exactly once (MLK_CONFIG_MULTILEVEL_WITH_SHARED).
 * Compiled only when CMake selects the native backend (KCORE_NATIVE), which also
 * defines MLK_CONFIG_USE_NATIVE_BACKEND_ARITH and MLK_CONFIG_USE_NATIVE_BACKEND_FIPS202.
 */
#ifndef MLK_CONFIG_MULTILEVEL_BUILD
#define MLK_CONFIG_MULTILEVEL_BUILD
#endif
/* kcore-private namespace; must match mlkem_multilevel.c (see the comment there). */
#ifndef MLK_CONFIG_NAMESPACE_PREFIX
#define MLK_CONFIG_NAMESPACE_PREFIX kcmlk
#endif
#define MLK_CONFIG_MULTILEVEL_WITH_SHARED
#define MLK_CONFIG_PARAMETER_SET 768
#include "../external/mlkem-native/mlkem/mlkem_native_asm.S"
