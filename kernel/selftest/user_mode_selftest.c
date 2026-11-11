/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "user_mode_selftest.h"

#include "cpu_local.h"
#include "gdt.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define USER_TEST_KERNEL_ADDRESS 0x00100000ULL
#define USER_TEST_SYSCALL_MODE 1U
#define USER_TEST_PROTECTION_MODE 2U
#define USER_TEST_GS_MODE 3U
#define IA32_GS_BASE_MSR 0xC0000101U
#define USER_TEST_KERNEL_STACK_SIZE 16384U
#define IDENTITY_MAP_LIMIT 0x40000000ULL

extern void user_mode_test_enter(uintptr_t instruction_pointer,
                                 uintptr_t user_stack_pointer);
extern void user_mode_test_return_trampoline(void);
extern unsigned char stack_top;

uintptr_t user_mode_test_saved_rsp;
uint64_t user_mode_test_saved_rflags;
volatile uint32_t user_mode_test_exit_requested;

static uint8_t user_test_kernel_stack[USER_TEST_KERNEL_STACK_SIZE]
    __attribute__((aligned(16)));
static volatile uint32_t user_test_active;
static volatile uint32_t user_test_cpu_id;
static volatile uint32_t user_test_mode;
static volatile uint32_t user_test_syscall_seen;
static volatile uint32_t user_test_protection_fault_seen;
static volatile uint32_t user_test_gs_fault_seen;
static volatile uint32_t user_test_unexpected;

int user_mode_selftest_is_active(void)
{
    return __atomic_load_n(&user_test_active, __ATOMIC_ACQUIRE) != 0U;
}

static int user_test_owned_by_current_cpu(void)
{
    const cpu_local_t* cpu = cpu_local_current();
    return cpu &&
        cpu->logical_id ==
            __atomic_load_n(&user_test_cpu_id, __ATOMIC_ACQUIRE);
}

static void write_hex(uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    char buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (unsigned int i = 0; i < 16U; i++)
        buffer[i + 2U] = digits[(value >> (60U - i * 4U)) & 0x0FU];
    buffer[18] = '\0';
    serial_write(buffer);
}

static void copy_bytes(void* destination, const uint8_t* source, size_t count)
{
    uint8_t* out = (uint8_t*)destination;
    for (size_t i = 0; i < count; i++)
        out[i] = source[i];
}

static void user_test_redirect_to_kernel(struct exception_frame* frame)
{
    __atomic_store_n(&user_mode_test_exit_requested, 1U, __ATOMIC_RELEASE);
    frame->rip = (uintptr_t)user_mode_test_return_trampoline;
    frame->cs = 0x08U;
    /* Return to a controlled kernel trampoline with maskable interrupts off. */
    frame->rflags = 0x02U;
}

void user_mode_selftest_syscall_dispatch(struct exception_frame* frame)
{
    if (!frame)
        return;

    if (__atomic_load_n(&user_test_active, __ATOMIC_ACQUIRE) == 0U ||
        (frame->cs & 3U) != 3U ||
        !user_test_owned_by_current_cpu())
    {
        /* No general syscall ABI is exposed by this test-only IDT gate. */
        frame->rax = (uint64_t)(int64_t)-38;
        return;
    }

    const uint32_t mode =
        __atomic_load_n(&user_test_mode, __ATOMIC_ACQUIRE);
    if (mode == USER_TEST_SYSCALL_MODE && frame->rax == 1U)
        __atomic_store_n(&user_test_syscall_seen, 1U, __ATOMIC_RELEASE);
    else
        __atomic_store_n(&user_test_unexpected, 1U, __ATOMIC_RELEASE);

    user_test_redirect_to_kernel(frame);
}

int user_mode_selftest_handle_exception(unsigned int number,
                                        struct exception_frame* frame)
{
    /*
     * Asynchronous NMI and architecturally fatal machine exceptions must
     * reach the kernel's fatal path even while a ring-3 probe is active.
     */
    if (number == 2U || number == 8U || number == 18U)
        return 0;

