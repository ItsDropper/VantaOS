/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_bringup.h"
#include "nvx_survey.h"
#include "nvx_attach.h"
#include "nvx_fw_manifest.h"
#include "nvx_gsp_bundle.h"
#include "nvx_gsp_preflight.h"
#include "nvx_gsp_vbios.h"
#include "nvx_gsp_vbios_prom.h"
#include "nvx_resource.h"
#include "pci.h"
#include "vfs.h"
#include "heap.h"

#include <string.h>

static void out(const nvx_bringup_io_t *io, const char *s)
{
    if (io && io->text) io->text(s, io->context);
}

static void out_hex(const nvx_bringup_io_t *io, const char *label, uint64_t v)
{
    char b[48]; size_t n = 0U;
    static const char d[] = "0123456789abcdef";
    b[n++] = '0'; b[n++] = 'x';
    int started = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned nib = (unsigned)((v >> shift) & 0xFU);
        if (nib || started || shift == 0) { b[n++] = d[nib]; started = 1; }
    }
    b[n++] = '\n'; b[n] = '\0';
    out(io, label); out(io, b);
}

static int attach_first(const nvx_bringup_io_t *io, const char *tag, nvx_attached_device_t *dev)
{
    const struct pci_device *found = NULL;
    for (int i = 0, n = pci_get_device_count(); i < n; i++) {
        const struct pci_device *d = pci_get_device(i);
        if (d && nvx_match_pci_device(d)) { found = d; break; }
    }
    char t[32]; size_t n = strlen(tag);
    if (n + 3U > sizeof(t)) return 1;
    memcpy(t, tag, n); memcpy(t + n, ": ", 3U);
    if (!found) { out(io, t); out(io, "no supported NVIDIA GPU (RTX 3060) found on PCI.\n"); return 1; }
    out(io, t); out_hex(io, "PCI device id ", found->device_id);

    nvx_bar_resource_t bar0;
    nvx_resource_status_t rs = nvx_bar_resource_read(found, 0U, &bar0);
    if (rs != NVX_RESOURCE_OK) {
        out(io, t); out_hex(io, "BAR0 unusable, resource status ", (uint64_t)rs);
        return 1;
    }
    out(io, t); out_hex(io, "BAR0 base ", bar0.base);
    out(io, t); out_hex(io, "BAR0 size ", bar0.size);
    memset(dev, 0, sizeof(*dev));
    nvx_attach_status_t as = nvx_device_attach(found, &bar0, dev);
    if (as != NVX_ATTACH_OK) {
        out(io, t); out_hex(io, "attach failed, status ", (uint64_t)as);
        return 1;
    }
    return 0;
}

int nvx_bringup_status(const nvx_bringup_io_t *io)
{
    nvx_attached_device_t dev;
    if (attach_first(io, "nvx status", &dev)) return 1;
    out_hex(io, "nvx status: PMC_BOOT_0  ", dev.registers.pmc_boot_0);
    out_hex(io, "nvx status: PMC_BOOT_42 ", dev.registers.pmc_boot_42);
    out_hex(io, "nvx status: chip id     ", dev.registers.extended_chip_id);
    out(io, dev.registers.identity_mismatch
            ? "nvx status: WARNING PCI id and register identity disagree.\n"
            : "nvx status: register identity matches PCI identity.\n");
    nvx_device_detach(&dev);
    out(io, "nvx status: OK (read-only; no engine was started).\n");
    return 0;
}

int nvx_bringup_vbios(const nvx_bringup_io_t *io)
{
    nvx_attached_device_t dev;
    if (attach_first(io, "nvx vbios", &dev)) return 1;
    uint8_t *image = kmalloc(NVX_VBIOS_PROM_MAX_SIZE);
    if (!image) { nvx_device_detach(&dev); out(io, "nvx vbios: out of memory.\n"); return 1; }

    nvx_mmio_region_t bar0 = { (volatile uint8_t *)dev.bar0_mapping, (size_t)dev.bar0.size };
    size_t size = 0U, expansion = 0U;
    nvx_gsp_vbios_prom_status_t ps = nvx_gsp_vbios_read_from_prom(
        &bar0, image, NVX_VBIOS_PROM_MAX_SIZE, &size, &expansion);
    int rc = 1;
    if (ps != NVX_VBIOS_PROM_OK) {
        out_hex(io, "nvx vbios: PROM read failed, status ", (uint64_t)ps);
        out(io, "nvx vbios: the PROM window may be unreadable in this boot state; nothing was written.\n");
    } else {
        out_hex(io, "nvx vbios: image bytes ", size);
        out_hex(io, "nvx vbios: expansion ROM offset ", expansion);
        nvx_gsp_vbios_fwsec_info_t info;
        memset(&info, 0, sizeof(info));
        nvx_gsp_vbios_status_t vs = nvx_gsp_vbios_find_ga10x_fwsec(image, size, expansion, &info);
        if (vs != NVX_GSP_VBIOS_OK) {
            out_hex(io, "nvx vbios: FWSEC lookup failed, status ", (uint64_t)vs);
        } else {
            out_hex(io, "nvx vbios: FWSEC image offset ", info.image_offset);
            out_hex(io, "nvx vbios: FWSEC image size   ", info.image_size);
            out_hex(io, "nvx vbios: FWSEC ucode id     ", info.ucode_id);
            out_hex(io, "nvx vbios: FWSEC signatures   ", info.signature_count);
            out(io, "nvx vbios: OK. FWSEC located; it has NOT been executed.\n");
            rc = 0;
        }
    }
    kfree(image);
    nvx_device_detach(&dev);
    return rc;
}

