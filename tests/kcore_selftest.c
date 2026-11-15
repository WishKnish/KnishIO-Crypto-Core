/*
 * kcore_selftest <differential.json>
 *
 * Runs every case of <build>/vectors/differential.json (written by tests/gen_vectors.py, one case per
 * line) through kcore_* and compares with the case's `expect`. Chains run at ways 1 and 4. ML-KEM
 * cases run ML-KEM-1024, or ML-KEM-768 when they carry "set":768. Every ML-KEM keygen case checks
 * determinism, that sk embeds pk (FIPS 203 dk = dk_pke || ek || H(ek) || z) and that
 * decaps(encaps) returns the shared secret; the canonical cases (the fixture's mlkem1024.keygen and
 * mlkem768.keygen) also pin the public key bytes every SDK derives from the same seed, and the ACVP
 * keygen cases pin both keys. The ACVP sections (NIST sample vectors) also check encapsulation and
 * decapsulation outputs, implicit rejection of modified ciphertexts, and that kcore refuses keys
 * that fail the FIPS 203 input checks (-1, outputs untouched). Finally the invalid-argument
 * contract: -1 and untouched outputs. Prints "SELFTEST ok <n>" or "SELFTEST FAIL <case id>".
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

/* Returns 0 when the set is deterministic, sk embeds pk, the optional expected pk and sk hex match,
 * and decaps(encaps) recovers the shared secret. hex needs room for 2 * m->sk characters. */
static int check_mlkem(const struct mlkem_set *m, const uint8_t seed[64], const uint8_t coins[32],
                       const char *expect, size_t elen, const char *expect_sk, size_t sklen, char *hex) {
    uint8_t pk[KCORE_MLKEM1024_PK], sk[KCORE_MLKEM1024_SK], pk2[KCORE_MLKEM1024_PK], sk2[KCORE_MLKEM1024_SK];
    uint8_t ct[KCORE_MLKEM1024_CT], ct2[KCORE_MLKEM1024_CT];
    uint8_t ss[KCORE_MLKEM_SS], ss2[KCORE_MLKEM_SS], dss[KCORE_MLKEM_SS];
    if (m->keypair(seed, pk, sk) != 0 || m->keypair(seed, pk2, sk2) != 0) return 1;
    if (memcmp(pk, pk2, m->pk) != 0 || memcmp(sk, sk2, m->sk) != 0) return 1;
    if (memcmp(sk + m->pk_offset, pk, m->pk) != 0) return 1;
    /* Canonical cases carry the byte-frozen public key (cross-SDK KAT); ACVP cases add sk. */
    if (expect != NULL) {
        if (elen != 2 * m->pk) return 1;
        vr_tohex(pk, m->pk, hex);
        if (memcmp(hex, expect, elen) != 0) return 1;
    }
    if (expect_sk != NULL) {
        if (sklen != 2 * m->sk) return 1;
        vr_tohex(sk, m->sk, hex);
        if (memcmp(hex, expect_sk, sklen) != 0) return 1;
    }
    if (m->encaps(pk, coins, ct, ss) != 0 || m->encaps(pk, coins, ct2, ss2) != 0) return 1;
    if (memcmp(ct, ct2, m->ct) != 0 || memcmp(ss, ss2, sizeof ss) != 0) return 1;
    if (m->decaps(ct, sk, dss) != 0 || memcmp(dss, ss, sizeof ss) != 0) return 1;
    return 0;
}

/* The case's parameter set: ML-KEM-1024 without "set", ML-KEM-768 with "set":768, NULL otherwise. */
static const struct mlkem_set *pick_set(const char *line) {
    size_t slen = 0;
    const char *set = vr_field(line, "set", &slen);
    if (set == NULL) return &MLKEM1024;
    if (slen == 3 && memcmp(set, "768", 3) == 0) return &MLKEM768;
    return NULL;
}

/* Reads hex field `name` of exactly n bytes into out; 0 on success. */
static int hex_field(const char *line, const char *name, uint8_t *out, size_t n) {
    size_t len = 0;
    const char *v = vr_field(line, name, &len);
    if (v == NULL || len != 2 * n) return 1;
    return vr_unhex(v, len, out) != 0;
}

/* The "valid" field: 1 or 0, or -1 when absent or malformed. */
static int valid_field(const char *line) {
    size_t len = 0;
    const char *v = vr_field(line, "valid", &len);
    if (v == NULL || len != 1 || (v[0] != '0' && v[0] != '1')) return -1;
    return v[0] - '0';
}

/* An entry point's result against a key-check case: 0 for a valid key; for an invalid key -1 with
 * the 0xA5-filled outputs untouched (first and last byte). */
static int check_refusal(int rc, int valid, const uint8_t *out1, size_t n1, const uint8_t *out2, size_t n2) {
    if (valid == 1) return rc != 0;
    if (rc != -1) return 1;
    if (out1[0] != 0xA5 || out1[n1 - 1] != 0xA5) return 1;
    if (out2 != NULL && (out2[0] != 0xA5 || out2[n2 - 1] != 0xA5)) return 1;
    return 0;
}

