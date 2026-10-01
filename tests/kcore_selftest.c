/*
 * kcore_selftest <differential.json>
 *
 * Runs every case of <build>/vectors/differential.json (written by tests/gen_vectors.py, one case per
 * line) through kcore_* and compares with the case's `expect`. Chains run at ways 1 and 4. ML-KEM
 * cases run ML-KEM-1024, or ML-KEM-768 when they carry "set":768. Every ML-KEM case checks
 * determinism, that sk embeds pk (FIPS 203 dk = dk_pke || ek || H(ek) || z) and that
 * decaps(encaps) returns the shared secret; the canonical cases (the fixture's mlkem1024.keygen and
 * mlkem768.keygen) also pin the public key bytes every SDK derives from the same seed. Finally the
 * invalid-argument contract: -1 and untouched outputs. Prints "SELFTEST ok <n>" or
 * "SELFTEST FAIL <case id>".
 */
#include "kcore.h"
#include "vecread.h"
#if defined(__x86_64__) || defined(_M_X64)
#include "kcore_cpu_x86.h"
#endif

static int fail(const char *id, size_t idlen) {
    printf("SELFTEST FAIL %.*s\n", (int)idlen, id);
    return 1;
}

/* One ML-KEM parameter set; pk_offset is where ek starts inside dk (384 * k). */
struct mlkem_set {
    int (*keypair)(const uint8_t *seed, uint8_t *pk, uint8_t *sk);
    int (*encaps)(const uint8_t *pk, const uint8_t *coins, uint8_t *ct, uint8_t *ss);
    int (*decaps)(const uint8_t *ct, const uint8_t *sk, uint8_t *ss);
    size_t pk, sk, ct, pk_offset;
};
static const struct mlkem_set MLKEM1024 = {kcore_mlkem1024_keypair, kcore_mlkem1024_encaps, kcore_mlkem1024_decaps,
                                           KCORE_MLKEM1024_PK, KCORE_MLKEM1024_SK, KCORE_MLKEM1024_CT, 1536};
static const struct mlkem_set MLKEM768 = {kcore_mlkem768_keypair, kcore_mlkem768_encaps, kcore_mlkem768_decaps,
                                          KCORE_MLKEM768_PK, KCORE_MLKEM768_SK, KCORE_MLKEM768_CT, 1152};

/* Returns 0 when the set is deterministic, sk embeds pk, the optional expected pk hex matches, and
 * decaps(encaps) recovers the shared secret. hex needs room for 2 * m->pk characters. */
static int check_mlkem(const struct mlkem_set *m, const uint8_t seed[64], const uint8_t coins[32],
                       const char *expect, size_t elen, char *hex) {
    uint8_t pk[KCORE_MLKEM1024_PK], sk[KCORE_MLKEM1024_SK], pk2[KCORE_MLKEM1024_PK], sk2[KCORE_MLKEM1024_SK];
    uint8_t ct[KCORE_MLKEM1024_CT], ct2[KCORE_MLKEM1024_CT];
    uint8_t ss[KCORE_MLKEM_SS], ss2[KCORE_MLKEM_SS], dss[KCORE_MLKEM_SS];
    if (m->keypair(seed, pk, sk) != 0 || m->keypair(seed, pk2, sk2) != 0) return 1;
    if (memcmp(pk, pk2, m->pk) != 0 || memcmp(sk, sk2, m->sk) != 0) return 1;
    if (memcmp(sk + m->pk_offset, pk, m->pk) != 0) return 1;
    /* Canonical cases carry the byte-frozen public key (cross-SDK KAT). */
    if (expect != NULL) {
        if (elen != 2 * m->pk) return 1;
        vr_tohex(pk, m->pk, hex);
        if (memcmp(hex, expect, elen) != 0) return 1;
    }
    if (m->encaps(pk, coins, ct, ss) != 0 || m->encaps(pk, coins, ct2, ss2) != 0) return 1;
    if (memcmp(ct, ct2, m->ct) != 0 || memcmp(ss, ss2, sizeof ss) != 0) return 1;
    if (m->decaps(ct, sk, dss) != 0 || memcmp(dss, ss, sizeof ss) != 0) return 1;
    return 0;
}

