/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "process_internal.h"
#include "pmm.h"
#include "vm_region.h"
#include "heap.h"

#include <stddef.h>
#include <stdint.h>

#define ELFCLASS64 2U
#define ELFDATA2LSB 1U
#define EV_CURRENT 1U
#define ET_EXEC 2U
#define EM_X86_64 62U
#define PT_LOAD 1U
#define PF_X 1U
#define PF_W 2U
#define ELF_PHDR_MAX 16U
/* Pages mapped eagerly at the top of the stack to hold argv and auxv. */
#define USER_STACK_PAGES 4U
/* Whole reserved stack range; pages below the eager ones fault in on demand. */
#define USER_STACK_RESERVE_PAGES PROCESS_USER_MAX_PAGES
#define USER_STACK_TOP ((uintptr_t)PAGING_USER_END + 1U)
#define USER_STACK_SIZE (USER_STACK_PAGES * PAGE_SIZE)
#define USER_STACK_RESERVE_SIZE ((uintptr_t)USER_STACK_RESERVE_PAGES * PAGE_SIZE)
#define IDENTITY_MAP_LIMIT 0x40000000ULL

typedef struct __attribute__((packed)) elf64_header
{
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} elf64_header_t;

typedef struct __attribute__((packed)) elf64_program_header
{
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} elf64_program_header_t;

_Static_assert(sizeof(elf64_header_t) == 64U, "ELF64 header layout");
_Static_assert(sizeof(elf64_program_header_t) == 56U, "ELF64 program header layout");

static int range_in_image(uint64_t offset, uint64_t length, size_t image_size)
{
    return offset <= image_size && length <= (uint64_t)image_size - offset;
}

