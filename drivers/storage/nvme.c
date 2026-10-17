#include "nvme.h"
#include "block_device.h"
#include "heap.h"
#include "dma.h"
#include "mmio.h"
#include "pci.h"
#include "paging.h"
#include "serial.h"
#include "spinlock.h"
#include "storage_driver.h"

#include <stdint.h>
#include <stddef.h>

#define NVME_CLASS 0x01U
#define NVME_SUBCLASS 0x08U
#define NVME_PROGIF 0x02U
#define NVME_MAX_CONTROLLERS 4U
#define NVME_MAX_NAMESPACES 16U
#define NVME_QUEUE_DEPTH 64U
#define NVME_PAGE_SIZE 4096U
#define NVME_REGISTERS_SIZE 4096U
#define NVME_WAIT_LIMIT 10000000U
#define NVME_CSTS_RDY 0x01U
#define NVME_CSTS_CFS 0x02U
#define NVME_CC_EN 0x01U
#define NVME_ADMIN_IDENTIFY 0x06U
#define NVME_ADMIN_CREATE_IO_CQ 0x05U
#define NVME_ADMIN_CREATE_IO_SQ 0x01U
#define NVME_IO_FLUSH 0x00U
#define NVME_IO_WRITE 0x01U
#define NVME_IO_READ 0x02U
#define NVME_CNS_NAMESPACE 0U
#define NVME_CNS_CONTROLLER 1U
#define NVME_CNS_ACTIVE_NAMESPACES 2U

typedef struct __attribute__((packed)) {
    uint8_t opcode;
    uint8_t flags;
    uint16_t command_id;
    uint32_t namespace_id;
    uint64_t reserved2;
    uint64_t metadata_pointer;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t command_dword10;
    uint32_t command_dword11;
    uint32_t command_dword12;
    uint32_t command_dword13;
    uint32_t command_dword14;
    uint32_t command_dword15;
} nvme_command_t;

typedef struct __attribute__((packed)) {
    uint32_t result;
    uint32_t reserved;
    uint16_t submission_head;
    uint16_t submission_id;
    uint16_t command_id;
    uint16_t status_phase;
} nvme_completion_t;
typedef char nvme_command_must_be_64_bytes[(sizeof(nvme_command_t) == 64U) ? 1 : -1];
typedef char nvme_completion_must_be_16_bytes[(sizeof(nvme_completion_t) == 16U) ? 1 : -1];

#define NVME_IO_SLOTS 8U
#define NVME_MAX_TRANSFER_PAGES 16U

typedef struct {
    dma_page_t pages[NVME_MAX_TRANSFER_PAGES];
    dma_page_t prp_list;
    uint64_t lba;
    uint32_t sector_count;
    size_t byte_offset;
    uint16_t command_id;
    uint8_t page_count;
    uint8_t direct;
    uint8_t active;
    uint8_t completed;
    uint8_t success;
} nvme_io_slot_t;

typedef struct {
    dma_page_t admin_submission;
    dma_page_t admin_completion;
    dma_page_t io_submission;
    dma_page_t io_completion;
    dma_page_t identify;
    nvme_io_slot_t io_slots[NVME_IO_SLOTS];
    volatile uint8_t* registers;
    volatile uint32_t* admin_submission_doorbell;
    volatile uint32_t* admin_completion_doorbell;
    volatile uint32_t* io_submission_doorbell;
    volatile uint32_t* io_completion_doorbell;
    uint64_t capacity_sectors;
    uint64_t doorbell_stride;
    uint32_t namespace_count;
    uint32_t controller_index;
    uint32_t max_transfer_pages;
    uint64_t direct_dma_commands;
    uint64_t bounce_dma_commands;
    uint16_t admin_tail;
    uint16_t admin_head;
    uint16_t io_tail;
    uint16_t io_head;
    uint16_t next_command_id;
    uint8_t admin_phase;
    uint8_t io_phase;
    uint8_t initialized;
    uint8_t failed;
    spinlock_t io_lock;
} nvme_controller_t;

typedef struct {
    block_device_t block;
    nvme_controller_t* controller;
    uint32_t namespace_id;
    uint64_t sector_count;
    uint32_t sector_size;
    uint8_t used;
} nvme_namespace_t;

static nvme_controller_t controllers[NVME_MAX_CONTROLLERS];
static nvme_namespace_t namespaces[NVME_MAX_NAMESPACES];
static uint32_t controller_count;
static uint32_t namespace_count;
static uint8_t driver_registered;

static inline uint32_t mmio_read32(volatile uint8_t* base, uint32_t offset)
{
    return *(volatile uint32_t*)(base + offset);
}

