/*
 * kcore_loadtest <shared-library-path>
 *
 * Loads libkcore dynamically the way every FFI binding does (dlopen / LoadLibraryA), resolves the
 * ten exported kcore_* symbols, requires kcore_abi_version() == 1 and runs known answers:
 * SHAKE256("") -> 32 bytes (canonical vector empty_string_32_bytes) and ML-KEM-1024 and ML-KEM-768
 * keypair/encaps/decaps round trips. Prints "LOADTEST ok" or "LOADTEST FAIL <step>".
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE lib_t;
static lib_t lib_open(const char *p) { return LoadLibraryA(p); }
static void *lib_sym(lib_t l, const char *n) { return (void *)GetProcAddress(l, n); }
#else
#include <dlfcn.h>
typedef void *lib_t;
static lib_t lib_open(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void *lib_sym(lib_t l, const char *n) { return dlsym(l, n); }
#endif

typedef int (*abi_fn)(void);
typedef int (*shake_fn)(const uint8_t *, size_t, uint8_t *, size_t);
typedef int (*keypair_fn)(const uint8_t *, uint8_t *, uint8_t *);
typedef int (*encaps_fn)(const uint8_t *, const uint8_t *, uint8_t *, uint8_t *);
typedef int (*decaps_fn)(const uint8_t *, const uint8_t *, uint8_t *);

static int fail(const char *step) {
    printf("LOADTEST FAIL %s\n", step);
    return 1;
}

int main(int argc, char **argv) {
    static const char *names[] = {"kcore_shake256",          "kcore_chains_hex",       "kcore_wots_address",
                                  "kcore_mlkem1024_keypair", "kcore_mlkem1024_encaps", "kcore_mlkem1024_decaps",
                                  "kcore_abi_version",       "kcore_mlkem768_keypair", "kcore_mlkem768_encaps",
                                  "kcore_mlkem768_decaps"};
    enum { NSYM = sizeof names / sizeof names[0] };
    static const char expect[] = "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f";
    void *sym[NSYM];
    uint8_t out[32], seed[64] = {0}, coins[32] = {0}, pk[1568], sk[3168], ct[1568], ss[32], dss[32];
    char hex[65];

    if (argc != 2) {
        fprintf(stderr, "usage: kcore_loadtest <shared-library-path>\n");
        return 2;
    }
    lib_t lib = lib_open(argv[1]);
    if (lib == NULL) return fail("open");
    for (int i = 0; i < NSYM; i++) {
        sym[i] = lib_sym(lib, names[i]);
        if (sym[i] == NULL) return fail(names[i]);
    }
    if (((abi_fn)sym[6])() != 1) return fail("abi");
    if (((shake_fn)sym[0])(NULL, 0, out, sizeof out) != 0) return fail("shake256");
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", out[i]);
    if (memcmp(hex, expect, 64) != 0) return fail("shake256-kat");
    if (((keypair_fn)sym[3])(seed, pk, sk) != 0) return fail("keypair");
    if (((encaps_fn)sym[4])(pk, coins, ct, ss) != 0) return fail("encaps");
    if (((decaps_fn)sym[5])(ct, sk, dss) != 0) return fail("decaps");
    if (memcmp(ss, dss, sizeof ss) != 0) return fail("roundtrip");
    /* ML-KEM-768 sizes (1184 / 2400 / 1088) fit the 1024 buffers. */
    if (((keypair_fn)sym[7])(seed, pk, sk) != 0) return fail("keypair768");
    if (((encaps_fn)sym[8])(pk, coins, ct, ss) != 0) return fail("encaps768");
    if (((decaps_fn)sym[9])(ct, sk, dss) != 0) return fail("decaps768");
    if (memcmp(ss, dss, sizeof ss) != 0) return fail("roundtrip768");
    printf("LOADTEST ok\n");
    return 0;
}