    if (!frame ||
        __atomic_load_n(&user_test_active, __ATOMIC_ACQUIRE) == 0U ||
        !user_test_owned_by_current_cpu())
        return 0;

    if ((frame->cs & 3U) != 3U)
    {
        /*
         * An exception during IRETQ/entry is a failed probe too. Recover to
         * the known kernel continuation so CI reports the fault instead of
         * hanging without a useful serial trace.
         */
        uint32_t low, high;
        __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high)
                          : "c"(IA32_GS_BASE_MSR));
        if ((low | high) == 0U)
            __asm__ volatile ("swapgs" ::: "memory");
        __atomic_store_n(&user_test_unexpected, 1U, __ATOMIC_RELEASE);
        serial_write("VANTAOS_RING3_KERNEL_EXCEPTION number=");
        write_hex(number);
        serial_write(" rip=");
        write_hex(frame->rip);
        serial_write(" error=");
        write_hex(frame->error_code);
        serial_write("\n");
        user_test_redirect_to_kernel(frame);
        return 1;
    }

    uintptr_t fault_address = 0;
    if (number == 14U)
        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));

    const uint32_t mode =
        __atomic_load_n(&user_test_mode, __ATOMIC_ACQUIRE);
    if (mode == USER_TEST_PROTECTION_MODE && number == 14U &&
        fault_address == (uintptr_t)USER_TEST_KERNEL_ADDRESS &&
        (frame->error_code & 0x5U) == 0x5U)
    {
        __atomic_store_n(&user_test_protection_fault_seen, 1U,
                         __ATOMIC_RELEASE);
    }
    else if (mode == USER_TEST_GS_MODE && number == 14U &&
             fault_address == 0U &&
             (frame->error_code & 0x4U) == 0x4U)
    {
        __atomic_store_n(&user_test_gs_fault_seen, 1U, __ATOMIC_RELEASE);
    }
    else
    {
        __atomic_store_n(&user_test_unexpected, 1U, __ATOMIC_RELEASE);
    }

    serial_write("VANTAOS_RING3_USER_EXCEPTION_HANDLED\n");
    user_test_redirect_to_kernel(frame);
    return 1;
}

static int run_user_probe(unsigned int mode,
                          uintptr_t user_stack_top)
{
    __atomic_store_n(&user_mode_test_exit_requested, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&user_test_syscall_seen, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&user_test_protection_fault_seen, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&user_test_gs_fault_seen, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&user_test_unexpected, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&user_test_mode, mode, __ATOMIC_RELEASE);
    __atomic_store_n(&user_test_cpu_id,
                     cpu_local_current()->logical_id, __ATOMIC_RELEASE);
    __atomic_store_n(&user_test_active, 1U, __ATOMIC_RELEASE);

    serial_write(mode == USER_TEST_SYSCALL_MODE ?
        "VANTAOS_RING3_ENTER_SYSCALL\n" :
        (mode == USER_TEST_GS_MODE ?
         "VANTAOS_RING3_ENTER_GS_ISOLATION\n" :
         "VANTAOS_RING3_ENTER_ISOLATION\n"));
    user_mode_test_enter((uintptr_t)PAGING_USER_BASE, user_stack_top);
    serial_write(mode == USER_TEST_SYSCALL_MODE ?
        "VANTAOS_RING3_RETURN_SYSCALL\n" :
        (mode == USER_TEST_GS_MODE ?
         "VANTAOS_RING3_RETURN_GS_ISOLATION\n" :
         "VANTAOS_RING3_RETURN_ISOLATION\n"));

    __atomic_store_n(&user_test_active, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&user_mode_test_exit_requested, 0U, __ATOMIC_RELEASE);
    if (mode == USER_TEST_SYSCALL_MODE)
        return __atomic_load_n(&user_test_syscall_seen, __ATOMIC_ACQUIRE) != 0U &&
               __atomic_load_n(&user_test_unexpected, __ATOMIC_ACQUIRE) == 0U;

    if (mode == USER_TEST_GS_MODE)
        return __atomic_load_n(&user_test_gs_fault_seen, __ATOMIC_ACQUIRE) != 0U &&
               __atomic_load_n(&user_test_unexpected, __ATOMIC_ACQUIRE) == 0U;

    return __atomic_load_n(&user_test_protection_fault_seen,
                           __ATOMIC_ACQUIRE) != 0U &&
           __atomic_load_n(&user_test_unexpected, __ATOMIC_ACQUIRE) == 0U;
}


