/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

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

#ifdef PCI_TEST
/* Host-side tests replace privileged port I/O with a deterministic config model. */
void pci_test_outl(uint16_t port, uint32_t value);
uint32_t pci_test_inl(uint16_t port);

static inline void outl(uint16_t port, uint32_t value)
{
    pci_test_outl(port, value);
}

static inline uint32_t inl(uint16_t port)
{
    return pci_test_inl(port);
}

static inline void outw(uint16_t port, uint16_t value)
{
    uint16_t aligned_port = (uint16_t)(port & (uint16_t)~3U);
    uint32_t old_value = pci_test_inl(aligned_port);
    unsigned shift = (unsigned)(port & 2U) * 8U;
    uint32_t mask = 0xFFFFU << shift;
    pci_test_outl(aligned_port, (old_value & ~mask) |
                  ((uint32_t)value << shift));
}
#else
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

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}
#endif

static uint64_t pci_lock_config(void)
{
#ifdef PCI_TEST
    spinlock_lock(&pci_config_lock);
    return 0;
#else
    return spinlock_lock_irqsave(&pci_config_lock);
#endif
}

static void pci_unlock_config(uint64_t flags)
{
#ifdef PCI_TEST
    (void)flags;
    spinlock_unlock(&pci_config_lock);
#else
    spinlock_unlock_irqrestore(&pci_config_lock, flags);
#endif
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
    uint64_t flags = pci_lock_config();
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
    pci_unlock_config(flags);
}

static uint32_t pci_read_config(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset
)
{
    uint64_t flags = pci_lock_config();
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
    pci_unlock_config(flags);
    return value;
}