static inline uint64_t mmio_read64(volatile uint8_t* base, uint32_t offset)
{
    return *(volatile uint64_t*)(base + offset);
}

static inline void mmio_write32(volatile uint8_t* base, uint32_t offset,
                                uint32_t value)
{
    *(volatile uint32_t*)(base + offset) = value;
    __asm__ volatile ("" ::: "memory");
}

static inline void mmio_write64(volatile uint8_t* base, uint32_t offset,
                                uint64_t value)
{
    *(volatile uint64_t*)(base + offset) = value;
    __asm__ volatile ("" ::: "memory");
}

static int wait_ready(nvme_controller_t* c, int ready)
{
    for (uint32_t i = 0; i < NVME_WAIT_LIMIT; ++i) {
        uint32_t status = mmio_read32(c->registers, 0x1CU);
        if (status & NVME_CSTS_CFS)
            return 0;
        if (((status & NVME_CSTS_RDY) != 0U) == (ready != 0))
            return 1;
        __asm__ volatile ("pause");
    }
    return 0;
}

static int allocate_controller_memory(nvme_controller_t* c)
{
    if (!dma_alloc_page(&c->admin_submission)) return 0;
    if (!dma_alloc_page(&c->admin_completion)) return 0;
    if (!dma_alloc_page(&c->io_submission)) return 0;
    if (!dma_alloc_page(&c->io_completion)) return 0;
    if (!dma_alloc_page(&c->identify)) return 0;
    for (uint32_t slot = 0; slot < NVME_IO_SLOTS; ++slot) {
        nvme_io_slot_t* io = &c->io_slots[slot];
        if (!dma_alloc_page(&io->prp_list)) return 0;
        for (uint32_t page = 0; page < NVME_MAX_TRANSFER_PAGES; ++page)
            if (!dma_alloc_page(&io->pages[page])) return 0;
    }
    c->max_transfer_pages = NVME_MAX_TRANSFER_PAGES;
    return 1;
}

static void free_controller_memory(nvme_controller_t* c)
{
    for (uint32_t slot = 0; slot < NVME_IO_SLOTS; ++slot) {
        nvme_io_slot_t* io = &c->io_slots[slot];
        for (uint32_t page = 0; page < NVME_MAX_TRANSFER_PAGES; ++page)
            dma_free_page(&io->pages[page]);
        dma_free_page(&io->prp_list);
    }
    dma_free_page(&c->identify);
    dma_free_page(&c->io_completion);
    dma_free_page(&c->io_submission);
    dma_free_page(&c->admin_completion);
    dma_free_page(&c->admin_submission);
}

static int wait_completion(nvme_controller_t* c, int admin,
                           uint16_t command_id, uint32_t* result)
{
    volatile nvme_completion_t* queue = (volatile nvme_completion_t*)
        (admin ? c->admin_completion.virtual_address :
                 c->io_completion.virtual_address);
    uint16_t* head = admin ? &c->admin_head : &c->io_head;
    uint8_t* phase = admin ? &c->admin_phase : &c->io_phase;
    volatile uint32_t* doorbell = admin ? c->admin_completion_doorbell :
                                          c->io_completion_doorbell;

    for (uint32_t spin = 0; spin < NVME_WAIT_LIMIT; ++spin) {
        nvme_completion_t completion;
        uint16_t status_phase = queue[*head].status_phase;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if ((status_phase & 1U) == *phase) {
            completion.result = queue[*head].result;
            completion.reserved = queue[*head].reserved;
            completion.submission_head = queue[*head].submission_head;
            completion.submission_id = queue[*head].submission_id;
            completion.command_id = queue[*head].command_id;
            completion.status_phase = status_phase;
            if (completion.command_id != command_id) {
                c->failed = 1;
                return 0;
            }
            if (result) *result = completion.result;
            uint16_t status = completion.status_phase >> 1;
            *head = (uint16_t)((*head + 1U) % NVME_QUEUE_DEPTH);
            if (*head == 0) *phase ^= 1U;
            *doorbell = *head;
            __asm__ volatile ("" ::: "memory");
            if (status != 0U)
                return 0;
            return 1;
        }
        if (c->failed || (mmio_read32(c->registers, 0x1CU) & NVME_CSTS_CFS)) {
            c->failed = 1;
            return 0;
        }
        __asm__ volatile ("pause");
    }
    c->failed = 1;
    return 0;
}

