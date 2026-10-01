/*
 * kcore: SHAKE256, WOTS+ chains, WOTS+ address and ML-KEM-1024 on mlkem-native v1.2.0.
 *
 * Every kcmlk* symbol below is compiled by mlkem_multilevel.c under the `kcmlk` namespace; with
 * MLK_CONFIG_USE_NATIVE_BACKEND_FIPS202 the permutations use mlkem-native's native backend (on a
 * Cortex-A72 build: x1_scalar and x4_v8a_scalar), otherwise portable C.
 */
#include "kcore.h"

#include <string.h>

/* keccakf1600.h:20-96 under MLK_NAMESPACE(x) == kcmlk_##x. The x4 state layout is
 * implementation-defined: it is only ever touched through the x4 xor/extract helpers. */
void kcmlk_keccakf1600_extract_bytes(uint64_t *state, unsigned char *data, unsigned offset, unsigned length);
void kcmlk_keccakf1600_xor_bytes(uint64_t *state, const unsigned char *data, unsigned offset, unsigned length);
void kcmlk_keccakf1600x4_extract_bytes(uint64_t *state, unsigned char *data0, unsigned char *data1,
                                       unsigned char *data2, unsigned char *data3, unsigned offset,
                                       unsigned length);
void kcmlk_keccakf1600x4_xor_bytes(uint64_t *state, const unsigned char *data0, const unsigned char *data1,
                                   const unsigned char *data2, const unsigned char *data3, unsigned offset,
                                   unsigned length);
void kcmlk_keccakf1600x4_permute(uint64_t *state);
void kcmlk_keccakf1600_permute(uint64_t *state);

/* fips202.h:86-87 */
void kcmlk_shake256(uint8_t *output, size_t outlen, const uint8_t *input, size_t inlen);

/* mlkem_native.h:216-219, 282-286, 350-353 with the kcmlk1024 / kcmlk768 API namespaces. */
int kcmlk1024_keypair_derand(uint8_t pk[1568], uint8_t sk[3168], const uint8_t coins[64]);
int kcmlk1024_enc_derand(uint8_t ct[1568], uint8_t ss[32], const uint8_t pk[1568], const uint8_t coins[32]);
int kcmlk1024_dec(uint8_t ss[32], const uint8_t ct[1568], const uint8_t sk[3168]);
int kcmlk768_keypair_derand(uint8_t pk[1184], uint8_t sk[2400], const uint8_t coins[64]);
int kcmlk768_enc_derand(uint8_t ct[1088], uint8_t ss[32], const uint8_t pk[1184], const uint8_t coins[32]);
int kcmlk768_dec(uint8_t ss[32], const uint8_t ct[1088], const uint8_t sk[2400]);

#define KCORE_RATE 136u
#define KCORE_CHUNK 128u
#define KCORE_STEP_OUT 64u
#define KCORE_MAX_OUT 1048576u
#define KCORE_MAX_CHAINS 64u
#define KCORE_MAX_COUNT 64

int kcore_abi_version(void) { return KCORE_ABI_VERSION; }

static const char kcore_digits[] = "0123456789abcdef";

static void hex_encode(const uint8_t *data, size_t len, char *out) {
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = kcore_digits[data[i] >> 4];
        out[2 * i + 1] = kcore_digits[data[i] & 0x0f];
    }
}

/* SHAKE256 padding of a 128-byte message at rate 136: 0x1F, zeros, 0x80 in the last byte. */
static void load_block(unsigned char block[KCORE_RATE], const char *chunk) {
    memcpy(block, chunk, KCORE_CHUNK);
    memset(block + KCORE_CHUNK, 0, KCORE_RATE - KCORE_CHUNK);
    block[KCORE_CHUNK] = 0x1F;
    block[KCORE_RATE - 1] = 0x80;
}

