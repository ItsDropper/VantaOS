/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "cpu_features.h"
#include "serial.h"
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t eax, ebx, ecx, edx; } cpuid_regs_t;
static cpu_features_t cpu_states[CPU_FEATURES_MAX_CPUS];
static uint8_t cpu_state_valid[CPU_FEATURES_MAX_CPUS];
#ifdef CPU_FEATURES_TEST
int cpu_test_cpuid(uint32_t leaf, uint32_t subleaf, cpuid_regs_t* regs);
uint64_t cpu_test_xgetbv(uint32_t index);
#endif
static int cpuid_read(uint32_t leaf, uint32_t subleaf, cpuid_regs_t* regs)
{
 if (!regs) return 0;
#ifdef CPU_FEATURES_TEST
 return cpu_test_cpuid(leaf, subleaf, regs);
#else
 __asm__ volatile("cpuid" : "=a"(regs->eax), "=b"(regs->ebx),
                  "=c"(regs->ecx), "=d"(regs->edx)
                  : "a"(leaf), "c"(subleaf) : "memory");
 return 1;
#endif
}
static uint64_t read_xcr0(void)
{
#ifdef CPU_FEATURES_TEST
 return cpu_test_xgetbv(0U);
#else
 uint32_t low, high;
 __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0U) : "memory");
 return ((uint64_t)high << 32U) | low;
#endif
}
static void set_feature(cpu_features_t* state, uint64_t feature, int supported)
{
 if (supported) state->features |= feature;
}
static void copy_vendor(cpu_features_t* state, uint32_t ebx, uint32_t edx, uint32_t ecx)
{
 const uint32_t words[3] = {ebx, edx, ecx};
 for (uint32_t w=0; w<3U; ++w) {
  for (uint32_t b=0; b<4U; ++b)
   state->vendor[w*4U+b]=(char)(words[w]>>(b*8U));
 }
 state->vendor[12] = 0;
}
static void decode_signature(cpu_features_t* state, uint32_t signature)
{
 uint32_t base_family=(signature>>8U)&0x0FU;
 uint32_t base_model=(signature>>4U)&0x0FU;
 state->signature=signature;
 state->stepping=signature&0x0FU;
 state->family=base_family;
 state->model=base_model;
 if (base_family==0x0FU) state->family+=(signature>>20U)&0xFFU;
 if (base_family==0x06U || base_family==0x0FU)
  state->model|=((signature>>16U)&0x0FU)<<4U;
}
static void decode_basic_features(cpu_features_t* state, const cpuid_regs_t* r)
{
 uint32_t c=r->ecx, d=r->edx;
 set_feature(state, CPU_FEATURE_FPU,d&(1U<<0)); set_feature(state, CPU_FEATURE_TSC,d&(1U<<4));
 set_feature(state, CPU_FEATURE_MSR,d&(1U<<5)); set_feature(state, CPU_FEATURE_PAE,d&(1U<<6));
 set_feature(state, CPU_FEATURE_APIC,d&(1U<<9)); set_feature(state, CPU_FEATURE_SEP,d&(1U<<11));
 set_feature(state, CPU_FEATURE_MTRR,d&(1U<<12)); set_feature(state, CPU_FEATURE_PGE,d&(1U<<13));
 set_feature(state, CPU_FEATURE_CMOV,d&(1U<<15)); set_feature(state, CPU_FEATURE_MMX,d&(1U<<23));
 set_feature(state, CPU_FEATURE_FXSR,d&(1U<<24)); set_feature(state, CPU_FEATURE_SSE,d&(1U<<25));
 set_feature(state, CPU_FEATURE_SSE2,d&(1U<<26)); set_feature(state, CPU_FEATURE_SSE3,c&(1U<<0));
 set_feature(state, CPU_FEATURE_SSSE3,c&(1U<<9)); set_feature(state, CPU_FEATURE_SSE41,c&(1U<<19));
 set_feature(state, CPU_FEATURE_SSE42,c&(1U<<20)); set_feature(state, CPU_FEATURE_AES,c&(1U<<25));
 set_feature(state, CPU_FEATURE_XSAVE,c&(1U<<26)); set_feature(state, CPU_FEATURE_OSXSAVE,c&(1U<<27));
 set_feature(state, CPU_FEATURE_AVX,c&(1U<<28)); set_feature(state, CPU_FEATURE_FMA,c&(1U<<12));
 set_feature(state, CPU_FEATURE_PCID,c&(1U<<17)); set_feature(state, CPU_FEATURE_X2APIC,c&(1U<<21));
 set_feature(state, CPU_FEATURE_TSC_DEADLINE,c&(1U<<24));
 set_feature(state, CPU_FEATURE_RDRAND,c&(1U<<30));
 state->cache_line_bytes=((r->ebx>>8U)&0xFFU)*8U;
 state->logical_processors=(r->ebx>>16U)&0xFFU;
 state->initial_apic_id=(r->ebx>>24U)&0xFFU;
 state->threads_per_core=1U;
 state->cores_per_package=state->logical_processors ? state->logical_processors : 1U;
}
static void decode_structured_features(cpu_features_t* state, const cpuid_regs_t* r)
{
 uint32_t b=r->ebx;
 set_feature(state, CPU_FEATURE_AVX2,b&(1U<<5)); set_feature(state, CPU_FEATURE_BMI1,b&(1U<<3));
 set_feature(state, CPU_FEATURE_BMI2,b&(1U<<8)); set_feature(state, CPU_FEATURE_INVPCID,b&(1U<<10));
 set_feature(state, CPU_FEATURE_RDSEED,b&(1U<<18));
 set_feature(state, CPU_FEATURE_FSGSBASE,b&(1U<<0));
 set_feature(state, CPU_FEATURE_SMEP,b&(1U<<7));
 set_feature(state, CPU_FEATURE_AVX512F,b&(1U<<16));
 set_feature(state, CPU_FEATURE_SMAP,b&(1U<<20));
 set_feature(state, CPU_FEATURE_CLFLUSHOPT,b&(1U<<23));
 set_feature(state, CPU_FEATURE_CLWB,b&(1U<<24));
 set_feature(state, CPU_FEATURE_UMIP,r->ecx&(1U<<2));
 set_feature(state, CPU_FEATURE_PKU,r->ecx&(1U<<3));
 set_feature(state, CPU_FEATURE_OSPKE,r->ecx&(1U<<4));
 set_feature(state, CPU_FEATURE_CET_SS,r->ecx&(1U<<7));
 set_feature(state, CPU_FEATURE_LA57,r->ecx&(1U<<16));
 set_feature(state, CPU_FEATURE_IBRS_IBPB,r->edx&(1U<<26));
 set_feature(state, CPU_FEATURE_STIBP,r->edx&(1U<<27));
 set_feature(state, CPU_FEATURE_ARCH_CAP,r->edx&(1U<<29));
 set_feature(state, CPU_FEATURE_SSBD,r->edx&(1U<<31));
}