static int submit_command(nvme_controller_t* c, int admin,
                          nvme_command_t* command, uint32_t* result)
{
    if (!c || !command || !c->initialized || c->failed) return 0;
    volatile nvme_command_t* queue = (volatile nvme_command_t*)
        (admin ? c->admin_submission.virtual_address :
                 c->io_submission.virtual_address);
    uint16_t* tail = admin ? &c->admin_tail : &c->io_tail;
    volatile uint32_t* doorbell = admin ? c->admin_submission_doorbell :
                                          c->io_submission_doorbell;
    uint16_t id = ++c->next_command_id;
    if (id == 0) id = ++c->next_command_id;
    command->command_id = id;
    queue[*tail] = *command;
    *tail = (uint16_t)((*tail + 1U) % NVME_QUEUE_DEPTH);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *doorbell = *tail;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return wait_completion(c, admin, id, result);
}

static int identify(nvme_controller_t* c, uint32_t namespace_id,
                    uint32_t cns)
{
    nvme_command_t command = {0};
    command.opcode = NVME_ADMIN_IDENTIFY;
    command.namespace_id = namespace_id;
    command.prp1 = c->identify.physical_address;
    command.command_dword10 = cns;
    return submit_command(c, 1, &command, 0);
}

static void copy_bytes(void* destination, const void* source, size_t length)
{
    uint8_t* dst = (uint8_t*)destination;
    const uint8_t* src = (const uint8_t*)source;
    for (size_t i = 0; i < length; ++i) dst[i] = src[i];
}

static uint16_t next_io_command_id(nvme_controller_t* c)
{
    uint16_t id = ++c->next_command_id;
    if (id == 0U) id = ++c->next_command_id;
    return id;
}

static int try_direct_dma(nvme_controller_t* c, nvme_io_slot_t* io,
                           uintptr_t virtual_start, size_t bytes, int write,
                           uint64_t* prp1, uint64_t* prp2,
                           uint32_t* page_count_out)
{
    const uint64_t dma_limit = 0x100000000ULL;
    const uintptr_t offset = virtual_start & (NVME_PAGE_SIZE - 1U);
    if (bytes > SIZE_MAX - offset ||
        bytes + offset > SIZE_MAX - (NVME_PAGE_SIZE - 1U))
        return 0;
    uint32_t page_count = (uint32_t)((offset + bytes + NVME_PAGE_SIZE - 1U) /
                                     NVME_PAGE_SIZE);
    if (!page_count || page_count > c->max_transfer_pages ||
        page_count > NVME_MAX_TRANSFER_PAGES)
        return 0;

    uint64_t first = paging_get_physical(virtual_start);
    if (!first || first >= dma_limit || bytes > dma_limit - first ||
        (first & (NVME_PAGE_SIZE - 1U)) != offset)
        return 0;
    uintptr_t first_virtual_page = virtual_start & ~(uintptr_t)(NVME_PAGE_SIZE - 1U);
    uint64_t* list = (uint64_t*)io->prp_list.virtual_address;
    for (uint32_t page = 0; page < NVME_MAX_TRANSFER_PAGES; ++page)
        list[page] = 0U;
    for (uint32_t page = 0; page < page_count; ++page) {
        uintptr_t virtual_page = first_virtual_page +
                                 (uintptr_t)page * NVME_PAGE_SIZE;
        uint64_t page_flags = paging_get_page_flags(virtual_page);
        if (!(page_flags & PAGE_PRESENT) || (!write && !(page_flags & PAGE_WRITE)))
            return 0;
        if (page == 0U) continue;
        uint64_t physical_page = paging_get_physical(virtual_page);
        if (!physical_page || physical_page >= dma_limit ||
            (physical_page & (NVME_PAGE_SIZE - 1U)))
            return 0;
        list[page - 1U] = physical_page;
    }
    *prp1 = first;
    *prp2 = page_count == 2U ? list[0] :
            page_count > 2U ? io->prp_list.physical_address : 0U;
    *page_count_out = page_count;
    return 1;
}