static int check_contract(void) {
    char chunk[129];
    char copy[129];
    int counts[1] = {65};
    uint8_t out[4] = {1, 2, 3, 4};
    memset(chunk, 'a', 128);
    chunk[128] = 0;
    memcpy(copy, chunk, sizeof chunk);
    if (kcore_chains_hex(chunk, counts, 1, 4) != -1 || memcmp(chunk, copy, 129) != 0) return 1;
    counts[0] = -1;
    if (kcore_chains_hex(chunk, counts, 1, 1) != -1 || memcmp(chunk, copy, 129) != 0) return 1;
    counts[0] = 1;
    if (kcore_chains_hex(chunk, counts, 0, 4) != -1) return 1;
    if (kcore_chains_hex(chunk, counts, 1, 2) != -1) return 1;
    chunk[127] = 0; /* a 127-character chunk */
    memcpy(copy, chunk, sizeof chunk);
    if (kcore_chains_hex(chunk, counts, 1, 4) != -1 || memcmp(chunk, copy, 129) != 0) return 1;
    if (kcore_shake256((const uint8_t *)"x", 1, out, 0) != -1) return 1;
    if (kcore_shake256((const uint8_t *)"x", 1, out, 1048577) != -1) return 1;
    if (kcore_shake256(NULL, 1, out, 4) != -1) return 1;
    if (out[0] != 1 || out[3] != 4) return 1;
    if (kcore_shake256(NULL, 0, out, 4) != 0) return 1; /* empty input is valid */
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: kcore_selftest <differential.json>\n");
        return 2;
    }
    char *buf = vr_slurp(argv[1]);
    if (buf == NULL) {
        perror(argv[1]);
        return 2;
    }
#if defined(__x86_64__) || defined(_M_X64)
    fprintf(stderr, "cpu x86_64 avx2=%d\n", kcore_x86_avx2_usable());
#endif

    /* Every buffer lives on the heap: any .bss makes lld emit a file-less PT_LOAD segment, which
     * Docker Desktop's x86_64 emulation fails to map (segfault before main). */
    enum { IN_CAP = 4096, OUT_CAP = 1048576, WORK_CAP = 64 * 128 };
    uint8_t *in = malloc(IN_CAP);
    uint8_t *out = malloc(OUT_CAP);
    char *hex = malloc(2 * OUT_CAP);
    char *work = malloc(WORK_CAP);
    if (in == NULL || out == NULL || hex == NULL || work == NULL) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    int counts[64];
    int cases = 0;
    if (kcore_abi_version() != KCORE_ABI_VERSION) {
        printf("SELFTEST FAIL abi\n");
        return 1;
    }

    for (char *line = strtok(buf, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        size_t idlen, len, elen = 0;
        const char *id = vr_field(line, "id", &idlen);
        if (id == NULL) continue;
        const char *expect = vr_field(line, "expect", &elen);
        const char *v;

        if ((v = vr_field(line, "counts", &len)) != NULL) {
            int n = vr_counts(v, len, counts, 64);
            const char *chunks = vr_field(line, "chunks", &len);
            if (n <= 0 || chunks == NULL || len != (size_t)n * 128 || expect == NULL || elen != len) return fail(id, idlen);
            for (int ways = 1; ways <= 4; ways += 3) {
                memcpy(work, chunks, len);
                if (kcore_chains_hex(work, counts, (size_t)n, ways) != 0 || memcmp(work, expect, len) != 0) {
                    return fail(id, idlen);
                }
            }
        } else if ((v = vr_field(line, "key", &len)) != NULL) {
            char addr[64];
            if (len != 2048 || expect == NULL || elen != 64) return fail(id, idlen);
            memcpy(work, v, 2048);
            if (kcore_wots_address(work, addr) != 0 || memcmp(addr, expect, 64) != 0) return fail(id, idlen);
        } else if ((v = vr_field(line, "seed", &len)) != NULL) {
            uint8_t seed[64], coins[32];
            size_t clen = 0, slen = 0;
            const struct mlkem_set *m = &MLKEM1024;
            if (len != 128 || vr_unhex(v, len, seed) != 0) return fail(id, idlen);
            const char *c = vr_field(line, "coins", &clen);
            if (c == NULL || clen != 64 || vr_unhex(c, clen, coins) != 0) return fail(id, idlen);
            const char *set = vr_field(line, "set", &slen);
            if (set != NULL) {
                if (slen != 3 || memcmp(set, "768", 3) != 0) return fail(id, idlen);
                m = &MLKEM768;
            }
            if (check_mlkem(m, seed, coins, expect, elen, hex) != 0) return fail(id, idlen);
        } else if ((v = vr_field(line, "outlen", &len)) != NULL) {
            long outlen = strtol(v, NULL, 10);
            const char *data = vr_field(line, "in", &len);
            if (data == NULL || len / 2 > IN_CAP || vr_unhex(data, len, in) != 0 || expect == NULL) return fail(id, idlen);
            if (outlen < 1 || outlen > OUT_CAP || (size_t)outlen * 2 != elen) return fail(id, idlen);
            if (kcore_shake256(in, len / 2, out, (size_t)outlen) != 0) return fail(id, idlen);
            vr_tohex(out, (size_t)outlen, hex);
            if (memcmp(hex, expect, elen) != 0) return fail(id, idlen);
        } else {
            return fail(id, idlen);
        }
        cases++;
    }
    free(buf);
    free(in);
    free(out);
    free(hex);
    free(work);
    if (check_contract() != 0) {
        printf("SELFTEST FAIL contract\n");
        return 1;
    }
    printf("SELFTEST ok %d\n", cases);
    return 0;
}