static int is_power_of_two(uint64_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static process_user_page_t* find_page(process_user_page_t* pages,
                                      uint32_t count, uintptr_t address)
{
    uintptr_t page_address = address & ~(uintptr_t)(PAGE_SIZE - 1U);
    for (uint32_t i = 0; i < count; i++)
        if (pages[i].virtual_address == page_address)
            return &pages[i];
    return NULL;
}

static int write_user_stack(process_user_page_t* pages, uint32_t page_count,
                            uintptr_t address, const void* source,
                            size_t length)
{
    const uint8_t* input = (const uint8_t*)source;
    for (size_t i = 0; i < length; i++)
    {
        uintptr_t target = address + i;
        process_user_page_t* page =
            find_page(pages, page_count, target);
        if (!page || !(page->flags & PROCESS_USER_PAGE_WRITE))
            return 0;
        ((uint8_t*)page->physical_frame)
            [target & (PAGE_SIZE - 1U)] = input[i];
    }
    return 1;
}

static void release_unpublished_image(paging_address_space_t* space,
                                      process_user_page_t* pages,
                                      uint32_t page_count)
{
    const void* image_backing = space ? space->vm.image_backing : NULL;
    if (space && !paging_destroy_address_space(space))
        return; /* Keep frames and their source bytes alive if teardown failed. */
    if (image_backing)
        kfree((void*)image_backing);

    for (uint32_t i = 0; i < page_count; i++)
        if (pages[i].physical_frame)
            pmm_free_block(pages[i].physical_frame);
}

static int allocate_and_map_page(paging_address_space_t* space,
                                 process_user_page_t* pages,
                                 uint32_t* page_count,
                                 uintptr_t virtual_address,
                                 int writable, int executable)
{
    if (*page_count >= PROCESS_USER_MAX_PAGES ||
        (virtual_address & (PAGE_SIZE - 1U)) != 0U ||
        virtual_address < PAGING_USER_BASE ||
        virtual_address > PAGING_USER_END ||
        find_page(pages, *page_count, virtual_address))
        return 0;

    void* frame = pmm_alloc_block();
    if (!frame)
        return 0;
    if (((uintptr_t)frame & (PAGE_SIZE - 1U)) != 0U ||
        (uintptr_t)frame >= IDENTITY_MAP_LIMIT)
    {
        pmm_free_block(frame);
        return 0;
    }

    uint8_t* bytes = (uint8_t*)frame;
    for (size_t i = 0; i < PAGE_SIZE; i++)
        bytes[i] = 0U;

    if (!paging_map_user_page_ex(space, virtual_address, (uintptr_t)frame,
                                 writable, executable))
    {
        pmm_free_block(frame);
        return 0;
    }

    process_user_page_t* page = &pages[*page_count];
    page->virtual_address = virtual_address;
    page->physical_frame = frame;
    page->flags = (writable ? PROCESS_USER_PAGE_WRITE : 0U) |
                  (executable ? PROCESS_USER_PAGE_EXEC : 0U);
    (*page_count)++;
    return 1;
}

int process_create_user_elf_args(const char* name, uint32_t parent_pid,
                                 const void* image, size_t image_size,
                                 uint32_t argc, const char* const argv[])
{
    if (!process_initialized_state || !name || !name[0] || !image ||
        image_size < sizeof(elf64_header_t) ||
        argc > PROCESS_USER_MAX_ARGS || (argc != 0U && !argv) ||
        !paging_nx_available())
        return -1;

    size_t argument_lengths[PROCESS_USER_MAX_ARGS] = {0};
    size_t total_argument_bytes = 0U;
    for (uint32_t i = 0; i < argc; i++)
    {
        if (!argv[i] || (i == 0U && argv[i][0] == '\0'))
            return -1;
        size_t length = 0U;
        while (length <= PROCESS_USER_ARG_BYTES_MAX && argv[i][length])
            length++;
        if (length >= PROCESS_USER_ARG_BYTES_MAX ||
            total_argument_bytes > PROCESS_USER_ARG_BYTES_MAX - (length + 1U))
            return -1;
        argument_lengths[i] = length;
        total_argument_bytes += length + 1U;
    }

    const uint8_t* bytes = (const uint8_t*)image;
    const elf64_header_t* header = (const elf64_header_t*)bytes;
    if (header->ident[0] != 0x7FU || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != ELFCLASS64 ||
        header->ident[5] != ELFDATA2LSB ||
        header->ident[6] != EV_CURRENT ||
        header->type != ET_EXEC || header->machine != EM_X86_64 ||
        header->version != EV_CURRENT ||
        header->header_size != sizeof(elf64_header_t) ||
        header->program_header_entry_size != sizeof(elf64_program_header_t) ||
        header->program_header_offset < header->header_size ||
        (header->program_header_offset & 7U) != 0U ||
        header->program_header_count == 0U ||
        header->program_header_count > ELF_PHDR_MAX ||
        !range_in_image(header->program_header_offset,
            (uint64_t)header->program_header_count *
                sizeof(elf64_program_header_t), image_size))
        return -1;

    /* The caller may release its input as soon as this function returns.
       Keep an immutable kernel-owned copy until the address space is retired. */
    uint8_t* retained_image = (uint8_t*)kmalloc(image_size);
    if (!retained_image)
        return -1;
    for (size_t i = 0U; i < image_size; i++)
        retained_image[i] = bytes[i];
    bytes = retained_image;
    header = (const elf64_header_t*)bytes;
    const elf64_program_header_t* program_headers =
        (const elf64_program_header_t*)(bytes + header->program_header_offset);
    paging_address_space_t* space = paging_create_address_space();
    process_user_page_t pages[PROCESS_USER_MAX_PAGES] = {{0}};
    uint32_t page_count = 0U;
    int has_load_segment = 0;
    int entry_is_executable = 0;

    if (!space)
    {
        kfree(retained_image);
        return -1;
    }
    if (!vm_space_set_image_backing(&space->vm, retained_image, image_size))
    {
        (void)paging_destroy_address_space(space);
        kfree(retained_image);
        return -1;
    }

    const uintptr_t stack_top = USER_STACK_TOP;
    /* Reserved stack range; only the top USER_STACK_PAGES are mapped up front. */
    const uintptr_t stack_base = USER_STACK_TOP - USER_STACK_RESERVE_SIZE;
    const uintptr_t eager_stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    /* One inaccessible page below the stack turns overflow into a clean fault. */
    const uintptr_t guard_base = stack_base - PAGE_SIZE;

    for (uint32_t i = 0; i < header->program_header_count; i++)
    {
        const elf64_program_header_t* ph = &program_headers[i];
        if (ph->type != PT_LOAD)
            continue;

        if (ph->memory_size == 0U || ph->file_size > ph->memory_size ||
            !range_in_image(ph->offset, ph->file_size, image_size) ||
            ph->virtual_address < PAGING_USER_BASE ||
            ph->virtual_address > PAGING_USER_END ||
            ph->memory_size >
                (uint64_t)PAGING_USER_END - ph->virtual_address + 1U ||
            (ph->flags & (PF_W | PF_X)) == (PF_W | PF_X) ||
            (ph->flags & ~7U) != 0U ||
            (ph->virtual_address & (PAGE_SIZE - 1U)) !=
                (ph->offset & (PAGE_SIZE - 1U)) ||
            (ph->alignment > 1U &&
             (!is_power_of_two(ph->alignment) ||
              ((ph->virtual_address - ph->offset) &
               (ph->alignment - 1U)) != 0U)))
        {
            release_unpublished_image(space, pages, page_count);
            return -1;
        }

        uintptr_t segment_start = (uintptr_t)ph->virtual_address;
        uintptr_t segment_end = segment_start + (uintptr_t)ph->memory_size;
        uintptr_t page_start =
            segment_start & ~(uintptr_t)(PAGE_SIZE - 1U);
        uintptr_t page_end =
            (segment_end + PAGE_SIZE - 1U) &
            ~(uintptr_t)(PAGE_SIZE - 1U);
        if (segment_end < segment_start || page_end < segment_end ||
            page_start < PAGING_USER_BASE || page_end > guard_base)
        {
            release_unpublished_image(space, pages, page_count);
            return -1;
        }

        const int writable = (ph->flags & PF_W) != 0U;
        const int executable = (ph->flags & PF_X) != 0U;
        const uint32_t prot = VM_PROT_READ |
                              (writable ? VM_PROT_WRITE : 0U) |
                              (executable ? VM_PROT_EXEC : 0U);

        /*
         * Executable text remains eager because the demand leaf API is NX-only.
         * Non-executable PT_LOAD file bytes become private file-backed demand
         * pages; pure .bss pages remain demand-zero. The image is immutable.
         */
        uintptr_t populated_end = page_end;
        if (!executable)
        {
            uintptr_t file_end = segment_start + (uintptr_t)ph->file_size;
            populated_end = ph->file_size == 0U ? page_start :
                ((file_end + PAGE_SIZE - 1U) & ~(uintptr_t)(PAGE_SIZE - 1U));
            if (populated_end > page_end)
                populated_end = page_end;
        }
        if (executable)
        {
            for (uintptr_t va = page_start; va < populated_end; va += PAGE_SIZE)
            {
                if (!allocate_and_map_page(space, pages, &page_count, va,
                                           writable, executable))
                {
                    release_unpublished_image(space, pages, page_count);
                    return -1;
                }
            }
            if (!vm_space_add_region(&space->vm, page_start,
                                     populated_end - page_start, prot,
                                     VM_REGION_ELF, VM_BACKING_POPULATED))
            {
                release_unpublished_image(space, pages, page_count);
                return -1;
            }
        }
        else if (populated_end > page_start)
        {
            const uint32_t prefix = (uint32_t)(segment_start - page_start);
            if (!vm_space_add_file_region(&space->vm, page_start,
                                          populated_end - page_start, prot,
                                          ph->offset, prefix, ph->file_size))
            {
                release_unpublished_image(space, pages, page_count);
                return -1;
            }
        }
        if (populated_end < page_end &&
            !vm_space_add_region(&space->vm, populated_end,
                                 page_end - populated_end, prot,
                                 VM_REGION_ELF, VM_BACKING_ZERO))
        {
            release_unpublished_image(space, pages, page_count);
            return -1;
        }

        if (executable)
        {
            for (uint64_t offset = 0; offset < ph->file_size; offset++)
            {
                uintptr_t target = segment_start + (uintptr_t)offset;
                process_user_page_t* page =
                    find_page(pages, page_count, target);
                if (!page)
                {
                    release_unpublished_image(space, pages, page_count);
                    return -1;
                }
                ((uint8_t*)page->physical_frame)
                    [target & (PAGE_SIZE - 1U)] = bytes[ph->offset + offset];
            }
        }

        if (executable && header->entry >= segment_start &&
            header->entry < segment_end)
            entry_is_executable = 1;
        has_load_segment = 1;
    }

    if (!has_load_segment || !entry_is_executable ||
        header->entry < PAGING_USER_BASE ||
        header->entry > PAGING_USER_END ||
        page_count + USER_STACK_PAGES > PROCESS_USER_MAX_PAGES)
    {
        release_unpublished_image(space, pages, page_count);
        return -1;
    }

    for (uint32_t i = 0; i < USER_STACK_PAGES; i++)
    {
        uintptr_t va = eager_stack_base + (uintptr_t)i * PAGE_SIZE;
        if (!allocate_and_map_page(space, pages, &page_count, va, 1, 0))
        {
            release_unpublished_image(space, pages, page_count);
            return -1;
        }
    }

    if (!vm_space_add_region(&space->vm, stack_base, USER_STACK_RESERVE_SIZE,
                             VM_PROT_READ | VM_PROT_WRITE, VM_REGION_STACK,
                             VM_BACKING_ZERO) ||
        !vm_space_add_region(&space->vm, guard_base, PAGE_SIZE, 0U,
                             VM_REGION_GUARD, VM_BACKING_NONE))
    {
        release_unpublished_image(space, pages, page_count);
        return -1;
    }

    uintptr_t argument_addresses[PROCESS_USER_MAX_ARGS] = {0};
    uintptr_t stack_pointer = stack_top;
    for (uint32_t i = argc; i > 0U; i--)
    {
        uint32_t index = i - 1U;
        size_t length = argument_lengths[index] + 1U;
        if (length > stack_pointer - eager_stack_base)
        {
            release_unpublished_image(space, pages, page_count);
            return -1;
        }
        stack_pointer -= length;
        if (!write_user_stack(pages, page_count, stack_pointer,
                              argv[index], length))
        {
            release_unpublished_image(space, pages, page_count);
            return -1;
        }
        argument_addresses[index] = stack_pointer;
    }

    const size_t word_count = (size_t)argc + 5U;
    const size_t table_bytes = word_count * sizeof(uint64_t);
    if (table_bytes > stack_pointer - eager_stack_base)
    {
        release_unpublished_image(space, pages, page_count);
        return -1;
    }
    stack_pointer = (stack_pointer - table_bytes) & ~(uintptr_t)0xFU;
    if (stack_pointer < eager_stack_base ||
        table_bytes > stack_top - stack_pointer)
    {
        release_unpublished_image(space, pages, page_count);
        return -1;
    }

    uint64_t initial_words[PROCESS_USER_MAX_ARGS + 5U] = {0};
    initial_words[0] = argc;
    for (uint32_t i = 0; i < argc; i++)
        initial_words[1U + i] = argument_addresses[i];
    /* argv terminator, envp terminator, then AT_NULL and its value. */
    initial_words[1U + argc] = 0U;
    initial_words[2U + argc] = 0U;
    initial_words[3U + argc] = 0U;
    initial_words[4U + argc] = 0U;

    if (!write_user_stack(pages, page_count, stack_pointer,
                          initial_words, table_bytes))
    {
        release_unpublished_image(space, pages, page_count);
        return -1;
    }

    int pid = process_install_user_image(
        name, parent_pid, space, (uintptr_t)header->entry,
        stack_base, stack_top, stack_pointer, image_size,
        pages, page_count);
    if (pid <= 0)
    {
        release_unpublished_image(space, pages, page_count);
        return -1;
    }
    return pid;
}

int process_create_user_elf(const char* name, uint32_t parent_pid,
                            const void* image, size_t image_size)
{
    return process_create_user_elf_args(name, parent_pid, image, image_size,
                                        0U, NULL);
}