static void step_x1(char *chunk) {
    uint64_t state[25];
    unsigned char block[KCORE_RATE];
    unsigned char out[KCORE_STEP_OUT];
    memset(state, 0, sizeof state);
    load_block(block, chunk);
    kcmlk_keccakf1600_xor_bytes(state, block, 0, KCORE_RATE);
    kcmlk_keccakf1600_permute(state);
    kcmlk_keccakf1600_extract_bytes(state, out, 0, KCORE_STEP_OUT);
    hex_encode(out, KCORE_STEP_OUT, chunk);
}

int kcore_shake256(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen) {
    if (out == NULL || (in == NULL && inlen != 0) || outlen < 1 || outlen > KCORE_MAX_OUT) {
        return -1;
    }
    static const uint8_t empty = 0;
    kcmlk_shake256(out, outlen, in != NULL ? in : &empty, inlen);
    return 0;
}

static void chains_x1(char *chunks, const int *counts, size_t n) {
    for (size_t i = 0; i < n; i++) {
        for (int s = 0; s < counts[i]; s++) {
            step_x1(chunks + i * KCORE_CHUNK);
        }
    }
}

/* Four lanes, each holding a chain index or -1, refilled in index order with the next chain whose
 * count is > 0. >= 2 active lanes: one x4 step (idle lanes xor and extract their own scratch
 * buffers); exactly 1 active lane: one x1 step. */
static void chains_x4(char *chunks, const int *counts, size_t n) {
    long lane[4];
    int remaining[4];
    size_t next = 0;
    unsigned char block[4][KCORE_RATE];
    unsigned char out[4][KCORE_STEP_OUT];
    uint64_t state[100];

    memset(block, 0, sizeof block);
    for (int l = 0; l < 4; l++) {
        lane[l] = -1;
        remaining[l] = 0;
        while (next < n && counts[next] == 0) next++;
        if (next < n) {
            lane[l] = (long)next;
            remaining[l] = counts[next];
            next++;
        }
    }

    for (;;) {
        int active = 0;
        int last = -1;
        for (int l = 0; l < 4; l++) {
            if (lane[l] >= 0) {
                active++;
                last = l;
            }
        }
        if (active == 0) break;

        if (active == 1) {
            step_x1(chunks + (size_t)lane[last] * KCORE_CHUNK);
        } else {
            for (int l = 0; l < 4; l++) {
                if (lane[l] >= 0) load_block(block[l], chunks + (size_t)lane[l] * KCORE_CHUNK);
            }
            memset(state, 0, sizeof state);
            kcmlk_keccakf1600x4_xor_bytes(state, block[0], block[1], block[2], block[3], 0, KCORE_RATE);
            kcmlk_keccakf1600x4_permute(state);
            kcmlk_keccakf1600x4_extract_bytes(state, out[0], out[1], out[2], out[3], 0, KCORE_STEP_OUT);
            for (int l = 0; l < 4; l++) {
                if (lane[l] >= 0) hex_encode(out[l], KCORE_STEP_OUT, chunks + (size_t)lane[l] * KCORE_CHUNK);
            }
        }

        for (int l = 0; l < 4; l++) {
            if (lane[l] < 0) continue;
            if (--remaining[l] > 0) continue;
            lane[l] = -1;
            while (next < n && counts[next] == 0) next++;
            if (next < n) {
                lane[l] = (long)next;
                remaining[l] = counts[next];
                next++;
            }
        }
    }
}

int kcore_chains_hex(char *chunks, const int *counts, size_t n, int ways) {
    if (chunks == NULL || counts == NULL || n < 1 || n > KCORE_MAX_CHAINS || (ways != 1 && ways != 4)) {
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        if (counts[i] < 0 || counts[i] > KCORE_MAX_COUNT) return -1;
    }
    if (memchr(chunks, '\0', n * KCORE_CHUNK) != NULL) return -1;

    if (ways == 1) {
        chains_x1(chunks, counts, n);
    } else {
        chains_x4(chunks, counts, n);
    }
    return 0;
}