static int user_address_space_isolation_probe(void)
{
    const uint32_t free_before = pmm_get_free_blocks();
    paging_address_space_t* first = paging_create_address_space();
    paging_address_space_t* second = paging_create_address_space();
    void* first_frame = pmm_alloc_block();
    void* second_frame = pmm_alloc_block();
    const uintptr_t probe_address = PAGING_USER_BASE + 0x00200000U;
    int first_mapped = 0;
    int second_mapped = 0;
    int ok = first && second && first_frame && second_frame &&
        (uintptr_t)first_frame < IDENTITY_MAP_LIMIT &&
        (uintptr_t)second_frame < IDENTITY_MAP_LIMIT;

    if (ok)
    {
        *(volatile uint8_t*)first_frame = 0xA5U;
        *(volatile uint8_t*)second_frame = 0x5AU;
        first_mapped = paging_map_user_page_ex(
            first, probe_address, (uintptr_t)first_frame, 0, 1);
        second_mapped = paging_map_user_page_ex(
            second, probe_address, (uintptr_t)second_frame, 1, 0);
        ok = first_mapped && second_mapped;
    }

    if (ok)
    {
        paging_switch_address_space(first);
        uint64_t first_flags = paging_get_page_flags(probe_address);
        ok = paging_get_current_address_space() == first &&
            paging_get_physical(probe_address) == (uintptr_t)first_frame &&
            *(volatile uint8_t*)paging_get_physical(probe_address) == 0xA5U &&
            (first_flags & PAGE_USER) != 0U &&
            (first_flags & PAGE_WRITE) == 0U &&
            (first_flags & PAGE_NX) == 0U &&
            paging_user_range_valid(first, probe_address, 1U, 0) &&
            !paging_user_range_valid(first, probe_address, 1U, 1) &&
            !paging_user_range_valid(first, probe_address + PAGE_SIZE, 1U, 0);
    }

    if (ok)
    {
        paging_switch_address_space(second);
        uint64_t second_flags = paging_get_page_flags(probe_address);
        ok = paging_get_current_address_space() == second &&
            paging_get_physical(probe_address) == (uintptr_t)second_frame &&
            *(volatile uint8_t*)paging_get_physical(probe_address) == 0x5AU &&
            (second_flags & PAGE_USER) != 0U &&
            (second_flags & PAGE_WRITE) != 0U &&
            (second_flags & PAGE_NX) != 0U &&
            paging_user_range_valid(second, probe_address, 1U, 1);
    }

    paging_switch_address_space(paging_get_kernel_address_space());
    int first_destroyed = !first || paging_destroy_address_space(first);
    int second_destroyed = !second || paging_destroy_address_space(second);

    /* A mapped frame is freed only after its address space is retired. */
    if (first_frame && (!first_mapped || first_destroyed))
        pmm_free_block(first_frame);
    if (second_frame && (!second_mapped || second_destroyed))
        pmm_free_block(second_frame);

    ok = ok && first_destroyed && second_destroyed &&
         pmm_get_free_blocks() == free_before;
    serial_write(ok ?
        "VANTAOS_USER_ADDRESS_SPACE_ISOLATION_OK\n" :
        "VANTAOS_USER_ADDRESS_SPACE_ISOLATION_FAIL\n");
    return ok;
}