static void prepare_io_command(nvme_controller_t* c, nvme_namespace_t* ns,
                               nvme_io_slot_t* io, uint64_t lba,
                               uint32_t sectors, size_t byte_offset,
                               void* user_buffer, int write)
{
    const size_t bytes = (size_t)sectors * ns->sector_size;
    const uintptr_t virtual_start = (uintptr_t)user_buffer + byte_offset;
    uint64_t prp1 = 0U, prp2 = 0U;
    uint32_t page_count = 0U;
    uint64_t* list = (uint64_t*)io->prp_list.virtual_address;
    for (uint32_t i = 0; i < NVME_MAX_TRANSFER_PAGES; ++i) list[i] = 0U;

    io->direct = (uint8_t)try_direct_dma(c, io, virtual_start, bytes, write,
                                         &prp1, &prp2, &page_count);
    if (io->direct)
        ++c->direct_dma_commands;
    else
        ++c->bounce_dma_commands;
    if (!io->direct) {
        page_count = (uint32_t)((bytes + NVME_PAGE_SIZE - 1U) / NVME_PAGE_SIZE);
        if (!page_count || page_count > NVME_MAX_TRANSFER_PAGES) {
            c->failed = 1U;
            io->active = 0U;
            return;
        }
        for (uint32_t i = 0; i < NVME_MAX_TRANSFER_PAGES; ++i) list[i] = 0U;
        uint8_t* user = (uint8_t*)user_buffer + byte_offset;
        size_t remaining = bytes;
        for (uint32_t page = 0; page < page_count; ++page) {
            size_t page_bytes = remaining < NVME_PAGE_SIZE ? remaining : NVME_PAGE_SIZE;
            if (write)
                copy_bytes(io->pages[page].virtual_address, user, page_bytes);
            if (page > 0U) list[page - 1U] = io->pages[page].physical_address;
            user += page_bytes;
            remaining -= page_bytes;
        }
        prp1 = io->pages[0].physical_address;
        prp2 = page_count == 2U ? list[0] :
               page_count > 2U ? io->prp_list.physical_address : 0U;
    }

    io->lba = lba;
    io->sector_count = sectors;
    io->byte_offset = byte_offset;
    io->page_count = (uint8_t)page_count;
    io->command_id = next_io_command_id(c);
    io->active = 1U;
    io->completed = 0U;
    io->success = 0U;

    nvme_command_t command = {0};
    command.opcode = write ? NVME_IO_WRITE : NVME_IO_READ;
    command.command_id = io->command_id;
    command.namespace_id = ns->namespace_id;
    command.prp1 = prp1;
    command.prp2 = prp2;
    command.command_dword10 = (uint32_t)lba;
    command.command_dword11 = (uint32_t)(lba >> 32);
    command.command_dword12 = sectors - 1U; /* NLB is zero-based. */

    volatile nvme_command_t* queue =
        (volatile nvme_command_t*)c->io_submission.virtual_address;
    queue[c->io_tail] = command;
    c->io_tail = (uint16_t)((c->io_tail + 1U) % NVME_QUEUE_DEPTH);
}
static int complete_io_batch(nvme_controller_t* c, uint32_t pending)
{
    uint32_t completed = 0U;
    uint32_t spins = 0U;
    volatile nvme_completion_t* queue =
        (volatile nvme_completion_t*)c->io_completion.virtual_address;
    while (completed < pending && spins < NVME_WAIT_LIMIT) {
        if (mmio_read32(c->registers, 0x1CU) & NVME_CSTS_CFS) {
            c->failed = 1U;
            return 0;
        }
        nvme_completion_t completion;
        uint16_t status_phase = queue[c->io_head].status_phase;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if ((status_phase & 1U) != c->io_phase) {
            ++spins;
            __asm__ volatile ("pause");
            continue;
        }
        completion.result = queue[c->io_head].result;
        completion.reserved = queue[c->io_head].reserved;
        completion.submission_head = queue[c->io_head].submission_head;
        completion.submission_id = queue[c->io_head].submission_id;
        completion.command_id = queue[c->io_head].command_id;
        completion.status_phase = status_phase;
        nvme_io_slot_t* found = 0;
        for (uint32_t i = 0; i < NVME_IO_SLOTS; ++i) {
            nvme_io_slot_t* candidate = &c->io_slots[i];
            if (candidate->active && !candidate->completed &&
                candidate->command_id == completion.command_id) {
                found = candidate;
                break;
            }
        }
        if (!found) {
            c->failed = 1U;
            return 0;
        }
        found->completed = 1U;
        found->success = ((completion.status_phase >> 1) == 0U);
        ++completed;
        c->io_head = (uint16_t)((c->io_head + 1U) % NVME_QUEUE_DEPTH);
        if (c->io_head == 0U) c->io_phase ^= 1U;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        *c->io_completion_doorbell = c->io_head;
        ++spins;
    }
    if (completed != pending) {
        c->failed = 1U;
        return 0;
    }
    return 1;
}

