#define _GNU_SOURCE
#include "acpi.h"
#include "vanta_boot_info.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
const vanta_boot_info_t* kernel_get_boot_info(void) { return NULL; }
void serial_write(const char* s) { (void)s; }
void serial_write_bytes(const char* s, size_t n) { (void)s; (void)n; }
static void checksum(uint8_t* p, size_t n, size_t at)
{
    p[at] = 0U; uint8_t sum = 0U;
    for (size_t i = 0U; i < n; ++i) sum = (uint8_t)(sum + p[i]);
    p[at] = (uint8_t)(0U - sum);
}
static void table(uint8_t* p, const char* sig, uint32_t n)
{
    memset(p, 0, n); memcpy(p, sig, 4U); *(uint32_t*)(p + 4U) = n;
}
int main(void)
{
    uint8_t* mem = mmap((void*)0xE0000U, 0x80000U,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |
        MAP_FIXED_NOREPLACE, -1, 0);
    if (mem == MAP_FAILED) { perror("ACPI mmap"); return 1; }
    memset(mem, 0, 0x80000U);
    uint8_t* rsdp=mem, *rsdt=(uint8_t*)0x101000U;
    uint8_t* fadt=(uint8_t*)0x102000U, *dsdt=(uint8_t*)0x103000U;
    uint8_t* ssdt=(uint8_t*)0x104000U;
    table(rsdt,"RSDT",44U); *(uint32_t*)(rsdt+36U)=0x102000U;
    *(uint32_t*)(rsdt+40U)=0x104000U; checksum(rsdt,44U,9U);
    table(fadt,"FACP",148U); *(uint32_t*)(fadt+40U)=0x103000U;
    *(uint16_t*)(fadt+46U)=9U; *(uint64_t*)(fadt+140U)=0x103000U;
    checksum(fadt,148U,9U);
    table(dsdt,"DSDT",40U); checksum(dsdt,40U,9U);
    table(ssdt,"SSDT",40U); checksum(ssdt,40U,9U);
    memcpy(rsdp,"RSD PTR ",8U); rsdp[15U]=1U;
    *(uint32_t*)(rsdp+16U)=0x101000U; checksum(rsdp,20U,8U);

    acpi_table_inventory_t info;
    assert(acpi_discover_tables(&info));
    assert(info.dsdt_address==0x103000U && info.dsdt_length==40U);
    assert(info.sci_interrupt==9U && info.fadt_revision==6U);
    assert(info.ssdt_count==1U && info.ssdt_total==1U);
    assert(info.ssdt_addresses[0]==0x104000U && info.ssdt_lengths[0]==40U);

    /* An invalid extended pointer must fall back to the legacy DSDT. */
    *(uint64_t*)(fadt+140U)=0x105000U; checksum(fadt,148U,9U);
    assert(acpi_discover_tables(&info) && info.dsdt_address==0x103000U);
    dsdt[9U]^=0x80U; assert(!acpi_discover_tables(&info)); dsdt[9U]^=0x80U;
    rsdt[9U]^=1U; assert(!acpi_discover_tables(&info));
    assert(!acpi_discover_tables(NULL));
    assert(munmap(mem,0x80000U)==0);
    puts("ACPI table discovery tests passed");
    return 0;
}