int user_mode_selftest_run(void)
{
    serial_write("VANTAOS_RING3_TEST_START\n");
    paging_address_space_t* space = paging_create_address_space();
    void* code_frame = pmm_alloc_block();
    void* stack_frame = pmm_alloc_block();
    int code_mapped = 0;
    int stack_mapped = 0;
    int syscall_ok = 0;
    int isolation_ok = 0;
    int gs_ok = 0;
    int address_space_ok = user_address_space_isolation_probe();
    int ok = space != NULL && code_frame != NULL && stack_frame != NULL &&
             address_space_ok;
    const uint32_t cpu_id = process_current_cpu_id();

    if (!ok || cpu_id >= CPU_LOCAL_MAX ||
        (uintptr_t)code_frame >= IDENTITY_MAP_LIMIT ||
        (uintptr_t)stack_frame >= IDENTITY_MAP_LIMIT)
        ok = 0;

    if (ok)
    {
        code_mapped = paging_map_user_page(
            space, (uintptr_t)PAGING_USER_BASE,
            (uintptr_t)code_frame, 0);
        stack_mapped = paging_map_user_page(
            space, (uintptr_t)(PAGING_USER_BASE + PAGE_SIZE),
            (uintptr_t)stack_frame, 1);
        ok = code_mapped && stack_mapped;
    }

    if (ok)
    {
        serial_write("VANTAOS_RING3_TEST_MAPPINGS_READY\n");
        /* mov eax,1; int 0x80; ud2 -- the test-only exit syscall. */
        static const uint8_t syscall_program[] = {
            0xB8, 0x01, 0x00, 0x00, 0x00, 0xCD, 0x80, 0x0F, 0x0B
        };
        copy_bytes(code_frame, syscall_program, sizeof(syscall_program));

        if (!gdt_set_kernel_stack(
                cpu_id, (uintptr_t)user_test_kernel_stack +
                        sizeof(user_test_kernel_stack)))
            ok = 0;
    }

    if (ok)
    {
        paging_switch_address_space(space);
        ok = paging_get_current_address_space() == space;
        if (ok)
            serial_write("VANTAOS_RING3_TEST_ADDRESS_SPACE_ACTIVE\n");
        if (ok)
            syscall_ok = run_user_probe(
                USER_TEST_SYSCALL_MODE,
                (uintptr_t)(PAGING_USER_BASE + 2U * PAGE_SIZE - 16U));
    }

    if (space && code_mapped && stack_mapped &&
        paging_get_current_address_space() == space)
    {
        /*
         * Attempt a read from supervisor-only identity-mapped kernel memory.
         * If CPL3 isolation fails, the program reaches syscall 2 instead.
         */
        static const uint8_t protection_program[] = {
            0x48, 0xB8, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x8B, 0x00,
            0xB8, 0x02, 0x00, 0x00, 0x00, 0xCD, 0x80, 0x0F, 0x0B
        };
        copy_bytes(code_frame, protection_program,
                   sizeof(protection_program));
        isolation_ok = run_user_probe(
            USER_TEST_PROTECTION_MODE,
            (uintptr_t)(PAGING_USER_BASE + 2U * PAGE_SIZE - 16U));
    }
    if (space && code_mapped && stack_mapped &&
        paging_get_current_address_space() == space)
    {
        /* mov rax, qword ptr gs:[0]; a user GS base of zero must fault at VA 0. */
        static const uint8_t gs_program[] = {
            0x65, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00
        };
        copy_bytes(code_frame, gs_program, sizeof(gs_program));
        gs_ok = run_user_probe(
            USER_TEST_GS_MODE,
            (uintptr_t)(PAGING_USER_BASE + 2U * PAGE_SIZE - 16U));
    }
    ok = ok && syscall_ok && isolation_ok && gs_ok;

    /* Never destroy an address space while it is loaded in CR3. */
    if (paging_get_current_address_space() == space)
        paging_switch_address_space(paging_get_kernel_address_space());

    if (space)
    {
        if (code_mapped)
            (void)paging_unmap_user_page(space, (uintptr_t)PAGING_USER_BASE);
        if (stack_mapped)
            (void)paging_unmap_user_page(
                space, (uintptr_t)(PAGING_USER_BASE + PAGE_SIZE));
        paging_destroy_address_space(space);
    }

    if (code_frame)
        pmm_free_block(code_frame);
    if (stack_frame)
        pmm_free_block(stack_frame);

    /* Restore a mapped, stable boot stack for any later privilege transition. */
    (void)gdt_set_kernel_stack(cpu_id, (uintptr_t)&stack_top);

    serial_write(syscall_ok ?
        "VANTAOS_RING3_SYSCALL_OK\n" :
        "VANTAOS_RING3_SYSCALL_FAIL\n");
    serial_write(isolation_ok ?
        "VANTAOS_RING3_KERNEL_ISOLATION_OK\n" :
        "VANTAOS_RING3_KERNEL_ISOLATION_FAIL\n");
    serial_write(gs_ok ?
        "VANTAOS_RING3_GS_ISOLATION_OK\n" :
        "VANTAOS_RING3_GS_ISOLATION_FAIL\n");
    return ok && address_space_ok;
}


