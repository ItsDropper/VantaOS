#include "pci.h"
#include "spinlock.h"

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_INVALID_VENDOR 0xFFFF

#define PCI_MAX_DEVICES 256

static struct pci_device devices[PCI_MAX_DEVICES];
static int device_count = 0;
static spinlock_t pci_config_lock = SPINLOCK_INITIALIZER;

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static uint32_t pci_config_address(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset
)
{
    return
        (1U << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);
}

static void pci_write_config(
    uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value
)
{
    uint64_t flags = spinlock_lock_irqsave(&pci_config_lock);
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
    spinlock_unlock_irqrestore(&pci_config_lock, flags);
}

static uint32_t pci_read_config(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset
)
{
    uint64_t flags = spinlock_lock_irqsave(&pci_config_lock);
    outl(
        PCI_CONFIG_ADDRESS,
        pci_config_address(
            bus,
            slot,
            function,
            offset
        )
    );
    uint32_t value = inl(PCI_CONFIG_DATA);
    spinlock_unlock_irqrestore(&pci_config_lock, flags);
    return value;
}

static uint16_t pci_get_vendor(
    uint8_t bus,
    uint8_t slot,
    uint8_t function
)
{
    return (uint16_t)(
        pci_read_config(
            bus,
            slot,
            function,
            0x00
        ) & 0xFFFF
    );
}

static void pci_read_device(
    struct pci_device* device,
    uint8_t bus,
    uint8_t slot,
    uint8_t function
)
{
    uint32_t id =
        pci_read_config(
            bus,
            slot,
            function,
            0x00
        );

    uint32_t class_info =
        pci_read_config(
            bus,
            slot,
            function,
            0x08
        );

    device->bus = bus;
    device->slot = slot;
    device->function = function;

    device->vendor_id =
        (uint16_t)(id & 0xFFFF);

    device->device_id =
        (uint16_t)(id >> 16);

    device->revision =
        (uint8_t)(class_info & 0xFF);

    device->prog_if =
        (uint8_t)((class_info >> 8) & 0xFF);

    device->subclass =
        (uint8_t)((class_info >> 16) & 0xFF);

    device->class_code =
        (uint8_t)((class_info >> 24) & 0xFF);
}

static void pci_scan(void)
{
    device_count = 0;

    for (uint16_t bus = 0; bus < 256; bus++)
    {
        for (uint8_t slot = 0; slot < 32; slot++)
        {
            uint16_t vendor =
                pci_get_vendor(
                    (uint8_t)bus,
                    slot,
                    0
                );

            if (vendor == PCI_INVALID_VENDOR)
                continue;

            uint8_t header_type =
                (uint8_t)(
                    pci_read_config(
                        (uint8_t)bus,
                        slot,
                        0,
                        0x0C
                    ) >> 16
                );

            uint8_t function_count =
                (header_type & 0x80) ? 8 : 1;

            for (uint8_t function = 0;
                 function < function_count;
                 function++)
            {
                vendor =
                    pci_get_vendor(
                        (uint8_t)bus,
                        slot,
                        function
                    );

                if (vendor == PCI_INVALID_VENDOR)
                    continue;

                if (device_count >= PCI_MAX_DEVICES)
                    return;

                pci_read_device(
                    &devices[device_count],
                    (uint8_t)bus,
                    slot,
                    function
                );

                device_count++;
            }
        }
    }
}

void pci_initialize(void)
{
    pci_scan();
}

int pci_get_device_count(void)
{
    return device_count;
}

const struct pci_device* pci_get_device(int index)
{
    if (index < 0 || index >= device_count)
        return 0;

    return &devices[index];
}

int pci_get_bar(const struct pci_device* device, uint8_t index,
                uint64_t* address, int* is_io, int* is_64bit)
{
    if (!device || !address || index >= 6)
        return 0;

    uint8_t offset = (uint8_t)(0x10 + index * 4);
    uint32_t low = pci_read_config(device->bus, device->slot, device->function, offset);
    if (low == 0 || low == 0xFFFFFFFFU)
        return 0;

    if (low & 1U)
    {
        *address = (uint64_t)(low & 0xFFFFFFFCU);
        if (is_io) *is_io = 1;
        if (is_64bit) *is_64bit = 0;
        return *address != 0;
    }

    uint32_t memory_type = (low >> 1) & 0x03U;
    if (memory_type == 0x03U)
        return 0;
    int wide = memory_type == 0x02U;
    if (wide && index >= 5)
        return 0;

    uint64_t high = 0;
    if (wide)
        high = pci_read_config(device->bus, device->slot, device->function,
                               (uint8_t)(offset + 4));
    *address = (high << 32) | (uint64_t)(low & 0xFFFFFFF0U);
    if (is_io) *is_io = 0;
    if (is_64bit) *is_64bit = wide;
    return *address != 0;
}

int pci_enable_io_space(const struct pci_device* device)
{
    if (!device) return 0;
    uint32_t command_status = pci_read_config(device->bus, device->slot, device->function, 0x04);
    uint16_t command = (uint16_t)command_status;
    if (command & 1U) return 1;
    command |= 1U;
    pci_write_config(device->bus, device->slot, device->function, 0x04,
                     (uint32_t)command);
    return (pci_read_config(device->bus, device->slot, device->function, 0x04) & 1U) != 0;
}