int kcore_wots_address(const char *key_hex2048, char *address_hex64) {
    char work[16 * KCORE_CHUNK];
    int counts[16];
    uint8_t digest[1024];
    char digest_hex[2048];
    uint8_t address[32];

    if (key_hex2048 == NULL || address_hex64 == NULL) return -1;
    if (memchr(key_hex2048, '\0', sizeof work) != NULL) return -1;
    memcpy(work, key_hex2048, sizeof work);
    for (int i = 0; i < 16; i++) counts[i] = 16;
    if (kcore_chains_hex(work, counts, 16, 4) != 0) return -1;
    kcmlk_shake256(digest, sizeof digest, (const uint8_t *)work, sizeof work);
    hex_encode(digest, sizeof digest, digest_hex);
    kcmlk_shake256(address, sizeof address, (const uint8_t *)digest_hex, sizeof digest_hex);
    hex_encode(address, sizeof address, address_hex64);
    return 0;
}

int kcore_mlkem1024_keypair(const uint8_t seed[64], uint8_t pk[1568], uint8_t sk[3168]) {
    uint8_t tpk[KCORE_MLKEM1024_PK];
    uint8_t tsk[KCORE_MLKEM1024_SK];
    if (seed == NULL || pk == NULL || sk == NULL) return -1;
    if (kcmlk1024_keypair_derand(tpk, tsk, seed) != 0) return -1;
    memcpy(pk, tpk, sizeof tpk);
    memcpy(sk, tsk, sizeof tsk);
    return 0;
}

int kcore_mlkem1024_encaps(const uint8_t pk[1568], const uint8_t coins[32], uint8_t ct[1568], uint8_t ss[32]) {
    uint8_t tct[KCORE_MLKEM1024_CT];
    uint8_t tss[KCORE_MLKEM_SS];
    if (pk == NULL || coins == NULL || ct == NULL || ss == NULL) return -1;
    if (kcmlk1024_enc_derand(tct, tss, pk, coins) != 0) return -1;
    memcpy(ct, tct, sizeof tct);
    memcpy(ss, tss, sizeof tss);
    return 0;
}

int kcore_mlkem1024_decaps(const uint8_t ct[1568], const uint8_t sk[3168], uint8_t ss[32]) {
    uint8_t tss[KCORE_MLKEM_SS];
    if (ct == NULL || sk == NULL || ss == NULL) return -1;
    if (kcmlk1024_dec(tss, ct, sk) != 0) return -1;
    memcpy(ss, tss, sizeof tss);
    return 0;
}

int kcore_mlkem768_keypair(const uint8_t seed[64], uint8_t pk[1184], uint8_t sk[2400]) {
    uint8_t tpk[KCORE_MLKEM768_PK];
    uint8_t tsk[KCORE_MLKEM768_SK];
    if (seed == NULL || pk == NULL || sk == NULL) return -1;
    if (kcmlk768_keypair_derand(tpk, tsk, seed) != 0) return -1;
    memcpy(pk, tpk, sizeof tpk);
    memcpy(sk, tsk, sizeof tsk);
    return 0;
}

int kcore_mlkem768_encaps(const uint8_t pk[1184], const uint8_t coins[32], uint8_t ct[1088], uint8_t ss[32]) {
    uint8_t tct[KCORE_MLKEM768_CT];
    uint8_t tss[KCORE_MLKEM_SS];
    if (pk == NULL || coins == NULL || ct == NULL || ss == NULL) return -1;
    if (kcmlk768_enc_derand(tct, tss, pk, coins) != 0) return -1;
    memcpy(ct, tct, sizeof tct);
    memcpy(ss, tss, sizeof tss);
    return 0;
}

int kcore_mlkem768_decaps(const uint8_t ct[1088], const uint8_t sk[2400], uint8_t ss[32]) {
    uint8_t tss[KCORE_MLKEM_SS];
    if (ct == NULL || sk == NULL || ss == NULL) return -1;
    if (kcmlk768_dec(tss, ct, sk) != 0) return -1;
    memcpy(ss, tss, sizeof tss);
    return 0;
}