/* These helpers require pci_config_lock to be held by the caller. */
static uint32_t pci_read_config_locked(
    uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

static void pci_write_config_locked(
    uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset,
    uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
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
    uint8_t header_type;
    uint8_t bar_count;
    uint8_t offset;
    uint32_t low;
    uint32_t high = 0;
    int wide = 0;

    /* Require all outputs so callers never consume partially initialized data. */
    if (address)
        *address = 0;
    if (is_io)
        *is_io = 0;
    if (is_64bit)
        *is_64bit = 0;
    if (!device || !address || !is_io || !is_64bit)
        return 0;

    header_type = (uint8_t)(pci_read_config8(device, 0x0EU) & 0x7FU);
    /* Type-0 endpoint headers have six BARs; type-1 PCI bridges have two. */
    if (header_type == 0U)
        bar_count = 6U;
    else if (header_type == 1U)
        bar_count = 2U;
    else
        return 0;
    if (index >= bar_count)
        return 0;

    /* A 64-bit BAR consumes the following BAR slot as its high dword. */
    for (uint8_t prior = 0; prior < index; ++prior)
    {
        uint32_t prior_low = pci_read_config32(
            device, (uint8_t)(0x10U + prior * 4U));
        if (prior_low != 0U && prior_low != 0xFFFFFFFFU &&
            !(prior_low & 1U) && ((prior_low >> 1) & 0x03U) == 0x02U)
        {
            if ((uint8_t)(prior + 1U) == index)
                return 0;
            ++prior;
        }
    }

    offset = (uint8_t)(0x10U + index * 4U);
    low = pci_read_config32(device, offset);
    if (low == 0U || low == 0xFFFFFFFFU)
        return 0;

    if (low & 1U)
    {
        *address = (uint64_t)(low & 0xFFFFFFFCU);
        *is_io = 1;
        return *address != 0U;
    }

    {
        uint32_t memory_type = (low >> 1) & 0x03U;
        if (memory_type == 0x03U)
            return 0;
        wide = memory_type == 0x02U;
    }
    if (wide)
    {
        if ((uint8_t)(index + 1U) >= bar_count)
            return 0;
        high = pci_read_config32(device, (uint8_t)(offset + 4U));
    }

    *address = ((uint64_t)high << 32) | (uint64_t)(low & 0xFFFFFFF0U);
    *is_io = 0;
    *is_64bit = wide;
    return *address != 0U;
}

/*
 * PCI BAR sizing is destructive to the BAR register for a short interval.
 * To avoid disrupting a live device, refuse unless I/O decode, memory decode,
 * and bus mastering are all disabled. Hold the configuration lock across
 * save/probe/restore so another CPU cannot interleave a config-space access.
 */
int pci_get_bar_size(const struct pci_device* device, uint8_t index,
                     uint64_t* size)
{
    uint64_t flags;
    uint64_t base = 0;
    uint64_t mask = 0;
    uint64_t measured_size = 0;
    uint8_t header_type;
    uint8_t bar_count;
    uint8_t offset;
    uint32_t command_status;
    uint32_t low;
    uint32_t high = 0;
    uint32_t mask_low;
    uint32_t mask_high = 0;
    uint32_t restored_low;
    uint32_t restored_high = 0;
    int is_io;
    int is_64bit = 0;
    int touched = 0;
    int valid = 0;

    if (size != 0)
        *size = 0;
    if (device == 0 || size == 0)
        return 0;

    flags = pci_lock_config();
    command_status = pci_read_config_locked(
        device->bus, device->slot, device->function, 0x04U);
    if ((command_status & 0x0007U) != 0U)
        goto done;

    header_type = (uint8_t)((pci_read_config_locked(
        device->bus, device->slot, device->function, 0x0CU) >> 16) & 0x7FU);
    if (header_type == 0U)
        bar_count = 6U;
    else if (header_type == 1U)
        bar_count = 2U;
    else
        goto done;
    if (index >= bar_count)
        goto done;

    /* Refuse an index that is the upper half of an earlier 64-bit BAR. */
    for (uint8_t prior = 0; prior < index; ++prior)
    {
        uint32_t prior_low = pci_read_config_locked(
            device->bus, device->slot, device->function,
            (uint8_t)(0x10U + prior * 4U));
        if (prior_low != 0U && prior_low != 0xFFFFFFFFU &&
            !(prior_low & 1U) && ((prior_low >> 1) & 0x03U) == 0x02U)
        {
            if ((uint8_t)(prior + 1U) == index)
                goto done;
            ++prior;
        }
    }

    offset = (uint8_t)(0x10U + index * 4U);
    low = pci_read_config_locked(
        device->bus, device->slot, device->function, offset);
    if (low == 0U || low == 0xFFFFFFFFU)
        goto done;

    is_io = (low & 1U) != 0U;
    if (is_io)
        base = (uint64_t)(low & 0xFFFFFFFCU);
    else
    {
        uint32_t memory_type = (low >> 1) & 0x03U;
        if (memory_type == 0x03U)
            goto done;
        is_64bit = memory_type == 0x02U;
        if (is_64bit)
        {
            if ((uint8_t)(index + 1U) >= bar_count)
                goto done;
            high = pci_read_config_locked(
                device->bus, device->slot, device->function,
                (uint8_t)(offset + 4U));
        }
        base = ((uint64_t)high << 32) |
               (uint64_t)(low & 0xFFFFFFF0U);
    }
    if (base == 0U)
        goto done;

    /* Probe mask while decode and DMA are disabled; always restore original. */
    pci_write_config_locked(device->bus, device->slot, device->function,
                            offset, 0xFFFFFFFFU);
    if (is_64bit)
        pci_write_config_locked(device->bus, device->slot, device->function,
                                (uint8_t)(offset + 4U), 0xFFFFFFFFU);
    touched = 1;
    mask_low = pci_read_config_locked(
        device->bus, device->slot, device->function, offset);
    if (is_64bit)
        mask_high = pci_read_config_locked(
            device->bus, device->slot, device->function,
            (uint8_t)(offset + 4U));

    /* Restore upper half before low half so the original 64-bit BAR is whole. */
    if (is_64bit)
        pci_write_config_locked(device->bus, device->slot, device->function,
                                (uint8_t)(offset + 4U), high);
    pci_write_config_locked(device->bus, device->slot, device->function,
                            offset, low);
    restored_low = pci_read_config_locked(
        device->bus, device->slot, device->function, offset);
    if (is_64bit)
        restored_high = pci_read_config_locked(
            device->bus, device->slot, device->function,
            (uint8_t)(offset + 4U));
    touched = 0;
    if (restored_low != low || (is_64bit && restored_high != high)) {
        touched = 1;
        goto done;
    }

    if (is_io)
    {
        mask = (uint64_t)(mask_low & 0xFFFFFFFCU);
        measured_size = (uint32_t)(~(uint32_t)mask + 1U);
    }
    else if (is_64bit)
    {
        mask = ((uint64_t)mask_high << 32) |
               (uint64_t)(mask_low & 0xFFFFFFF0U);
        measured_size = ~mask + 1ULL;
    }
    else
    {
        mask = (uint64_t)(mask_low & 0xFFFFFFF0U);
        measured_size = (uint32_t)(~(uint32_t)mask + 1U);
    }

    if (measured_size < (is_io ? 4ULL : 16ULL) ||
        (measured_size & (measured_size - 1ULL)) != 0ULL ||
        (base & (measured_size - 1ULL)) != 0ULL ||
        measured_size - 1ULL > UINT64_MAX - base)
        goto done;
    /* A non-64-bit BAR cannot describe an address range above 4 GiB. */
    if (!is_64bit && base + measured_size - 1ULL > UINT32_MAX)
        goto done;

    *size = measured_size;
    valid = 1;

done:
    /* Defensive restoration if future validation is added after probing. */
    if (touched)
    {
        if (is_64bit)
            pci_write_config_locked(device->bus, device->slot, device->function,
                                    (uint8_t)(0x10U + (index + 1U) * 4U), high);
        pci_write_config_locked(device->bus, device->slot, device->function,
                                (uint8_t)(0x10U + index * 4U), low);
    }
    pci_unlock_config(flags);
    return valid;
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


int pci_write_config16(const struct pci_device *device, uint8_t offset,
                       uint16_t value)
{
    uint64_t flags;
    uint16_t actual;
    if (device == 0 || offset > 254U)
        return 0;
    flags = pci_lock_config();
    outl(PCI_CONFIG_ADDRESS,
         pci_config_address(device->bus, device->slot, device->function, offset));
    outw((uint16_t)(PCI_CONFIG_DATA + (offset & 2U)), value);
    actual = (uint16_t)(pci_read_config_locked(
        device->bus, device->slot, device->function, offset) >>
        ((offset & 2U) * 8U));
    pci_unlock_config(flags);
    return actual == value;
}

int pci_write_config32(const struct pci_device *device, uint8_t offset,
                       uint32_t value)
{
    uint64_t flags;
    uint32_t actual;
    if (device == 0 || offset > 252U || (offset & 3U) != 0U)
        return 0;
    flags = pci_lock_config();
    pci_write_config_locked(device->bus, device->slot, device->function,
                            offset, value);
    actual = pci_read_config_locked(device->bus, device->slot,
                                    device->function, offset);
    pci_unlock_config(flags);
    return actual == value;
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

/* Find one conventional PCI capability without writing config space. */
int pci_find_capability(const struct pci_device *device, uint8_t capability_id,
                        uint8_t *out_offset)
{
    uint8_t header_type;
    uint8_t offset;
    uint64_t visited = 0;

    if (out_offset != 0)
        *out_offset = 0;
    if (device == 0 || out_offset == 0 || capability_id == 0U ||
        capability_id == 0xFFU)
        return 0;

    if ((pci_read_config16(device, 0x06U) & 0x0010U) == 0U)
        return 0;

    header_type = (uint8_t)(pci_read_config8(device, 0x0EU) & 0x7FU);
    if (header_type != 0U && header_type != 1U)
        return 0;

    offset = pci_read_config8(device, 0x34U);
    for (unsigned hops = 0; offset != 0U && hops < 48U; ++hops)
    {
        uint8_t id;
        uint8_t next;
        unsigned slot;

        if (offset < 0x40U || offset > 0xFCU || (offset & 0x03U) != 0U)
            return 0;
        slot = (unsigned)(offset - 0x40U) / 4U;
        if (slot >= 48U || (visited & (1ULL << slot)) != 0U)
            return 0;
        visited |= 1ULL << slot;

        id = pci_read_config8(device, offset);
        if (id == 0U || id == 0xFFU)
            return 0;
        if (id == capability_id)
        {
            *out_offset = offset;
            return 1;
        }
        next = pci_read_config8(device, (uint8_t)(offset + 1U));
        if (next == 0U)
            return 0;
        offset = next;
    }
    return 0;
}
