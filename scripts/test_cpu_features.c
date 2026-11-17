/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "cpu_features.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef struct { uint32_t eax,ebx,ecx,edx; } cpuid_regs_t;
static cpuid_regs_t l0,l1,l7,e0,e1,e7,e8,topology_smt,topology_core,cache_l1,cache_l2,cache_amd;
static uint64_t xcr0;
static cpuid_regs_t xsave0,xsave1;
int cpu_test_cpuid(uint32_t leaf,uint32_t subleaf,cpuid_regs_t* r)
{
 if(!r) return 0;
 if(leaf==0x0DU && subleaf==0U) {*r=xsave0;return 1;}
 if(leaf==0x0DU && subleaf==1U) {*r=xsave1;return 1;}
 if(leaf==0x8000001DU) {
  if(subleaf==0U) {*r=cache_amd;return 1;}
  memset(r,0,sizeof(*r));return 1;
 }
 if(leaf==4U) {
  if(subleaf==0U) {*r=cache_l1;return 1;}
  if(subleaf==1U) {*r=cache_l2;return 1;}
  memset(r,0,sizeof(*r));return 1;
 }
 if(leaf==0x1FU) {
  if(subleaf==0U) {*r=topology_smt;return 1;}
  if(subleaf==1U) {*r=topology_core;return 1;}
  memset(r,0,sizeof(*r));return 1;
 }
 if(subleaf) return 0;
 switch(leaf) {
 case 0U:*r=l0;return 1; case 1U:*r=l1;return 1; case 7U:*r=l7;return 1;
 case 0x80000000U:*r=e0;return 1; case 0x80000001U:*r=e1;return 1;
 case 0x80000007U:*r=e7;return 1; case 0x80000008U:*r=e8;return 1;
 default:memset(r,0,sizeof(*r));return 1;
 }
}
uint64_t cpu_test_xgetbv(uint32_t index) { assert(index==0U);return xcr0; }
void serial_write(const char* text) { (void)text; }
static uint32_t pack4(const char* s)
{
 return (uint32_t)(uint8_t)s[0]|((uint32_t)(uint8_t)s[1]<<8U)|
        ((uint32_t)(uint8_t)s[2]<<16U)|((uint32_t)(uint8_t)s[3]<<24U);
}
static void reset(void)
{
 memset(&l0,0,sizeof(l0));memset(&l1,0,sizeof(l1));memset(&l7,0,sizeof(l7));
 memset(&e0,0,sizeof(e0));memset(&e1,0,sizeof(e1));memset(&e7,0,sizeof(e7));
 memset(&e8,0,sizeof(e8));memset(&topology_smt,0,sizeof(topology_smt));
 memset(&topology_core,0,sizeof(topology_core));
 memset(&cache_l1,0,sizeof(cache_l1));memset(&cache_l2,0,sizeof(cache_l2));
 memset(&cache_amd,0,sizeof(cache_amd));memset(&xsave0,0,sizeof(xsave0));
 memset(&xsave1,0,sizeof(xsave1));xcr0=0;
 l0.eax=7U;l0.ebx=pack4("Genu");l0.edx=pack4("ineI");l0.ecx=pack4("ntel");
 e0.eax=0x80000008U;
}
int main(void)
{
 reset();l0.eax=0xDU;
 l1.eax=(6U<<8U)|(10U<<4U)|(3U<<16U)|3U;
 l1.ebx=(8U<<8U)|(12U<<16U);
 l1.ecx=(1U<<0U)|(1U<<9U)|(1U<<19U)|(1U<<20U)|(1U<<26U)|
        (1U<<27U)|(1U<<28U)|(1U<<30U);
 l1.edx=(1U<<4U)|(1U<<9U)|(1U<<25U)|(1U<<26U);
 cache_l1.eax=1U|(1U<<5U);cache_l1.ebx=(7U<<22U)|63U;cache_l1.ecx=63U;
 cache_l2.eax=3U|(2U<<5U);cache_l2.ebx=(7U<<22U)|63U;cache_l2.ecx=511U;
 l7.ebx=(1U<<5U)|(1U<<3U)|(1U<<8U)|(1U<<10U)|(1U<<18U);
 e1.edx=(1U<<11U)|(1U<<20U)|(1U<<26U)|(1U<<27U)|(1U<<29U);
 e1.ecx=1U;e7.edx=1U<<8U;e8.eax=48U|(48U<<8U);xcr0=0x6U;
 xsave0.eax=0x7U;xsave0.ebx=832U;xsave0.ecx=832U;
 xsave1.eax=0xFU;
 assert(cpu_features_initialize());
 const cpu_features_t* c=cpu_features_get();assert(c);
 assert(strcmp(c->vendor,"GenuineIntel")==0);
 assert(c->family==6U&&c->model==0x3AU&&c->stepping==3U);
 assert(c->logical_processors==12U&&c->cache_line_bytes==64U);
 assert(c->threads_per_core==1U&&c->cores_per_package==12U&&c->topology_leaf==0U);
 assert(c->physical_address_bits==48U&&c->virtual_address_bits==48U);
 assert(c->cache_count==2U);
 assert(c->caches[0].level==1U&&c->caches[0].type==1U);
 assert(c->caches[0].line_bytes==64U&&c->caches[0].ways==8U);
 assert(c->caches[0].size_bytes==32768U);
 assert(c->caches[1].level==2U&&c->caches[1].type==3U);
 assert(c->caches[1].size_bytes==262144U);
 assert(cpu_feature_has(CPU_FEATURE_LONG_MODE)&&cpu_feature_has(CPU_FEATURE_NX));
 assert(cpu_feature_has(CPU_FEATURE_APIC)&&cpu_feature_has(CPU_FEATURE_INVARIANT_TSC));
 assert(cpu_feature_has(CPU_FEATURE_AVX2)&&cpu_feature_has(CPU_FEATURE_BMI1));
 assert(cpu_feature_has(CPU_FEATURE_BMI2)&&cpu_feature_has(CPU_FEATURE_INVPCID));
 assert(cpu_feature_has(CPU_FEATURE_RDSEED)&&cpu_feature_avx_usable());
 assert(cpu_feature_avx2_usable());
 assert(cpu_features_get()->xsave_supported_mask==0x7U);
 assert(cpu_features_get()->xsave_enabled_mask==0x6U);
 assert(cpu_features_get()->xsave_area_size==832U);
 assert(cpu_features_get()->xsave_max_area_size==832U);
 assert(cpu_feature_has(CPU_FEATURE_XSAVEOPT)&&cpu_feature_has(CPU_FEATURE_XSAVEC));
 xcr0=0x2U;
 assert(cpu_features_initialize()&&!cpu_feature_avx_usable());
 xcr0=0x6U;
 l1.ecx&=~(1U<<27U);
 assert(cpu_features_initialize()&&!cpu_feature_avx_usable());
 assert(!cpu_feature_avx2_usable());
 /* Modern privilege, address-space and mitigation capabilities are decoded
  * independently from whether the kernel has enabled them. */
 reset();l0.eax=7U;
 l1.ecx=(1U<<17U)|(1U<<24U);
 l7.ebx=(1U<<0U)|(1U<<7U)|(1U<<20U)|(1U<<16U)|(1U<<23U)|(1U<<24U);
 l7.ecx=(1U<<2U)|(1U<<3U)|(1U<<4U)|(1U<<7U)|(1U<<16U);
 l7.edx=(1U<<26U)|(1U<<27U)|(1U<<29U)|(1U<<31U);
 assert(cpu_features_initialize());
 c=cpu_features_get();
 assert(cpu_feature_has(CPU_FEATURE_PCID)&&cpu_feature_has(CPU_FEATURE_TSC_DEADLINE));
 assert(cpu_feature_has(CPU_FEATURE_FSGSBASE)&&cpu_feature_has(CPU_FEATURE_SMEP));
 assert(cpu_feature_has(CPU_FEATURE_SMAP)&&cpu_feature_has(CPU_FEATURE_UMIP));
 assert(cpu_feature_has(CPU_FEATURE_PKU)&&cpu_feature_has(CPU_FEATURE_OSPKE));
 assert(cpu_feature_has(CPU_FEATURE_LA57)&&cpu_feature_has(CPU_FEATURE_CET_SS));
 assert(cpu_feature_has(CPU_FEATURE_CLFLUSHOPT)&&cpu_feature_has(CPU_FEATURE_CLWB));
 assert(cpu_feature_has(CPU_FEATURE_IBRS_IBPB)&&cpu_feature_has(CPU_FEATURE_STIBP));
 assert(cpu_feature_has(CPU_FEATURE_ARCH_CAP)&&cpu_feature_has(CPU_FEATURE_SSBD));
 assert(!cpu_feature_has(CPU_FEATURE_AVX512_USABLE));

 reset();l0.eax=0xDU;l1.ecx=(1U<<26U)|(1U<<27U)|(1U<<28U);
 l7.ebx=1U<<16U;xcr0=0xE6U;
 xsave0.eax=0xE7U;xsave0.ebx=2688U;xsave0.ecx=2688U;
 assert(cpu_features_initialize());
 assert(cpu_feature_has(CPU_FEATURE_AVX512F));
 assert(cpu_feature_has(CPU_FEATURE_AVX512_USABLE));

 /* Prefer architectural topology leaves when firmware/CPU exposes them. */
 reset();l0.eax=0x1FU;l1.ebx=(12U<<16U)|(0x23U<<24U);
 topology_smt.eax=1U;topology_smt.ebx=2U;topology_smt.ecx=(1U<<8U);
 topology_smt.edx=0x123U;
 topology_core.eax=4U;topology_core.ebx=12U;topology_core.ecx=(2U<<8U)|1U;
 topology_core.edx=0x123U;
 assert(cpu_features_initialize());
 c=cpu_features_get();
 assert(c->topology_leaf==0x1FU&&c->initial_apic_id==0x123U);
 assert(c->logical_processors==12U&&c->threads_per_core==2U);
 assert(c->cores_per_package==6U);
 /* AMD's extended deterministic-cache leaf is a supported fallback. */
 reset();l0.eax=3U;e0.eax=0x8000001DU;
 cache_amd.eax=3U|(3U<<5U);cache_amd.ebx=(15U<<22U)|63U;
 cache_amd.ecx=2047U;
 assert(cpu_features_initialize());
 c=cpu_features_get();
 assert(c->cache_count==1U&&c->caches[0].level==3U);
 assert(c->caches[0].size_bytes==2097152U);
 /* Reject unrepresentable cache-set counts instead of truncating them. */
 reset();l0.eax=4U;e0.eax=0x80000000U;
 cache_l1.eax=1U|(1U<<5U);cache_l1.ebx=63U;cache_l1.ecx=0xFFFFFFFFU;
 assert(cpu_features_initialize());
 assert(cpu_features_get()->cache_count==0U);
 reset();l0.eax=1U;e0.eax=0x80000000U;l1.edx=1U<<9U;
 assert(cpu_features_initialize()&&cpu_feature_has(CPU_FEATURE_APIC));
 assert(!cpu_feature_has(CPU_FEATURE_NX)&&!cpu_feature_has(CPU_FEATURE_LONG_MODE));
 assert(cpu_features_get()->physical_address_bits==0U);
 reset();l0.eax=0U;e0.eax=0x80000000U;
 assert(cpu_features_initialize()&&cpu_features_get()->max_basic_leaf==0U);
 assert(!cpu_feature_has(CPU_FEATURE_APIC)&&!cpu_feature_avx_usable());

 /* Per-logical-CPU inventories preserve heterogeneous optional features. */
 const cpu_feature_t required=CPU_FEATURE_FPU|CPU_FEATURE_TSC|CPU_FEATURE_MSR|
  CPU_FEATURE_PAE|CPU_FEATURE_APIC|CPU_FEATURE_SSE2|CPU_FEATURE_SYSCALL|
  CPU_FEATURE_LONG_MODE|CPU_FEATURE_NX;
 reset();l1.edx=(1U<<0U)|(1U<<4U)|(1U<<5U)|(1U<<6U)|(1U<<9U)|(1U<<26U);
 e1.edx=(1U<<11U)|(1U<<20U)|(1U<<29U);
 e8.eax=52U|(57U<<8U);l7.ebx=1U<<5U;
 assert(cpu_features_initialize_cpu(4U));
 l7.ebx=0U;e8.eax=36U|(48U<<8U);
 assert(cpu_features_initialize_cpu(5U));
 assert(cpu_feature_has_cpu(4U,CPU_FEATURE_AVX2));
 assert(!cpu_feature_has_cpu(5U,CPU_FEATURE_AVX2));
 assert(cpu_features_smp_compatible(4U,5U,required));
 uint32_t online_ids[2]={4U,5U};cpu_feature_t common=0U;
 assert(cpu_features_compute_common(online_ids,2U,&common));
 assert((common&required)==required);
 assert((common&CPU_FEATURE_AVX2)==0U);
 assert(!cpu_features_compute_common((uint32_t[]){4U,4U},2U,&common));
 e1.edx&=~(1U<<20U);
 assert(cpu_features_initialize_cpu(6U));
 assert(!cpu_features_smp_compatible(4U,6U,required));
 e1.edx|=(1U<<20U);l1.edx&=~(1U<<26U);
 assert(cpu_features_initialize_cpu(7U));
 assert(!cpu_features_smp_compatible(4U,7U,required));
 l1.edx|=(1U<<26U);l1.edx&=~(1U<<9U);
 assert(cpu_features_initialize_cpu(8U));
 assert(!cpu_features_smp_compatible(4U,8U,required));
 assert(!cpu_features_initialize_cpu(CPU_FEATURES_MAX_CPUS));
 assert(cpu_features_set_bootstrap_cpu(4U));
 assert(cpu_features_get()==cpu_features_get_cpu(4U));
 assert(cpu_feature_has(CPU_FEATURE_AVX2));
 assert(cpu_features_set_bootstrap_cpu(5U));
 assert(!cpu_feature_has(CPU_FEATURE_AVX2));
 assert(!cpu_features_set_bootstrap_cpu(CPU_FEATURES_MAX_CPUS));
 return 0;
}
