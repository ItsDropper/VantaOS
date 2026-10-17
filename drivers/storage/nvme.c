#include "nvme.h"
#include "block_device.h"
#include "dma.h"
#include "mmio.h"
#include "pci.h"
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

typedef struct {
    dma_page_t admin_submission;
    dma_page_t admin_completion;
    dma_page_t io_submission;
    dma_page_t io_completion;
    dma_page_t bounce;
    dma_page_t identify;
    volatile uint8_t* registers;
    volatile uint32_t* admin_submission_doorbell;
    volatile uint32_t* admin_completion_doorbell;
    volatile uint32_t* io_submission_doorbell;
    volatile uint32_t* io_completion_doorbell;
    uint64_t capacity_sectors;
    uint64_t doorbell_stride;
    uint32_t namespace_count;
    uint32_t controller_index;
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
    if (!dma_alloc_page(&c->bounce)) return 0;
    if (!dma_alloc_page(&c->identify)) return 0;
    return 1;
}

static void free_controller_memory(nvme_controller_t* c)
{
    dma_free_page(&c->identify);
    dma_free_page(&c->bounce);
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
        completion.result = queue[*head].result;
        completion.reserved = queue[*head].reserved;
        completion.submission_head = queue[*head].submission_head;
        completion.submission_id = queue[*head].submission_id;
        completion.command_id = queue[*head].command_id;
        completion.status_phase = queue[*head].status_phase;
        __asm__ volatile ("" ::: "memory");
        if ((completion.status_phase & 1U) == *phase) {
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
            if (status != 0U) {
                c->failed = 1;
                return 0;
            }
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
    __asm__ volatile ("" ::: "memory");
    *tail = (uint16_t)((*tail + 1U) % NVME_QUEUE_DEPTH);
    *doorbell = *tail;
    __asm__ volatile ("" ::: "memory");
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

static block_status_t namespace_transfer(nvme_namespace_t* ns, uint64_t lba,
                                         uint32_t count, void* buffer,
                                         int write)
{
    if (!ns || !ns->used || !buffer || count == 0)
        return BLOCK_STATUS_INVALID;
    nvme_controller_t* c = ns->controller;
    if (!c || !c->initialized || c->failed)
        return BLOCK_STATUS_NOT_READY;
    if (lba >= ns->sector_count || (uint64_t)count > ns->sector_count - lba)
        return BLOCK_STATUS_OUT_OF_RANGE;
    if ((uint64_t)count > UINT32_MAX / ns->sector_size)
        return BLOCK_STATUS_INVALID;

    uint64_t flags = spinlock_lock_irqsave(&c->io_lock);
    block_status_t status = BLOCK_STATUS_OK;
    uint8_t* user_buffer = (uint8_t*)buffer;
    for (uint32_t sector = 0; sector < count; ++sector) {
        if (c->failed) { status = BLOCK_STATUS_IO_ERROR; break; }
        if (write)
            copy_bytes(c->bounce.virtual_address,
                       user_buffer + (size_t)sector * ns->sector_size,
                       ns->sector_size);
        nvme_command_t command = {0};
        command.opcode = write ? NVME_IO_WRITE : NVME_IO_READ;
        command.namespace_id = ns->namespace_id;
        command.prp1 = c->bounce.physical_address;
        command.command_dword10 = (uint32_t)(lba + sector);
        command.command_dword11 = (uint32_t)((lba + sector) >> 32);
        command.command_dword12 = 0U; /* NLB=0 means one logical block. */
        if (!submit_command(c, 0, &command, 0)) {
            status = BLOCK_STATUS_IO_ERROR;
            break;
        }
        if (!write)
            copy_bytes(user_buffer + (size_t)sector * ns->sector_size,
                       c->bounce.virtual_address, ns->sector_size);
    }
    spinlock_unlock_irqrestore(&c->io_lock, flags);
    return status;
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
    ns->block.name[4] = (char)('0' + (c->controller_index % 10U));
    ns->block.name[5] = 'n';
    ns->block.name[6] = (char)('0' + (nsid % 10U));
    ns->block.name[7] = '\0';
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

    /* CAP is in the first 8 bytes; map enough for all spec-valid doorbell strides. */
    c->registers = (volatile uint8_t*)mmio_map_region(bar, 0x42000U);
    if (!c->registers) {
        free_controller_memory(c);
        return STORAGE_PROBE_FAILED;
    }
    uint64_t cap = mmio_read64(c->registers, 0x00U);
    uint32_t max_entries = (uint32_t)(cap & 0xFFFFU) + 1U;
    uint32_t timeout_units = (uint32_t)((cap >> 24) & 0xFFU);
    uint32_t stride_shift = (uint32_t)((cap >> 32) & 0x0FU);
    if (max_entries < NVME_QUEUE_DEPTH || stride_shift > 15U) {
        mmio_unmap_region((void*)c->registers, 0x42000U);
        free_controller_memory(c);
        return STORAGE_PROBE_FAILED;
    }
    c->doorbell_stride = 4ULL << stride_shift;
    uint64_t last_doorbell = 0x1000ULL + 3ULL * c->doorbell_stride + 4ULL;
    if (last_doorbell > 0x42000ULL) {
        mmio_unmap_region((void*)c->registers, 0x42000U);
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
    /* IOSQES=64-byte command, IOCQES=16-byte completion, MPS=4 KiB. */
    mmio_write32(c->registers, 0x14U, NVME_CC_EN | (6U << 16) | (4U << 20));
    if (!pci_enable_bus_mastering(pci)) goto fail;
    if (!wait_ready(c, 1)) goto fail;

    c->initialized = 1U;
    c->io_tail = c->io_head = 0U;
    c->io_phase = 1U;
    if (!identify(c, 0U, NVME_CNS_CONTROLLER)) goto fail;
    const uint8_t* controller_id = (const uint8_t*)c->identify.virtual_address;
    uint32_t namespace_total = *(const uint32_t*)(controller_id + 516U);
    if (!namespace_total) goto fail;

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
    (void)wait_ready(c, 0);
    mmio_unmap_region((void*)c->registers, 0x42000U);
    c->registers = 0;
    free_controller_memory(c);
    serial_write("VANTAOS_NVME_CONTROLLER_FAIL\n");
    return STORAGE_PROBE_FAILED;
}

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
}
