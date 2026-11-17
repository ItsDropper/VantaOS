/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

/*
 * Demand-paging regression suite. Every case runs a real ring-3 program
 * (kernel/selftest/vm/*.asm) and asserts on facts the kernel recorded about
 * that process: its exit status, how many demand pages it faulted in, the
 * last fault's class and address, and how many frames were reclaimed when its
 * address space died. Nothing is inferred from a marker the program prints.
 * Empty in ordinary builds; VANTAOS_SELF_TEST enables it.
 */
#include "process.h"
#include "process_exit_log.h"
#include "vm_fault.h"
#include "pmm.h"
#include "serial.h"
#include "smp.h"
#include "timer.h"
#include "paging.h"

#ifdef VANTAOS_SELF_TEST
#include "selftest.h"
#include "vm_selftest_programs.h"

#define VMT_PAGE 4096U
#define VMT_CASE_TIMEOUT_MS 8000U
#define VMT_USER_BASE PAGING_USER_BASE
#define VMT_EVENTS_EITHER 0xFFFFFFFFU

static int vmt_all_ok = 1;

static void vmt_hex(uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[19] = "0x";
    for (int i = 0; i < 16; i++)
        text[2 + i] = digits[(value >> (60 - 4 * i)) & 0xFU];
    text[18] = '\0';
    serial_write(text);
}

static void vmt_fail(const char* name, const char* what,
                     uint64_t expected, uint64_t actual)
{
    serial_write("VANTAOS_VM_CASE_DIAG ");
    serial_write(name);
    serial_write(": ");
    serial_write(what);
    serial_write(" expected=");
    vmt_hex(expected);
    serial_write(" actual=");
    vmt_hex(actual);
    serial_write("\n");
    vmt_all_ok = 0;
}

/* Region layout shared by every flat program (see the generator script). */
static const process_vm_region_spec_t vmt_flat_regions[] = {
    { VMT_ANON, 64U * VMT_PAGE, VM_PROT_READ | VM_PROT_WRITE, VM_REGION_ANON },
    { VMT_RO, VMT_PAGE, VM_PROT_READ, VM_REGION_ANON },
    { VMT_GUARD, VMT_PAGE, 0U, VM_REGION_GUARD },
    { VMT_OOMWIN, 4U * VMT_PAGE, VM_PROT_READ | VM_PROT_WRITE, VM_REGION_ANON },
    { VMT_SYSBUF, VMT_PAGE, VM_PROT_READ | VM_PROT_WRITE, VM_REGION_ANON },
};

typedef struct vmt_expect
{
    const char* name;
    int32_t status;
    uint32_t demand_faults;
    uint32_t fault_events;
    int check_class;           /* compare last_fault_class/address */
    vm_fault_class_t last_class;
    uint64_t last_address;
    uint64_t error_mask;       /* bits that must be set in the error code */
    uint64_t error_clear;      /* bits that must be clear */
} vmt_expect_t;

static uint64_t vmt_deadline(void)
{
    return timer_get_uptime_ms() + VMT_CASE_TIMEOUT_MS;
}

static int vmt_wait(uint32_t pid, const char* name, uint64_t mark,
                    process_exit_record_t* rec)
{
    const uint64_t deadline = vmt_deadline();
    while (timer_get_uptime_ms() < deadline)
    {
        if (process_exit_log_find(pid, mark, name, rec))
            return 1;
        __asm__ volatile ("pause" ::: "memory");
    }
    return 0;
}

