#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "smbios.h"

static char log_buffer[8192];
static size_t log_length;

void serial_write(const char* text)
{
    size_t length = strlen(text);
    assert(length <= sizeof(log_buffer) - log_length - 1U);
    memcpy(log_buffer + log_length, text, length);
    log_length += length;
    log_buffer[log_length] = '\0';
}

void serial_write_bytes(const char* data, size_t length)
{
    assert(length <= sizeof(log_buffer) - log_length - 1U);
    memcpy(log_buffer + log_length, data, length);
    log_length += length;
    log_buffer[log_length] = '\0';
}

static uint8_t checksum(const uint8_t* bytes, size_t length)
{
    uint8_t sum = 0U;
    for (size_t i = 0U; i < length; ++i)
        sum = (uint8_t)(sum + bytes[i]);
    return (uint8_t)(0U - sum);
}

int main(void)
{
    const uintptr_t scan_address = 0xF0000U;
    const uintptr_t table_address = 0x100000U;
    const size_t mapping_size = 0x11000U;
    uint8_t* memory = mmap((void*)scan_address, mapping_size,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                           -1, 0);
    if (memory == MAP_FAILED)
    {
        perror("mmap SMBIOS test region");
        return 1;
    }
    memset(memory, 0, mapping_size);

    uint8_t* table = (uint8_t*)table_address;
    const uint8_t system_info[] = {
        1U, 8U, 0U, 0U, 1U, 2U, 3U, 4U,
        'L','e','n','o','v','o',0U,
        'I','d','e','a','P','a','d',' ','5','3','0','S',0U,
        'R','e','v','1',0U,
        'D','O','_','N','O','T','_','L','O','G','_','T','H','I','S',
        '_','S','E','R','I','A','L',0U,0U
    };
    memcpy(table, system_info, sizeof(system_info));
    size_t offset = sizeof(system_info);
    const uint8_t end_marker[] = {127U, 4U, 0U, 0U, 0U, 0U};
    memcpy(table + offset, end_marker, sizeof(end_marker));
    offset += sizeof(end_marker);

    uint8_t* entry = memory;
    memcpy(entry, "_SM_", 4U);
    entry[5] = 31U;
    entry[6] = 3U;
    entry[7] = 2U;
    entry[16] = '_'; entry[17] = 'D'; entry[18] = 'M';
    entry[19] = 'I'; entry[20] = '_';
    entry[22] = (uint8_t)offset;
    entry[23] = (uint8_t)(offset >> 8U);
    entry[24] = (uint8_t)table_address;
    entry[25] = (uint8_t)(table_address >> 8U);
    entry[26] = (uint8_t)(table_address >> 16U);
    entry[27] = (uint8_t)(table_address >> 24U);
    entry[28] = 2U;
    entry[29] = 0U;
    entry[21] = checksum(entry + 16U, 15U);
    entry[4] = checksum(entry, 31U);

    smbios_inventory_initialize();
    assert(strstr(log_buffer, "VANTAOS_HW_SMBIOS_VERSION=3.2") != NULL);
    assert(strstr(log_buffer, "manufacturer=Lenovo") != NULL);
    assert(strstr(log_buffer, "product=IdeaPad 530S") != NULL);
    assert(strstr(log_buffer, "DO_NOT_LOG_THIS_SERIAL") == NULL);

    /* Exercise the SMBIOS 3.x 64-bit entrypoint as well. */
    log_length = 0U;
    log_buffer[0] = '\0';
    memset(entry, 0, 31U);
    memcpy(entry, "_SM3_", 5U);
    entry[6] = 24U;
    entry[7] = 3U;
    entry[8] = 5U;
    entry[12] = 0x00U;
    entry[13] = 0x10U;
    entry[16] = (uint8_t)table_address;
    entry[17] = (uint8_t)(table_address >> 8U);
    entry[18] = (uint8_t)(table_address >> 16U);
    entry[19] = (uint8_t)(table_address >> 24U);
    entry[5] = checksum(entry, 24U);
    smbios_inventory_initialize();
    assert(strstr(log_buffer, "VANTAOS_HW_SMBIOS_VERSION=3.5") != NULL);
    assert(strstr(log_buffer, "product=IdeaPad 530S") != NULL);

    /* A corrupt entrypoint must fail closed rather than dereference a table. */
    log_length = 0U;
    log_buffer[0] = '\0';
    entry[5] ^= 0x80U;
    smbios_inventory_initialize();
    assert(strstr(log_buffer, "VANTAOS_HW_SMBIOS_UNAVAILABLE") != NULL);

    assert(munmap(memory, mapping_size) == 0);
    puts("SMBIOS inventory tests passed");
    return 0;
}