/*
 * Intel's 0x1F/0x0B leaves describe hierarchical logical-processor counts.
 * Treat malformed or empty levels as unavailable and retain the leaf-1
 * fallback rather than manufacturing a topology from partial data.
 */
static int decode_topology_leaf(cpu_features_t* state, uint32_t leaf)
{
 cpuid_regs_t r;
 uint32_t threads=0U, package_logical=0U, apic_id=0U;
 int found=0;
 for (uint32_t subleaf=0U; subleaf<8U; ++subleaf) {
  if (!cpuid_read(leaf,subleaf,&r)) break;
  uint32_t count=r.ebx&0xFFFFU;
  uint32_t level_type=(r.ecx>>8U)&0xFFU;
  if (count==0U || level_type==0U) break;
  found=1;
  apic_id=r.edx;
  if (level_type==1U) threads=count;
  if (count>package_logical) package_logical=count;
 }
 if (!found || package_logical==0U) return 0;
 if (threads==0U) threads=1U;
 state->initial_apic_id=apic_id;
 state->logical_processors=package_logical;
 state->threads_per_core=threads;
 state->cores_per_package=package_logical/threads;
 if (state->cores_per_package==0U) state->cores_per_package=1U;
 state->topology_leaf=(uint8_t)(leaf==0x1FU ? 0x1FU : 0x0BU);
 return 1;
}

