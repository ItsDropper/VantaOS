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
static cpuid_regs_t l0,l1,l7,e0,e1,e7,e8,topology_smt,topology_core;
static uint64_t xcr0;
int cpu_test_cpuid(uint32_t leaf,uint32_t subleaf,cpuid_regs_t* r)
{
 if(!r) return 0;
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
 memset(&topology_core,0,sizeof(topology_core));xcr0=0;
 l0.eax=7U;l0.ebx=pack4("Genu");l0.edx=pack4("ineI");l0.ecx=pack4("ntel");
 e0.eax=0x80000008U;
}
int main(void)
{
 reset();
 l1.eax=(6U<<8U)|(10U<<4U)|(3U<<16U)|3U;
 l1.ebx=(8U<<8U)|(12U<<16U);
 l1.ecx=(1U<<0U)|(1U<<9U)|(1U<<19U)|(1U<<20U)|(1U<<26U)|
        (1U<<27U)|(1U<<28U)|(1U<<30U);
 l1.edx=(1U<<4U)|(1U<<9U)|(1U<<25U)|(1U<<26U);
 l7.ebx=(1U<<5U)|(1U<<3U)|(1U<<8U)|(1U<<10U)|(1U<<18U);
 e1.edx=(1U<<11U)|(1U<<20U)|(1U<<26U)|(1U<<27U)|(1U<<29U);
 e1.ecx=1U;e7.edx=1U<<8U;e8.eax=48U|(48U<<8U);xcr0=0x6U;
 assert(cpu_features_initialize());
 const cpu_features_t* c=cpu_features_get();assert(c);
 assert(strcmp(c->vendor,"GenuineIntel")==0);
 assert(c->family==6U&&c->model==0x3AU&&c->stepping==3U);
 assert(c->logical_processors==12U&&c->cache_line_bytes==64U);
 assert(c->threads_per_core==1U&&c->cores_per_package==12U&&c->topology_leaf==0U);
 assert(c->physical_address_bits==48U&&c->virtual_address_bits==48U);
 assert(cpu_feature_has(CPU_FEATURE_LONG_MODE)&&cpu_feature_has(CPU_FEATURE_NX));
 assert(cpu_feature_has(CPU_FEATURE_APIC)&&cpu_feature_has(CPU_FEATURE_INVARIANT_TSC));
 assert(cpu_feature_has(CPU_FEATURE_AVX2)&&cpu_feature_has(CPU_FEATURE_BMI1));
 assert(cpu_feature_has(CPU_FEATURE_BMI2)&&cpu_feature_has(CPU_FEATURE_INVPCID));
 assert(cpu_feature_has(CPU_FEATURE_RDSEED)&&cpu_feature_avx_usable());
 l1.ecx&=~(1U<<27U);
 assert(cpu_features_initialize()&&!cpu_feature_avx_usable());
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
 reset();l0.eax=1U;e0.eax=0x80000000U;l1.edx=1U<<9U;
 assert(cpu_features_initialize()&&cpu_feature_has(CPU_FEATURE_APIC));
 assert(!cpu_feature_has(CPU_FEATURE_NX)&&!cpu_feature_has(CPU_FEATURE_LONG_MODE));
 assert(cpu_features_get()->physical_address_bits==0U);
 reset();l0.eax=0U;e0.eax=0x80000000U;
 assert(cpu_features_initialize()&&cpu_features_get()->max_basic_leaf==0U);
 assert(!cpu_feature_has(CPU_FEATURE_APIC)&&!cpu_feature_avx_usable());
 return 0;
}
