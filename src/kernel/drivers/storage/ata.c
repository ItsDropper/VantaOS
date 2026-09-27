#include "ata.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_CONTROL     0x3F6

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ_PIO 0x20

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

static int available;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void ata_wait_400ns(void)
{
    (void)inb(ATA_CONTROL);
    (void)inb(ATA_CONTROL);
    (void)inb(ATA_CONTROL);
    (void)inb(ATA_CONTROL);
}

static int ata_wait_ready(void)
{
    uint8_t status;

    for (unsigned int i = 0; i < 1000000; i++)
    {
        status = inb(ATA_STATUS);

        if (!(status & ATA_STATUS_BSY))
            return (status & ATA_STATUS_ERR) ? 0 : 1;
    }

    return 0;
}

void ata_initialize(void)
{
    available = 0;

    outb(ATA_DRIVE, 0xA0);
    ata_wait_400ns();

    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_STATUS);

    if (status == 0)
        return;

    if (!ata_wait_ready())
        return;

    for (unsigned int i = 0; i < 1000000; i++)
    {
        status = inb(ATA_STATUS);

        if (status & ATA_STATUS_ERR)
            return;

        if (status & ATA_STATUS_DRQ)
        {
            for (unsigned int word = 0; word < 256; word++)
                (void)inw(ATA_DATA);

            available = 1;
            return;
        }
    }
}

int ata_is_available(void)
{
    return available;
}

int ata_read_sectors(
    uint32_t lba,
    uint8_t count,
    void* buffer
)
{
    if (!available || count == 0 || !buffer)
        return 0;

    if (lba > 0x0FFFFFFFU ||
        (uint32_t)count > 0x100)
        return 0;

    uint8_t* destination = (uint8_t*)buffer;

    outb(ATA_DRIVE,
         (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));

    outb(ATA_SECTOR_COUNT, count);
    outb(ATA_LBA_LOW, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, ATA_CMD_READ_PIO);

    for (unsigned int sector = 0; sector < count; sector++)
    {
        if (!ata_wait_ready())
            return 0;

        uint8_t status = inb(ATA_STATUS);

        if (!(status & ATA_STATUS_DRQ))
            return 0;

        for (unsigned int word = 0; word < 256; word++)
        {
            uint16_t value = inw(ATA_DATA);
            destination[sector * 512 + word * 2] =
                (uint8_t)value;
            destination[sector * 512 + word * 2 + 1] =
                (uint8_t)(value >> 8);
        }
    }

    ata_wait_400ns();
    return 1;
}
