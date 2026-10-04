#include "ata.h"

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ_PIO 0x20

#define ATA_STATUS_BSY   0x80
#define ATA_STATUS_DRQ   0x08
#define ATA_STATUS_ERR   0x01
#define ATA_STATUS_DF    0x20

typedef struct
{
    uint16_t base;
    uint16_t control;
} ata_channel_t;

static int available;
static ata_channel_t active_channel;
static uint8_t active_drive;
static uint8_t last_status;
static uint8_t last_error;
static uint8_t last_signature_mid;
static uint8_t last_signature_high;

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

static void ata_wait_400ns(const ata_channel_t* channel)
{
    (void)inb(channel->control);
    (void)inb(channel->control);
    (void)inb(channel->control);
    (void)inb(channel->control);
}

static int ata_wait_not_busy(const ata_channel_t* channel)
{
    for (unsigned int i = 0; i < 1000000; i++)
    {
        uint8_t status = inb(channel->base + 7);

        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF))
        {
            last_status = status;
            last_error = inb(channel->base + 1);
            return 0;
        }

        if (!(status & ATA_STATUS_BSY))
        {
            last_status = status;
            return 1;
        }
    }

    last_status = inb(channel->base + 7);
    last_error = inb(channel->base + 1);
    return 0;
}

static int ata_wait_data(const ata_channel_t* channel)
{
    for (unsigned int i = 0; i < 1000000; i++)
    {
        uint8_t status = inb(channel->base + 7);

        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF))
        {
            last_status = status;
            last_error = inb(channel->base + 1);
            return 0;
        }

        if (status & ATA_STATUS_DRQ)
        {
            last_status = status;
            return 1;
        }
    }

    last_status = inb(channel->base + 7);
    last_error = inb(channel->base + 1);
    return 0;
}

static int ata_probe_device(
    const ata_channel_t* channel,
    uint8_t drive
)
{
    outb(channel->base + 6, drive);
    ata_wait_400ns(channel);

    uint8_t status = inb(channel->base + 7);
    last_status = status;

    if (status == 0x00 || status == 0xFF)
        return 0;

    if (!ata_wait_not_busy(channel))
        return 0;

    outb(channel->base + 2, 0);
    outb(channel->base + 3, 0);
    outb(channel->base + 4, 0);
    outb(channel->base + 5, 0);
    outb(channel->base + 7, ATA_CMD_IDENTIFY);

    status = inb(channel->base + 7);
    last_status = status;

    if (status == 0x00 || status == 0xFF)
        return 0;

    /*
     * The device signature is available after IDENTIFY has been issued
     * and BSY has cleared. 0x14/0xEB identifies an ATAPI device.
     */
    if (!ata_wait_not_busy(channel))
        return 0;

    last_signature_mid = inb(channel->base + 4);
    last_signature_high = inb(channel->base + 5);

    if (last_signature_mid != 0 || last_signature_high != 0)
        return 0;

    if (!ata_wait_data(channel))
        return 0;

    for (unsigned int word = 0; word < 256; word++)
        (void)inw(channel->base);

    active_channel = *channel;
    active_drive = drive;
    available = 1;
    return 1;
}

void ata_initialize(void)
{
    static const ata_channel_t channels[] =
    {
        {0x1F0, 0x3F6},
        {0x170, 0x376}
    };

    available = 0;
    active_channel.base = 0;
    active_channel.control = 0;
    active_drive = 0xA0;
    last_status = 0;
    last_error = 0;
    last_signature_mid = 0;
    last_signature_high = 0;

    /*
     * QEMU's optical ISO can occupy the primary IDE master while
     * vantaos.img is attached to the secondary IDE channel. Probe
     * both channels instead of assuming the disk is primary master.
     */
    static const uint8_t drives[] = {0xA0, 0xB0};

    for (unsigned int i = 0; i < 2; i++)
    {
        for (unsigned int j = 0; j < 2; j++)
        {
            if (ata_probe_device(&channels[i], drives[j]))
                return;
        }
    }
}

int ata_is_available(void)
{
    return available;
}

uint8_t ata_status(void)
{
    return last_status;
}

uint8_t ata_error(void)
{
    return last_error;
}

uint8_t ata_signature_mid(void)
{
    return last_signature_mid;
}

uint8_t ata_signature_high(void)
{
    return last_signature_high;
}

int ata_read_sectors(
    uint32_t lba,
    uint8_t count,
    void* buffer
)
{
    if (!available || count == 0 || !buffer)
        return 0;

    if (lba > 0x0FFFFFFFU)
        return 0;

    uint8_t* destination = (uint8_t*)buffer;

    /*
     * IDENTIFY uses 0xA0/0xB0, but READ SECTORS must set the
     * LBA bit (0x40). Preserve the selected master/slave bit and
     * add the upper four LBA bits.
     */
    outb(active_channel.base + 6,
         (uint8_t)(active_drive |
                   0x40 |
                   ((lba >> 24) & 0x0F)));

    ata_wait_400ns(&active_channel);

    outb(active_channel.base + 2, count);
    outb(active_channel.base + 3, (uint8_t)lba);
    outb(active_channel.base + 4, (uint8_t)(lba >> 8));
    outb(active_channel.base + 5, (uint8_t)(lba >> 16));
    outb(active_channel.base + 7, ATA_CMD_READ_PIO);

    for (unsigned int sector = 0; sector < count; sector++)
    {
        if (!ata_wait_not_busy(&active_channel))
            return 0;

        if (!ata_wait_data(&active_channel))
            return 0;

        for (unsigned int word = 0; word < 256; word++)
        {
            uint16_t value = inw(active_channel.base);

            destination[sector * 512 + word * 2] =
                (uint8_t)value;
            destination[sector * 512 + word * 2 + 1] =
                (uint8_t)(value >> 8);
        }
    }

    ata_wait_400ns(&active_channel);
    return 1;
}
