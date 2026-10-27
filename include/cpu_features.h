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
 cpu_cache_info_t caches[CPU_FEATURES_MAX_CACHES];
 uint64_t features;
} cpu_features_t;
int cpu_features_initialize(void);
const cpu_features_t* cpu_features_get(void);
int cpu_feature_has(cpu_feature_t feature);
int cpu_feature_avx_usable(void);
int cpu_feature_avx2_usable(void);
#endif