static int decode_cache_leaf(cpu_features_t* state, uint32_t leaf)
{
 cpuid_regs_t r;
 uint32_t count=0U;
 for (uint32_t subleaf=0U; subleaf<CPU_FEATURES_MAX_CACHES; ++subleaf) {
  if (!cpuid_read(leaf,subleaf,&r)) break;
  uint32_t type=r.eax&0x1FU;
  if (type==0U) break;
  if (type>3U) continue;
  uint32_t level=(r.eax>>5U)&0x7U;
  uint64_t line=(uint64_t)(r.ebx&0xFFFU)+1U;
  uint64_t partitions=(uint64_t)((r.ebx>>12U)&0x3FFU)+1U;
  uint64_t ways=(uint64_t)((r.ebx>>22U)&0x3FFU)+1U;
  uint64_t sets=(uint64_t)r.ecx+1U;
  if (level==0U || line==0U || ways==0U || sets==0U ||
      line>UINT16_MAX || ways>UINT16_MAX || sets>UINT32_MAX ||
      partitions>UINT64_MAX/line)
   continue;
  uint64_t size=line*partitions;
  if (ways>UINT64_MAX/size) continue;
  size*=ways;
  if (sets>UINT64_MAX/size) continue;
  size*=sets;
  if (size==0U || count>=CPU_FEATURES_MAX_CACHES) continue;
  cpu_cache_info_t* cache=&state->caches[count++];
  cache->level=(uint8_t)level;
  cache->type=(uint8_t)type; /* 1=data, 2=instruction, 3=unified */
  cache->line_bytes=(uint16_t)line;
  cache->ways=(uint16_t)ways;
  cache->sets=(uint32_t)sets;
  cache->size_bytes=size;
 }
 state->cache_count=count;
 return count!=0U;
}
static void decode_cache_geometry(cpu_features_t* state)
{
 if (state->max_basic_leaf>=4U && decode_cache_leaf(state, 4U)) return;
 if (state->max_extended_leaf>=0x8000001DU)
  (void)decode_cache_leaf(state, 0x8000001DU);
}

static void decode_topology(cpu_features_t* state)
{
 if (state->max_basic_leaf>=0x1FU && decode_topology_leaf(state, 0x1FU)) return;
 if (state->max_basic_leaf>=0x0BU && decode_topology_leaf(state, 0x0BU)) return;
 state->topology_leaf=0U;
}

static void decode_xsave_state(cpu_features_t* state)
{
 cpuid_regs_t r;
 if ((state->features & CPU_FEATURE_XSAVE)==0U ||
     state->max_basic_leaf<0x0DU ||
     !cpuid_read(0x0DU,0U,&r))
  return;
 state->xsave_supported_mask=((uint64_t)r.edx<<32U)|r.eax;
 state->xsave_max_area_size=r.ecx;
 if (state->features & CPU_FEATURE_OSXSAVE) {
  state->xsave_enabled_mask=read_xcr0();
  state->xsave_area_size=r.ebx;
 }
 if (cpuid_read(0x0DU,1U,&r)) {
  set_feature(state, CPU_FEATURE_XSAVEOPT,r.eax&(1U<<0));
  set_feature(state, CPU_FEATURE_XSAVEC,r.eax&(1U<<1));
  set_feature(state, CPU_FEATURE_XGETBV1,r.eax&(1U<<2));
  set_feature(state, CPU_FEATURE_XSAVES,r.eax&(1U<<3));
 }
}

