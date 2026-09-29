/*
 * Runtime AVX2 detection for kcore's x86_64 builds.
 *
 * AVX2 is usable only if the CPU reports it (CPUID.(7,0):EBX bit 5) AND the OS saves the YMM
 * state (CPUID.1:ECX bit 27 OSXSAVE and XGETBV(0) bits 1-2). The upstream mlkem-native CPUID
 * example (test/configs/custom_native_capability_config_CPUID_AVX2.h) checks only the first.
 */
#ifndef KCORE_CPU_X86_H
#define KCORE_CPU_X86_H

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#include <immintrin.h>
#endif

static int kcore_x86_avx2_usable(void) {
#if defined(__x86_64__) || defined(_M_X64)
    unsigned int eax, ebx, ecx, edx;
    unsigned long long xcr0;
#if defined(_MSC_VER) && !defined(__clang__)
    int regs[4];
    __cpuidex(regs, 0, 0);
    eax = (unsigned int)regs[0];
    if (eax < 7) return 0;
    __cpuidex(regs, 1, 0);
    ecx = (unsigned int)regs[2];
    if (!(ecx & (1u << 27))) return 0;
    xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6) != 0x6) return 0;
    __cpuidex(regs, 7, 0);
    ebx = (unsigned int)regs[1];
    (void)edx;
#else
    unsigned int lo, hi;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0), "c"(0));
    if (eax < 7) return 0;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    if (!(ecx & (1u << 27))) return 0;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    xcr0 = ((unsigned long long)hi << 32) | lo;
    if ((xcr0 & 0x6) != 0x6) return 0;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
#endif
    return (ebx & (1u << 5)) ? 1 : 0;
#else
    return 0;
#endif
}

#endif /* KCORE_CPU_X86_H */
