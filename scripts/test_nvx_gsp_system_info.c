/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_system_info.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

uint32_t pci_read_config32(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    if(offset==0x08U)return UINT32_C(0x030000A5);
    if(offset==0x2CU)return UINT32_C(0x56781234);
    return UINT32_MAX;
}

static uint64_t le64(const uint8_t *p)
{
    uint64_t v=0U;
    for (unsigned i=0U;i<8U;i++) v|=(uint64_t)p[i]<<(i*8U);
    return v;
}

int main(void)
{
    uint8_t output[256];
    uint8_t small[8];
    size_t size=123U;
    nvx_gsp_system_info_t info={0};

    assert(nvx_gsp_system_info_encode(&info,output,sizeof(output),&size)==
           NVX_GSP_NVKV_INVALID_ARGUMENT);
    assert(size==0U);

    info.present=NVX_GSP_SI_PCI_IDENTITY|NVX_GSP_SI_HOST_IDENTITY|
                 NVX_GSP_SI_HOST_PAGE_SIZE|NVX_GSP_SI_GPU_PHYS_ADDRESSES|
                 NVX_GSP_SI_PLATFORM_FLAGS;
    info.pci_device_id=0x2503U;
    info.pci_sub_device_id=0x1234U;
    info.pci_revision_id=0xA5U;
    info.host_arch=1U;
    info.host_os_type=0U;
    info.host_page_size=4096U;
    info.pcie_flags=UINT64_C(0x5);
    info.platform_flags=UINT64_C(0x12);
    info.feature_flags=UINT64_C(0x41);
    info.gpu_phys_addr=UINT64_C(0x100000000);
    info.gpu_phys_fb_addr=UINT64_C(0x200000000);
    info.gpu_phys_inst_addr=UINT64_C(0x300000000);
    info.gpu_phys_io_addr=UINT64_C(0x400000000);

    memset(output,0xCC,sizeof(output));
    assert(nvx_gsp_system_info_encode(&info,output,sizeof(output),&size)==
           NVX_GSP_NVKV_OK);
    assert(size>0U && (size%8U)==0U);
    assert(nvx_gsp_nvkv_validate(output,size)==NVX_GSP_NVKV_OK);

    /* PCI identity SEQ32 instruction and first packed pair. */
    assert((le64(output)&UINT64_C(0xFFFF))==UINT64_C(1));
    assert(((le64(output)>>28U)&UINT64_C(0xF))==UINT64_C(1));
    assert(output[8U]==0x03U && output[9U]==0x25U);
    assert(output[12U]==0x34U && output[13U]==0x12U);
    assert(output[16U]==0xA5U);

    /* No caller-presence bit means no corresponding key is emitted. */
    info.present=NVX_GSP_SI_HOST_PAGE_SIZE;
    assert(nvx_gsp_system_info_encode(&info,output,sizeof(output),&size)==
           NVX_GSP_NVKV_OK);
    assert(size==8U);
    assert((le64(output)&UINT64_C(0xFFFF))==UINT64_C(0x78));
    assert((uint32_t)(le64(output)>>32U)==4096U);

    size=99U;
    assert(nvx_gsp_system_info_encode(&info,small,sizeof(small),&size)==
           NVX_GSP_NVKV_OK);
    assert(size==8U);

    info.present=NVX_GSP_SI_PCI_IDENTITY|NVX_GSP_SI_GPU_PHYS_ADDRESSES;
    size=99U;
    assert(nvx_gsp_system_info_encode(&info,small,sizeof(small),&size)==
           NVX_GSP_NVKV_NO_SPACE);
    assert(size==0U);

    {
        struct pci_device pci={0};
        nvx_gsp_system_info_t facts={0},collected={0};
        pci.vendor_id=0x10DEU;pci.device_id=0x2503U;
        facts.present=NVX_GSP_SI_GPU_PHYS_ADDRESSES|NVX_GSP_SI_PLATFORM_FLAGS;
        facts.gpu_phys_addr=UINT64_C(0x10000000);
        facts.gpu_phys_fb_addr=UINT64_C(0x20000000);
        facts.gpu_phys_inst_addr=UINT64_C(0x30000000);
        facts.gpu_phys_io_addr=UINT64_C(0x40000000);
        facts.pcie_flags=UINT64_C(0x4);
        assert(nvx_gsp_system_info_collect_pci(&pci,&facts,&collected)==NVX_GSP_NVKV_OK);
        assert((collected.present&NVX_GSP_SI_PCI_IDENTITY)!=0U);
        assert(collected.pci_device_id==0x2503U);
        assert(collected.pci_sub_device_id==0x5678U);
        assert(collected.pci_revision_id==0xA5U);
        assert((collected.present&NVX_GSP_SI_HOST_IDENTITY)!=0U);
        assert(collected.host_arch==1U && collected.host_os_type==0U);
        assert(collected.host_page_size==4096U);
        assert((collected.present&NVX_GSP_SI_GPU_PHYS_ADDRESSES)!=0U);
        assert(nvx_gsp_system_info_encode(&collected,output,sizeof(output),&size)==NVX_GSP_NVKV_OK);
        pci.vendor_id=0x8086U;
        assert(nvx_gsp_system_info_collect_pci(&pci,&facts,&collected)==NVX_GSP_NVKV_INVALID_ARGUMENT);
    }
    return 0;
}