static void decode_extended_features(cpu_features_t* state)
{
 cpuid_regs_t r;
 if (state->max_extended_leaf<0x80000001U ||
     !cpuid_read(0x80000001U,0U,&r)) return;
 set_feature(state, CPU_FEATURE_SYSCALL,r.edx&(1U<<11));
 set_feature(state, CPU_FEATURE_NX,r.edx&(1U<<20));
 set_feature(state, CPU_FEATURE_PAGE_1G,r.edx&(1U<<26));
 set_feature(state, CPU_FEATURE_RDTSCP,r.edx&(1U<<27));
 set_feature(state, CPU_FEATURE_LONG_MODE,r.edx&(1U<<29));
 set_feature(state, CPU_FEATURE_LAHF_LM,r.ecx&(1U<<0));
 if (state->max_extended_leaf>=0x80000007U &&
     cpuid_read(0x80000007U,0U,&r))
  set_feature(state, CPU_FEATURE_INVARIANT_TSC,r.edx&(1U<<8));
 if (state->max_extended_leaf>=0x80000008U &&
     cpuid_read(0x80000008U,0U,&r)) {
  state->physical_address_bits=(uint8_t)(r.eax&0xFFU);
  state->virtual_address_bits=(uint8_t)((r.eax>>8U)&0xFFU);
 }
}
int cpu_features_initialize_cpu(uint32_t logical_id)
{
 cpuid_regs_t r;
 if (logical_id>=CPU_FEATURES_MAX_CPUS) return 0;
 cpu_features_t* state=&cpu_states[logical_id];
 cpu_state_valid[logical_id]=0;
 for (size_t i=0; i<sizeof(*state); ++i) ((uint8_t*)state)[i]=0;
 if (!cpuid_read(0U,0U,&r)) return 0;
 state->max_basic_leaf=r.eax;
 copy_vendor(state,r.ebx,r.edx,r.ecx);
 if (!cpuid_read(0x80000000U,0U,&r)) return 0;
 state->max_extended_leaf=r.eax;
 if (state->max_basic_leaf>=1U && cpuid_read(1U,0U,&r)) {
  decode_signature(state,r.eax);
  decode_basic_features(state,&r);
 }
 if (state->max_basic_leaf>=7U && cpuid_read(7U,0U,&r))
  decode_structured_features(state,&r);
 decode_topology(state);
 decode_cache_geometry(state);
 decode_extended_features(state);
 decode_xsave_state(state);
 /* Optional vector ISA is usable only when every required XSTATE component
  * is both implemented by the CPU and enabled by the operating system. */
 if ((state->features & (CPU_FEATURE_AVX | CPU_FEATURE_XSAVE |
                            CPU_FEATURE_OSXSAVE)) ==
     (CPU_FEATURE_AVX | CPU_FEATURE_XSAVE | CPU_FEATURE_OSXSAVE) &&
     (state->xsave_supported_mask&0x6U)==0x6U &&
     (state->xsave_enabled_mask&0x6U)==0x6U)
  set_feature(state, CPU_FEATURE_AVX_USABLE,1);
 if ((state->features & (CPU_FEATURE_AVX_USABLE | CPU_FEATURE_AVX512F)) ==
     (CPU_FEATURE_AVX_USABLE | CPU_FEATURE_AVX512F) &&
     (state->xsave_supported_mask&0xE6U)==0xE6U &&
     (state->xsave_enabled_mask&0xE6U)==0xE6U)
  set_feature(state, CPU_FEATURE_AVX512_USABLE,1);
 cpu_state_valid[logical_id]=1;
 serial_write("VANTAOS_CPU_VENDOR="); serial_write(state->vendor); serial_write("\n");
 serial_write((state->features&CPU_FEATURE_LONG_MODE) ?
              "VANTAOS_CPU_LONG_MODE=1\n" : "VANTAOS_CPU_LONG_MODE=0\n");
 serial_write((state->features&CPU_FEATURE_NX) ?
              "VANTAOS_CPU_NX=1\n" : "VANTAOS_CPU_NX=0\n");
 serial_write((state->features&CPU_FEATURE_APIC) ?
              "VANTAOS_CPU_APIC=1\n" : "VANTAOS_CPU_APIC=0\n");
 return 1;
}
int cpu_features_initialize(void) { return cpu_features_initialize_cpu(0U); }
const cpu_features_t* cpu_features_get_cpu(uint32_t id)
{
 return id<CPU_FEATURES_MAX_CPUS && cpu_state_valid[id] ? &cpu_states[id] : 0;
}
const cpu_features_t* cpu_features_get(void) { return cpu_features_get_cpu(0U); }
int cpu_feature_has_cpu(uint32_t id, cpu_feature_t feature)
{
 const cpu_features_t* state=cpu_features_get_cpu(id);
 return state && (state->features&(uint64_t)feature)!=0U;
}
int cpu_feature_has(cpu_feature_t feature) { return cpu_feature_has_cpu(0U,feature); }
int cpu_features_smp_compatible(uint32_t bsp_id, uint32_t candidate_id,
                                cpu_feature_t required)
{
 const cpu_features_t* bsp=cpu_features_get_cpu(bsp_id);
 const cpu_features_t* candidate=cpu_features_get_cpu(candidate_id);
 if (!bsp || !candidate ||
     (candidate->features&(uint64_t)required)!=(uint64_t)required)
  return 0;
 if ((bsp->features&CPU_FEATURE_NX)!=0U &&
     (candidate->features&CPU_FEATURE_NX)==0U)
  return 0;
 if (bsp->physical_address_bits!=0U &&
     candidate->physical_address_bits<bsp->physical_address_bits)
  return 0;
 if (bsp->virtual_address_bits!=0U &&
     candidate->virtual_address_bits<bsp->virtual_address_bits)
  return 0;
 return 1;
}
int cpu_features_compute_common(const uint32_t* ids, uint32_t count,
                                cpu_feature_t* common)
{
 if (!ids || !common || count==0U || count>CPU_FEATURES_MAX_CPUS)
  return 0;
 const cpu_features_t* first=cpu_features_get_cpu(ids[0]);
 if (!first) return 0;
 uint64_t mask=first->features;
 for (uint32_t i=1U;i<count;i++) {
  const cpu_features_t* state=cpu_features_get_cpu(ids[i]);
  if (!state) return 0;
  for (uint32_t j=0U;j<i;j++)
   if (ids[j]==ids[i]) return 0;
  mask&=state->features;
 }
 *common=mask;
 return 1;
}
int cpu_feature_avx_usable(void) { return cpu_feature_has(CPU_FEATURE_AVX_USABLE); }
int cpu_feature_avx2_usable(void)
{
 return cpu_feature_avx_usable() && cpu_feature_has(CPU_FEATURE_AVX2);
}