static block_status_t namespace_transfer(nvme_namespace_t* ns, uint64_t lba,
                                         uint32_t count, void* buffer,
                                         int write)
{
    if (!ns || !ns->used || !buffer || count == 0U)
        return BLOCK_STATUS_INVALID;
    nvme_controller_t* c = ns->controller;
    if (!c || !c->initialized || c->failed)
        return BLOCK_STATUS_NOT_READY;
    if (lba >= ns->sector_count || (uint64_t)count > ns->sector_count - lba)
        return BLOCK_STATUS_OUT_OF_RANGE;
    if ((uint64_t)count > UINT32_MAX / ns->sector_size)
        return BLOCK_STATUS_INVALID;
    size_t total_bytes = (size_t)count * ns->sector_size;
    if ((uintptr_t)buffer > UINTPTR_MAX - total_bytes)
        return BLOCK_STATUS_INVALID;

    uint64_t flags = spinlock_lock_irqsave(&c->io_lock);
    block_status_t status = BLOCK_STATUS_OK;
    uint32_t transferred = 0U;
    uint8_t* user_buffer = (uint8_t*)buffer;
    const uint32_t sectors_per_page = NVME_PAGE_SIZE / ns->sector_size;
    uint32_t sectors_per_command = c->max_transfer_pages * sectors_per_page;
    if (!sectors_per_command) {
        spinlock_unlock_irqrestore(&c->io_lock, flags);
        return BLOCK_STATUS_IO_ERROR;
    }

    while (transferred < count && !c->failed) {
        uint32_t pending = 0U;
        for (uint32_t i = 0; i < NVME_IO_SLOTS && transferred < count; ++i) {
            uint32_t sectors = count - transferred;
            if (sectors > sectors_per_command) sectors = sectors_per_command;
            prepare_io_command(c, ns, &c->io_slots[i], lba + transferred,
                               sectors, (size_t)transferred * ns->sector_size,
                               user_buffer, write);
            transferred += sectors;
            ++pending;
        }
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        *c->io_submission_doorbell = c->io_tail;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (!complete_io_batch(c, pending)) {
            status = BLOCK_STATUS_IO_ERROR;
            break;
        }
        for (uint32_t i = 0; i < pending; ++i) {
            nvme_io_slot_t* io = &c->io_slots[i];
            if (!io->success) {
                status = BLOCK_STATUS_IO_ERROR;
            } else if (!write && !io->direct) {
                size_t bytes = (size_t)io->sector_count * ns->sector_size;
                size_t copied = 0U;
                uint8_t* destination = user_buffer + io->byte_offset;
                for (uint32_t page = 0; page < io->page_count; ++page) {
                    size_t amount = bytes - copied;
                    if (amount > NVME_PAGE_SIZE) amount = NVME_PAGE_SIZE;
                    copy_bytes(destination + copied, io->pages[page].virtual_address,
                               amount);
                    copied += amount;
                }
            }
            io->active = 0U;
        }
        if (status != BLOCK_STATUS_OK) break;
    }
    spinlock_unlock_irqrestore(&c->io_lock, flags);
    return status;
}

static size_t append_decimal(char* output, size_t offset, uint32_t value)
{
    char digits[10];
    size_t count = 0;
    do { digits[count++] = (char)('0' + (value % 10U)); value /= 10U; } while (value && count < sizeof(digits));
    while (count) output[offset++] = digits[--count];
    return offset;
}

static block_status_t namespace_read(block_device_t* block, uint64_t lba,
                                     uint32_t count, void* output)
{
    nvme_namespace_t* ns = (nvme_namespace_t*)block->driver_data;
    return namespace_transfer(ns, lba, count, output, 0);
}

static block_status_t namespace_write(block_device_t* block, uint64_t lba,
                                      uint32_t count, const void* input)
{
    nvme_namespace_t* ns = (nvme_namespace_t*)block->driver_data;
    return namespace_transfer(ns, lba, count, (void*)input, 1);
}

static block_status_t namespace_flush(block_device_t* block)
{
    nvme_namespace_t* ns = (nvme_namespace_t*)block->driver_data;
    if (!ns || !ns->controller || ns->controller->failed)
        return BLOCK_STATUS_NOT_READY;
    nvme_controller_t* c = ns->controller;
    uint64_t flags = spinlock_lock_irqsave(&c->io_lock);
    nvme_command_t command = {0};
    command.opcode = NVME_IO_FLUSH;
    command.namespace_id = ns->namespace_id;
    int ok = submit_command(c, 0, &command, 0);
    spinlock_unlock_irqrestore(&c->io_lock, flags);
    return ok ? BLOCK_STATUS_OK : BLOCK_STATUS_IO_ERROR;
}

static const block_device_ops_t namespace_ops = {
    namespace_read, namespace_write, namespace_flush
};

