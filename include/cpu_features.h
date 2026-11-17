/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef CPU_FEATURES_H
#define CPU_FEATURES_H
#include <stdint.h>
#define CPU_VENDOR_STRING_LENGTH 13U
#define CPU_FEATURES_MAX_CACHES 8U
typedef uint64_t cpu_feature_t;
#define CPU_FEATURE_FPU           (1ULL << 0)
#define CPU_FEATURE_TSC           (1ULL << 1)
#define CPU_FEATURE_MSR           (1ULL << 2)
#define CPU_FEATURE_PAE           (1ULL << 3)
#define CPU_FEATURE_APIC          (1ULL << 4)
#define CPU_FEATURE_SEP           (1ULL << 5)
#define CPU_FEATURE_MTRR          (1ULL << 6)
#define CPU_FEATURE_PGE           (1ULL << 7)
#define CPU_FEATURE_CMOV          (1ULL << 8)
#define CPU_FEATURE_MMX           (1ULL << 9)
#define CPU_FEATURE_FXSR          (1ULL << 10)
#define CPU_FEATURE_SSE           (1ULL << 11)
#define CPU_FEATURE_SSE2          (1ULL << 12)
#define CPU_FEATURE_SSE3          (1ULL << 13)
#define CPU_FEATURE_SSSE3         (1ULL << 14)
#define CPU_FEATURE_SSE41         (1ULL << 15)
#define CPU_FEATURE_SSE42         (1ULL << 16)
#define CPU_FEATURE_AES           (1ULL << 17)
#define CPU_FEATURE_XSAVE         (1ULL << 18)
#define CPU_FEATURE_OSXSAVE       (1ULL << 19)
#define CPU_FEATURE_AVX           (1ULL << 20)
#define CPU_FEATURE_FMA           (1ULL << 21)
#define CPU_FEATURE_X2APIC        (1ULL << 22)
#define CPU_FEATURE_RDRAND        (1ULL << 23)
#define CPU_FEATURE_AVX2          (1ULL << 24)
#define CPU_FEATURE_BMI1          (1ULL << 25)
#define CPU_FEATURE_BMI2          (1ULL << 26)
#define CPU_FEATURE_INVPCID       (1ULL << 27)
#define CPU_FEATURE_RDSEED        (1ULL << 28)
#define CPU_FEATURE_SYSCALL       (1ULL << 29)
#define CPU_FEATURE_NX            (1ULL << 30)
#define CPU_FEATURE_PAGE_1G       (1ULL << 31)
#define CPU_FEATURE_RDTSCP        (1ULL << 32)
#define CPU_FEATURE_INVARIANT_TSC (1ULL << 33)
#define CPU_FEATURE_LAHF_LM       (1ULL << 34)
#define CPU_FEATURE_LONG_MODE     (1ULL << 35)
#define CPU_FEATURE_AVX_USABLE    (1ULL << 36)
#define CPU_FEATURE_PCID          (1ULL << 37)
#define CPU_FEATURE_TSC_DEADLINE  (1ULL << 38)
#define CPU_FEATURE_FSGSBASE      (1ULL << 39)
#define CPU_FEATURE_SMEP          (1ULL << 40)
#define CPU_FEATURE_SMAP          (1ULL << 41)
#define CPU_FEATURE_UMIP          (1ULL << 42)
#define CPU_FEATURE_PKU           (1ULL << 43)
#define CPU_FEATURE_OSPKE         (1ULL << 44)
#define CPU_FEATURE_LA57          (1ULL << 45)
#define CPU_FEATURE_CLFLUSHOPT    (1ULL << 46)
#define CPU_FEATURE_CLWB          (1ULL << 47)
#define CPU_FEATURE_CET_SS        (1ULL << 48)
#define CPU_FEATURE_IBRS_IBPB     (1ULL << 49)
#define CPU_FEATURE_STIBP         (1ULL << 50)
#define CPU_FEATURE_ARCH_CAP      (1ULL << 51)
#define CPU_FEATURE_SSBD          (1ULL << 52)
#define CPU_FEATURE_AVX512F       (1ULL << 53)
#define CPU_FEATURE_AVX512_USABLE (1ULL << 54)
#define CPU_FEATURE_XSAVEOPT      (1ULL << 55)
#define CPU_FEATURE_XSAVEC        (1ULL << 56)
#define CPU_FEATURE_XGETBV1       (1ULL << 57)
#define CPU_FEATURE_XSAVES        (1ULL << 58)
typedef struct {
 uint8_t level, type;
 uint16_t line_bytes, ways;
 uint32_t sets;
 uint64_t size_bytes;
} cpu_cache_info_t;
typedef struct {
 char vendor[CPU_VENDOR_STRING_LENGTH];
 uint32_t max_basic_leaf, max_extended_leaf, signature, family, model, stepping;
 uint32_t logical_processors, cache_line_bytes;
 uint32_t initial_apic_id, threads_per_core, cores_per_package;
 uint8_t topology_leaf, physical_address_bits, virtual_address_bits;
 uint32_t cache_count;
 uint64_t xsave_supported_mask, xsave_enabled_mask;
 uint32_t xsave_area_size, xsave_max_area_size;
 cpu_cache_info_t caches[CPU_FEATURES_MAX_CACHES];
 uint64_t features;
} cpu_features_t;
int cpu_features_initialize(void);
const cpu_features_t* cpu_features_get(void);
int cpu_feature_has(cpu_feature_t feature);
int cpu_feature_avx_usable(void);
int cpu_feature_avx2_usable(void);
#endif