int nvx_bringup_survey(const nvx_bringup_io_t *io)
{
    nvx_attached_device_t dev;
    if (attach_first(io, "nvx survey", &dev)) return 1;
    nvx_mmio_region_t bar0 = { (volatile uint8_t *)dev.bar0_mapping, (size_t)dev.bar0.size };
    int n = nvx_survey_run(&bar0, dev.pci_device->vendor_id, dev.pci_device->device_id, io);
    nvx_device_detach(&dev);
    return n < 0 ? 1 : 0;
}

/* ---- VFS provider -------------------------------------------------------- */

#define MANIFEST_MAX 16384U

static int vfs_request(const char *name, nvx_firmware_blob_t *blob, void *ctx)
{
    (void)ctx;
    char path[160]; size_t n = strlen(name);
    if (n + 2U > sizeof(path)) return 0;
    path[0] = '/'; memcpy(path + 1, name, n + 1U);
    int id = vfs_lookup(path);
    if (id < 0) return 0;
    const fs_node_t *node = vfs_get_node((uint32_t)id);
    if (!node || !node->size) return 0;
    uint8_t *buf = kmalloc(node->size);
    if (!buf) return 0;
    int h = vfs_open(path, VFS_OPEN_READ);
    if (h < 0) { kfree(buf); return 0; }
    uint32_t got = 0U;
    while (got < node->size) {
        int r = vfs_read(h, (char *)buf + got, node->size - got);
        if (r <= 0) break;
        got += (uint32_t)r;
    }
    vfs_close(h);
    if (got != node->size) { kfree(buf); return 0; }
    blob->data = buf; blob->size = got; blob->release_context = buf;
    return 1;
}

static int vfs_verify(const char *name, const nvx_firmware_blob_t *blob, void *ctx)
{
    (void)ctx;
    char dir[160]; size_t n = strlen(name), cut = 0U;
    for (size_t i = 0; i < n; i++) if (name[i] == '/') cut = i + 1U;
    if (cut + 10U >= sizeof(dir) - 2U) return 0;
    dir[0] = '/'; memcpy(dir + 1, name, cut); memcpy(dir + 1 + cut, "SHA256SUMS", 11U);
    int id = vfs_lookup(dir);
    if (id < 0) return 0;                       /* no pinned manifest: fail closed */
    const fs_node_t *node = vfs_get_node((uint32_t)id);
    if (!node || !node->size || node->size > MANIFEST_MAX) return 0;
    char *m = kmalloc(node->size);
    if (!m) return 0;
    int h = vfs_open(dir, VFS_OPEN_READ), ok = 0;
    if (h >= 0) {
        uint32_t got = 0U;
        while (got < node->size) {
            int r = vfs_read(h, m + got, node->size - got);
            if (r <= 0) break;
            got += (uint32_t)r;
        }
        vfs_close(h);
        ok = got == node->size &&
             nvx_fw_manifest_verify(m, got, name, blob->data, blob->size) == NVX_FW_MANIFEST_OK;
    }
    kfree(m);
    return ok;
}

static void vfs_release(nvx_firmware_blob_t *blob, void *ctx)
{
    (void)ctx;
    if (blob && blob->release_context) kfree(blob->release_context);
    if (blob) { blob->data = NULL; blob->size = 0U; blob->release_context = NULL; }
}

nvx_firmware_provider_t nvx_bringup_vfs_provider(void)
{
    nvx_firmware_provider_t p = { vfs_request, vfs_verify, vfs_release, NULL };
    return p;
}

int nvx_bringup_firmware(const nvx_bringup_io_t *io, const nvx_firmware_provider_t *provider,
                         const char *chip, const char *version)
{
    static nvx_gsp_bundle_t bundle;      /* large; keep off the kernel stack */
    static nvx_gsp_preflight_report_t report;
    memset(&bundle, 0, sizeof(bundle)); memset(&report, 0, sizeof(report));

    nvx_gsp_bundle_status_t bs = nvx_gsp_bundle_acquire(provider, NVX_FAMILY_AMPERE,
                                                        chip, version, &bundle);
    if (bs != NVX_GSP_BUNDLE_OK) {
        out_hex(io, "nvx fw: bundle acquire failed, status ", (uint64_t)bs);
        out(io, "nvx fw: need nvidia/<chip>/gsp/{gsp,ucodes,booter_load,booter_unload,bootloader}-<version>.bin\n"
                "        plus a SHA256SUMS you pinned in the same directory.\n");
        return 1;
    }
    out_hex(io, "nvx fw: 5 files loaded and manifest-verified, total bytes ", bundle.total_bytes);
    nvx_gsp_preflight_status_t ps = nvx_gsp_preflight_validate(&bundle, &report);
    int rc = 0;
    if (ps != NVX_GSP_PREFLIGHT_OK) {
        out_hex(io, "nvx fw: structural preflight failed, status ", (uint64_t)ps);
        rc = 1;
    } else {
        out(io, "nvx fw: structural preflight OK (ELF/containers parse, versions agree).\n");
        out(io, "nvx fw: version "); out(io, report.version); out(io, "\n");
        out(io, "nvx fw: OK. The GPU was not touched; GSP has NOT been started.\n");
    }
    nvx_gsp_bundle_release(&bundle);
    return rc;
}
