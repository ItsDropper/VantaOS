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
static cpu_features_t cpu_state;
static int cpu_state_valid;
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
static void set_feature(uint64_t feature, int supported)
{
 if (supported) cpu_state.features |= feature;
}
static void copy_vendor(uint32_t ebx, uint32_t edx, uint32_t ecx)
{
 const uint32_t words[3] = {ebx, edx, ecx};
 for (uint32_t w=0; w<3U; ++w) {
  for (uint32_t b=0; b<4U; ++b)
   cpu_state.vendor[w*4U+b]=(char)(words[w]>>(b*8U));
 }
 cpu_state.vendor[12] = 0;
}
static void decode_signature(uint32_t signature)
{
 uint32_t base_family=(signature>>8U)&0x0FU;
 uint32_t base_model=(signature>>4U)&0x0FU;
 cpu_state.signature=signature;
 cpu_state.stepping=signature&0x0FU;
 cpu_state.family=base_family;
 cpu_state.model=base_model;
 if (base_family==0x0FU) cpu_state.family+=(signature>>20U)&0xFFU;
 if (base_family==0x06U || base_family==0x0FU)
  cpu_state.model|=((signature>>16U)&0x0FU)<<4U;
}
static void decode_basic_features(const cpuid_regs_t* r)
{
 uint32_t c=r->ecx, d=r->edx;
 set_feature(CPU_FEATURE_FPU,d&(1U<<0)); set_feature(CPU_FEATURE_TSC,d&(1U<<4));
 set_feature(CPU_FEATURE_MSR,d&(1U<<5)); set_feature(CPU_FEATURE_PAE,d&(1U<<6));
 set_feature(CPU_FEATURE_APIC,d&(1U<<9)); set_feature(CPU_FEATURE_SEP,d&(1U<<11));
 set_feature(CPU_FEATURE_MTRR,d&(1U<<12)); set_feature(CPU_FEATURE_PGE,d&(1U<<13));
 set_feature(CPU_FEATURE_CMOV,d&(1U<<15)); set_feature(CPU_FEATURE_MMX,d&(1U<<23));
 set_feature(CPU_FEATURE_FXSR,d&(1U<<24)); set_feature(CPU_FEATURE_SSE,d&(1U<<25));
 set_feature(CPU_FEATURE_SSE2,d&(1U<<26)); set_feature(CPU_FEATURE_SSE3,c&(1U<<0));
 set_feature(CPU_FEATURE_SSSE3,c&(1U<<9)); set_feature(CPU_FEATURE_SSE41,c&(1U<<19));
 set_feature(CPU_FEATURE_SSE42,c&(1U<<20)); set_feature(CPU_FEATURE_AES,c&(1U<<25));
 set_feature(CPU_FEATURE_XSAVE,c&(1U<<26)); set_feature(CPU_FEATURE_OSXSAVE,c&(1U<<27));
 set_feature(CPU_FEATURE_AVX,c&(1U<<28)); set_feature(CPU_FEATURE_FMA,c&(1U<<12));
 set_feature(CPU_FEATURE_X2APIC,c&(1U<<21)); set_feature(CPU_FEATURE_RDRAND,c&(1U<<30));
 cpu_state.cache_line_bytes=((r->ebx>>8U)&0xFFU)*8U;
 cpu_state.logical_processors=(r->ebx>>16U)&0xFFU;
 cpu_state.initial_apic_id=(r->ebx>>24U)&0xFFU;
 cpu_state.threads_per_core=1U;
 cpu_state.cores_per_package=cpu_state.logical_processors ? cpu_state.logical_processors : 1U;
}
static void decode_structured_features(const cpuid_regs_t* r)
{
 uint32_t b=r->ebx;
 set_feature(CPU_FEATURE_AVX2,b&(1U<<5)); set_feature(CPU_FEATURE_BMI1,b&(1U<<3));
 set_feature(CPU_FEATURE_BMI2,b&(1U<<8)); set_feature(CPU_FEATURE_INVPCID,b&(1U<<10));
 set_feature(CPU_FEATURE_RDSEED,b&(1U<<18));
}

/*
 * Intel's 0x1F/0x0B leaves describe hierarchical logical-processor counts.
 * Treat malformed or empty levels as unavailable and retain the leaf-1
 * fallback rather than manufacturing a topology from partial data.
 */
static int decode_topology_leaf(uint32_t leaf)
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
 cpu_state.initial_apic_id=apic_id;
 cpu_state.logical_processors=package_logical;
 cpu_state.threads_per_core=threads;
 cpu_state.cores_per_package=package_logical/threads;
 if (cpu_state.cores_per_package==0U) cpu_state.cores_per_package=1U;
 cpu_state.topology_leaf=(uint8_t)(leaf==0x1FU ? 0x1FU : 0x0BU);
 return 1;
}

