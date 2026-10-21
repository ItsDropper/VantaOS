/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_SYSTEM_INFO_H
#define VANTA_NVX_GSP_SYSTEM_INFO_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_nvkv.h"
#include "pci.h"

/* Presence bits describe facts actually collected by the caller. */
#define NVX_GSP_SI_PCI_IDENTITY          (UINT32_C(1) << 0)
#define NVX_GSP_SI_PCI_CONFIG_MIRROR     (UINT32_C(1) << 1)
#define NVX_GSP_SI_HOST_IDENTITY         (UINT32_C(1) << 2)
#define NVX_GSP_SI_HOST_PAGE_SIZE        (UINT32_C(1) << 3)
#define NVX_GSP_SI_PLATFORM_FLAGS        (UINT32_C(1) << 4)
#define NVX_GSP_SI_GPU_PHYS_ADDRESSES    (UINT32_C(1) << 5)
#define NVX_GSP_SI_DOMAIN_SURFACES       (UINT32_C(1) << 6)
#define NVX_GSP_SI_ATOMICS_CONSOLE_VA    (UINT32_C(1) << 7)
#define NVX_GSP_SI_TIMER_OFFSET          (UINT32_C(1) << 8)
#define NVX_GSP_SI_FHB_BUSINFO           (UINT32_C(1) << 9)
#define NVX_GSP_SI_NV_DOMAIN_BDF         (UINT32_C(1) << 10)
#define NVX_GSP_SI_SIM_ACCESS_ADDRESS    (UINT32_C(1) << 11)
#define NVX_GSP_SI_NOTIFY_SURFACE        (UINT32_C(1) << 12)
#define NVX_GSP_SI_PCIE_ATOMICS_MASK     (UINT32_C(1) << 13)
#define NVX_GSP_SI_CONSOLE_MEMORY_SIZE   (UINT32_C(1) << 14)
#define NVX_GSP_SI_MAX_USER_VA           (UINT32_C(1) << 15)
#define NVX_GSP_SI_FHB_BUSINFO_ONLY      (UINT32_C(1) << 16)
#define NVX_GSP_SI_CHIPSET_BUSINFO_ONLY  (UINT32_C(1) << 17)

typedef struct nvx_gsp_system_info {
    uint32_t present;

    uint32_t pci_device_id;
    uint32_t pci_sub_device_id;
    uint32_t pci_revision_id;

    uint32_t pci_config_mirror_base;
    uint32_t pci_config_mirror_size;

    uint32_t host_arch; /* NVGMC_SI_HOST_ARCH_* wire value, not an OS enum. */
    uint32_t host_os_type; /* NVGMC_SI_HOST_OS_TYPE_* wire value. */
    uint32_t host_page_size;

    uint64_t pcie_flags;    /* NVGMC_SI_PCIE_FLAGS key 0x1000. */
    uint64_t platform_flags;/* NVGMC_SI_PLATFORM_FLAGS key 0x1001. */
    uint64_t feature_flags; /* NVGMC_SI_FEATURE_FLAGS key 0x1002. */

    uint64_t gpu_phys_addr;
    uint64_t gpu_phys_fb_addr;
    uint64_t gpu_phys_inst_addr;
    uint64_t gpu_phys_io_addr;

    uint64_t nv_domain_bus_device_func;
    uint64_t sim_access_buf_phys_addr;
    uint64_t notify_op_shared_surface_phys_addr;

    uint64_t pcie_atomics_op_mask;
    uint64_t console_mem_size;
    uint64_t max_user_va;
    uint64_t sys_timer_offset_ns;

    uint64_t fhb_businfo;
    uint64_t chipset_businfo;
} nvx_gsp_system_info_t;

/*
 * Encode only groups marked present. No absent platform data is replaced with
 * guessed values. The caller must include every key required by its matching
 * GSP firmware branch and use the exact NVGMC_SI_* wire values.
 */
/*
 * Merge trusted GPU/platform facts with PCI identity read from the real PCI
 * function and host architecture/page size from the VantaOS build. Facts may
 * not override PCI or host fields; unknown GPU addresses/flags remain absent.
 */
nvx_gsp_nvkv_status_t nvx_gsp_system_info_collect_pci(
    const struct pci_device *device,
    const nvx_gsp_system_info_t *trusted_gpu_facts,
    nvx_gsp_system_info_t *out_info);

nvx_gsp_nvkv_status_t nvx_gsp_system_info_encode(
    const nvx_gsp_system_info_t *info, void *buffer, size_t capacity,
    size_t *out_size_bytes);

#endif
