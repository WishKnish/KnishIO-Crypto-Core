/*
 * Consumer smoke program for an installed KnishIO Crypto Core package.
 *
 * packaging/check-release-package.sh builds it against a relocated release package three
 * ways (plain -I/-L, pkg-config, find_package) and requires its output to equal the digest
 * computed independently in Python. Nothing in the library build references this file.
 *
 * It checks the ABI version and prints SHAKE256("knishio-consumer-smoke") as 32 bytes of hex.
 */
#include <stdio.h>
#include <kcore.h>

int main(void) {
    static const char msg[] = "knishio-consumer-smoke";
    uint8_t out[32];

    if (kcore_abi_version() != KCORE_ABI_VERSION) {
        return 1;
    }
    if (kcore_shake256((const uint8_t *)msg, sizeof msg - 1, out, sizeof out) != 0) {
        return 1;
    }
    for (size_t i = 0; i < sizeof out; i++) {
        printf("%02x", out[i]);
    }
    printf("\n");
    return 0;
}