static int decode_cache_leaf(uint32_t leaf)
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
  cpu_cache_info_t* cache=&cpu_state.caches[count++];
  cache->level=(uint8_t)level;
  cache->type=(uint8_t)type; /* 1=data, 2=instruction, 3=unified */
  cache->line_bytes=(uint16_t)line;
  cache->ways=(uint16_t)ways;
  cache->sets=(uint32_t)sets;
  cache->size_bytes=size;
 }
 cpu_state.cache_count=count;
 return count!=0U;
}
static void decode_cache_geometry(void)
{
 if (cpu_state.max_basic_leaf>=4U && decode_cache_leaf(4U)) return;
 if (cpu_state.max_extended_leaf>=0x8000001DU)
  (void)decode_cache_leaf(0x8000001DU);
}

static void decode_topology(void)
{
 if (cpu_state.max_basic_leaf>=0x1FU && decode_topology_leaf(0x1FU)) return;
 if (cpu_state.max_basic_leaf>=0x0BU && decode_topology_leaf(0x0BU)) return;
 cpu_state.topology_leaf=0U;
}

static void decode_extended_features(void)
{
 cpuid_regs_t r;
 if (cpu_state.max_extended_leaf<0x80000001U ||
     !cpuid_read(0x80000001U,0U,&r)) return;
 set_feature(CPU_FEATURE_SYSCALL,r.edx&(1U<<11));
 set_feature(CPU_FEATURE_NX,r.edx&(1U<<20));
 set_feature(CPU_FEATURE_PAGE_1G,r.edx&(1U<<26));
 set_feature(CPU_FEATURE_RDTSCP,r.edx&(1U<<27));
 set_feature(CPU_FEATURE_LONG_MODE,r.edx&(1U<<29));
 set_feature(CPU_FEATURE_LAHF_LM,r.ecx&(1U<<0));
 if (cpu_state.max_extended_leaf>=0x80000007U &&
     cpuid_read(0x80000007U,0U,&r))
  set_feature(CPU_FEATURE_INVARIANT_TSC,r.edx&(1U<<8));
 if (cpu_state.max_extended_leaf>=0x80000008U &&
     cpuid_read(0x80000008U,0U,&r)) {
  cpu_state.physical_address_bits=(uint8_t)(r.eax&0xFFU);
  cpu_state.virtual_address_bits=(uint8_t)((r.eax>>8U)&0xFFU);
 }
}
int cpu_features_initialize(void)
{
 cpuid_regs_t r;
 cpu_state_valid=0;
 for (size_t i=0; i<sizeof(cpu_state); ++i) ((uint8_t*)&cpu_state)[i]=0;
 if (!cpuid_read(0U,0U,&r)) return 0;
 cpu_state.max_basic_leaf=r.eax;
 copy_vendor(r.ebx,r.edx,r.ecx);
 if (!cpuid_read(0x80000000U,0U,&r)) return 0;
 cpu_state.max_extended_leaf=r.eax;
 if (cpu_state.max_basic_leaf>=1U && cpuid_read(1U,0U,&r)) {
  decode_signature(r.eax);
  decode_basic_features(&r);
 }
 if (cpu_state.max_basic_leaf>=7U && cpuid_read(7U,0U,&r))
  decode_structured_features(&r);
 decode_topology();
 decode_cache_geometry();
 decode_extended_features();
 /* AVX requires both OS-managed extended state and enabled XMM/YMM state. */
 if ((cpu_state.features & (CPU_FEATURE_AVX | CPU_FEATURE_XSAVE | CPU_FEATURE_OSXSAVE)) ==
     (CPU_FEATURE_AVX | CPU_FEATURE_XSAVE | CPU_FEATURE_OSXSAVE) &&
     (read_xcr0()&0x6U)==0x6U)
  set_feature(CPU_FEATURE_AVX_USABLE,1);
 cpu_state_valid=1;
 serial_write("VANTAOS_CPU_VENDOR="); serial_write(cpu_state.vendor); serial_write("\n");
 serial_write(cpu_feature_has(CPU_FEATURE_LONG_MODE) ?
              "VANTAOS_CPU_LONG_MODE=1\n" : "VANTAOS_CPU_LONG_MODE=0\n");
 serial_write(cpu_feature_has(CPU_FEATURE_NX) ?
              "VANTAOS_CPU_NX=1\n" : "VANTAOS_CPU_NX=0\n");
 serial_write(cpu_feature_has(CPU_FEATURE_APIC) ?
              "VANTAOS_CPU_APIC=1\n" : "VANTAOS_CPU_APIC=0\n");
 return 1;
}
const cpu_features_t* cpu_features_get(void) { return cpu_state_valid ? &cpu_state : 0; }
int cpu_feature_has(cpu_feature_t f)
{
 return cpu_state_valid && (cpu_state.features&(uint64_t)f)!=0U;
}
int cpu_feature_avx_usable(void) { return cpu_feature_has(CPU_FEATURE_AVX_USABLE); }
int cpu_feature_avx2_usable(void)
{
 return cpu_feature_avx_usable() && cpu_feature_has(CPU_FEATURE_AVX2);
}
