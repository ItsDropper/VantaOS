#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hda.h"
#include "mmio.h"
static uint8_t regs[0x1000];static uint16_t command=2U;static uint64_t bar=0xFEBF0000ULL;
static int io,bar_ok=1,map_fail,unmap_fail;static unsigned maps,unmaps;
uint16_t pci_read_config16(const struct pci_device*d,uint8_t o){(void)d;assert(o==4U);return command;}
int pci_get_bar(const struct pci_device*d,uint8_t i,uint64_t*a,int*x,int*w){(void)d;if(!bar_ok||i)return 0;*a=bar;*x=io;*w=0;return 1;}
void *mmio_map_region(uint64_t a,size_t n){++maps;return map_fail||a!=bar||n!=sizeof(regs)?NULL:regs;}
int mmio_unmap_region(void*p,size_t n){++unmaps;assert(p==regs&&n==sizeof(regs));return !unmap_fail;}
static struct pci_device dev(void){struct pci_device d;memset(&d,0,sizeof(d));d.class_code=4;d.subclass=3;return d;}
static void reset(void){memset(regs,0,sizeof(regs));command=2;bar=0xFEBF0000ULL;io=0;bar_ok=1;map_fail=unmap_fail=0;maps=unmaps=0;regs[0]=0x34;regs[1]=0x12;regs[2]=1;regs[3]=2;regs[8]=1;regs[14]=5;}
int main(void){
 struct pci_device d=dev();hda_controller_info_t out;
 reset();d.class_code=2;assert(hda_probe_controller(&d,&out)==HDA_PROBE_UNSUPPORTED_FUNCTION);assert(!maps);
 d=dev();reset();command=0;assert(hda_probe_controller(&d,&out)==HDA_PROBE_DECODE_DISABLED);assert(!maps&&command==0);
 reset();io=1;assert(hda_probe_controller(&d,&out)==HDA_PROBE_INVALID_BAR);assert(!maps);
 reset();map_fail=1;assert(hda_probe_controller(&d,&out)==HDA_PROBE_MAPPING_FAILED);assert(!unmaps);
 reset();regs[0]=regs[1]=0xFF;assert(hda_probe_controller(&d,&out)==HDA_PROBE_INVALID_REGISTERS);assert(unmaps==1);
 reset();assert(hda_probe_controller(&d,&out)==HDA_PROBE_OK);assert(out.register_base==bar&&out.capabilities==0x1234);assert(out.version_major==2&&out.version_minor==1);assert(out.codec_presence==5&&!out.controller_in_reset);assert(maps==1&&unmaps==1&&command==2);
 reset();regs[8]=0;assert(hda_probe_controller(&d,&out)==HDA_PROBE_OK&&out.controller_in_reset);
 reset();unmap_fail=1;assert(hda_probe_controller(&d,&out)==HDA_PROBE_UNMAP_FAILED);assert(out.register_base==0);
 puts("HDA passive controller and codec inventory tests passed.");return 0;
}