int pci_enable_memory_space(const struct pci_device* device)
{
    if (!device) return 0;
    uint32_t command_status = pci_read_config(device->bus, device->slot, device->function, 0x04);
    uint16_t command = (uint16_t)command_status;
    if (command & (1U << 1)) return 1;
    command |= (1U << 1);
    pci_write_config(device->bus, device->slot, device->function, 0x04,
                     (uint32_t)command);
    return (pci_read_config(device->bus, device->slot, device->function, 0x04) & (1U << 1)) != 0;
}

int pci_enable_bus_mastering(const struct pci_device* device)
{
    if (!device) return 0;
    uint32_t command_status = pci_read_config(device->bus, device->slot, device->function, 0x04);
    uint16_t command = (uint16_t)command_status;
    if (command & (1U << 2)) return 1;
    command |= (1U << 2);
    pci_write_config(device->bus, device->slot, device->function, 0x04,
                     (uint32_t)command);
    return (pci_read_config(device->bus, device->slot, device->function, 0x04) & (1U << 2)) != 0;
}

int pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if,
                   int start_index)
{
    if (start_index < 0) start_index = 0;
    for (int i = start_index; i < device_count; ++i)
    {
        const struct pci_device* device = &devices[i];
        if (device->class_code == class_code && device->subclass == subclass &&
            (prog_if < 0 || device->prog_if == (uint8_t)prog_if))
            return i;
    }
    return -1;
}

int pci_get_bar0(const struct pci_device* device, uint32_t* bar)
{
    if (!device || !bar)
        return 0;

    uint32_t value = pci_read_config(
        device->bus,
        device->slot,
        device->function,
        0x10
    );

    if (value & 1U)
        return 0;

    *bar = value & 0xFFFFFFF0U;
    return *bar != 0;
}

const char* pci_get_vendor_name(uint16_t vendor_id)
{
    switch (vendor_id)
    {
        case 0x10DE:
            return "NVIDIA";

        case 0x1022:
            return "AMD";

        case 0x8086:
            return "Intel";

        case 0x10EC:
            return "Realtek";

        case 0x1AF4:
            return "VirtIO";

        case 0x1234:
            return "QEMU";

        default:
            return "Unknown";
    }
}

const char* pci_get_device_name(
    uint16_t vendor_id,
    uint16_t device_id
)
{
    /*
     * NVIDIA
     */
    if (vendor_id == 0x10DE)
    {
        /*
         * RTX 3060.
         *
         * Multiple board variants can use different
         * device IDs, so this database will grow over time.
         */
        switch (device_id)
        {
            case 0x2503:
            case 0x2504:
            case 0x2507:
            case 0x2508:
            case 0x2509:
            case 0x2544:
            case 0x2548:
                return "GeForce RTX 3060";

            default:
                return "NVIDIA Graphics Device";
        }
    }

    /*
     * AMD.
     */
    if (vendor_id == 0x1022)
    {
        switch (device_id)
        {
            default:
                return "AMD Device";
        }
    }

    /*
     * Realtek Ethernet.
     */
    if (vendor_id == 0x10EC)
    {
        switch (device_id)
        {
            case 0x8125:
                return "2.5GbE Ethernet Controller";

            case 0x8168:
                return "Gigabit Ethernet Controller";

            default:
                return "Realtek Ethernet Controller";
        }
    }

    /*
     * Intel.
     */
    if (vendor_id == 0x8086)
        return "Intel Device";

    /*
     * QEMU.
     */
    if (vendor_id == 0x1234 &&
        device_id == 0x1111)
    {
        return "QEMU Virtual VGA";
    }

    return "Unknown Device";
}

const char* pci_get_class_name(
    uint8_t class_code,
    uint8_t subclass
)
{
    switch (class_code)
    {
        case 0x01:
            switch (subclass)
            {
                case 0x06:
                    return "SATA Storage";

                case 0x08:
                    return "NVMe Storage";

                default:
                    return "Storage Controller";
            }

        case 0x02:
            return "Network Controller";

        case 0x03:
            switch (subclass)
            {
                case 0x00:
                    return "Graphics";

                case 0x01:
                    return "Display";

                default:
                    return "Display Controller";
            }

        case 0x04:
            return "Multimedia";

        case 0x0C:
            switch (subclass)
            {
                case 0x03:
                    return "USB Controller";

                default:
                    return "Serial Bus Controller";
            }

        default:
            return "Other Device";
    }
}

uint32_t pci_read_config32(const struct pci_device *device, uint8_t offset)
{
    if (!device || offset > 252U) return 0xFFFFFFFFU;
    return pci_read_config(device->bus, device->slot, device->function, offset);
}
uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset)
{
    if (!device) return 0xFFU;
    uint32_t value = pci_read_config(device->bus, device->slot, device->function,
                                     (uint8_t)(offset & 0xFCU));
    return (uint8_t)(value >> ((offset & 3U) * 8U));
}
uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset)
{
    if (!device || offset > 254U) return 0xFFFFU;
    uint16_t lo = pci_read_config8(device, offset);
    uint16_t hi = pci_read_config8(device, (uint8_t)(offset + 1U));
    return (uint16_t)(lo | (uint16_t)(hi << 8));
}