static int register_namespace(nvme_controller_t* c, uint32_t nsid)
{
    if (namespace_count >= NVME_MAX_NAMESPACES) return 0;
    if (!identify(c, nsid, NVME_CNS_NAMESPACE)) return 0;
    const uint8_t* id = (const uint8_t*)c->identify.virtual_address;
    uint64_t size = *(const uint64_t*)(id + 0U);
    uint8_t format_count = id[25];
    uint8_t active_format = id[26] & 0x0FU;
    if (!size || active_format > format_count || active_format > 15U) return 0;
    const uint8_t* format = id + 128U + (size_t)active_format * 4U;
    uint16_t metadata_size = *(const uint16_t*)format;
    uint8_t lba_shift = format[2];
    if (metadata_size != 0U || lba_shift < 9U || lba_shift > 12U)
        return 0;
    uint32_t sector_size = 1U << lba_shift;
    nvme_namespace_t* ns = &namespaces[namespace_count];
    ns->controller = c;
    ns->namespace_id = nsid;
    ns->sector_count = size;
    ns->sector_size = sector_size;
    ns->used = 1U;
    ns->block.name[0] = 'n'; ns->block.name[1] = 'v'; ns->block.name[2] = 'm'; ns->block.name[3] = 'e';
    size_t name_length = append_decimal(ns->block.name, 4U, c->controller_index);
    ns->block.name[name_length++] = 'n';
    name_length = append_decimal(ns->block.name, name_length, nsid);
    ns->block.name[name_length] = '\0';
    ns->block.sector_count = size;
    ns->block.sector_size = sector_size;
    ns->block.flags = 0U;
    ns->block.ops = &namespace_ops;
    ns->block.driver_data = ns;
    if (block_device_register(&ns->block) != BLOCK_STATUS_OK) {
        ns->used = 0U;
        return 0;
    }
    ++namespace_count;
    serial_write("VANTAOS_NVME_NAMESPACE_OK\n");
    return 1;
}