int user_mode_process_smoke_test_start(void)
{
    static const uint8_t flat_image[] = {
        0xB8, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xBF, 0x20, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
        0xBE, 0x18, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        0xB8, 0x00, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        0x90, 0x90, 0x90,
        'V','A','N','T','A','O','S','_',
        'U','S','E','R','_','P','R','O','G','R','A','M','_','O','K','\n'
    };
    int flat_pid = process_create_user(
        "ring3-flat-smoke", process_current_pid(),
        flat_image, sizeof(flat_image));
    if (flat_pid <= 0)
    {
        serial_write("VANTAOS_USER_PROCESS_CREATE_FAIL\n");
        return 0;
    }

    /*
     * Construct a minimal valid ELF64 ET_EXEC in memory. Its one PT_LOAD
     * segment is RX and contains a write/exit program plus its message.
     */
    struct __attribute__((packed)) test_elf_header
    {
        uint8_t ident[16];
        uint16_t type, machine;
        uint32_t version;
        uint64_t entry, phoff, shoff;
        uint32_t flags;
        uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
    };
    struct __attribute__((packed)) test_elf_program_header
    {
        uint32_t type, flags;
        uint64_t offset, vaddr, paddr, filesz, memsz, align;
    };
    static const uint8_t elf_program[] = {
        0xB8, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xBF, 0x98, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
        0xBE, 0x1C, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        0xB8, 0x00, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        0x90, 0x90, 0x90,
        'V','A','N','T','A','O','S','_',
        'E','L','F','_','U','S','E','R','_',
        'P','R','O','G','R','A','M','_','O','K','\n'
    };
    enum { ELF_PAYLOAD_OFFSET = 64 + 56 };
    static uint8_t elf_image[ELF_PAYLOAD_OFFSET + sizeof(elf_program)];
    for (size_t i = 0; i < sizeof(elf_image); i++)
        elf_image[i] = 0U;

    struct test_elf_header* header =
        (struct test_elf_header*)elf_image;
    header->ident[0] = 0x7FU;
    header->ident[1] = 'E';
    header->ident[2] = 'L';
    header->ident[3] = 'F';
    header->ident[4] = 2U;
    header->ident[5] = 1U;
    header->ident[6] = 1U;
    header->type = 2U;
    header->machine = 62U;
    header->version = 1U;
    header->entry = PAGING_USER_BASE + ELF_PAYLOAD_OFFSET;
    header->phoff = sizeof(*header);
    header->ehsize = sizeof(*header);
    header->phentsize = sizeof(struct test_elf_program_header);
    header->phnum = 1U;

    struct test_elf_program_header* ph =
        (struct test_elf_program_header*)(elf_image + header->phoff);
    ph->type = 1U;
    ph->flags = 5U; /* PF_R | PF_X; writable+executable is rejected. */
    ph->offset = ELF_PAYLOAD_OFFSET;
    ph->vaddr = header->entry;
    ph->filesz = sizeof(elf_program);
    ph->memsz = sizeof(elf_program);
    ph->align = 1U;
    for (size_t i = 0; i < sizeof(elf_program); i++)
        elf_image[ELF_PAYLOAD_OFFSET + i] = elf_program[i];

    int elf_pid = process_create_user_elf(
        "ring3-elf-smoke", process_current_pid(),
        elf_image, sizeof(elf_image));
    if (elf_pid <= 0)
    {
        serial_write("VANTAOS_ELF_USER_PROCESS_CREATE_FAIL\n");
        return 0;
    }


    /*
     * Exercise malformed ELF metadata and late loader failures against the
     * production loader. Every rejected image must return all temporary PMM
     * frames, including page-table frames allocated before the rejection.
     */
    static uint8_t malformed_elf[sizeof(elf_image)];
    struct test_elf_header* bad_header =
        (struct test_elf_header*)malformed_elf;
    struct test_elf_program_header* bad_ph =
        (struct test_elf_program_header*)(malformed_elf +
            sizeof(struct test_elf_header));
    uint32_t elf_validation_cases = 0U;
    int elf_validation_ok = 1;
#define RESET_MALFORMED_ELF() do { \
    copy_bytes(malformed_elf, elf_image, sizeof(elf_image)); \
    bad_header = (struct test_elf_header*)malformed_elf; \
    bad_ph = (struct test_elf_program_header*)(malformed_elf + \
        sizeof(struct test_elf_header)); \
} while (0)
#define EXPECT_MALFORMED_ELF() do { \
    uint32_t free_before_case = pmm_get_free_blocks(); \
    int rejected_pid = process_create_user_elf( \
        "malformed-elf-probe", process_current_pid(), \
        malformed_elf, sizeof(elf_image)); \
    if (rejected_pid != -1 || \
        pmm_get_free_blocks() != free_before_case) \
        elf_validation_ok = 0; \
    else \
        elf_validation_cases++; \
} while (0)

    RESET_MALFORMED_ELF();
    bad_header->phoff = 0U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_header->phoff = 65U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_header->phnum = 0U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_header->phnum = 17U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_header->phentsize--;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_header->machine = 3U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_header->type = 3U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_ph->flags = 7U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_ph->flags = 0x100U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_ph->filesz = bad_ph->memsz + 1U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_ph->offset = UINT64_MAX;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_ph->memsz = 0U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_ph->align = 3U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_ph->align = 8192U;
    bad_ph->vaddr += PAGE_SIZE;
    EXPECT_MALFORMED_ELF();

    /* This rejection occurs after a load page has already been allocated. */
    RESET_MALFORMED_ELF();
    bad_header->entry = PAGING_USER_BASE + 0x2000U;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_ph->vaddr =
        PAGING_USER_END + 1U - PAGE_SIZE + ELF_PAYLOAD_OFFSET;
    bad_ph->filesz = 0U;
    bad_ph->memsz = PAGE_SIZE;
    bad_ph->align = 1U;
    bad_header->entry = bad_ph->vaddr;
    EXPECT_MALFORMED_ELF();

    RESET_MALFORMED_ELF();
    bad_ph->vaddr =
        PAGING_USER_END + 1U - 4U * PAGE_SIZE;
    bad_ph->offset = 0U;
    bad_ph->filesz = 0U;
    bad_ph->memsz = 1U;
    bad_ph->align = 1U;
    bad_header->entry = bad_ph->vaddr;
    EXPECT_MALFORMED_ELF();

    /*
     * Force the page budget check after allocating 61 image pages. The
     * loader must unwind every data frame and every intermediate page table.
     */
    RESET_MALFORMED_ELF();
    bad_ph->vaddr = PAGING_USER_BASE + 0x10000U;
    bad_ph->offset = 0U;
    bad_ph->filesz = 0U;
    bad_ph->memsz = 61U * PAGE_SIZE;
    bad_ph->align = PAGE_SIZE;
    bad_header->entry = bad_ph->vaddr;
    EXPECT_MALFORMED_ELF();

    const char* one_argument[] = { "probe" };
    const char* empty_argument[] = { "" };
    char oversized_argument[PROCESS_USER_ARG_BYTES_MAX + 1U];
    for (size_t i = 0; i < PROCESS_USER_ARG_BYTES_MAX; i++)
        oversized_argument[i] = 'x';
    oversized_argument[PROCESS_USER_ARG_BYTES_MAX] = '\0';
    const char* oversized_arguments[] = { oversized_argument };
    uint32_t free_before_args = pmm_get_free_blocks();
    if (process_create_user_elf_args(
            "bad-args", process_current_pid(), elf_image,
            sizeof(elf_image), PROCESS_USER_MAX_ARGS + 1U,
            one_argument) != -1 ||
        process_create_user_elf_args(
            "bad-args", process_current_pid(), elf_image,
            sizeof(elf_image), 1U, NULL) != -1 ||
        process_create_user_elf_args(
            "bad-args", process_current_pid(), elf_image,
            sizeof(elf_image), 1U, empty_argument) != -1 ||
        process_create_user_elf_args(
            "bad-args", process_current_pid(), elf_image,
            sizeof(elf_image), 1U, oversized_arguments) != -1 ||
        pmm_get_free_blocks() != free_before_args)
        elf_validation_ok = 0;
    else
        elf_validation_cases += 4U;