/* ACVP encapsulation (ek, coins, ct, ss) and encapsulation-key-check (ek, valid) cases. */
static int check_acvp_ek(const struct mlkem_set *m, const char *line, const char *ek, size_t eklen) {
    uint8_t pk[KCORE_MLKEM1024_PK], coins[KCORE_MLKEM_COINS];
    uint8_t ct[KCORE_MLKEM1024_CT], ss[KCORE_MLKEM_SS], want_ct[KCORE_MLKEM1024_CT], want_ss[KCORE_MLKEM_SS];
    size_t ctlen = 0;
    if (eklen != 2 * m->pk || vr_unhex(ek, eklen, pk) != 0) return 1;
    if (vr_field(line, "ct", &ctlen) != NULL) {
        if (hex_field(line, "coins", coins, sizeof coins) != 0) return 1;
        if (hex_field(line, "ct", want_ct, m->ct) != 0 || hex_field(line, "ss", want_ss, sizeof want_ss) != 0) return 1;
        if (m->encaps(pk, coins, ct, ss) != 0) return 1;
        return memcmp(ct, want_ct, m->ct) != 0 || memcmp(ss, want_ss, sizeof ss) != 0;
    }
    int valid = valid_field(line);
    if (valid < 0) return 1;
    memset(coins, 0, sizeof coins);
    memset(ct, 0xA5, sizeof ct);
    memset(ss, 0xA5, sizeof ss);
    return check_refusal(m->encaps(pk, coins, ct, ss), valid, ct, m->ct, ss, sizeof ss);
}

/* ACVP decapsulation (dk, ct, ss; includes modified ciphertexts) and decapsulation-key-check
 * (dk, valid) cases. */
static int check_acvp_dk(const struct mlkem_set *m, const char *line, const char *dk, size_t dklen) {
    uint8_t sk[KCORE_MLKEM1024_SK], ct[KCORE_MLKEM1024_CT], ss[KCORE_MLKEM_SS], want_ss[KCORE_MLKEM_SS];
    size_t sslen = 0;
    if (dklen != 2 * m->sk || vr_unhex(dk, dklen, sk) != 0) return 1;
    if (vr_field(line, "ss", &sslen) != NULL) {
        if (hex_field(line, "ct", ct, m->ct) != 0 || hex_field(line, "ss", want_ss, sizeof want_ss) != 0) return 1;
        if (m->decaps(ct, sk, ss) != 0) return 1;
        return memcmp(ss, want_ss, sizeof ss) != 0;
    }
    int valid = valid_field(line);
    if (valid < 0) return 1;
    memset(ct, 0, sizeof ct);
    memset(ss, 0xA5, sizeof ss);
    return check_refusal(m->decaps(ct, sk, ss), valid, ss, sizeof ss, NULL, 0);
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
    /* A 2048-hex key is not a 64-chain key: kcore_wots_address_v2 refuses it and leaves the output. */
    char key[8 * 1024 + 1];
    char addr[64];
    memset(key, 'a', sizeof key);
    key[2048] = 0;
    memset(addr, 'z', sizeof addr);
    if (kcore_wots_address_v2(key, addr) != -1 || addr[0] != 'z' || addr[63] != 'z') return 1;
    if (kcore_wots_address_v2(NULL, addr) != -1 || kcore_wots_address_v2(key, NULL) != -1) return 1;
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
    enum { IN_CAP = 8192, OUT_CAP = 1048576, WORK_CAP = 64 * 128 };
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
            if ((len != 2048 && len != 8192) || expect == NULL || elen != 64) return fail(id, idlen);
            memcpy(work, v, len);
            int rc = len == 2048 ? kcore_wots_address(work, addr) : kcore_wots_address_v2(work, addr);
            if (rc != 0 || memcmp(addr, expect, 64) != 0) return fail(id, idlen);
        } else if ((v = vr_field(line, "ek", &len)) != NULL) {
            const struct mlkem_set *m = pick_set(line);
            if (m == NULL || check_acvp_ek(m, line, v, len) != 0) return fail(id, idlen);
        } else if ((v = vr_field(line, "dk", &len)) != NULL) {
            const struct mlkem_set *m = pick_set(line);
            if (m == NULL || check_acvp_dk(m, line, v, len) != 0) return fail(id, idlen);
        } else if ((v = vr_field(line, "seed", &len)) != NULL) {
            uint8_t seed[64], coins[32];
            size_t sklen = 0;
            const struct mlkem_set *m = pick_set(line);
            if (m == NULL || len != 128 || vr_unhex(v, len, seed) != 0) return fail(id, idlen);
            if (hex_field(line, "coins", coins, sizeof coins) != 0) return fail(id, idlen);
            const char *expect_sk = vr_field(line, "expect_sk", &sklen);
            if (check_mlkem(m, seed, coins, expect, elen, expect_sk, sklen, hex) != 0) return fail(id, idlen);
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