static int vmt_check(const vmt_expect_t* e, const process_exit_record_t* r)
{
    int ok = 1;
    if (r->exit_status != e->status)
    { vmt_fail(e->name, "exit status", (uint64_t)(int64_t)e->status,
               (uint64_t)(int64_t)r->exit_status); ok = 0; }
    if (r->demand_faults != e->demand_faults)
    { vmt_fail(e->name, "demand faults", e->demand_faults,
               r->demand_faults); ok = 0; }
    if (e->fault_events == VMT_EVENTS_EITHER)
    {
        /* Hardware raises #GP for a non-canonical address; QEMU's TCG raises
           #PF. Both must be contained, and a #PF must classify as unmapped. */
        if (r->fault_events > 1U ||
            (r->fault_events == 1U &&
             r->last_fault_class != (uint32_t)VM_FAULT_NO_REGION))
        { vmt_fail(e->name, "non-canonical fault shape", 1U,
                   r->fault_events); ok = 0; }
    }
    else if (r->fault_events != e->fault_events)
    { vmt_fail(e->name, "fault events", e->fault_events,
               r->fault_events); ok = 0; }
    /* Leak check: every demand frame the process owned came back. */
    if (r->frames_reclaimed != r->resident_at_exit ||
        r->frames_reclaimed != r->demand_faults)
    { vmt_fail(e->name, "frames reclaimed", r->resident_at_exit,
               r->frames_reclaimed); ok = 0; }
    if (e->check_class)
    {
        if (r->last_fault_class != (uint32_t)e->last_class)
        { vmt_fail(e->name, "fault class", (uint64_t)e->last_class,
                   r->last_fault_class); ok = 0; }
        if ((r->last_fault_address & ~(uint64_t)(VMT_PAGE - 1U)) != e->last_address)
        { vmt_fail(e->name, "fault page", e->last_address,
                   r->last_fault_address); ok = 0; }
        if ((r->last_fault_error & e->error_mask) != e->error_mask ||
            (r->last_fault_error & e->error_clear) != 0U)
        { vmt_fail(e->name, "fault error code", e->error_mask,
                   r->last_fault_error); ok = 0; }
    }
    return ok;
}

static void vmt_report(const char* name, int ok)
{
    if (ok)
    {
        serial_write("VANTAOS_VM_");
        serial_write(name);
        serial_write("_OK\n");
    }
    else
        vmt_all_ok = 0;
}

static void vmt_patch32(uint8_t* image, uint32_t offset, uint32_t value)
{
    for (uint32_t i = 0; i < 4U; i++)
        image[offset + i] = (uint8_t)(value >> (8U * i));
}

static int vmt_spawn_flat(const char* name, const uint8_t* code, size_t length,
                          uint32_t pattern, uint32_t count)
{
    uint8_t image[512];
    if (length > sizeof(image))
        return -1;
    for (size_t i = 0; i < length; i++)
        image[i] = code[i];
    if (code == vmt_prog_zero_rw)
    {
        vmt_patch32(image, VMT_ZERO_RW_PATTERN_OFFSET, pattern);
        vmt_patch32(image, VMT_ZERO_RW_COUNT_OFFSET, count);
    }
    return process_create_user_with_regions(
        name, process_current_pid(), image, length, vmt_flat_regions,
        (uint32_t)(sizeof(vmt_flat_regions) / sizeof(vmt_flat_regions[0])));
}

static int vmt_run_flat(const vmt_expect_t* e, const uint8_t* code,
                        size_t length, uint32_t pattern, uint32_t count)
{
    uint64_t mark = process_exit_log_sequence();
    int pid = vmt_spawn_flat(e->name, code, length, pattern, count);
    process_exit_record_t rec;
    if (pid <= 0)
    { vmt_fail(e->name, "spawn", 1U, 0U); return 0; }
    if (!vmt_wait((uint32_t)pid, e->name, mark, &rec))
    { vmt_fail(e->name, "timeout waiting for exit", 1U, 0U); return 0; }
    return vmt_check(e, &rec);
}

/* ---- ELF images ------------------------------------------------------- */