static storage_probe_result_t nvme_probe(const struct pci_device* pci)
{
    if (!pci || pci->class_code != NVME_CLASS ||
        pci->subclass != NVME_SUBCLASS || pci->prog_if != NVME_PROGIF)
        return STORAGE_PROBE_NO_MATCH;
    if (controller_count >= NVME_MAX_CONTROLLERS)
        return STORAGE_PROBE_FAILED;

    uint64_t bar = 0;
    int is_io = 0, is_64 = 0;
    if (!pci_get_bar(pci, 0, &bar, &is_io, &is_64) || is_io || !bar)
        return STORAGE_PROBE_FAILED;
    if (!pci_enable_memory_space(pci))
        return STORAGE_PROBE_FAILED;

    nvme_controller_t* c = &controllers[controller_count];
    *c = (nvme_controller_t){0};
    c->controller_index = controller_count;
    spinlock_init(&c->io_lock);
    if (!allocate_controller_memory(c)) {
        free_controller_memory(c);
        return STORAGE_PROBE_FAILED;
    }

    /* Read CAP through a single-page mapping, then map only required doorbells. */
    c->registers = (volatile uint8_t*)mmio_map_region(bar, NVME_REGISTERS_SIZE);
    if (!c->registers) {
        free_controller_memory(c);
        return STORAGE_PROBE_FAILED;
    }
    uint64_t cap = mmio_read64(c->registers, 0x00U);
    uint32_t max_entries = (uint32_t)(cap & 0xFFFFU) + 1U;
    uint32_t timeout_units = (uint32_t)((cap >> 24) & 0xFFU);
    uint32_t stride_shift = (uint32_t)((cap >> 32) & 0x0FU);
    uint32_t minimum_page_shift = (uint32_t)((cap >> 48) & 0x0FU);
    uint32_t command_sets = (uint32_t)((cap >> 37) & 0xFFU);
    if (max_entries < NVME_QUEUE_DEPTH || stride_shift > 15U ||
        minimum_page_shift != 0U || (command_sets & 1U) == 0U) {
        mmio_unmap_region((void*)c->registers, NVME_REGISTERS_SIZE);
        free_controller_memory(c);
        return STORAGE_PROBE_FAILED;
    }
    c->doorbell_stride = 4ULL << stride_shift;
    uint64_t required_length = 0x1000ULL + 3ULL * c->doorbell_stride + sizeof(uint32_t);
    if (required_length > PAGING_MMIO_SIZE ||
        !mmio_unmap_region((void*)c->registers, NVME_REGISTERS_SIZE)) {
        c->registers = 0;
        free_controller_memory(c);
        return STORAGE_PROBE_FAILED;
    }
    c->registers = (volatile uint8_t*)mmio_map_region(bar, (size_t)required_length);
    if (!c->registers) {
        free_controller_memory(c);
        return STORAGE_PROBE_FAILED;
    }
    c->admin_submission_doorbell = (volatile uint32_t*)
        (c->registers + 0x1000U);
    c->admin_completion_doorbell = (volatile uint32_t*)
        (c->registers + 0x1000U + c->doorbell_stride);
    c->io_submission_doorbell = (volatile uint32_t*)
        (c->registers + 0x1000U + 2ULL * c->doorbell_stride);
    c->io_completion_doorbell = (volatile uint32_t*)
        (c->registers + 0x1000U + 3ULL * c->doorbell_stride);

    /* Disable first, then wait for the controller to report not-ready. */
    uint32_t cc = mmio_read32(c->registers, 0x14U);
    if (cc & NVME_CC_EN) {
        mmio_write32(c->registers, 0x14U, cc & ~NVME_CC_EN);
        if (!wait_ready(c, 0)) goto fail;
    } else if (!wait_ready(c, 0)) {
        goto fail;
    }

    c->admin_tail = c->admin_head = 0U;
    c->admin_phase = 1U;
    c->next_command_id = 0U;
    mmio_write32(c->registers, 0x24U,
                 ((NVME_QUEUE_DEPTH - 1U) << 16) | (NVME_QUEUE_DEPTH - 1U));
    mmio_write64(c->registers, 0x28U, c->admin_submission.physical_address);
    mmio_write64(c->registers, 0x30U, c->admin_completion.physical_address);
    /* DMA is prepared; enable bus mastering before enabling the controller. */
    if (!pci_enable_bus_mastering(pci)) goto fail;
    /* IOSQES=64-byte command, IOCQES=16-byte completion, MPS=4 KiB. */
    mmio_write32(c->registers, 0x14U, NVME_CC_EN | (6U << 16) | (4U << 20));
    if (!wait_ready(c, 1)) goto fail;

    c->initialized = 1U;
    c->io_tail = c->io_head = 0U;
    c->io_phase = 1U;
    if (!identify(c, 0U, NVME_CNS_CONTROLLER)) goto fail;
    const uint8_t* controller_id = (const uint8_t*)c->identify.virtual_address;
    uint32_t namespace_total = *(const uint32_t*)(controller_id + 516U);
    if (!namespace_total) goto fail;
    uint32_t mdts = controller_id[77U];
    c->max_transfer_pages = mdts == 0U || mdts >= 4U ?
        NVME_MAX_TRANSFER_PAGES : (1U << mdts);
    if (!c->max_transfer_pages) c->max_transfer_pages = 1U;

    nvme_command_t create_cq = {0};
    create_cq.opcode = NVME_ADMIN_CREATE_IO_CQ;
    create_cq.prp1 = c->io_completion.physical_address;
    create_cq.command_dword10 = 1U | ((NVME_QUEUE_DEPTH - 1U) << 16);
    create_cq.command_dword11 = 1U; /* physically contiguous, polling completion */
    if (!submit_command(c, 1, &create_cq, 0)) goto fail;

    nvme_command_t create_sq = {0};
    create_sq.opcode = NVME_ADMIN_CREATE_IO_SQ;
    create_sq.prp1 = c->io_submission.physical_address;
    create_sq.command_dword10 = 1U | ((NVME_QUEUE_DEPTH - 1U) << 16);
    create_sq.command_dword11 = 1U | (1U << 16); /* CQID=1 */
    if (!submit_command(c, 1, &create_sq, 0)) goto fail;

    /* A single page holds up to 1024 active namespace IDs. */
    if (!identify(c, 0U, NVME_CNS_ACTIVE_NAMESPACES)) goto fail;
    const uint32_t* active = (const uint32_t*)c->identify.virtual_address;
    uint32_t found = 0U;
    for (uint32_t i = 0; i < NVME_PAGE_SIZE / sizeof(uint32_t) &&
         found < namespace_total && namespace_count < NVME_MAX_NAMESPACES; ++i) {
        uint32_t nsid = active[i];
        if (!nsid) break;
        if (register_namespace(c, nsid)) ++found;
    }
    if (!found) goto fail;
    c->capacity_sectors = namespace_count;
    ++controller_count;
    serial_write("VANTAOS_NVME_CONTROLLER_OK\n");
    (void)timeout_units;
    (void)is_64;
    return STORAGE_PROBE_BOUND;

fail:
    c->failed = 1U;
    c->initialized = 0U;
    mmio_write32(c->registers, 0x14U, 0U);
    /* Never release DMA memory unless the controller is confirmed stopped. */
    if (wait_ready(c, 0)) {
        uint64_t mapped_length = 0x1000ULL + 3ULL * c->doorbell_stride + sizeof(uint32_t);
        (void)mmio_unmap_region((void*)c->registers, (size_t)mapped_length);
        c->registers = 0;
        free_controller_memory(c);
    } else {
        serial_write("VANTAOS_NVME_DMA_RETAINED_AFTER_RESET_TIMEOUT\n");
    }
    serial_write("VANTAOS_NVME_CONTROLLER_FAIL\n");
    return STORAGE_PROBE_FAILED;
}

