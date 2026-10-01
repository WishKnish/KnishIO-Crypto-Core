/*
 * kcore: the KnishIO shared crypto core (SHAKE256, WOTS+ chains, WOTS+ address, ML-KEM-1024).
 * Every function returns 0 on success and -1 on invalid arguments; on -1 no output is touched.
 */
#ifndef KCORE_H
#define KCORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KCORE_VERSION_MAJOR 0
#define KCORE_VERSION_MINOR 1
#define KCORE_VERSION_PATCH 0
#define KCORE_VERSION_STRING "0.1.0"
#define KCORE_ABI_VERSION 1

#if defined(_WIN32)
#if defined(KCORE_BUILDING)
#define KCORE_API __declspec(dllexport)
#elif defined(KCORE_STATIC)
#define KCORE_API
#else
#define KCORE_API __declspec(dllimport)
#endif
#else
#define KCORE_API __attribute__((visibility("default")))
#endif

#define KCORE_MLKEM1024_PK 1568
#define KCORE_MLKEM1024_SK 3168
#define KCORE_MLKEM1024_CT 1568
#define KCORE_MLKEM768_PK 1184
#define KCORE_MLKEM768_SK 2400
#define KCORE_MLKEM768_CT 1088
#define KCORE_MLKEM_SS 32
#define KCORE_MLKEM_SEED 64
#define KCORE_MLKEM_COINS 32

/* Returns KCORE_ABI_VERSION; bindings refuse a library whose value differs from the one they were
 * written for. */
KCORE_API int kcore_abi_version(void);

/* SHAKE256(in) -> outlen bytes; outlen 1..1048576. */
KCORE_API int kcore_shake256(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen);

/* Advances n WOTS+ chains in place. chunks holds n*128 hex characters (no NULs); chunk i becomes
 * hex(SHAKE256(chunk, 64 bytes)) applied counts[i] times. counts 0..64; n 1..64; ways 1 or 4. */
KCORE_API int kcore_chains_hex(char *chunks, const int *counts, size_t n, int ways);

/* WOTS+ address of a 2048-hex key; writes 64 hex characters, no NUL. */
KCORE_API int kcore_wots_address(const char *key_hex2048, char *address_hex64);

KCORE_API int kcore_mlkem1024_keypair(const uint8_t seed[64], uint8_t pk[1568], uint8_t sk[3168]);
KCORE_API int kcore_mlkem1024_encaps(const uint8_t pk[1568], const uint8_t coins[32], uint8_t ct[1568],
                                     uint8_t ss[32]);
KCORE_API int kcore_mlkem1024_decaps(const uint8_t ct[1568], const uint8_t sk[3168], uint8_t ss[32]);

/* ML-KEM-768 (FIPS 203), the opt-in step-back parameter set. Same seed/coins contract as 1024. */
KCORE_API int kcore_mlkem768_keypair(const uint8_t seed[64], uint8_t pk[1184], uint8_t sk[2400]);
KCORE_API int kcore_mlkem768_encaps(const uint8_t pk[1184], const uint8_t coins[32], uint8_t ct[1088],
                                    uint8_t ss[32]);
KCORE_API int kcore_mlkem768_decaps(const uint8_t ct[1088], const uint8_t sk[2400], uint8_t ss[32]);

#ifdef __cplusplus
}
#endif

#endif /* KCORE_H */
