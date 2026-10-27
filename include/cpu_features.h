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
typedef enum {
 CPU_FEATURE_FPU=1ULL<<0, CPU_FEATURE_TSC=1ULL<<1, CPU_FEATURE_MSR=1ULL<<2,
 CPU_FEATURE_PAE=1ULL<<3, CPU_FEATURE_APIC=1ULL<<4, CPU_FEATURE_SEP=1ULL<<5,
 CPU_FEATURE_MTRR=1ULL<<6, CPU_FEATURE_PGE=1ULL<<7, CPU_FEATURE_CMOV=1ULL<<8,
 CPU_FEATURE_MMX=1ULL<<9, CPU_FEATURE_FXSR=1ULL<<10, CPU_FEATURE_SSE=1ULL<<11,
 CPU_FEATURE_SSE2=1ULL<<12, CPU_FEATURE_SSE3=1ULL<<13, CPU_FEATURE_SSSE3=1ULL<<14,
 CPU_FEATURE_SSE41=1ULL<<15, CPU_FEATURE_SSE42=1ULL<<16, CPU_FEATURE_AES=1ULL<<17,
 CPU_FEATURE_XSAVE=1ULL<<18, CPU_FEATURE_OSXSAVE=1ULL<<19, CPU_FEATURE_AVX=1ULL<<20,
 CPU_FEATURE_FMA=1ULL<<21, CPU_FEATURE_X2APIC=1ULL<<22, CPU_FEATURE_RDRAND=1ULL<<23,
 CPU_FEATURE_AVX2=1ULL<<24, CPU_FEATURE_BMI1=1ULL<<25, CPU_FEATURE_BMI2=1ULL<<26,
 CPU_FEATURE_INVPCID=1ULL<<27, CPU_FEATURE_RDSEED=1ULL<<28, CPU_FEATURE_SYSCALL=1ULL<<29,
 CPU_FEATURE_NX=1ULL<<30, CPU_FEATURE_PAGE_1G=1ULL<<31, CPU_FEATURE_RDTSCP=1ULL<<32,
 CPU_FEATURE_INVARIANT_TSC=1ULL<<33, CPU_FEATURE_LAHF_LM=1ULL<<34,
 CPU_FEATURE_LONG_MODE=1ULL<<35, CPU_FEATURE_AVX_USABLE=1ULL<<36
} cpu_feature_t;
typedef struct {
 char vendor[CPU_VENDOR_STRING_LENGTH];
 uint32_t max_basic_leaf, max_extended_leaf, signature, family, model, stepping;
 uint32_t logical_processors, cache_line_bytes;
 uint8_t physical_address_bits, virtual_address_bits;
 uint64_t features;
} cpu_features_t;
int cpu_features_initialize(void);
const cpu_features_t* cpu_features_get(void);
int cpu_feature_has(cpu_feature_t feature);
int cpu_feature_avx_usable(void);
#endif