#undef RESET_MALFORMED_ELF
#undef EXPECT_MALFORMED_ELF

    if (!elf_validation_ok || elf_validation_cases < 20U)
    {
        serial_write("VANTAOS_ELF_VALIDATION_MATRIX_FAIL\n");
        return 0;
    }
    serial_write("VANTAOS_ELF_VALIDATION_MATRIX_OK\n");

    /*
     * Attempt to execute from the writable stack. NX must fault the process,
     * not panic the kernel; the exception path then reaps it after switching
     * away from its stack and CR3.
     */
    static const uint8_t nx_fault_image[] = {
        0x48, 0xB8, 0x00, 0x10, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
        0xFF, 0xE0
    };
    int fault_pid = process_create_user(
        "ring3-nx-fault", process_current_pid(),
        nx_fault_image, sizeof(nx_fault_image));
    if (fault_pid <= 0)
    {
        serial_write("VANTAOS_USER_FAULT_PROCESS_CREATE_FAIL\n");
        return 0;
    }

    /* Distinct real ring-3 fault classes must be contained independently. */
    uint8_t write_protect_image[] = {
        0x48, 0xB8, 0,0,0,0,0,0,0,0, 0xC6,0x00,0xCC
    };
    uint8_t unmapped_read_image[] = {
        0x48, 0xB8, 0,0,0,0,0,0,0,0, 0x8A,0x00
    };
    const uint64_t write_target = PAGING_USER_BASE;
    const uint64_t unmapped_target = PAGING_USER_BASE + 2U * PAGE_SIZE;
    for (uint32_t i = 0U; i < 8U; ++i)
    {
        write_protect_image[2U + i] = (uint8_t)(write_target >> (i * 8U));
        unmapped_read_image[2U + i] =
            (uint8_t)(unmapped_target >> (i * 8U));
    }
    static const uint8_t invalid_opcode_image[] = { 0x0F, 0x0B };
    if (process_create_user("ring3-write-protect-fault",
                            process_current_pid(), write_protect_image,
                            sizeof(write_protect_image)) <= 0 ||
        process_create_user("ring3-unmapped-read-fault",
                            process_current_pid(), unmapped_read_image,
                            sizeof(unmapped_read_image)) <= 0 ||
        process_create_user("ring3-invalid-opcode-fault",
                            process_current_pid(), invalid_opcode_image,
                            sizeof(invalid_opcode_image)) <= 0)
    {
        serial_write("VANTAOS_USER_FAULT_MATRIX_CREATE_FAIL\n");
        return 0;
    }

    /*
     * Verify syscall input/output checks, including an output range that
     * crosses from the RX code page into the RW stack page.
     */
    uint8_t pointer_guard_image[160] = {0};
    size_t code_size = 0U;
    size_t failure_branches[3] = {0U};
    size_t failure_branch_count = 0U;
