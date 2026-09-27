#include "heap.h"
#include "paging.h"
#include "pmm.h"

#include <stdint.h>
#include <stddef.h>

#define BLOCK_FREE 0
#define BLOCK_USED 1
#define HEAP_INITIAL_PAGES 16
#define HEAP_MAX_PAGES (HEAP_MAX_SIZE / PAGE_SIZE)

typedef struct heap_block
{
    size_t size;
    uint32_t status;
    struct heap_block* next;
} heap_block_t;

static heap_block_t* first_block;
static size_t heap_used;
static size_t heap_size;
static uint32_t heap_pages;
static int heap_initialized;

static uintptr_t heap_physical_pages[HEAP_MAX_PAGES];

static size_t align_up(size_t value)
{
    if (value > (size_t)-1 - (HEAP_ALIGNMENT - 1))
        return 0;

    return (value + (HEAP_ALIGNMENT - 1)) &
           ~(size_t)(HEAP_ALIGNMENT - 1);
}

static void split_block(heap_block_t* block, size_t size)
{
    size_t remaining = block->size - size;

    if (remaining <= sizeof(heap_block_t) + HEAP_ALIGNMENT)
        return;

    heap_block_t* next =
        (heap_block_t*)((uint8_t*)block +
                        sizeof(heap_block_t) + size);

    next->size = remaining - sizeof(heap_block_t);
    next->status = BLOCK_FREE;
    next->next = block->next;

    block->size = size;
    block->next = next;
}

static void merge_free_blocks(void)
{
    heap_block_t* block = first_block;

    while (block && block->next)
    {
        heap_block_t* next = block->next;

        if (block->status == BLOCK_FREE &&
            next->status == BLOCK_FREE)
        {
            block->size += sizeof(heap_block_t) + next->size;
            block->next = next->next;
            continue;
        }

        block = next;
    }
}

static int heap_add_page(void)
{
    if (heap_pages >= HEAP_MAX_PAGES)
        return 0;

    void* physical = pmm_alloc_block();

    if (!physical)
        return 0;

    uintptr_t virtual_address =
        HEAP_VIRTUAL_BASE +
        (uintptr_t)heap_pages * PAGE_SIZE;

    if (virtual_address < HEAP_VIRTUAL_BASE ||
        virtual_address + PAGE_SIZE < virtual_address ||
        !paging_map_page(
            virtual_address,
            (uintptr_t)physical))
    {
        pmm_free_block(physical);
        return 0;
    }

    heap_physical_pages[heap_pages] =
        (uintptr_t)physical;

    heap_pages++;
    heap_size += PAGE_SIZE;

    if (!first_block)
    {
        first_block = (heap_block_t*)virtual_address;
        first_block->size =
            PAGE_SIZE - sizeof(heap_block_t);
        first_block->status = BLOCK_FREE;
        first_block->next = NULL;
        return 1;
    }

    heap_block_t* block = first_block;

    while (block->next)
        block = block->next;

    uintptr_t expected =
        (uintptr_t)block +
        sizeof(heap_block_t) +
        block->size;

    if (block->status == BLOCK_FREE &&
        expected == virtual_address)
    {
        block->size += PAGE_SIZE;
    }
    else
    {
        heap_block_t* next =
            (heap_block_t*)virtual_address;

        next->size =
            PAGE_SIZE - sizeof(heap_block_t);
        next->status = BLOCK_FREE;
        next->next = NULL;
        block->next = next;
    }

    return 1;
}

static void heap_release_pages(void)
{
    for (uint32_t i = 0; i < heap_pages; i++)
    {
        uintptr_t virtual_address =
            HEAP_VIRTUAL_BASE +
            (uintptr_t)i * PAGE_SIZE;

        paging_unmap_page(virtual_address);

        if (heap_physical_pages[i] != 0)
            pmm_free_block(
                (void*)heap_physical_pages[i]
            );

        heap_physical_pages[i] = 0;
    }

    heap_pages = 0;
    heap_size = 0;
    first_block = NULL;
    heap_used = 0;
}

void heap_initialize(void)
{
    first_block = NULL;
    heap_used = 0;
    heap_size = 0;
    heap_pages = 0;
    heap_initialized = 0;

    for (uint32_t i = 0; i < HEAP_MAX_PAGES; i++)
        heap_physical_pages[i] = 0;

    for (uint32_t i = 0; i < HEAP_INITIAL_PAGES; i++)
    {
        if (!heap_add_page())
        {
            heap_release_pages();
            return;
        }
    }

    heap_initialized = 1;
}

void* kmalloc(size_t size)
{
    if (!heap_initialized || size == 0)
        return NULL;

    size = align_up(size);

    if (size == 0)
        return NULL;

    while (1)
    {
        heap_block_t* block = first_block;

        while (block)
        {
            if (block->status == BLOCK_FREE &&
                block->size >= size)
            {
                split_block(block, size);
                block->status = BLOCK_USED;
                heap_used += size;

                return (void*)((uint8_t*)block +
                               sizeof(heap_block_t));
            }

            block = block->next;
        }

        if (!heap_add_page())
            return NULL;
    }
}

void kfree(void* ptr)
{
    if (!heap_initialized || !ptr)
        return;

    uintptr_t address = (uintptr_t)ptr;

    if (address < HEAP_VIRTUAL_BASE + sizeof(heap_block_t) ||
        address >= HEAP_VIRTUAL_BASE + heap_size)
        return;

    heap_block_t* block = first_block;

    while (block)
    {
        uintptr_t payload =
            (uintptr_t)block + sizeof(heap_block_t);

        if (payload == address)
            break;

        block = block->next;
    }

    if (!block ||
        block->status != BLOCK_USED)
        return;

    block->status = BLOCK_FREE;

    if (heap_used >= block->size)
        heap_used -= block->size;
    else
        heap_used = 0;

    merge_free_blocks();
}

size_t heap_get_total_size(void)
{
    size_t total = 0;
    heap_block_t* block = first_block;

    while (block)
    {
        total += block->size;
        block = block->next;
    }

    return total;
}

size_t heap_get_used_size(void)
{
    return heap_used;
}

size_t heap_get_free_size(void)
{
    size_t free_size = 0;
    heap_block_t* block = first_block;

    while (block)
    {
        if (block->status == BLOCK_FREE)
            free_size += block->size;

        block = block->next;
    }

    return free_size;
}

int heap_is_initialized(void)
{
    return heap_initialized;
}
