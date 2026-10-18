#include "rtl8188eu.h"
#include "xhci.h"
#include "fat32.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

static uint8_t configured_in;
static uint8_t configured_out;
static uint16_t packet_in;
static uint16_t packet_out;

int xhci_controller_online(void) { return 1; }
int xhci_configure_bulk_endpoint(uint32_t slot, uint8_t ep, uint16_t packet)
{
    (void)slot;
    if (ep & 0x80U) { configured_in = ep; packet_in = packet; }
    else { configured_out = ep; packet_out = packet; }
    return 1;
}
int xhci_disable_device(uint32_t slot) { (void)slot; return 1; }
int xhci_control_transfer(uint32_t slot, const uint8_t setup[8],
                          void *data, uint16_t length)
{
    (void)slot;
    if ((setup[0] & 0x80U) && data) {
        uint8_t *bytes = data;
        for (uint16_t i = 0; i < length; ++i) bytes[i] = 0;
    }
    return 1;
}
int xhci_bulk_transfer(uint32_t slot, uint8_t ep, void *buffer,
                       uint32_t length, uint32_t *actual)
{
    (void)slot; (void)ep; (void)buffer; (void)length; (void)actual;
    return 0;
}

int xhci_start_bulk_in(uint32_t slot, uint8_t ep)
{ (void)slot; (void)ep; return 1; }
int xhci_take_bulk_in(uint32_t slot, uint8_t ep, void *buffer,
                      uint32_t capacity, uint32_t *actual)
{
    (void)slot; (void)ep; (void)buffer; (void)capacity; (void)actual;
    return 0;
}
int fat32_is_mounted(void) { return 0; }
uint32_t fat32_root_cluster(void) { return 0; }
int fat32_list_directory(uint32_t c, fat32_dirent_t *e, unsigned int n)
{ (void)c; (void)e; (void)n; return -1; }
int fat32_read_file(uint32_t c, uint32_t s, char *b, unsigned int n)
{ (void)c; (void)s; (void)b; (void)n; return -1; }
void serial_write(const char *s) { (void)s; }
void serial_write_bytes(const char *s, size_t n) { (void)s; (void)n; }

int main(void)
{
    /* Vendor interface, one bulk IN (0x81) and one bulk OUT (0x02). */
    const uint8_t valid[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 0xFF, 0, 0, 0,
        7, 5, 0x81, 2, 64, 0, 0,
        7, 5, 0x02, 2, 64, 0, 0
    };
    const uint8_t malformed[] = {
        9, 2, 16, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 0xFF, 0, 0, 0,
        7, 5, 0x81, 2, 64, 0, 0
    };
    if (rtl8188eu_probe(1, 1, 0x0BDA, 0x8179, malformed,
                        sizeof(malformed)) != 0)
        return 1;
    if (rtl8188eu_probe(1, 1, 0x1234, 0x8179, valid, sizeof(valid)) != 0)
        return 2;
    if (rtl8188eu_probe(1, 1, 0x0BDA, 0x8179, valid, sizeof(valid)) != 1)
        return 3;
    if (configured_in != 0x81 || configured_out != 0x02 ||
        packet_in != 64 || packet_out != 64)
        return 4;
    return 0;
}