#define EMIT_BYTE(value) do { pointer_guard_image[code_size++] = (uint8_t)(value); } while (0)
#define EMIT_U32(value) do { \
    uint32_t emit_value = (uint32_t)(value); \
    for (uint32_t emit_i = 0U; emit_i < 4U; ++emit_i) \
        EMIT_BYTE(emit_value >> (emit_i * 8U)); \
} while (0)
#define EMIT_U64(value) do { \
    uint64_t emit_value = (uint64_t)(value); \
    for (uint32_t emit_i = 0U; emit_i < 8U; ++emit_i) \
        EMIT_BYTE(emit_value >> (emit_i * 8U)); \
} while (0)
#define EMIT_EFAULT_CHECK(number, address, length) do { \
    EMIT_BYTE(0xB8); EMIT_U32(number); \
    EMIT_BYTE(0x48); EMIT_BYTE(0xBF); EMIT_U64(address); \
    EMIT_BYTE(0xBE); EMIT_U32(length); \
    EMIT_BYTE(0xCD); EMIT_BYTE(0x80); \
    EMIT_BYTE(0x48); EMIT_BYTE(0x83); EMIT_BYTE(0xF8); EMIT_BYTE(0xF2); \
    EMIT_BYTE(0x75); failure_branches[failure_branch_count++] = code_size; \
    EMIT_BYTE(0); \
} while (0)

    const uintptr_t unmapped_user_address =
        PAGING_USER_BASE + 2U * PAGE_SIZE;
    EMIT_EFAULT_CHECK(40U, unmapped_user_address, 1U);
    EMIT_EFAULT_CHECK(41U, unmapped_user_address, 1U);
    EMIT_EFAULT_CHECK(41U, PAGING_USER_BASE + PAGE_SIZE - 1U, 2U);

    EMIT_BYTE(0xB8); EMIT_U32(1U);
    EMIT_BYTE(0x48); EMIT_BYTE(0xBF);
    const size_t success_message_pointer_offset = code_size;
    EMIT_U64(0U);
    static const char success_message[] =
        "VANTAOS_SYSCALL_POINTER_GUARDS_OK\n";
    EMIT_BYTE(0xBE); EMIT_U32(sizeof(success_message) - 1U);
    EMIT_BYTE(0xCD); EMIT_BYTE(0x80);
    EMIT_BYTE(0xB8); EMIT_U32(0U);
    EMIT_BYTE(0xCD); EMIT_BYTE(0x80);

    const size_t failure_target = code_size;
    EMIT_BYTE(0xB8); EMIT_U32(99U);
    EMIT_BYTE(0xCD); EMIT_BYTE(0x80);
    const size_t message_offset = code_size;
    for (size_t i = 0U; i < sizeof(success_message) - 1U; ++i)
        EMIT_BYTE(success_message[i]);

    const uint64_t message_address = PAGING_USER_BASE + message_offset;
    for (uint32_t i = 0U; i < 8U; ++i)
        pointer_guard_image[success_message_pointer_offset + i] =
            (uint8_t)(message_address >> (i * 8U));
    for (size_t i = 0U; i < failure_branch_count; ++i)
    {
        const size_t displacement = failure_target -
            (failure_branches[i] + 1U);
        if (displacement > 127U)
        {
            serial_write("VANTAOS_SYSCALL_POINTER_GUARDS_BUILD_FAIL\n");
            return 0;
        }
        pointer_guard_image[failure_branches[i]] = (uint8_t)displacement;
    }
#undef EMIT_EFAULT_CHECK
#undef EMIT_U64
#undef EMIT_U32
#undef EMIT_BYTE

    if (process_create_user("ring3-syscall-pointer-guards",
                            process_current_pid(), pointer_guard_image,
                            code_size) <= 0)
    {
        serial_write("VANTAOS_SYSCALL_POINTER_GUARDS_CREATE_FAIL\n");
        return 0;
    }

    static const uint8_t invalid_elf[64] = {0};
    if (process_create_user_elf("invalid-elf", process_current_pid(),
                                invalid_elf, sizeof(invalid_elf)) != -1)
    {
        serial_write("VANTAOS_ELF_VALIDATION_FAIL\n");
        return 0;
    }
    serial_write("VANTAOS_ELF_VALIDATION_OK\n");
    serial_write("VANTAOS_USER_PROCESS_QUEUED\n");
    return 1;
}