struct __attribute__((packed)) vmt_ehdr
{
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct __attribute__((packed)) vmt_phdr
{
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};

static uint8_t vmt_elf_image[0x1100];

/* Text-only ELF, or text plus one RW data segment with a lazy bss tail. */
static size_t vmt_build_elf(const uint8_t* code, size_t code_len,
                            int with_data)
{
    for (size_t i = 0; i < sizeof(vmt_elf_image); i++)
        vmt_elf_image[i] = 0U;
    struct vmt_ehdr* h = (struct vmt_ehdr*)vmt_elf_image;
    struct vmt_phdr* ph = (struct vmt_phdr*)(vmt_elf_image + sizeof(*h));
    const uint32_t phnum = with_data ? 2U : 1U;
    const uint64_t code_off = sizeof(*h) + phnum * sizeof(*ph);

    h->ident[0] = 0x7F; h->ident[1] = 'E'; h->ident[2] = 'L';
    h->ident[3] = 'F'; h->ident[4] = 2; h->ident[5] = 1; h->ident[6] = 1;
    h->type = 2; h->machine = 62; h->version = 1;
    h->entry = VMT_USER_BASE + code_off;
    h->phoff = sizeof(*h);
    h->ehsize = sizeof(*h); h->phentsize = sizeof(*ph); h->phnum = (uint16_t)phnum;

    ph[0].type = 1; ph[0].flags = 5U; /* R+X */
    ph[0].offset = code_off; ph[0].vaddr = VMT_USER_BASE + code_off;
    ph[0].paddr = ph[0].vaddr; ph[0].filesz = code_len; ph[0].memsz = code_len;
    ph[0].align = 1;
    for (size_t i = 0; i < code_len; i++)
        vmt_elf_image[code_off + i] = code[i];

    size_t total = code_off + code_len;
    if (with_data)
    {
        ph[1].type = 1; ph[1].flags = 6U; /* R+W */
        ph[1].offset = 0x1000U; ph[1].vaddr = VMT_DATA; ph[1].paddr = VMT_DATA;
        ph[1].filesz = 16U; ph[1].memsz = 16U * VMT_PAGE; ph[1].align = 1;
        vmt_elf_image[0x1000] = 0x5A;
        total = 0x1000U + 16U;
    }
    return total;
}

static int vmt_run_elf(const vmt_expect_t* e, size_t image_size)
{
    uint64_t mark = process_exit_log_sequence();
    int pid = process_create_user_elf(e->name, process_current_pid(),
                                      vmt_elf_image, image_size);
    process_exit_record_t rec;
    if (pid <= 0)
    { vmt_fail(e->name, "elf spawn", 1U, 0U); return 0; }
    if (!vmt_wait((uint32_t)pid, e->name, mark, &rec))
    { vmt_fail(e->name, "timeout waiting for exit", 1U, 0U); return 0; }
    return vmt_check(e, &rec);
}

/* ---- Suites ------------------------------------------------------------ */

static void vmt_fault_matrix(void)
{
    const uint64_t kInstr = VM_PF_INSTR, kWrite = VM_PF_WRITE;
    const uint64_t kPresent = VM_PF_PRESENT;

    const vmt_expect_t lazy = { "LAZY_ZERO_RW", 0, 16, 16, 0, 0, 0, 0, 0 };
    vmt_report("LAZY_ZERO_RW",
        vmt_run_flat(&lazy, vmt_prog_zero_rw, sizeof(vmt_prog_zero_rw),
                     0xA5A50000U, 16U));

    const vmt_expect_t exec = { "EXEC_ANON_DENIED", -1, 0, 1, 1,
        VM_FAULT_ACCESS_DENIED, VMT_ANON, kInstr, kPresent };
    vmt_report("EXEC_ANON_DENIED",
        vmt_run_flat(&exec, vmt_prog_exec_anon, sizeof(vmt_prog_exec_anon),
                     0, 0));

    const vmt_expect_t rowf = { "RO_WRITE_FIRST_DENIED", -1, 0, 1, 1,
        VM_FAULT_ACCESS_DENIED, VMT_RO, kWrite, kPresent };
    vmt_report("RO_WRITE_FIRST_DENIED",
        vmt_run_flat(&rowf, vmt_prog_ro_write_first,
                     sizeof(vmt_prog_ro_write_first), 0, 0));

    const vmt_expect_t rorw = { "RO_WRITE_PROTECTION", -1, 1, 2, 1,
        VM_FAULT_PROTECTION, VMT_RO, kWrite | kPresent, 0 };
    vmt_report("RO_WRITE_PROTECTION",
        vmt_run_flat(&rorw, vmt_prog_ro_read_then_write,
                     sizeof(vmt_prog_ro_read_then_write), 0, 0));

    const vmt_expect_t guard = { "GUARD_REGION", -1, 0, 1, 1,
        VM_FAULT_GUARD, VMT_GUARD, 0, kPresent };
    vmt_report("GUARD_REGION",
        vmt_run_flat(&guard, vmt_prog_guard_read, sizeof(vmt_prog_guard_read),
                     0, 0));

    const vmt_expect_t gap = { "UNRESERVED_ADDRESS", -1, 0, 1, 1,
        VM_FAULT_NO_REGION, VMT_GAP, 0, kPresent };
    vmt_report("UNRESERVED_ADDRESS",
        vmt_run_flat(&gap, vmt_prog_gap_read, sizeof(vmt_prog_gap_read), 0, 0));

    const vmt_expect_t kread = { "KERNEL_READ_CONTAINED", -1, 0, 1, 1,
        VM_FAULT_NO_REGION, 0x200000U, 0, 0 };
    vmt_report("KERNEL_READ_CONTAINED",
        vmt_run_flat(&kread, vmt_prog_kernel_read,
                     sizeof(vmt_prog_kernel_read), 0, 0));

    const vmt_expect_t kwrite = { "KERNEL_WRITE_CONTAINED", -1, 0, 1, 1,
        VM_FAULT_NO_REGION, 0x100000U, kWrite, 0 };
    vmt_report("KERNEL_WRITE_CONTAINED",
        vmt_run_flat(&kwrite, vmt_prog_kernel_write,
                     sizeof(vmt_prog_kernel_write), 0, 0));

    const vmt_expect_t khigh = { "KERNEL_HIGH_CONTAINED", -1, 0, 1, 1,
        VM_FAULT_NO_REGION, 0xFFFF800000000000ULL, 0, 0 };
    vmt_report("KERNEL_HIGH_CONTAINED",
        vmt_run_flat(&khigh, vmt_prog_kernel_high_read,
                     sizeof(vmt_prog_kernel_high_read), 0, 0));

    /* #GP, not #PF: contained, and the page-fault handler never saw it. */
    const vmt_expect_t nc = { "NONCANONICAL_CONTAINED", -1, 0, VMT_EVENTS_EITHER, 0, 0, 0, 0, 0 };
    vmt_report("NONCANONICAL_CONTAINED",
        vmt_run_flat(&nc, vmt_prog_noncanonical, sizeof(vmt_prog_noncanonical),
                     0, 0));

    const vmt_expect_t pref = { "SYSCALL_PREFAULT", 0, 1, 0, 0, 0, 0, 0, 0 };
    vmt_report("SYSCALL_PREFAULT",
        vmt_run_flat(&pref, vmt_prog_syscall_prefault,
                     sizeof(vmt_prog_syscall_prefault), 0, 0));
}

static void vmt_oom_case(void)
{
    /* Valid demand access, but the frame allocator "fails": the process must
       die cleanly with -ENOMEM, nothing may be mapped, nothing may leak. */
    const vmt_expect_t oom = { "OOM_CONTAINED", -12, 0, 1, 1,
        VM_FAULT_DEMAND_MAP, VMT_OOMWIN, VM_PF_WRITE, VM_PF_PRESENT };
    vm_fault_stats_t before, after;
    vm_fault_get_stats(&before);
    vm_fault_inject_alloc_failures(VMT_OOMWIN, 4U * VMT_PAGE, 1U);
    int ok = vmt_run_flat(&oom, vmt_prog_oom_touch,
                          sizeof(vmt_prog_oom_touch), 0, 0);
    uint32_t armed = vm_fault_injected_failures_pending();
    vm_fault_inject_alloc_failures(0U, 0U, 0U);
    vm_fault_get_stats(&after);
    if (armed != 0U)
    { vmt_fail(oom.name, "injection consumed", 0U, armed); ok = 0; }
    if (after.out_of_memory != before.out_of_memory + 1U)
    { vmt_fail(oom.name, "oom counter", before.out_of_memory + 1U,
               after.out_of_memory); ok = 0; }
    vmt_report(oom.name, ok);
}

static void vmt_elf_cases(void)
{
    /* Stack growth by demand: pages below the eager top four fault in. */
    size_t size = vmt_build_elf(vmt_prog_stack_grow,
                                sizeof(vmt_prog_stack_grow), 0);
    uint32_t grow_pages = 40U;
    vmt_patch32(vmt_elf_image, 64U + 56U + VMT_STACK_GROW_COUNT_OFFSET,
                grow_pages);
    /* sub rsp,4096 #1..#3 land in the eager pages; #4.. fault in. */
    const vmt_expect_t grow = { "STACK_GROWTH", 0, grow_pages - 3U,
                                grow_pages - 3U, 0, 0, 0, 0, 0 };
    vmt_report("STACK_GROWTH", vmt_run_elf(&grow, size));

    /* Overflow: 60 lazy stack pages fault in, then the guard page kills it. */
    size = vmt_build_elf(vmt_prog_stack_grow, sizeof(vmt_prog_stack_grow), 0);
    vmt_patch32(vmt_elf_image, 64U + 56U + VMT_STACK_GROW_COUNT_OFFSET, 100U);
    const uintptr_t stack_top = (uintptr_t)PAGING_USER_END + 1U;
    const uintptr_t guard_page = stack_top -
        (uintptr_t)PROCESS_USER_MAX_PAGES * VMT_PAGE - VMT_PAGE;
    const vmt_expect_t overflow = { "STACK_GUARD", -1,
        PROCESS_USER_MAX_PAGES - 4U, PROCESS_USER_MAX_PAGES - 4U + 1U, 1,
        VM_FAULT_GUARD, guard_page, VM_PF_WRITE, VM_PF_PRESENT };
    vmt_report("STACK_GUARD", vmt_run_elf(&overflow, size));

    /* ELF .bss: file-backed page present, tail pages lazily zero-filled. */
    size = vmt_build_elf(vmt_prog_bss_check, sizeof(vmt_prog_bss_check), 1);
    const vmt_expect_t bss = { "ELF_LAZY_BSS", 0, 1, 1, 0, 0, 0, 0, 0 };
    vmt_report("ELF_LAZY_BSS", vmt_run_elf(&bss, size));
}

static void vmt_repeat_case(void)
{
    /* Each run leaves nonzero data in its pages; the next must still see
       zeros (frame zeroing, TLB flush on address-space switch/destroy) and
       the PMM must not drift (reclaim). */
    const uint32_t iterations = 20U;
    const uint32_t free_before = pmm_get_free_blocks();
    int ok = 1;
    for (uint32_t i = 0; i < iterations && ok; i++)
    {
        const vmt_expect_t e = { "REPEATED_ALLOC_RELEASE", 0, 16, 16,
                                 0, 0, 0, 0, 0 };
        ok = vmt_run_flat(&e, vmt_prog_zero_rw, sizeof(vmt_prog_zero_rw),
                          0x5A5A0000U + i * 0x101U, 16U);
    }
    const uint32_t free_after = pmm_get_free_blocks();
    /* 20 x 16 frames would be 320 if reclaim leaked; allow unrelated noise. */
    if (free_before > free_after && free_before - free_after > 64U)
    { vmt_fail("REPEATED_ALLOC_RELEASE", "pmm drift", free_before, free_after);
      ok = 0; }
    vmt_report("REPEATED_ALLOC_RELEASE", ok);
}

#define VMT_CONCURRENT_MAX 8U

static void vmt_concurrent_case(void)
{
    /* Many processes fault 64 pages each at once, spread across CPUs. Each
       carries a distinct pattern, so two processes sharing a frame (lost
       ownership race) would corrupt each other's data and fail. */
    uint32_t cpus = smp_cpu_count();
    uint32_t count = cpus * 2U;
    if (count < 4U) count = 4U;
    if (count > VMT_CONCURRENT_MAX) count = VMT_CONCURRENT_MAX;

    uint64_t mark = process_exit_log_sequence();
    int pids[VMT_CONCURRENT_MAX] = {0};
    int ok = 1;
    for (uint32_t i = 0; i < count; i++)
    {
        pids[i] = vmt_spawn_flat("CONCURRENT_FAULTS", vmt_prog_zero_rw,
                                 sizeof(vmt_prog_zero_rw),
                                 0x10000000U * (i + 1U), 64U);
        if (pids[i] <= 0)
        { vmt_fail("CONCURRENT_FAULTS", "spawn", 1U, 0U); ok = 0; break; }
        (void)process_set_affinity((uint32_t)pids[i], i % cpus);
    }
    const vmt_expect_t e = { "CONCURRENT_FAULTS", 0, 64, 64, 0, 0, 0, 0, 0 };
    for (uint32_t i = 0; i < count && pids[i] > 0; i++)
    {
        process_exit_record_t rec;
        if (!vmt_wait((uint32_t)pids[i], e.name, mark, &rec))
        { vmt_fail(e.name, "timeout waiting for exit", 1U, 0U); ok = 0; }
        else if (!vmt_check(&e, &rec))
            ok = 0;
    }
    vmt_report("CONCURRENT_FAULTS", ok);
}

/* ---- Runtime mapping: real ring-3 syscalls --------------------------- */

static int vmt_runtime_mapping_case(void)
{
    const char* name = "RUNTIME_MAPPING";
    /*
     * brk is anchored to the end of the executable image. Flat test processes
     * place their one-page stack immediately after the flat image, so growing
     * brk would correctly collide with that stack. Use a real ELF process,
     * whose stack is at the top of the user range, to test the heap ABI.
     */
    const size_t image_size = vmt_build_elf(vmt_prog_runtime_mapping,
                                             sizeof(vmt_prog_runtime_mapping), 0);
    const uint64_t mark = process_exit_log_sequence();
    int pid = process_create_user_elf(name, process_current_pid(),
                                      vmt_elf_image, image_size);
    process_exit_record_t rec;
    if (pid <= 0)
    {
        vmt_fail(name, "spawn", 1U, 0U);
        return 0;
    }
    if (!vmt_wait((uint32_t)pid, name, mark, &rec))
    {
        vmt_fail(name, "timeout waiting for exit", 1U, 0U);
        return 0;
    }

    int ok = 1;
    /* Four mmap pages, one remapped page, and two brk pages twice. */
    if (rec.exit_status != 0)
    { vmt_fail(name, "exit status", 0U, (uint64_t)(int64_t)rec.exit_status); ok = 0; }
    if (rec.demand_faults != 9U || rec.fault_events != 9U)
    { vmt_fail(name, "demand faults/events", 9U, rec.demand_faults); ok = 0; }
    /* munmap and brk shrink must release every demand page before exit. */
    if (rec.resident_at_exit != 0U || rec.frames_reclaimed != 0U)
    { vmt_fail(name, "resident frames after explicit release", 0U,
               rec.resident_at_exit + rec.frames_reclaimed); ok = 0; }
    if (rec.last_fault_class != (uint32_t)VM_FAULT_DEMAND_MAP)
    { vmt_fail(name, "last fault class", VM_FAULT_DEMAND_MAP,
               rec.last_fault_class); ok = 0; }
    return ok;
}

static void vmt_driver(void)
{
    vmt_fault_matrix();
    vmt_oom_case();
    vmt_elf_cases();
    vmt_repeat_case();
    vmt_concurrent_case();
    vmt_report("RUNTIME_MAPPING", vmt_runtime_mapping_case());

    serial_write(vmt_all_ok ? "VANTAOS_VM_DEMAND_PAGING_OK\n"
                            : "VANTAOS_VM_DEMAND_PAGING_FAIL\n");
}

void demand_paging_selftest_initialize(void)
{
    if (process_create_kernel_any("vm-demand-selftest", process_current_pid(),
                                  vmt_driver) <= 0)
        serial_write("VANTAOS_VM_DEMAND_PAGING_START_FAIL\n");
}
#endif