#ifdef VANTAOS_SELF_TEST
static int nvme_run_io_selftest(void)
{
    block_device_t* device = 0;
    uint32_t count = block_device_count();
    for (uint32_t i = 0; i < count; ++i) {
        block_device_t* candidate = block_device_get(i);
        if (candidate && candidate->name[0] == 'n' &&
            candidate->name[1] == 'v' && candidate->name[2] == 'm' &&
            candidate->name[3] == 'e') {
            device = candidate;
            break;
        }
    }
    if (!device) {
        serial_write("VANTAOS_NVME_IO_SELFTEST_SKIPPED\n");
        return 1;
    }
    if (device->sector_size > NVME_PAGE_SIZE) {
        serial_write("VANTAOS_NVME_IO_SELFTEST_FAIL\n");
        return 0;
    }
    /* Eight outstanding commands, each covering up to sixteen DMA pages. */
    const size_t test_capacity =
        (size_t)NVME_IO_SLOTS * NVME_MAX_TRANSFER_PAGES * NVME_PAGE_SIZE;
    uint8_t* test_memory = (uint8_t*)kmalloc(test_capacity * 3U);
    if (!test_memory) {
        serial_write("VANTAOS_NVME_IO_SELFTEST_FAIL\n");
        return 0;
    }
    uint8_t* before = test_memory;
    uint8_t* expected = test_memory + test_capacity;
    uint8_t* actual = test_memory + 2U * test_capacity;
    const uint32_t test_sectors = NVME_IO_SLOTS * NVME_MAX_TRANSFER_PAGES *
                                  (NVME_PAGE_SIZE / device->sector_size);
    const uint64_t test_lba = 8U;
    const size_t test_bytes = (size_t)test_sectors * device->sector_size;
    nvme_namespace_t* test_namespace = (nvme_namespace_t*)device->driver_data;
    nvme_controller_t* test_controller =
        test_namespace ? test_namespace->controller : 0;
    uint64_t direct_before = test_controller ?
        test_controller->direct_dma_commands : 0U;
    if (device->sector_count <= test_lba + test_sectors) {
        kfree(test_memory);
        serial_write("VANTAOS_NVME_IO_SELFTEST_FAIL\n");
        return 0;
    }
    for (size_t i = 0; i < test_bytes; ++i)
        expected[i] = (uint8_t)((i * 37U) ^ (i >> 7) ^ 0xA5U);
    if (block_device_read(device, test_lba, test_sectors, before) != BLOCK_STATUS_OK) {
        kfree(test_memory);
        serial_write("VANTAOS_NVME_IO_SELFTEST_FAIL\n");
        return 0;
    }
    int ok = block_device_write(device, test_lba, test_sectors, expected) == BLOCK_STATUS_OK;
    ok = ok && block_device_flush(device) == BLOCK_STATUS_OK;
    ok = ok && block_device_read(device, test_lba, test_sectors, actual) == BLOCK_STATUS_OK;
    for (size_t i = 0; i < test_bytes; ++i)
        ok = ok && actual[i] == expected[i];
    ok = ok && block_device_read(device, device->sector_count, 1U, actual) ==
               BLOCK_STATUS_OUT_OF_RANGE;
    /* Restore the complete scratch range even if verification fails. */
    if (block_device_write(device, test_lba, test_sectors, before) != BLOCK_STATUS_OK)
        ok = 0;
    if (block_device_flush(device) != BLOCK_STATUS_OK)
        ok = 0;
    int direct_dma_used = test_controller &&
        test_controller->direct_dma_commands > direct_before;
    if (!direct_dma_used)
        ok = 0;
    kfree(test_memory);
    if (direct_dma_used)
        serial_write("VANTAOS_NVME_DIRECT_DMA_OK\n");
    serial_write(ok ? "VANTAOS_NVME_IO_SELFTEST_OK\n" :
                      "VANTAOS_NVME_IO_SELFTEST_FAIL\n");
    return ok;
}
#endif

static const storage_pci_id_t nvme_ids[] = {
    {0xFFFFU, 0xFFFFU, NVME_CLASS, NVME_SUBCLASS, NVME_PROGIF}
};
static const storage_driver_t nvme_driver = {
    "NVMe", nvme_ids, sizeof(nvme_ids) / sizeof(nvme_ids[0]), nvme_probe
};

void nvme_initialize(void)
{
    if (!driver_registered) {
        if (!storage_driver_register(&nvme_driver)) {
            serial_write("VANTAOS_NVME_DRIVER_REGISTER_FAIL\n");
            return;
        }
        driver_registered = 1U;
    }
    uint32_t bound = storage_probe_pci_devices();
    if (!bound)
        serial_write("VANTAOS_NVME_NO_CONTROLLER_BOUND\n");
#ifdef VANTAOS_SELF_TEST
    (void)nvme_run_io_selftest();
#endif
}
